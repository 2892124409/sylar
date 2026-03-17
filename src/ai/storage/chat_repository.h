#ifndef __SYLAR_AI_STORAGE_CHAT_REPOSITORY_H__
#define __SYLAR_AI_STORAGE_CHAT_REPOSITORY_H__

#include "ai/config/ai_app_config.h"
#include "ai/service/chat_interfaces.h"
#include "ai/storage/mysql_connection_pool.h"

#include <mysql/mysql.h>

#include <memory>
#include <string>

/**
 * @file chat_repository.h
 * @brief ChatStore 的 MySQL 仓库实现声明。
 */

namespace ai
{
namespace storage
{

/**
 * @brief 聊天历史读取仓库（MySQL 实现）。
 *
 * 该类是 `service::ChatStore` 的具体实现，负责：
 * - 从连接池获取连接执行查询；
 * - 启动阶段自动检查/创建最小表结构；
 * - 根据 `sid + conversation_id` 读取历史消息；
 * - 返回按时间正序（从旧到新）的消息序列，供上下文拼接。
 *
 * 线程安全说明：
 * - 通过连接池分配独立连接，减少全局串行瓶颈。
 */
class ChatRepository : public service::ChatStore
{
  public:
    typedef std::shared_ptr<ChatRepository> ptr;

    /**
     * @brief 构造仓库对象。
     * @param pool MySQL 连接池。
     */
    explicit ChatRepository(const MysqlConnectionPool::ptr& pool);

    /**
     * @brief 析构函数。
     */
    virtual ~ChatRepository();

    /**
     * @brief 初始化仓库。
     * @details 幂等调用：重复调用会直接成功返回。
     * @param[out] error 失败时返回错误信息。
     * @return true 初始化成功；false 初始化失败。
     */
    bool Init(std::string& error);

    /**
     * @brief 加载最近 N 条消息。
     * @details V1 中直接复用 `LoadHistory` 逻辑。
     * @param sid 会话 SID。
     * @param conversation_id 业务会话 ID。
     * @param limit 最大返回条数。
     * @param[out] out 输出消息数组（时间正序）。
     * @param[out] error 失败时返回错误信息。
     * @return true 查询成功；false 查询失败。
     */
    virtual bool LoadRecentMessages(const std::string& sid, const std::string& conversation_id, size_t limit, std::vector<common::ChatMessage>& out, std::string& error) override;

    /**
     * @brief 加载会话历史消息。
     * @details 查询时按 `created_at_ms DESC LIMIT N` 读取，再反转成时间正序输出。
     * @param sid 会话 SID。
     * @param conversation_id 业务会话 ID。
     * @param limit 最大返回条数。
     * @param[out] out 输出消息数组（时间正序）。
     * @param[out] error 失败时返回错误信息。
     * @return true 查询成功；false 查询失败。
     */
    virtual bool LoadHistory(const std::string& sid, const std::string& conversation_id, size_t limit, std::vector<common::ChatMessage>& out, std::string& error) override;

    virtual bool LoadConversationSummary(const std::string& sid,
                                         const std::string& conversation_id,
                                         std::string& summary,
                                         uint64_t& updated_at_ms,
                                         std::string& error) override;

    virtual bool SaveConversationSummary(const std::string& sid,
                                         const std::string& conversation_id,
                                         const std::string& summary,
                                         uint64_t updated_at_ms,
                                         std::string& error) override;

  private:
    /**
     * @brief 确保最小表结构存在（幂等建表）。
     */
    bool EnsureSchema(std::string& error);

    /**
     * @brief 执行单条 SQL 语句。
     */
    bool ExecuteSql(MYSQL* conn, const std::string& sql, std::string& error);

    /**
     * @brief 对输入字符串做 SQL 转义，避免拼接 SQL 时注入风险。
     */
    std::string Escape(MYSQL* conn, const std::string& value);

  private:
    /** @brief 连接池句柄。 */
    MysqlConnectionPool::ptr m_pool;
    /** @brief 初始化完成标志。 */
    bool m_initialized;
};

} // namespace storage
} // namespace ai

#endif
