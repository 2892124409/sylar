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

MysqlConnectionPool::MysqlConnectionPool()
    : m_initialized(false)
    , m_shutdown(false)
    , m_total_connections(0)
{
}

MysqlConnectionPool::~MysqlConnectionPool()
{
    Shutdown();
}

bool MysqlConnectionPool::CreateConnection(MYSQL*& out, std::string& error)
{
    out = mysql_init(nullptr);
    if (!out)
    {
        error = "mysql_init failed";
        return false;
    }

    mysql_options(out, MYSQL_SET_CHARSET_NAME, m_settings.charset.c_str());
    mysql_options(out, MYSQL_OPT_CONNECT_TIMEOUT, &m_settings.connect_timeout_seconds);

    if (!mysql_real_connect(out,
                            m_settings.host.c_str(),
                            m_settings.user.c_str(),
                            m_settings.password.c_str(),
                            m_settings.database.c_str(),
                            m_settings.port,
                            nullptr,
                            CLIENT_MULTI_STATEMENTS))
    {
        error = mysql_error(out);
        mysql_close(out);
        out = nullptr;
        return false;
    }

    return true;
}

bool MysqlConnectionPool::Init(const config::MysqlSettings& settings, std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized)
    {
        return true;
    }

    m_settings = settings;
    m_shutdown = false;

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

bool MysqlConnectionPool::IsConnectionAlive(MYSQL* conn)
{
    if (!conn)
    {
        return false;
    }

    return mysql_ping(conn) == 0;
}

MYSQL* MysqlConnectionPool::Acquire(std::string& error)
{
    return Acquire(m_settings.pool_acquire_timeout_ms, error);
}

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

MYSQL* MysqlConnectionPool::Acquire(uint64_t timeout_ms, std::string& error)
{
    const uint64_t deadline = common::NowMs() + timeout_ms;

    while (true)
    {
        Waiter::ptr waiter;
        bool create_new_conn = false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_shutdown)
            {
                error = "mysql connection pool is shutdown";
                return nullptr;
            }

            while (!m_idle_conns.empty())
            {
                IdleConn idle = m_idle_conns.front();
                m_idle_conns.pop_front();
                MYSQL* conn = idle.conn;
                const uint64_t idle_ms = common::NowMs() - idle.last_used_ms;
                if (idle_ms > 30000 && !IsConnectionAlive(conn))
                {
                    mysql_close(conn);
                    --m_total_connections;
                    continue;
                }

                return conn;
            }

            if (m_total_connections < m_settings.pool_max_size)
            {
                ++m_total_connections;
                create_new_conn = true;
            }
            else
            {
                sylar::Fiber::ptr current = sylar::Fiber::GetThis();
                if (!current)
                {
                    error = "acquire requires fiber context when pool is exhausted";
                    return nullptr;
                }

                waiter.reset(new Waiter);
                waiter->fiber = current;
                waiter->thread = current->getBoundThread();
                m_waiters.push_back(waiter);
            }
        }

        if (create_new_conn)
        {
            MYSQL* conn = nullptr;
            if (!CreateConnection(conn, error))
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_total_connections > 0)
                {
                    --m_total_connections;
                }
                return nullptr;
            }
            return conn;
        }

        // 池已满：fiber 进入等待，避免阻塞工作线程。
        sylar::IOManager* iom = sylar::IOManager::GetThis();
        if (!iom)
        {
            error = "pool exhausted but no iomanager available for fiber wait";
            std::lock_guard<std::mutex> lock(m_mutex);
            m_waiters.erase(std::remove(m_waiters.begin(), m_waiters.end(), waiter), m_waiters.end());
            return nullptr;
        }

        uint64_t now = common::NowMs();
        if (now >= deadline)
        {
            error = "acquire mysql connection timeout";
            std::lock_guard<std::mutex> lock(m_mutex);
            m_waiters.erase(std::remove(m_waiters.begin(), m_waiters.end(), waiter), m_waiters.end());
            return nullptr;
        }

        uint64_t wait_ms = deadline - now;
        sylar::Timer::ptr timer = iom->addTimer(wait_ms,
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

        sylar::Fiber::YieldToHold();
        timer->cancel();

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

void MysqlConnectionPool::Release(MYSQL* conn)
{
    if (!conn)
    {
        return;
    }

    Waiter::ptr waiter;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_shutdown)
        {
            mysql_close(conn);
            if (m_total_connections > 0)
            {
                --m_total_connections;
            }
            return;
        }

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
            IdleConn idle;
            idle.conn = conn;
            idle.last_used_ms = common::NowMs();
            m_idle_conns.push_back(idle);
            return;
        }

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

    iom->schedule(waiter->fiber, waiter->thread);
}

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

        m_shutdown = true;
        idle.swap(m_idle_conns);
        waiters.swap(m_waiters);
    }

    for (size_t i = 0; i < idle.size(); ++i)
    {
        if (idle[i].conn)
        {
            mysql_close(idle[i].conn);
        }
    }

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

ScopedMysqlConn::ScopedMysqlConn(const MysqlConnectionPool::ptr& pool,
                                 uint64_t timeout_ms,
                                 std::string& error)
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
