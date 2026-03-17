#ifndef __SYLAR_AI_STORAGE_MYSQL_CONNECTION_POOL_H__
#define __SYLAR_AI_STORAGE_MYSQL_CONNECTION_POOL_H__

#include "ai/config/ai_app_config.h"
#include "sylar/fiber/fiber.h"

#include <mysql/mysql.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace ai
{
namespace storage
{

/**
 * @brief MySQL 连接池（支持 fiber 友好等待）。
 */
class MysqlConnectionPool
{
  public:
    typedef std::shared_ptr<MysqlConnectionPool> ptr;

    MysqlConnectionPool();
    ~MysqlConnectionPool();

    /**
     * @brief 初始化连接池并预热最小连接数。
     */
    bool Init(const config::MysqlSettings& settings, std::string& error);

    /**
     * @brief 获取一个可用连接，超时返回 nullptr。
     */
    MYSQL* Acquire(uint64_t timeout_ms, std::string& error);

    /**
     * @brief 按配置超时获取连接。
     */
    MYSQL* Acquire(std::string& error);

    /**
     * @brief 归还连接。
     */
    void Release(MYSQL* conn);

    /**
     * @brief 关闭池内所有连接并唤醒等待者。
     */
    void Shutdown();

  private:
    struct IdleConn
    {
        MYSQL* conn = nullptr;
        uint64_t last_used_ms = 0;
    };

    struct Waiter
    {
        typedef std::shared_ptr<Waiter> ptr;
        sylar::Fiber::ptr fiber;
        int thread = -1;
        MYSQL* assigned_conn = nullptr;
        bool done = false;
        bool timed_out = false;
    };

    bool CreateConnection(MYSQL*& out, std::string& error);
    bool IsConnectionAlive(MYSQL* conn);
    void WakeWaiterWithConn(const Waiter::ptr& waiter, MYSQL* conn);
    void WakeWaiterTimeout(const Waiter::ptr& waiter);

  private:
    config::MysqlSettings m_settings;

    std::mutex m_mutex;
    bool m_initialized;
    bool m_shutdown;

    size_t m_total_connections;
    std::deque<IdleConn> m_idle_conns;
    std::deque<Waiter::ptr> m_waiters;
};

/**
 * @brief RAII 连接守卫，析构时自动归还连接。
 */
class ScopedMysqlConn
{
  public:
    ScopedMysqlConn(const MysqlConnectionPool::ptr& pool,
                    uint64_t timeout_ms,
                    std::string& error);
    ~ScopedMysqlConn();

    MYSQL* get() const
    {
        return m_conn;
    }

    operator bool() const
    {
        return m_conn != nullptr;
    }

  private:
    MysqlConnectionPool::ptr m_pool;
    MYSQL* m_conn;
};

} // namespace storage
} // namespace ai

#endif
