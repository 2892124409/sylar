#include "ai/storage/mysql_connection_pool.h"

#include "ai/common/ai_utils.h"
#include "log/logger.h"
#include "sylar/fiber/iomanager.h"

#include <algorithm>

namespace ai
{
namespace storage
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

/**
 * @brief 构造连接池对象（不触发连接创建）。
 */
MysqlConnectionPool::MysqlConnectionPool()
    : m_initialized(false)
    , m_shutdown(false)
    , m_total_connections(0)
{
}

/**
 * @brief 析构时自动关闭连接池。
 */
MysqlConnectionPool::~MysqlConnectionPool()
{
    Shutdown();
}

/**
 * @brief 创建一条新的 MySQL 连接。
 */
bool MysqlConnectionPool::CreateConnection(MYSQL*& out, std::string& error)
{
    // 1) mysql_init: 初始化/分配一个 MYSQL 连接句柄。
    //    原型: MYSQL* mysql_init(MYSQL* mysql);
    //    - 入参 mysql:
    //      1. 传 nullptr: 由库内部新分配一个 MYSQL 对象（本项目采用这种方式）。
    //      2. 传已有 MYSQL*: 在该对象上做初始化（较少使用）。
    //    - 返回值:
    //      1. 非空: 成功，返回可继续配置/连接的句柄。
    //      2. 空指针: 失败（通常是内存分配失败），后续不能再调用连接相关 API。
    out = mysql_init(nullptr);
    if (!out)
    {
        error = "mysql_init failed";
        return false;
    }

    // 2) mysql_options: 在 mysql_real_connect 之前设置连接选项。
    //    原型: int mysql_options(MYSQL* mysql, enum mysql_option option, const void* arg);
    //    - 返回值: 0 表示成功，非 0 表示失败。
    //    - 这里设置 MYSQL_SET_CHARSET_NAME:
    //      arg 需要传 const char*，例如 "utf8mb4"；用于指定客户端连接字符集。
    //    - 这里设置 MYSQL_OPT_CONNECT_TIMEOUT:
    //      arg 需要传 unsigned int*（秒）；用于限制建连阶段超时时间。
    //    这里显式检查返回值，任何失败都立即中止建连流程。
    if (mysql_options(out, MYSQL_SET_CHARSET_NAME, m_settings.charset.c_str()) != 0)
    {
        // mysql_error:
        // 原型: const char* mysql_error(MYSQL* mysql);
        // - 入参: 对应的 MYSQL 句柄。
        // - 返回值: 最近一次 MySQL API 错误文本（库管理内存，无需释放）。
        error = std::string("mysql_options(MYSQL_SET_CHARSET_NAME) failed: ") + mysql_error(out);
        mysql_close(out);
        out = nullptr;
        return false;
    }
    if (mysql_options(out, MYSQL_OPT_CONNECT_TIMEOUT, &m_settings.connect_timeout_seconds) != 0)
    {
        // mysql_error 用法同上。
        error = std::string("mysql_options(MYSQL_OPT_CONNECT_TIMEOUT) failed: ") + mysql_error(out);
        mysql_close(out);
        out = nullptr;
        return false;
    }

    // 3) mysql_real_connect: 发起真实连接并完成认证。
    //    原型:
    //    MYSQL* mysql_real_connect(
    //        MYSQL* mysql,
    //        const char* host,
    //        const char* user,
    //        const char* passwd,
    //        const char* db,
    //        unsigned int port,
    //        const char* unix_socket,
    //        unsigned long client_flag);
    //    - 参数说明:
    //      1. mysql: 第 1 步拿到的 MYSQL 句柄。
    //      2. host/user/passwd/db/port: 目标实例地址、凭据、库名和端口。
    //      3. unix_socket: 传 nullptr 表示走 TCP；若填路径则可走本地 socket。
    //      4. client_flag:
    //         使用 CLIENT_MULTI_STATEMENTS，表示客户端允许发送多语句请求。
    //    - 返回值:
    //      1. 非空: 连接成功（通常返回 mysql 本身）。
    //      2. 空指针: 连接失败，可通过 mysql_error(mysql) 读取错误信息。
    if (!mysql_real_connect(out, m_settings.host.c_str(), m_settings.user.c_str(), m_settings.password.c_str(),
                            m_settings.database.c_str(), m_settings.port, nullptr, CLIENT_MULTI_STATEMENTS))
    {
        // 4) mysql_error: 获取最近一次 MySQL API 调用错误文本。
        //    原型: const char* mysql_error(MYSQL* mysql);
        //    - 入参: 对应的 MYSQL 句柄。
        //    - 返回值: 以 '\0' 结尾的错误字符串（由库管理内存，调用方无需释放）。
        error = mysql_error(out);

        // 5) mysql_close: 关闭连接并释放 MYSQL 句柄资源。
        //    原型: void mysql_close(MYSQL* sock);
        //    - 入参: 需要关闭的 MYSQL*。
        //    - 返回值: 无（void）。
        mysql_close(out);
        out = nullptr;
        return false;
    }

    // 6) 到这里表示连接建立成功，out 为可用连接句柄。
    return true;
}

/**
 * @brief 初始化连接池并预热最小连接数。
 */
bool MysqlConnectionPool::Init(const config::MysqlSettings& settings, std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // 已初始化时幂等返回。
    if (m_initialized)
    {
        return true;
    }

    // 记录配置并重置关闭态。
    m_settings = settings;
    m_shutdown = false;

    // 预热最小连接数，减少首批查询冷启动延迟。
    for (size_t i = 0; i < m_settings.pool_min_size; ++i)
    {
        MYSQL* conn = nullptr;
        if (!CreateConnection(conn, error))
        {
            return false;
        }

        IdleConn idle;
        idle.conn = conn;
        idle.last_used_ms = common::NowMs();
        m_idle_conns.push_back(idle);
        ++m_total_connections;
    }

    m_initialized = true;
    return true;
}

/**
 * @brief 检查连接是否存活。
 */
bool MysqlConnectionPool::IsConnectionAlive(MYSQL* conn)
{
    if (!conn)
    {
        return false;
    }

    // mysql_ping:
    // 原型: int mysql_ping(MYSQL* mysql);
    // - 入参: 已建立连接的 MYSQL*。
    // - 返回值: 0 表示连接可用；非 0 表示连接不可用/已断开。
    // 该调用会尝试探测并在可恢复场景下重连（取决于客户端配置与版本行为）。
    return mysql_ping(conn) == 0;
}

/**
 * @brief 使用默认超时配置获取连接。
 */
MYSQL* MysqlConnectionPool::Acquire(std::string& error)
{
    return Acquire(m_settings.pool_acquire_timeout_ms, error);
}

/**
 * @brief 给等待者分配连接并唤醒其 fiber。
 */
void MysqlConnectionPool::WakeWaiterWithConn(const Waiter::ptr& waiter, MYSQL* conn)
{
    if (!waiter)
    {
        return;
    }

    sylar::IOManager* iom = sylar::IOManager::GetThis();
    if (!iom)
    {
        return;
    }

    waiter->assigned_conn = conn;
    waiter->done = true;
    iom->schedule(waiter->fiber, waiter->thread);
}

/**
 * @brief 将等待者标记为超时并唤醒。
 */
void MysqlConnectionPool::WakeWaiterTimeout(const Waiter::ptr& waiter)
{
    if (!waiter)
    {
        return;
    }

    sylar::IOManager* iom = sylar::IOManager::GetThis();
    if (!iom)
    {
        return;
    }

    waiter->timed_out = true;
    waiter->done = true;
    iom->schedule(waiter->fiber, waiter->thread);
}

/**
 * @brief 获取连接（支持 fiber 等待与超时）。
 * @details
 * 策略顺序：
 * 1) 有空闲连接 -> 直接返回；
 * 2) 无空闲且未到上限 -> 新建连接并返回；
 * 3) 池已满 -> 当前 fiber 入等待队列并挂起，等待 Release/超时唤醒。
 */
MYSQL* MysqlConnectionPool::Acquire(uint64_t timeout_ms, std::string& error)
{
    // 本次获取请求的截止时间；整个 while 重试循环共享同一个 deadline。
    const uint64_t deadline = common::NowMs() + timeout_ms;

    while (true)
    {
        Waiter::ptr waiter;
        bool create_new_conn = false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // 池关闭后拒绝借连接。
            if (m_shutdown)
            {
                error = "mysql connection pool is shutdown";
                return nullptr;
            }

            // 分支 A：优先从空闲队列取连接。
            while (!m_idle_conns.empty())
            {
                IdleConn idle = m_idle_conns.front();
                m_idle_conns.pop_front();
                MYSQL* conn = idle.conn;
                // 空闲超过 30 秒做心跳检查，淘汰失效连接。
                const uint64_t idle_ms = common::NowMs() - idle.last_used_ms;
                if (idle_ms > 30000 && !IsConnectionAlive(conn))
                {
                    // mysql_close:
                    // 原型: void mysql_close(MYSQL* sock);
                    // - 入参: 要释放的连接句柄。
                    // - 返回值: 无。
                    // 这里用于淘汰空闲过久且心跳失败的失效连接。
                    mysql_close(conn);
                    --m_total_connections;
                    continue;
                }

                return conn;
            }

            // 分支 B：没有空闲连接，但还可以扩容，允许新建。
            if (m_total_connections < m_settings.pool_max_size)
            {
                ++m_total_connections;
                create_new_conn = true;
            }
            else
            {
                // 分支 C：池满，当前 fiber 进入等待队列。
                sylar::Fiber::ptr current = sylar::Fiber::GetThis();
                if (!current)
                {
                    error = "acquire requires fiber context when pool is exhausted";
                    return nullptr;
                }

                waiter.reset(new Waiter);
                waiter->fiber = current;
                // 新调度器下 Fiber 不再暴露绑定线程，使用 -1 让 IOManager 自行调度恢复。
                waiter->thread = -1;
                m_waiters.push_back(waiter);
            }
        }

        if (create_new_conn)
        {
            // 在锁外建连接，避免慢系统调用长时间占锁。
            MYSQL* conn = nullptr;
            if (!CreateConnection(conn, error))
            {
                // 建连失败时回滚计数，保持 total_connections 一致性。
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_total_connections > 0)
                {
                    --m_total_connections;
                }
                return nullptr;
            }
            return conn;
        }

        // 分支 C 继续：池满进入 fiber 挂起等待，避免阻塞工作线程。
        sylar::IOManager* iom = sylar::IOManager::GetThis();
        if (!iom)
        {
            error = "pool exhausted but no iomanager available for fiber wait";
            std::lock_guard<std::mutex> lock(m_mutex);
            m_waiters.erase(std::remove(m_waiters.begin(), m_waiters.end(), waiter), m_waiters.end());
            return nullptr;
        }

        // 若已超出 deadline，则直接失败并从等待队列移除自己。
        uint64_t now = common::NowMs();
        if (now >= deadline)
        {
            error = "acquire mysql connection timeout";
            std::lock_guard<std::mutex> lock(m_mutex);
            m_waiters.erase(std::remove(m_waiters.begin(), m_waiters.end(), waiter), m_waiters.end());
            return nullptr;
        }

        // 安装超时定时器：到时标记 waiter 超时并唤醒对应 fiber。
        uint64_t wait_ms = deadline - now;
        sylar::Timer::ptr timer =
            iom->addTimer(wait_ms,
                          [this, waiter]()
                          {
                              std::lock_guard<std::mutex> lock(m_mutex);
                              if (waiter->done)
                              {
                                  return;
                              }
                              waiter->timed_out = true;
                              waiter->done = true;
                              m_waiters.erase(std::remove(m_waiters.begin(), m_waiters.end(), waiter), m_waiters.end());

                              sylar::IOManager* local_iom = sylar::IOManager::GetThis();
                              if (local_iom)
                              {
                                  local_iom->schedule(waiter->fiber, waiter->thread);
                              }
                          });

        // 主动让出当前 fiber，等待 Release/超时回调恢复。
        sylar::Fiber::YieldToHold();
        // 恢复后取消定时器，避免后续重复触发。
        timer->cancel();

        // 被唤醒后按原因分流：拿到连接 or 超时失败。
        if (waiter->assigned_conn)
        {
            return waiter->assigned_conn;
        }

        if (waiter->timed_out)
        {
            error = "acquire mysql connection timeout";
            return nullptr;
        }
    }
}

/**
 * @brief 归还连接。
 * @details
 * 优先把连接直接交给等待者；没有等待者再放回空闲队列。
 */
void MysqlConnectionPool::Release(MYSQL* conn)
{
    if (!conn)
    {
        return;
    }

    Waiter::ptr waiter;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // 池关闭后不再复用，直接关闭连接。
        if (m_shutdown)
        {
            // mysql_close:
            // 原型: void mysql_close(MYSQL* sock);
            // - 入参: 要关闭的连接句柄。
            // - 返回值: 无。
            // 关闭后该连接不可再使用。
            // mysql_close: 池已关闭时不再复用该连接，直接释放。
            mysql_close(conn);
            if (m_total_connections > 0)
            {
                --m_total_connections;
            }
            return;
        }

        // 找到第一个仍在等待的有效 waiter。
        while (!m_waiters.empty())
        {
            waiter = m_waiters.front();
            m_waiters.pop_front();
            if (waiter && !waiter->done)
            {
                break;
            }
            waiter.reset();
        }

        if (!waiter)
        {
            // 无等待者：归还到空闲队列。
            IdleConn idle;
            idle.conn = conn;
            idle.last_used_ms = common::NowMs();
            m_idle_conns.push_back(idle);
            return;
        }

        // 有等待者：直接把连接转交给该 waiter。
        waiter->assigned_conn = conn;
        waiter->done = true;
    }

    sylar::IOManager* iom = sylar::IOManager::GetThis();
    if (!iom)
    {
        // 理论上 Release 运行在 IOM 线程，兜底直接放回空闲队列。
        std::lock_guard<std::mutex> lock(m_mutex);
        waiter->done = false;
        waiter->assigned_conn = nullptr;
        IdleConn idle;
        idle.conn = conn;
        idle.last_used_ms = common::NowMs();
        m_idle_conns.push_back(idle);
        return;
    }

    // 唤醒等待 fiber，使其从 Acquire 的 YieldToHold 处继续执行。
    iom->schedule(waiter->fiber, waiter->thread);
}

/**
 * @brief 关闭连接池并释放资源。
 */
void MysqlConnectionPool::Shutdown()
{
    std::deque<IdleConn> idle;
    std::deque<Waiter::ptr> waiters;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shutdown)
        {
            return;
        }

        // 切换关闭态，并把资源转移到局部变量，后续在锁外处理。
        m_shutdown = true;
        idle.swap(m_idle_conns);
        waiters.swap(m_waiters);
    }

    // 关闭所有空闲连接。
    for (size_t i = 0; i < idle.size(); ++i)
    {
        if (idle[i].conn)
        {
            // mysql_close 用法同上，这里批量释放空闲连接。
            // mysql_close: 关闭池内剩余空闲连接，释放底层网络与句柄资源。
            mysql_close(idle[i].conn);
        }
    }

    // 标记并唤醒全部等待者，防止其永久挂起。
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    for (size_t i = 0; i < waiters.size(); ++i)
    {
        if (!waiters[i])
        {
            continue;
        }
        waiters[i]->done = true;
        waiters[i]->timed_out = true;
        if (iom)
        {
            iom->schedule(waiters[i]->fiber, waiters[i]->thread);
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_total_connections = 0;
}

/**
 * @brief RAII 守卫构造：尝试借连接。
 */
ScopedMysqlConn::ScopedMysqlConn(const MysqlConnectionPool::ptr& pool, uint64_t timeout_ms, std::string& error)
    : m_pool(pool)
    , m_conn(nullptr)
{
    if (m_pool)
    {
        if (timeout_ms == 0)
        {
            m_conn = m_pool->Acquire(error);
        }
        else
        {
            m_conn = m_pool->Acquire(timeout_ms, error);
        }
    }
}

/**
 * @brief RAII 守卫析构：自动归还连接。
 */
ScopedMysqlConn::~ScopedMysqlConn()
{
    if (m_pool && m_conn)
    {
        m_pool->Release(m_conn);
        m_conn = nullptr;
    }
}

} // namespace storage
} // namespace ai
