/**
 * @file fd_manager.cc
 * @brief 文件句柄管理类实现
 * @details 只管理socket fd，记录fd是否为socket，用户是否设置非阻塞，系统是否设置非阻塞，send/recv超时时间
 *          提供FdManager单例和get/del方法，用于创建/获取/删除fd
 * @version 0.1
 * @date 2021-06-21
 */
#include "fd_manager.h"
#include "hook.h"
#include <fcntl.h>

namespace sylar
{

    /**
     * @brief FdCtx 构造函数
     * @param[in] fd 文件描述符
     * @details 初始化所有成员变量为默认值，并调用 init() 进行实际初始化
     *          使用位域优化内存占用（5个bool只占1字节）
     *          超时时间初始化为 (uint64_t)-1，表示无限超时
     */
    FdCtx::FdCtx(int fd)
        : m_isInit(false),          // 初始化标志，默认未初始化
          m_isSocket(false),         // socket标志，默认不是socket
          m_sysNonblock(false),      // 系统非阻塞标志，默认阻塞
          m_userNonblock(false),     // 用户非阻塞标志，默认阻塞
          m_isClosed(false),         // 关闭标志，默认未关闭
          m_fd(fd),                  // 保存文件描述符
          m_recvTimeout((uint64_t)-1), // 接收超时，-1表示永不超时
          m_sendTimeout((uint64_t)-1)  // 发送超时，-1表示永不超时
    {
        init(); // 调用初始化函数，检测fd类型并设置属性
    }

    /**
     * @brief FdCtx 析构函数
     * @details 空实现，因为所有资源都由智能指针自动管理
     *          fd的关闭由外部（hook的close函数）负责
     */
    FdCtx::~FdCtx()
    {
    }

    /**
     * @brief 初始化文件描述符上下文
     * @return true 初始化成功，false 初始化失败
     * @details 核心初始化逻辑：
     *          1. 使用 fstat 检测 fd 是否有效以及是否为 socket
     *          2. 如果是 socket，强制设置为非阻塞模式（系统级）
     *          3. 初始化超时时间为无限
     *
     * @note 为什么要强制设置 socket 为非阻塞？
     *       因为协程框架的 Hook 机制依赖非阻塞 IO：
     *       - 阻塞模式下，read/write 会卡死整个线程
     *       - 非阻塞模式下，遇到 EAGAIN 可以 yield 让出 CPU
     *       - IOManager 通过 epoll 监听，数据就绪后再 resume
     *
     * @note m_sysNonblock vs m_userNonblock 的区别：
     *       - m_sysNonblock: 系统实际的非阻塞状态（框架强制设置）
     *       - m_userNonblock: 用户通过 fcntl 主动设置的状态
     *       - 这种分离设计让 Hook 对用户透明：用户以为 fd 是阻塞的，
     *         但底层已经是非阻塞 + 协程调度
     */
    bool FdCtx::init()
    {
        // 防止重复初始化
        if (m_isInit)
        {
            return true;
        }

        // 重置超时时间为无限（-1 的补码表示最大值）
        m_recvTimeout = (uint64_t)-1;
        m_sendTimeout = (uint64_t)-1;

        // 使用 fstat 获取文件描述符的状态信息
        struct stat fd_stat;
        if (-1 == fstat(m_fd, &fd_stat))
        {
            // fstat 失败，说明 fd 无效（可能已关闭或从未打开）
            m_isInit = false;
            m_isSocket = false;
            return m_isInit;
        }
        else
        {
            // fstat 成功，标记为已初始化
            m_isInit = true;
            // 使用 S_ISSOCK 宏判断是否为 socket 类型
            // st_mode 包含文件类型和权限信息
            m_isSocket = S_ISSOCK(fd_stat.st_mode);
        }

        // 只对 socket 类型的 fd 进行非阻塞设置
        if (m_isSocket)
        {
            // 使用原始的 fcntl 函数（fcntl_f 是保存的系统原始函数指针）
            // 获取当前的文件状态标志
            int flags = fcntl_f(m_fd, F_GETFL, 0);

            // 检查是否已经是非阻塞模式
            if (!(flags & O_NONBLOCK))
            {
                // 如果不是非阻塞，则添加 O_NONBLOCK 标志
                // 这是协程框架的核心要求：所有 socket 必须是非阻塞的
                fcntl_f(m_fd, F_SETFL, flags | O_NONBLOCK);
            }

            // 标记系统已设置为非阻塞
            // 注意：即使原本就是非阻塞，这里也标记为 true
            // 因为我们要确保 socket 在框架中始终是非阻塞的
            m_sysNonblock = true;
        }
        else
        {
            // 非 socket 类型（如普通文件、管道等）不设置非阻塞
            // 因为普通文件的 read/write 不会真正阻塞（除非磁盘故障）
            m_sysNonblock = false;
        }

        // 用户非阻塞标志初始化为 false
        // 这个标志只有在用户显式调用 fcntl 设置 O_NONBLOCK 时才会变为 true
        m_userNonblock = false;

        // 关闭标志初始化为 false
        m_isClosed = false;

        return m_isInit;
    }

    /**
     * @brief 设置超时时间
     * @param[in] type 超时类型，SO_RCVTIMEO(接收超时) 或 SO_SNDTIMEO(发送超时)
     * @param[in] v 超时时间（毫秒）
     * @details 这个函数配合 Hook 的 setsockopt 使用
     *          当用户调用 setsockopt 设置超时时，Hook 函数会调用这里保存超时值
     *          后续在 Hook 的 read/write 中，会使用这个超时值创建定时器
     *
     * @note 为什么要单独保存超时时间？
     *       因为在非阻塞 + epoll 模式下，系统的 SO_RCVTIMEO/SO_SNDTIMEO 不起作用
     *       框架需要自己实现超时机制：
     *       1. 注册 IO 事件到 epoll
     *       2. 同时注册一个定时器（超时时间为这里保存的值）
     *       3. 哪个先触发就执行哪个（IO就绪 or 超时）
     */
    void FdCtx::setTimeout(int type, uint64_t v)
    {
        if (type == SO_RCVTIMEO)
        {
            // 设置接收（读）超时
            m_recvTimeout = v;
        }
        else
        {
            // 设置发送（写）超时
            // 这里假设 type 只能是 SO_RCVTIMEO 或 SO_SNDTIMEO
            m_sendTimeout = v;
        }
    }

    /**
     * @brief 获取超时时间
     * @param[in] type 超时类型，SO_RCVTIMEO(接收超时) 或 SO_SNDTIMEO(发送超时)
     * @return 超时时间（毫秒），(uint64_t)-1 表示永不超时
     * @details Hook 的 read/write 函数会调用这个方法获取超时时间
     *          用于决定 epoll_wait 的超时参数和定时器的触发时间
     */
    uint64_t FdCtx::getTimeout(int type)
    {
        if (type == SO_RCVTIMEO)
        {
            // 返回接收（读）超时
            return m_recvTimeout;
        }
        else
        {
            // 返回发送（写）超时
            return m_sendTimeout;
        }
    }

    /**
     * @brief FdManager 构造函数
     * @details 预分配 64 个 FdCtx 指针的空间
     *          这是一个性能优化：避免频繁的 vector 扩容
     *          64 是一个经验值，对于大多数程序来说，初期的 fd 数量不会超过这个值
     *
     * @note 为什么使用 vector 而不是 map？
     *       因为 fd 是从 0 开始的连续整数，使用 vector 可以实现 O(1) 的访问
     *       map 的查找是 O(log n)，在高并发场景下性能差距明显
     */
    FdManager::FdManager()
    {
        m_datas.resize(64); // 预分配 64 个槽位，初始值为 nullptr
    }

    /**
     * @brief 获取或创建文件描述符上下文
     * @param[in] fd 文件描述符
     * @param[in] auto_create 如果不存在是否自动创建，默认 false
     * @return FdCtx::ptr 文件描述符上下文智能指针，失败返回 nullptr
     *
     * @details 这是 FdManager 的核心方法，实现了线程安全的"懒加载"模式：
     *          1. 先用读锁尝试获取（快速路径）
     *          2. 如果不存在且需要创建，升级为写锁（慢速路径）
     *          3. 创建新的 FdCtx 并存入 vector
     *
     * @note 双重检查锁定（Double-Checked Locking）模式：
     *       1. 第一次检查（读锁）：快速判断是否已存在
     *       2. 释放读锁，获取写锁
     *       3. 第二次检查（写锁）：防止并发创建（虽然这里没有显式的第二次检查）
     *       4. 创建对象并存储
     *
     * @note 为什么要手动 unlock 读锁？
     *       因为 RAII 的 ReadLock 会在作用域结束时自动释放
     *       但我们需要在获取写锁之前释放读锁，避免死锁
     *       （同一线程不能同时持有读锁和写锁）
     *
     * @note vector 的动态扩容策略：
     *       当 fd >= size 时，扩容到 fd * 1.5
     *       这是一个平衡策略：既避免频繁扩容，又不会浪费太多内存
     */
    FdCtx::ptr FdManager::get(int fd, bool auto_create)
    {
        // 无效的 fd，直接返回空指针
        if (fd == -1)
        {
            return nullptr;
        }

        // ========== 快速路径：读锁保护的查询 ==========
        RWMutexType::ReadLock lock(m_mutex);

        // 情况1：fd 超出当前 vector 范围
        if ((int)m_datas.size() <= fd)
        {
            // 如果不自动创建，直接返回空
            if (auto_create == false)
            {
                return nullptr;
            }
            // 如果需要自动创建，跳到慢速路径（需要写锁）
        }
        // 情况2：fd 在范围内
        else
        {
            // 如果已存在 或 不需要自动创建，直接返回
            // m_datas[fd] 可能是 nullptr（从未创建过）或有效指针
            if (m_datas[fd] || !auto_create)
            {
                return m_datas[fd];
            }
            // 如果不存在且需要创建，跳到慢速路径
        }

        // 手动释放读锁，准备获取写锁
        // 如果不释放，下面的写锁会导致死锁（同一线程持有读锁又要获取写锁）
        lock.unlock();

        // ========== 慢速路径：写锁保护的创建 ==========
        RWMutexType::WriteLock lock2(m_mutex);

        // 创建新的 FdCtx 对象
        // 构造函数会自动调用 init() 检测 fd 类型并设置属性
        FdCtx::ptr ctx(new FdCtx(fd));

        // 如果 fd 超出当前容量，需要扩容
        if (fd >= (int)m_datas.size())
        {
            // 扩容策略：扩大到 fd * 1.5
            // 例如：fd=100, size=64 -> resize(150)
            // 这样可以减少后续的扩容次数
            m_datas.resize(fd * 1.5);
        }

        // 将新创建的 FdCtx 存入 vector
        m_datas[fd] = ctx;

        // 返回新创建的 FdCtx
        return ctx;
    }

    /**
     * @brief 删除文件描述符上下文
     * @param[in] fd 文件描述符
     * @details 从 vector 中移除指定 fd 的上下文
     *          使用 reset() 将智能指针置空，引用计数减1
     *          如果没有其他地方持有该 FdCtx，对象会被自动销毁
     *
     * @note 什么时候调用这个函数？
     *       在 Hook 的 close() 函数中，关闭 fd 后会调用这里清理上下文
     *       这样可以释放内存，避免 vector 无限增长
     *
     * @note 为什么不缩小 vector？
     *       因为 fd 可能会被重用（操作系统会优先分配最小的可用 fd）
     *       保留空间可以避免后续的重新分配
     *       只是将指针置空，内存开销很小（每个槽位只占 8 字节）
     *
     * @note 线程安全：
     *       使用写锁保护，确保删除操作的原子性
     *       防止并发的 get/del 操作导致数据竞争
     */
    void FdManager::del(int fd)
    {
        // 写锁保护整个删除过程
        RWMutexType::WriteLock lock(m_mutex);

        // 如果 fd 超出范围，说明从未创建过，直接返回
        if ((int)m_datas.size() <= fd)
        {
            return;
        }

        // 将智能指针置空
        // reset() 会减少引用计数，如果计数归零则自动销毁 FdCtx 对象
        m_datas[fd].reset();
    }

} // namespace sylar
