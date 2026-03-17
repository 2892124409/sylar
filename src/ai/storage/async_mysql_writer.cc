#include "ai/storage/async_mysql_writer.h"

#include "log/logger.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace ai
{
namespace storage
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

AsyncMySqlWriter::AsyncMySqlWriter(const MysqlConnectionPool::ptr& pool,
                                   const config::PersistSettings& persist_settings)
    : m_pool(pool)
    , m_persist_settings(persist_settings)
    , m_running(false)
{
}

AsyncMySqlWriter::~AsyncMySqlWriter()
{
    Stop();
}

bool AsyncMySqlWriter::Start(std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running)
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

    m_running = true;
    m_thread = std::thread(&AsyncMySqlWriter::Run, this);
    return true;
}

void AsyncMySqlWriter::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running)
        {
            return;
        }

        m_running = false;
    }

    m_cond.notify_all();

    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

bool AsyncMySqlWriter::Enqueue(const common::PersistMessage& message, std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_running)
    {
        error = "async mysql writer is not running";
        return false;
    }

    if (m_queue.size() >= m_persist_settings.queue_capacity)
    {
        error = "persist queue full";
        return false;
    }

    m_queue.push_back(message);
    m_cond.notify_one();
    return true;
}

void AsyncMySqlWriter::Run()
{
    while (true)
    {
        std::deque<common::PersistMessage> batch;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_queue.empty() && m_running)
            {
                m_cond.wait_for(lock, std::chrono::milliseconds(m_persist_settings.flush_interval_ms));
            }

            size_t fetch_count = std::min(m_queue.size(), m_persist_settings.batch_size);
            for (size_t i = 0; i < fetch_count; ++i)
            {
                batch.push_back(m_queue.front());
                m_queue.pop_front();
            }

            if (batch.empty() && !m_running)
            {
                break;
            }
        }

        if (batch.empty())
        {
            continue;
        }

        std::string error;
        if (!FlushBatch(batch, error))
        {
            BASE_LOG_ERROR(g_logger) << "Flush async mysql batch failed: " << error;
        }
    }
}

bool AsyncMySqlWriter::EnsureSchema(std::string& error)
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

bool AsyncMySqlWriter::ExecuteSql(MYSQL* conn, const std::string& sql, std::string& error)
{
    if (mysql_query(conn, sql.c_str()) != 0)
    {
        error = mysql_error(conn);
        return false;
    }
    return true;
}

bool AsyncMySqlWriter::FlushBatch(std::deque<common::PersistMessage>& batch, std::string& error)
{
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

std::string AsyncMySqlWriter::Escape(MYSQL* conn, const std::string& value)
{
    std::string escaped;
    escaped.resize(value.size() * 2 + 1);
    unsigned long length = mysql_real_escape_string(conn, &escaped[0], value.c_str(), value.size());
    escaped.resize(length);
    return escaped;
}

} // namespace storage
} // namespace ai
