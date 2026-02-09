#include "sylar/fiber/iomanager.h"
#include "sylar/base/macro.h"
#include "sylar/log/logger.h"

#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

namespace sylar
{

    static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

    /**
     * @brief 获取指定事件的上下文
     * @details READ 对应 read 成员，WRITE 对应 write 成员
     */
    IOManager::FdContext::EventContext &IOManager::FdContext::getEventContext(IOManager::Event event)
    {
        switch (event)
        {
        case IOManager::READ:
            return read;
        case IOManager::WRITE:
            return write;
        default:
            SYLAR_ASSERT2(false, "getEventContext invalid event");
        }
        throw std::invalid_argument("getEventContext invalid event");
    }

    /**
     * @brief 重置事件上下文，释放协程引用
     */
    void IOManager::FdContext::resetEventContext(EventContext &ctx)
    {
        ctx.scheduler = nullptr;
        ctx.fiber.reset();
        ctx.cb = nullptr;
    }

    /**
     * @brief 触发事件处理
     * @details 将对应的回调函数或协程投入调度器执行，并清除该事件标志
     */
    void IOManager::FdContext::triggerEvent(IOManager::Event event)
    {
        // 确认该事件确实在当前监听列表中
        SYLAR_ASSERT(events & event);

        // 清除掉该事件（一次性触发）
        events = (Event)(events & ~event);

        EventContext &ctx = getEventContext(event);
        if (ctx.cb)
        {
            // 如果注册的是回调函数，则将函数推入调度任务队列
            ctx.scheduler->schedule(&ctx.cb);
        }
        else
        {
            // 如果注册的是协程，则将协程推入调度任务队列
            ctx.scheduler->schedule(&ctx.fiber);
        }

        ctx.scheduler = nullptr;
        return;
    }

    IOManager::IOManager(size_t threads, bool use_caller, const std::string &name)
        : Scheduler(threads, use_caller, name)
    {

        // 1. 创建 epoll 句柄
        m_epfd = epoll_create(5000);
        SYLAR_ASSERT(m_epfd > 0);

        // 2. 创建用于 tickle (唤醒) 的管道
        int rt = pipe(m_tickleFds);
        SYLAR_ASSERT(rt == 0);

        // 3. 将管道的读端注册到 epoll 中
        // 使用边缘触发 (ET) 模式，配合非阻塞读取，这是高性能 IO 的标准做法
        struct epoll_event event;
        memset(&event, 0, sizeof(epoll_event));
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = m_tickleFds[0];

        // 将管道读端设为非阻塞，防止 epoll 误唤醒导致 read 阻塞整个线程
        rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
        SYLAR_ASSERT(rt == 0);

        // 将 tickle fd 添加进 epoll 监控
        rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
        SYLAR_ASSERT(rt == 0);

        // 4. 初始化上下文容器大小
        contextResize(32);

        // 5. 启动调度器线程池
        start();
    }

    IOManager::~IOManager()
    {
        // 停止调度循环
        stop();
        // 释放系统资源
        close(m_epfd);
        close(m_tickleFds[0]);
        close(m_tickleFds[1]);

        // 释放 Fd 上下文对象的堆内存
        for (size_t i = 0; i < m_fdContexts.size(); ++i)
        {
            if (m_fdContexts[i])
            {
                delete m_fdContexts[i];
            }
        }
    }

    /**
     * @brief 动态扩容 fd 上下文容器
     * @details 采用预分配策略，以 fd 为索引直接定位上下文，避免 Map 查找开销
     */
    void IOManager::contextResize(size_t size)
    {
        m_fdContexts.resize(size);

        for (size_t i = 0; i < m_fdContexts.size(); ++i)
        {
            if (!m_fdContexts[i])
            {
                m_fdContexts[i] = new FdContext;
                m_fdContexts[i]->fd = i;
            }
        }
    }

    /**
     * @brief 添加事件
     * @details 核心逻辑：
     * 1. 找到 fd 对应的 FdContext，必要时扩容。
     * 2. 检查该事件是否已经存在，避免重复添加。
     * 3. 确定 epoll_ctl 的操作类型 (ADD 或 MOD)。
     * 4. 修改 epoll 事件掩码并执行系统调用。
     * 5. 更新上下文信息（调度器、回调协程/函数）。
     */
    int IOManager::addEvent(int fd, Event event, std::function<void()> cb)
    {
        FdContext *fd_ctx = nullptr;
        RWMutexType::ReadLock lock(m_mutex);
        if ((int)m_fdContexts.size() > fd)
        {
            fd_ctx = m_fdContexts[fd];
            lock.unlock();
        }
        else
        {
            lock.unlock();
            RWMutexType::WriteLock lock2(m_mutex);
            contextResize(fd * 1.5);
            fd_ctx = m_fdContexts[fd];
        }

        FdContext::MutexType::Lock lock2(fd_ctx->mutex);
        // 检查事件是否重复添加
        if (SYLAR_UNLIKELY(fd_ctx->events & event))
        {
            SYLAR_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd
                                      << " event=" << event
                                      << " fd_ctx.event=" << fd_ctx->events;
            SYLAR_ASSERT(!(fd_ctx->events & event));
        }

        // 确定是第一次添加监听(ADD)还是在原有基础上修改(MOD)
        int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
        struct epoll_event epevent;
        // 采用边缘触发 (ET)，同时合并原有监听的事件
        epevent.events = EPOLLET | fd_ctx->events | event;
        // 关键点：将上下文对象指针存入 epoll，实现 O(1) 查找
        epevent.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, op, fd, &epevent);
        if (rt)
        {
            SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                      << op << "," << fd << "," << epevent.events << "):"
                                      << rt << " (" << errno << ") (" << strerror(errno) << ")";
            return -1;
        }

        // 待处理事件计数增加
        ++m_pendingEventCount;
        // 更新该 fd 上下文中的事件掩码
        fd_ctx->events = (Event)(fd_ctx->events | event);
        
        // 获取并配置对应事件的上下文
        FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
        SYLAR_ASSERT(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);

        event_ctx.scheduler = Scheduler::GetThis();
        if (cb)
        {
            event_ctx.cb.swap(cb);
        }
        else
        {
            // 如果没传回调，默认绑定当前协程，实现“在当前位置等待 IO 唤醒”的同步编程感
            event_ctx.fiber = Fiber::GetThis();
            SYLAR_ASSERT2(event_ctx.fiber->getState() == Fiber::EXEC, "state=" << event_ctx.fiber->getState());
        }
        return 0;
    }

    /**
     * @brief 删除事件
     * @details 物理删除：直接从 epoll 中撤销，且不触发任何回调
     */
    bool IOManager::delEvent(int fd, Event event)
    {
        RWMutexType::ReadLock lock(m_mutex);
        if ((int)m_fdContexts.size() <= fd)
        {
            return false;
        }
        FdContext *fd_ctx = m_fdContexts[fd];
        lock.unlock();

        FdContext::MutexType::Lock lock2(fd_ctx->mutex);
        if (SYLAR_UNLIKELY(!(fd_ctx->events & event)))
        {
            return false;
        }

        // 计算删除后的剩余事件掩码
        Event new_events = (Event)(fd_ctx->events & ~event);
        int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        struct epoll_event epevent;
        epevent.events = EPOLLET | new_events;
        epevent.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, op, fd, &epevent);
        if (rt)
        {
            SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                      << op << "," << fd << "," << epevent.events << "):"
                                      << rt << " (" << errno << ") (" << strerror(errno) << ")";
            return false;
        }

        --m_pendingEventCount;
        fd_ctx->events = new_events;
        FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
        fd_ctx->resetEventContext(event_ctx);
        return true;
    }

    /**
     * @brief 取消事件
     * @details 逻辑删除：如果事件存在，则强制触发它一次（恢复协程执行），然后撤销监听
     */
    bool IOManager::cancelEvent(int fd, Event event)
    {
        RWMutexType::ReadLock lock(m_mutex);
        if ((int)m_fdContexts.size() <= fd)
        {
            return false;
        }
        FdContext *fd_ctx = m_fdContexts[fd];
        lock.unlock();

        FdContext::MutexType::Lock lock2(fd_ctx->mutex);
        if (SYLAR_UNLIKELY(!(fd_ctx->events & event)))
        {
            return false;
        }

        Event new_events = (Event)(fd_ctx->events & ~event);
        int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        struct epoll_event epevent;
        epevent.events = EPOLLET | new_events;
        epevent.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, op, fd, &epevent);
        if (rt)
        {
            SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                      << op << "," << fd << "," << epevent.events << "):"
                                      << rt << " (" << errno << ") (" << strerror(errno) << ")";
            return false;
        }

        // 核心：在删除前主动触发，保证了即使事件没发生，被取消的协程也能从 yield 处醒来
        fd_ctx->triggerEvent(event);
        --m_pendingEventCount;
        return true;
    }

    /**
     * @brief 取消所有事件
     */
    bool IOManager::cancelAll(int fd)
    {
        RWMutexType::ReadLock lock(m_mutex);
        if ((int)m_fdContexts.size() <= fd)
        {
            return false;
        }
        FdContext *fd_ctx = m_fdContexts[fd];
        lock.unlock();

        FdContext::MutexType::Lock lock2(fd_ctx->mutex);
        if (!fd_ctx->events)
        {
            return false;
        }

        int op = EPOLL_CTL_DEL;
        struct epoll_event epevent;
        epevent.events = 0;
        epevent.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, op, fd, &epevent);
        if (rt)
        {
            SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                      << op << "," << fd << "," << epevent.events << "):"
                                      << rt << " (" << errno << ") (" << strerror(errno) << ")";
            return false;
        }

        // 遍历并触发读写事件
        if (fd_ctx->events & READ)
        {
            fd_ctx->triggerEvent(READ);
            --m_pendingEventCount;
        }
        if (fd_ctx->events & WRITE)
        {
            fd_ctx->triggerEvent(WRITE);
            --m_pendingEventCount;
        }

        SYLAR_ASSERT(fd_ctx->events == NONE);
        return true;
    }

    /**
     * @brief 唤醒调度器线程
     * @details 通过自唤醒管道写入数据，打破 epoll_wait 的阻塞
     */
    void IOManager::tickle()
    {
        if (!hasIdleThreads())
        {
            return;
        }
        int rt = write(m_tickleFds[1], "T", 1);
        SYLAR_ASSERT(rt == 1);
    }

    /**
     * @brief 停止条件判断
     */
    bool IOManager::stopping()
    {
        // 只有当没有待处理 IO 事件，且基类 Scheduler 确认可以停止时才真正停止
        return m_pendingEventCount == 0 && Scheduler::stopping();
    }

    /**
     * @brief 核心 IO 调度循环
     * @details 
     * 1. 当 Scheduler::run() 发现没活干时会切进此协程。
     * 2. 阻塞在 epoll_wait 处进入内核睡眠。
     * 3. 醒来后处理就绪事件，将对应的任务放入调度队列。
     * 4. 执行完分发后 Yield 出去，让线程去跑领到的业务协程。
     */
    void IOManager::idle()
    {
        // 每次处理的最大事件数
        static const uint64_t MAX_EVNETS = 256;
        epoll_event *events = new epoll_event[MAX_EVNETS]();
        std::shared_ptr<epoll_event> shared_events(events, [](epoll_event *ptr)
                                                   { delete[] ptr; });

        while (true)
        {
            if (stopping())
            {
                SYLAR_LOG_INFO(g_logger) << "name=" << getName() << " idle stopping exit";
                break;
            }

            int rt = 0;
            do
            {
                // 默认超时 5 秒（后续集成 TimerManager 后会改为最近定时器的超时时间）
                static const uint64_t MAX_TIMEOUT = 5000;
                rt = epoll_wait(m_epfd, events, MAX_EVNETS, (int)MAX_TIMEOUT);

                if (rt < 0 && errno == EINTR)
                {
                    // 忽略系统调用中断
                }
                else
                {
                    break;
                }
            } while (true);

            for (int i = 0; i < rt; ++i)
            {
                epoll_event &event = events[i];
                // 如果是自唤醒消息，读空管道继续
                if (event.data.fd == m_tickleFds[0])
                {
                    uint8_t dummy[256];
                    while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0);
                    continue;
                }

                // 拿到注册时绑定的 FdContext
                FdContext *fd_ctx = (FdContext *)event.data.ptr;
                FdContext::MutexType::Lock lock(fd_ctx->mutex);
                
                // 处理错误事件：转成读写事件由上层处理
                if (event.events & (EPOLLERR | EPOLLHUP))
                {
                    event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
                }
                
                uint32_t real_events = NONE;
                if (event.events & EPOLLIN)
                {
                    real_events |= READ;
                }
                if (event.events & EPOLLOUT)
                {
                    real_events |= WRITE;
                }

                if ((fd_ctx->events & real_events) == NONE)
                {
                    continue;
                }

                // 移除已触发的事件，剩下未触发的重新 MOD 注册（一次性触发模式）
                uint32_t left_events = (fd_ctx->events & ~real_events);
                int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
                event.events = EPOLLET | left_events;

                int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
                if (rt2)
                {
                    SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                              << op << "," << fd_ctx->fd << "," << event.events << "):"
                                              << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                    continue;
                }

                // 分别触发就绪的读写事件
                if (real_events & READ)
                {
                    fd_ctx->triggerEvent(READ);
                    --m_pendingEventCount;
                }
                if (real_events & WRITE)
                {
                    fd_ctx->triggerEvent(WRITE);
                    --m_pendingEventCount;
                }
            }

            // 执行完一轮事件分发后，让出执行权，让 run() 里的调度逻辑去运行刚才放入的任务
            Fiber::ptr cur = Fiber::GetThis();
            auto raw_ptr = cur.get();
            cur.reset();
            raw_ptr->yield();
        }
    }

    IOManager *IOManager::GetThis()
    {
        return dynamic_cast<IOManager *>(Scheduler::GetThis());
    }

}
