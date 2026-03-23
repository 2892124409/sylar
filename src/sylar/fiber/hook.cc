#include "hook.h"
#include <dlfcn.h>      // For dlsym
#include <stdarg.h>     // For va_list in fcntl and ioctl
#include <errno.h>      // For errno and error codes (EAGAIN, EINTR, EBADF, ETIMEDOUT)
#include <sys/socket.h> // For SO_RCVTIMEO, SO_SNDTIMEO, etc.
#include <string.h>     // For strerror

#include "sylar/fiber/fiber.h"
#include "sylar/fiber/iomanager.h"
#include "sylar/log/logger.h"
#include "sylar/base/macro.h"       // For SYLAR_LIKELY, SYLAR_UNLIKELY
#include "sylar/fiber/fd_manager.h" // For FdManager, FdCtx
#include "sylar/base/config.h"      // For ConfigVar

// 定义一个静态的logger，用于hook模块的日志输出
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

namespace sylar
{

    // 定义一个配置项，用于从配置文件中获取TCP连接超时时间，默认5000ms
    static sylar::ConfigVar<int>::ptr g_tcp_connect_timeout =
        sylar::Config::Lookup("tcp.connect.timeout", 5000, "tcp connect timeout");

    // 线程局部变量，用于控制当前线程是否启用hook功能
    static thread_local bool t_hook_enable = false;

    /**
     * @brief 判断当前线程是否启用了hook
     * @return true: 启用, false: 未启用
     */
    bool is_hook_enable()
    {
        return t_hook_enable;
    }

    /**
     * @brief 设置当前线程的hook状态
     * @param[in] flag true: 启用hook, false: 关闭hook
     */
    void set_hook_enable(bool flag)
    {
        t_hook_enable = flag;
    }

    // 外部C链接块，用于声明所有需要hook的原始系统函数指针
    // 这样做是为了防止C++的名称修饰(name mangling)导致链接错误，确保能够找到C语言库中的原始函数
    extern "C"
    {
// HOOK_FUN宏的定义：用于列出所有要hook的函数名
// 每次使用XX宏时，它会被替换为传入的函数名
#define HOOK_FUN(XX) \
    XX(sleep)        \
    XX(usleep)       \
    XX(nanosleep)    \
    XX(socket)       \
    XX(connect)      \
    XX(accept)       \
    XX(read)         \
    XX(readv)        \
    XX(recv)         \
    XX(recvfrom)     \
    XX(recvmsg)      \
    XX(write)        \
    XX(writev)       \
    XX(send)         \
    XX(sendto)       \
    XX(sendmsg)      \
    XX(close)        \
    XX(fcntl)        \
    XX(ioctl)        \
    XX(getsockopt)   \
    XX(setsockopt)

// 宏展开：实际声明所有函数指针，初始值为nullptr
// 例如：sleep_fun sleep_f = nullptr;
#define XX(name) name##_fun name##_f = nullptr;
        HOOK_FUN(XX) // 展开所有函数指针的声明
#undef XX
    } // extern "C"

    /**
     * @brief hook初始化函数
     * @details 在程序启动时通过dlsym获取原始系统函数的地址，并赋值给对应的函数指针
     */
    void hook_init()
    {
        static bool is_inited = false;
        if (is_inited)
        { // 防止重复初始化
            return;
        }
        is_inited = true; // 标记已初始化

        // 宏展开：通过dlsym获取每个原始函数的地址
        // 例如：sleep_f = (sleep_fun)dlsym(RTLD_NEXT, "sleep");
        // RTLD_NEXT表示在当前搜索顺序的下一个库中查找符号，通常是libc库
#define XX(name) name##_f = (name##_fun)dlsym(RTLD_NEXT, #name);
        HOOK_FUN(XX) // 展开所有函数指针的赋值
#undef XX
    }

    static uint64_t s_connect_timeout = -1;

    /**
     * @brief Hook初始化器
     * @details 这是一个静态全局对象，其构造函数会在main函数之前被调用
     *          用于确保hook_init()被执行，并监听tcp.connect.timeout配置项的变化
     */
    struct _HookIniter
    {
        _HookIniter()
        {
            hook_init(); // 调用hook初始化函数
            // 获取tcp连接超时配置的初始值
            s_connect_timeout = g_tcp_connect_timeout->getValue();

            // 为tcp.connect.timeout配置项添加监听器，当配置值改变时，更新s_connect_timeout
            g_tcp_connect_timeout->addListener([](const int &old_value, const int &new_value)
                                               {
                SYLAR_LOG_INFO(g_logger) << "tcp_connect_timeout changed from "
                                         << old_value << " to " << new_value;
                s_connect_timeout = new_value; });
        }
    };

    // 声明并定义静态Hook初始化器对象
    static _HookIniter s_hook_initer;

} // namespace sylar

namespace sylar
{

    /**
     * @brief 通用IO操作的Hook模板函数
     * @details 用于封装read, write, recv, send等阻塞IO操作，将其转化为非阻塞协程调度
     * @tparam F 函数类型 (例如: read_fun, write_fun)
     * @tparam Args 函数参数类型
     * @param fd 文件描述符
     * @param fun 原始的系统IO函数指针
     * @param hook_fun_name Hook函数的名称 (用于日志)
     * @param event IOManager事件类型 (READ 或 WRITE)
     * @param timeout_so_type 对应超时类型 (SO_RCVTIMEO 或 SO_SNDTIMEO)
     * @param args IO函数的实际参数
     * @return IO操作的结果，或者在超时时返回-1并设置errno为ETIMEDOUT
     */
    template <typename F, typename... Args>
    static ssize_t do_io(int fd, F fun, const char *hook_fun_name,
                         sylar::IOManager::Event event, int timeout_so_type, Args &&...args)
    {
        // 如果当前线程hook未启用，直接调用原始IO函数
        if (!is_hook_enable())
        {
            return fun(fd, std::forward<Args>(args)...);
        }

        // 从FdManager获取文件描述符的上下文
        sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
        if (!ctx)
        {
            ctx = sylar::FdMgr::GetInstance()->get(fd, true);
        }
        // 如果获取不到上下文，或者文件描述符无效/已关闭/不是socket/用户设置了非阻塞，
        // 则直接调用原始IO函数（因为这些情况不需要或不应该进行协程hook）
        if (!ctx || ctx->isClose() || !ctx->isSocket() || ctx->getUserNonblock())
        {
            return fun(fd, std::forward<Args>(args)...);
        }

        sylar::IOManager *iom = sylar::IOManager::GetThis();
        // 如果当前线程没有IOManager (例如在非IOManager管理的线程中)，则退化为调用原始IO函数
        if (!iom)
        {
            return fun(fd, std::forward<Args>(args)...);
        }

        uint64_t timeout = ctx->getTimeout(timeout_so_type);

        while (true)
        {
            ssize_t rt = fun(fd, std::forward<Args>(args)...);
            if (SYLAR_LIKELY(rt != -1))
            {
                return rt;
            }

            if (errno == EINTR)
            {
                continue;
            }

            if (errno != EAGAIN)
            {
                return rt;
            }

            int wait_rt = iom->waitEvent(fd, event, timeout);
            if (SYLAR_UNLIKELY(wait_rt < 0))
            {
                SYLAR_LOG_ERROR(g_logger) << hook_fun_name << " waitEvent(" << fd << ", "
                                          << event << ") error:" << errno << " " << strerror(errno);
                errno = EBADF;
                return -1;
            }
            if (wait_rt > 0)
            {
                return -1;
            }
        }
    }

} // namespace sylar

// 实现hooked函数，这些函数会替换原始的系统函数，并包含在extern "C"块中
extern "C"
{

    // Hooked sleep function implementations (不变)
    unsigned int sleep(unsigned int seconds)
    {
        if (!sylar::is_hook_enable())
        {
            return sleep_f(seconds);
        }

        sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
        sylar::IOManager *iom = sylar::IOManager::GetThis();
        if (iom)
        {
            iom->addTimer(seconds * 1000, [iom, fiber]()
                          { iom->schedule(fiber); });
            sylar::Fiber::YieldToHold();
        }
        else
        {
            return sleep_f(seconds);
        }
        return 0;
    }

    int usleep(useconds_t usec)
    {
        if (!sylar::is_hook_enable())
        {
            return usleep_f(usec);
        }
        sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
        sylar::IOManager *iom = sylar::IOManager::GetThis();
        if (iom)
        {
            iom->addTimer(usec / 1000, [iom, fiber]()
                          { iom->schedule(fiber); });
            sylar::Fiber::YieldToHold();
        }
        else
        {
            return usleep_f(usec);
        }
        return 0;
    }

    int nanosleep(const struct timespec *req, struct timespec *rem)
    {
        if (!sylar::is_hook_enable())
        {
            return nanosleep_f(req, rem);
        }

        int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000000;
        sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
        sylar::IOManager *iom = sylar::IOManager::GetThis();
        if (iom)
        {
            iom->addTimer(timeout_ms, [iom, fiber]()
                          { iom->schedule(fiber); });
            sylar::Fiber::YieldToHold();
        }
        else
        {
            return nanosleep_f(req, rem);
        }
        return 0;
    }

    // ============================================================================
    // Socket 相关函数
    // ============================================================================

    int socket(int domain, int type, int protocol)
    {
        if (!sylar::is_hook_enable())
        {
            return socket_f(domain, type, protocol);
        }
        int fd = socket_f(domain, type, protocol);
        if (fd == -1)
        {
            return fd;
        }
        // 创建 socket 后，将其注册到 FdManager 中
        sylar::FdMgr::GetInstance()->get(fd, true);
        return fd;
    }

    int connect_with_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen, uint64_t timeout_ms)
    {
        if (!sylar::is_hook_enable())
        {
            return connect_f(fd, addr, addrlen);
        }
        sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
        if (!ctx)
        {
            ctx = sylar::FdMgr::GetInstance()->get(fd, true);
        }
        if (!ctx || ctx->isClose())
        {
            errno = EBADF;
            return -1;
        }

        if (!ctx->isSocket())
        {
            return connect_f(fd, addr, addrlen);
        }

        if (ctx->getUserNonblock())
        {
            return connect_f(fd, addr, addrlen);
        }

        int n = connect_f(fd, addr, addrlen);
        if (n == 0)
        {
            return 0;
        }
        else if (n != -1 || errno != EINPROGRESS)
        {
            return n;
        }

        sylar::IOManager *iom = sylar::IOManager::GetThis();
        if (!iom)
        {
            return connect_f(fd, addr, addrlen);
        }

        int wait_rt = iom->waitEvent(fd, sylar::IOManager::WRITE, timeout_ms);
        if (wait_rt < 0)
        {
            return -1;
        }
        if (wait_rt > 0)
        {
            return -1;
        }

        int error = 0;
        socklen_t len = sizeof(int);
        if (-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len))
        {
            return -1;
        }
        if (!error)
        {
            return 0;
        }
        else
        {
            errno = error;
            return -1;
        }
    }

    int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
    {
        return connect_with_timeout(sockfd, addr, addrlen, sylar::s_connect_timeout);
    }

    int accept(int s, struct sockaddr *addr, socklen_t *addrlen)
    {
        int fd = do_io(s, accept_f, "accept", sylar::IOManager::READ, SO_RCVTIMEO, addr, addrlen);
        if (fd >= 0)
        {
            // accept 成功后，将新的 fd 注册到 FdManager
            sylar::FdMgr::GetInstance()->get(fd, true);
        }
        return fd;
    }

    // ============================================================================
    // IO 相关函数
    // ============================================================================

    ssize_t read(int fd, void *buf, size_t count)
    {
        return sylar::do_io(fd, read_f, "read", sylar::IOManager::READ, SO_RCVTIMEO, buf, count);
    }

    ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
    {
        return sylar::do_io(fd, readv_f, "readv", sylar::IOManager::READ, SO_RCVTIMEO, iov, iovcnt);
    }

    ssize_t recv(int sockfd, void *buf, size_t len, int flags)
    {
        return sylar::do_io(sockfd, recv_f, "recv", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags);
    }

    ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                     struct sockaddr *src_addr, socklen_t *addrlen)
    {
        return sylar::do_io(sockfd, recvfrom_f, "recvfrom", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
    }

    ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
    {
        return sylar::do_io(sockfd, recvmsg_f, "recvmsg", sylar::IOManager::READ, SO_RCVTIMEO, msg, flags);
    }

    ssize_t write(int fd, const void *buf, size_t count)
    {
        return sylar::do_io(fd, write_f, "write", sylar::IOManager::WRITE, SO_SNDTIMEO, buf, count);
    }

    ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
    {
        return sylar::do_io(fd, writev_f, "writev", sylar::IOManager::WRITE, SO_SNDTIMEO, iov, iovcnt);
    }

    ssize_t send(int s, const void *msg, size_t len, int flags)
    {
        return sylar::do_io(s, send_f, "send", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags);
    }

    ssize_t sendto(int s, const void *msg, size_t len, int flags,
                   const struct sockaddr *to, socklen_t tolen)
    {
        return sylar::do_io(s, sendto_f, "sendto", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
    }

    ssize_t sendmsg(int s, const struct msghdr *msg, int flags)
    {
        return sylar::do_io(s, sendmsg_f, "sendmsg", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, flags);
    }

    // Hooked socket related functions
    int close(int fd)
    {
        if (!sylar::is_hook_enable())
        {
            return close_f(fd);
        }
        sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
        if (ctx)
        {
            sylar::IOManager *iom = sylar::IOManager::GetThis();
            if (iom)
            {
                if (ctx->isSocket())
                { // Only cancel for sockets managed by IOManager
                    iom->cancelAll(fd);
                }
            }
        }
        int rt = close_f(fd);
        if (ctx && (rt == 0 || errno == EBADF))
        {
            ctx->setClose(true);
            sylar::FdMgr::GetInstance()->del(fd);
        }
        return rt;
    }

    int fcntl(int fd, int cmd, ...)
    {
        va_list va;
        va_start(va, cmd);

        switch (cmd)
        {
        case F_SETFL:
        {
            int arg = va_arg(va, int);
            va_end(va);

            sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
            if (!ctx || ctx->isClose() || !ctx->isSocket())
            {
                return fcntl_f(fd, cmd, arg);
            }

            ctx->setUserNonblock(arg & O_NONBLOCK);

            if (ctx->getSysNonblock())
            {
                arg |= O_NONBLOCK;
            }
            else
            {
                arg &= ~O_NONBLOCK;
            }
            return fcntl_f(fd, cmd, arg);
        }
        break;
        case F_GETFL:
        {
            va_end(va);
            sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
            if (!ctx || ctx->isClose() || !ctx->isSocket())
            {
                return fcntl_f(fd, cmd);
            }
            int arg = fcntl_f(fd, cmd);
            return ctx->getUserNonblock() ? (arg | O_NONBLOCK) : (arg & ~O_NONBLOCK);
        }
        break;
        case F_GETFD:
        case F_GETOWN:
#ifdef F_GETSIG
        case F_GETSIG:
#endif
#ifdef F_GETLEASE
        case F_GETLEASE:
#endif
#ifdef F_GETPIPE_SZ
        case F_GETPIPE_SZ:
#endif
#ifdef F_GET_SEALS
        case F_GET_SEALS:
#endif
            va_end(va);
            return fcntl_f(fd, cmd);
        case F_DUPFD:
#ifdef F_DUPFD_CLOEXEC
        case F_DUPFD_CLOEXEC:
#endif
        case F_SETFD:
        case F_SETOWN:
#ifdef F_SETSIG
        case F_SETSIG:
#endif
#ifdef F_SETLEASE
        case F_SETLEASE:
#endif
#ifdef F_NOTIFY
        case F_NOTIFY:
#endif
#ifdef F_SETPIPE_SZ
        case F_SETPIPE_SZ:
#endif
#ifdef F_ADD_SEALS
        case F_ADD_SEALS:
#endif
        {
            int arg = va_arg(va, int);
            va_end(va);
            return fcntl_f(fd, cmd, arg);
        }
        case F_SETLK:
        case F_GETLK:
        case F_SETLKW:
        {
            struct flock *arg = va_arg(va, struct flock *);
            va_end(va);
            return fcntl_f(fd, cmd, arg);
        }
#ifdef F_OFD_GETLK
        case F_OFD_GETLK:
#endif
#ifdef F_OFD_SETLK
        case F_OFD_SETLK:
#endif
#ifdef F_OFD_SETLKW
        case F_OFD_SETLKW:
#endif
        {
            struct flock *arg = va_arg(va, struct flock *);
            va_end(va);
            return fcntl_f(fd, cmd, arg);
        }
#ifdef F_GET_RW_HINT
        case F_GET_RW_HINT:
#endif
#ifdef F_GET_FILE_RW_HINT
        case F_GET_FILE_RW_HINT:
#endif
#ifdef F_SET_RW_HINT
        case F_SET_RW_HINT:
#endif
#ifdef F_SET_FILE_RW_HINT
        case F_SET_FILE_RW_HINT:
#endif
        {
            uint64_t *arg = va_arg(va, uint64_t *);
            va_end(va);
            return fcntl_f(fd, cmd, arg);
        }
        default:
        {
            long arg = va_arg(va, long);
            va_end(va);
            return fcntl_f(fd, cmd, arg);
        }
        }
    }

    int ioctl(int d, unsigned long int request, ...)
    {
        va_list va;
        va_start(va, request);
        void *arg = va_arg(va, void *);
        va_end(va);

        if (!sylar::is_hook_enable())
        {
            return ioctl_f(d, request, arg);
        }

        sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(d);
        if (!ctx || ctx->isClose() || !ctx->isSocket())
        {
            return ioctl_f(d, request, arg);
        }

        if (request == FIONBIO)
        {
            if (arg)
            {
                bool user_nonblock = !!(*static_cast<int *>(arg));
                ctx->setUserNonblock(user_nonblock);
            }
        }
        return ioctl_f(d, request, arg);
    }

    int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
    {
        return getsockopt_f(sockfd, level, optname, optval, optlen);
    }

    int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
    {
        if (!sylar::is_hook_enable())
        {
            return setsockopt_f(sockfd, level, optname, optval, optlen);
        }
        int rt = setsockopt_f(sockfd, level, optname, optval, optlen);
        if (rt != 0)
        {
            return rt;
        }
        if (level == SOL_SOCKET)
        {
            if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO)
            {
                if (optval && optlen >= (socklen_t)sizeof(timeval))
                {
                    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(sockfd);
                    if (ctx)
                    {
                        const timeval *v = static_cast<const timeval *>(optval);
                        ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
                    }
                }
            }
        }
        return rt;
    }

} // extern "C"
