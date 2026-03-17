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
 * @details
 * 该连接池的目标是：
 * 1) 复用 MySQL 连接，避免每次查询都新建 TCP/MySQL 会话；
 * 2) 在连接池耗尽时，让当前 fiber 挂起等待，而不是阻塞整条工作线程；
 * 3) 通过最小/最大连接数约束并发，保护数据库。
 *
 * 典型借还流程：
 * 1) `Acquire()`：优先取空闲连接；没有空闲则视情况新建或进入等待队列；
 * 2) 业务使用连接执行 SQL；
 * 3) `Release()`：优先转交给等待者，否则归还空闲队列。
 */
class MysqlConnectionPool
{
  public:
    typedef std::shared_ptr<MysqlConnectionPool> ptr;

    /** @brief 构造函数，仅初始化内部状态，不建立数据库连接。 */
    MysqlConnectionPool();
    /** @brief 析构函数，自动调用 `Shutdown()` 释放资源。 */
    ~MysqlConnectionPool();

    /**
     * @brief 初始化连接池并预热最小连接数。
     * @param settings MySQL 与连接池配置（地址、账号、池大小、超时等）。
     * @param[out] error 初始化失败时返回错误信息。
     * @return `true` 成功；`false` 失败。
     * @note
     * - 幂等：重复调用在已初始化状态下直接返回成功；
     * - 仅在此阶段创建 `pool_min_size` 条连接。
     */
    bool Init(const config::MysqlSettings& settings, std::string& error);

    /**
     * @brief 获取一个可用连接，超时返回 nullptr。
     * @param timeout_ms 获取连接的最大等待时间（毫秒）。
     * @param[out] error 失败时返回错误信息（如超时、池关闭、无 fiber 上下文）。
     * @return 可用 `MYSQL*`；失败返回 `nullptr`。
     * @details
     * 获取策略：
     * 1) 有空闲连接：直接返回；
     * 2) 无空闲但未达上限：新建连接后返回；
     * 3) 已达上限：进入等待队列并挂起当前 fiber，等待 `Release()` 唤醒。
     */
    MYSQL* Acquire(uint64_t timeout_ms, std::string& error);

    /**
     * @brief 按配置超时获取连接。
     * @param[out] error 失败时返回错误信息。
     * @return 可用 `MYSQL*`；失败返回 `nullptr`。
     */
    MYSQL* Acquire(std::string& error);

    /**
     * @brief 归还连接。
     * @param conn 要归还的 MySQL 连接指针。
     * @details
     * - 若存在等待者：优先把连接直接交给等待者并唤醒；
     * - 若无等待者：放回空闲队列；
     * - 若池已关闭：直接关闭连接。
     */
    void Release(MYSQL* conn);

    /**
     * @brief 关闭池内所有连接并唤醒等待者。
     * @details
     * 调用后：
     * 1) 池状态变为 shutdown；
     * 2) 关闭全部空闲连接；
     * 3) 标记等待者超时并尝试唤醒，避免永久挂起。
     */
    void Shutdown();

  private:
    /**
     * @brief 空闲连接记录。
     */
    struct IdleConn
    {
        /** @brief MySQL 连接对象。 */
        MYSQL* conn = nullptr;
        /** @brief 最近一次归还到池中的时间戳（毫秒）。 */
        uint64_t last_used_ms = 0;
    };

    /**
     * @brief 等待连接的 fiber 上下文记录。
     */
    struct Waiter
    {
        typedef std::shared_ptr<Waiter> ptr;
        /** @brief 等待中的 fiber。 */
        sylar::Fiber::ptr fiber;
        /** @brief fiber 绑定线程，唤醒时用于定向调度。 */
        int thread = -1;
        /** @brief 被分配到的连接；由 `Release()` 填充。 */
        MYSQL* assigned_conn = nullptr;
        /** @brief 是否已结束等待（拿到连接或超时/关闭）。 */
        bool done = false;
        /** @brief 是否因超时/关闭退出等待。 */
        bool timed_out = false;
    };

    /**
     * @brief 新建一个 MySQL 连接。
     * @param[out] out 成功时返回连接对象。
     * @param[out] error 失败时返回错误信息。
     * @return `true` 成功；`false` 失败。
     */
    bool CreateConnection(MYSQL*& out, std::string& error);

    /**
     * @brief 检查连接是否存活。
     * @param conn 待检查连接。
     * @return `true` 存活；`false` 不可用。
     */
    bool IsConnectionAlive(MYSQL* conn);

    /**
     * @brief 向指定等待者分配连接并唤醒其 fiber。
     * @param waiter 目标等待者。
     * @param conn 分配的连接。
     */
    void WakeWaiterWithConn(const Waiter::ptr& waiter, MYSQL* conn);
    
    /**
     * @brief 将指定等待者标记为超时并唤醒。
     * @param waiter 目标等待者。
     */
    void WakeWaiterTimeout(const Waiter::ptr& waiter);

  private:
    /** @brief 连接池配置快照。 */
    config::MysqlSettings m_settings;

    /** @brief 保护连接队列/等待队列与状态位的互斥锁。 */
    std::mutex m_mutex;
    /** @brief 是否完成初始化。 */
    bool m_initialized;
    /** @brief 是否已进入关闭状态。 */
    bool m_shutdown;

    /** @brief 当前已创建连接总数（空闲 + 借出）。 */
    size_t m_total_connections;
    /** @brief 空闲连接队列。 */
    std::deque<IdleConn> m_idle_conns;
    /** @brief 等待连接的 fiber 队列。 */
    std::deque<Waiter::ptr> m_waiters;
};

/**
 * @brief RAII 连接守卫，析构时自动归还连接。
 * @details
 * 用法：函数内声明一个 `ScopedMysqlConn`，作用域结束自动归还连接，
 * 避免早返回/异常路径遗漏 `Release()`。
 */
class ScopedMysqlConn
{
  public:
    /**
     * @brief 构造并尝试借连接。
     * @param pool 连接池实例。
     * @param timeout_ms 获取连接超时（毫秒）；`0` 表示采用池默认策略。
     * @param[out] error 获取失败时返回错误信息。
     */
    ScopedMysqlConn(const MysqlConnectionPool::ptr& pool,
                    uint64_t timeout_ms,
                    std::string& error);
    /** @brief 析构时自动归还连接（若持有）。 */
    ~ScopedMysqlConn();

    /** @brief 获取底层连接指针。 */
    MYSQL* get() const
    {
        return m_conn;
    }

    /** @brief 判空便捷接口：持有连接返回 true。 */
    operator bool() const
    {
        return m_conn != nullptr;
    }

  private:
    /** @brief 连接池引用。 */
    MysqlConnectionPool::ptr m_pool;
    /** @brief 当前持有连接。 */
    MYSQL* m_conn;
};

} // namespace storage
} // namespace ai

#endif
