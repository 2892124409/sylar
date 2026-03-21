#ifndef __SYLAR_AI_STORAGE_CHAT_MESSAGE_PERSISTER_H__
#define __SYLAR_AI_STORAGE_CHAT_MESSAGE_PERSISTER_H__

#include "ai/common/ai_types.h"
#include "ai/storage/mysql_connection_pool.h"

#include <memory>
#include <string>
#include <vector>

/**
 * @file chat_message_persister.h
 * @brief 聊天消息入库执行器（供 MQ 消费者/其他写路径复用）。
 */

namespace ai
{
namespace storage
{

/**
 * @brief 聊天消息持久化执行器。
 * @details
 * 提供 schema 初始化与批量事务写入能力，避免写 SQL 逻辑散落在多个进程中。
 */
class ChatMessagePersister
{
  public:
    typedef std::shared_ptr<ChatMessagePersister> ptr;

    /** @brief 构造执行器。 */
    explicit ChatMessagePersister(const MysqlConnectionPool::ptr& pool);

    /** @brief 初始化（幂等建表/补字段）。 */
    bool Init(std::string& error);

    /** @brief 持久化单条消息。 */
    bool Persist(const common::PersistMessage& message, std::string& error);

    /** @brief 事务性持久化一个批次。 */
    bool PersistBatch(const std::vector<common::PersistMessage>& batch, std::string& error);

  private:
    bool EnsureSchema(std::string& error);
    bool ExecuteSql(MYSQL* conn, const std::string& sql, std::string& error);
    std::string Escape(MYSQL* conn, const std::string& value);

  private:
    /** @brief MySQL 连接池。 */
    MysqlConnectionPool::ptr m_pool;
    /** @brief 是否完成初始化。 */
    bool m_initialized;
};

} // namespace storage
} // namespace ai

#endif
