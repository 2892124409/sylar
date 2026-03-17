#ifndef __SYLAR_AI_STORAGE_ASYNC_MYSQL_WRITER_H__
#define __SYLAR_AI_STORAGE_ASYNC_MYSQL_WRITER_H__

#include "ai/config/ai_app_config.h"
#include "ai/service/chat_interfaces.h"

#include <mysql/mysql.h>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

/**
 * @file async_mysql_writer.h
 * @brief MessageSink 的异步 MySQL 写入实现声明。
 */

namespace ai
{
namespace storage
{

/**
 * @brief 异步 MySQL 持久化写入器。
 *
 * 该类实现 `service::MessageSink`，用于将请求线程中的消息写入动作异步化：
 * - 前台线程调用 `Enqueue` 仅负责入队；
 * - 后台线程周期性/批量刷盘；
 * - 通过事务保证单批次写入的一致性。
 *
 * 语义说明：
 * - `Enqueue` 成功表示“消息已入内存队列”，不表示“已提交数据库”。
 * - 刷盘失败会记录日志，当前 V1 不做失败重放。
 */
class AsyncMySqlWriter : public service::MessageSink
{
  public:
    typedef std::shared_ptr<AsyncMySqlWriter> ptr;

    /**
     * @brief 构造异步写入器。
     * @param mysql_settings MySQL 连接配置。
     * @param persist_settings 持久化队列与批量刷盘配置。
     */
    AsyncMySqlWriter(const config::MysqlSettings& mysql_settings,
                     const config::PersistSettings& persist_settings);

    /**
     * @brief 析构函数，内部会调用 Stop() 回收线程和连接。
     */
    ~AsyncMySqlWriter();

    /**
     * @brief 启动异步写线程。
     * @details 启动前会先检查数据库连接与表结构。
     * @param[out] error 失败时返回错误信息。
     * @return true 启动成功；false 启动失败。
     */
    bool Start(std::string& error);

    /**
     * @brief 停止异步写线程并关闭连接。
     * @details 幂等调用，多次 Stop 安全。
     */
    void Stop();

    /**
     * @brief 入队一条待持久化消息。
     * @param message 待持久化消息。
     * @param[out] error 失败时返回错误信息（例如队列已满）。
     * @return true 入队成功；false 入队失败。
     */
    virtual bool Enqueue(const common::PersistMessage& message, std::string& error) override;

  private:
    /**
     * @brief 后台线程主循环：等待、取批、刷盘。
     */
    void Run();

    /**
     * @brief 确保数据库连接可用，不可用时自动重连。
     */
    bool EnsureConnected(std::string& error);

    /**
     * @brief 确保最小表结构存在（幂等建表）。
     */
    bool EnsureSchema(std::string& error);

    /**
     * @brief 执行单条 SQL。
     */
    bool ExecuteSql(const std::string& sql, std::string& error);

    /**
     * @brief 将一个批次事务性写入 MySQL。
     * @details 内部使用 BEGIN/COMMIT/ROLLBACK 管理事务边界。
     */
    bool FlushBatch(std::deque<common::PersistMessage>& batch, std::string& error);

    /**
     * @brief SQL 字符串转义，避免注入风险。
     */
    std::string Escape(const std::string& value);

  private:
    /** @brief MySQL 配置快照。 */
    config::MysqlSettings m_mysql_settings;
    /** @brief 持久化策略配置（队列容量、批大小、刷盘间隔）。 */
    config::PersistSettings m_persist_settings;

    /** @brief 运行标志；true 表示后台线程应继续工作。 */
    bool m_running;
    /** @brief 底层 MySQL 连接句柄。 */
    MYSQL* m_conn;

    /** @brief 互斥锁，保护运行状态、连接和队列。 */
    std::mutex m_mutex;
    /** @brief 条件变量，用于唤醒后台线程刷盘。 */
    std::condition_variable m_cond;
    /** @brief 待刷盘消息队列。 */
    std::deque<common::PersistMessage> m_queue;

    /** @brief 后台刷盘线程对象。 */
    std::thread m_thread;
};

} // namespace storage
} // namespace ai

#endif
