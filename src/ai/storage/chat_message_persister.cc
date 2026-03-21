#include "ai/storage/chat_message_persister.h"

#include <sstream>

namespace ai
{
namespace storage
{

ChatMessagePersister::ChatMessagePersister(const MysqlConnectionPool::ptr& pool)
    : m_pool(pool)
    , m_initialized(false)
{
}

bool ChatMessagePersister::Init(std::string& error)
{
    if (m_initialized)
    {
        return true;
    }

    if (!m_pool)
    {
        error = "mysql pool is null";
        return false;
    }

    if (!EnsureSchema(error))
    {
        return false;
    }

    m_initialized = true;
    return true;
}

bool ChatMessagePersister::Persist(const common::PersistMessage& message, std::string& error)
{
    std::vector<common::PersistMessage> one(1, message);
    return PersistBatch(one, error);
}

bool ChatMessagePersister::PersistBatch(const std::vector<common::PersistMessage>& batch, std::string& error)
{
    if (batch.empty())
    {
        return true;
    }

    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    if (!ExecuteSql(conn.get(), "BEGIN", error))
    {
        return false;
    }

    for (size_t i = 0; i < batch.size(); ++i)
    {
        const common::PersistMessage& message = batch[i];

        std::ostringstream upsert_conversation;
        upsert_conversation << "INSERT INTO conversations (sid, conversation_id, created_at_ms, updated_at_ms) VALUES ('"
                            << Escape(conn.get(), message.sid)
                            << "', '"
                            << Escape(conn.get(), message.conversation_id)
                            << "', "
                            << message.created_at_ms
                            << ", "
                            << message.created_at_ms
                            << ") ON DUPLICATE KEY UPDATE updated_at_ms=VALUES(updated_at_ms)";

        if (!ExecuteSql(conn.get(), upsert_conversation.str(), error))
        {
            ExecuteSql(conn.get(), "ROLLBACK", error);
            return false;
        }

        std::ostringstream insert_message;
        insert_message << "INSERT INTO chat_messages (sid, conversation_id, role, content, created_at_ms) VALUES ('"
                       << Escape(conn.get(), message.sid)
                       << "', '"
                       << Escape(conn.get(), message.conversation_id)
                       << "', '"
                       << Escape(conn.get(), message.role)
                       << "', '"
                       << Escape(conn.get(), message.content)
                       << "', "
                       << message.created_at_ms
                       << ")";

        if (!ExecuteSql(conn.get(), insert_message.str(), error))
        {
            ExecuteSql(conn.get(), "ROLLBACK", error);
            return false;
        }
    }

    if (!ExecuteSql(conn.get(), "COMMIT", error))
    {
        ExecuteSql(conn.get(), "ROLLBACK", error);
        return false;
    }

    return true;
}

bool ChatMessagePersister::EnsureSchema(std::string& error)
{
    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    static const char* kCreateConversationsSql =
        "CREATE TABLE IF NOT EXISTS conversations ("
        " id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
        " sid VARCHAR(128) NOT NULL,"
        " conversation_id VARCHAR(128) NOT NULL,"
        " created_at_ms BIGINT UNSIGNED NOT NULL,"
        " updated_at_ms BIGINT UNSIGNED NOT NULL,"
        " summary_text MEDIUMTEXT NOT NULL DEFAULT '',"
        " summary_updated_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        " PRIMARY KEY (id),"
        " UNIQUE KEY uniq_sid_conv (sid, conversation_id),"
        " KEY idx_updated_at_ms (updated_at_ms)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    static const char* kCreateMessagesSql =
        "CREATE TABLE IF NOT EXISTS chat_messages ("
        " id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
        " sid VARCHAR(128) NOT NULL,"
        " conversation_id VARCHAR(128) NOT NULL,"
        " role VARCHAR(16) NOT NULL,"
        " content MEDIUMTEXT NOT NULL,"
        " created_at_ms BIGINT UNSIGNED NOT NULL,"
        " PRIMARY KEY (id),"
        " KEY idx_sid_conv_time (sid, conversation_id, created_at_ms)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    static const char* kAlterConversationSummaryText =
        "ALTER TABLE conversations ADD COLUMN IF NOT EXISTS summary_text MEDIUMTEXT NOT NULL DEFAULT ''";

    static const char* kAlterConversationSummaryUpdatedAt =
        "ALTER TABLE conversations ADD COLUMN IF NOT EXISTS summary_updated_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0";

    return ExecuteSql(conn.get(), kCreateConversationsSql, error) &&
           ExecuteSql(conn.get(), kCreateMessagesSql, error) &&
           ExecuteSql(conn.get(), kAlterConversationSummaryText, error) &&
           ExecuteSql(conn.get(), kAlterConversationSummaryUpdatedAt, error);
}

bool ChatMessagePersister::ExecuteSql(MYSQL* conn, const std::string& sql, std::string& error)
{
    if (mysql_query(conn, sql.c_str()) != 0)
    {
        error = mysql_error(conn);
        return false;
    }
    return true;
}

std::string ChatMessagePersister::Escape(MYSQL* conn, const std::string& value)
{
    std::string escaped;
    escaped.resize(value.size() * 2 + 1);
    unsigned long length = mysql_real_escape_string(conn, &escaped[0], value.c_str(), value.size());
    escaped.resize(length);
    return escaped;
}

} // namespace storage
} // namespace ai
