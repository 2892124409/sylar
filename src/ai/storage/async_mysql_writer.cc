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

AsyncMySqlWriter::AsyncMySqlWriter(const config::MysqlSettings &mysql_settings,
                                   const config::PersistSettings &persist_settings)
    : m_mysql_settings(mysql_settings)
    , m_persist_settings(persist_settings)
    , m_running(false)
    , m_conn(nullptr)
{
}

AsyncMySqlWriter::~AsyncMySqlWriter()
{
    Stop();
}

bool AsyncMySqlWriter::Start(std::string &error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running)
    {
        return true;
    }

    if (!EnsureConnected(error) || !EnsureSchema(error))
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
            if (m_conn)
            {
                mysql_close(m_conn);
                m_conn = nullptr;
            }
            return;
        }

        m_running = false;
    }

    m_cond.notify_all();

    if (m_thread.joinable())
    {
        m_thread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_conn)
    {
        mysql_close(m_conn);
        m_conn = nullptr;
    }
}

bool AsyncMySqlWriter::Enqueue(const common::PersistMessage &message, std::string &error)
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

bool AsyncMySqlWriter::EnsureConnected(std::string &error)
{
    if (m_conn)
    {
        if (mysql_ping(m_conn) == 0)
        {
            return true;
        }

        mysql_close(m_conn);
        m_conn = nullptr;
    }

    m_conn = mysql_init(nullptr);
    if (!m_conn)
    {
        error = "mysql_init failed";
        return false;
    }

    mysql_options(m_conn, MYSQL_SET_CHARSET_NAME, m_mysql_settings.charset.c_str());
    mysql_options(m_conn, MYSQL_OPT_CONNECT_TIMEOUT, &m_mysql_settings.connect_timeout_seconds);

    if (!mysql_real_connect(m_conn,
                            m_mysql_settings.host.c_str(),
                            m_mysql_settings.user.c_str(),
                            m_mysql_settings.password.c_str(),
                            m_mysql_settings.database.c_str(),
                            m_mysql_settings.port,
                            nullptr,
                            CLIENT_MULTI_STATEMENTS))
    {
        error = mysql_error(m_conn);
        mysql_close(m_conn);
        m_conn = nullptr;
        return false;
    }

    return true;
}

bool AsyncMySqlWriter::EnsureSchema(std::string &error)
{
    static const char *kCreateConversationsSql =
        "CREATE TABLE IF NOT EXISTS conversations ("
        " id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
        " sid VARCHAR(128) NOT NULL,"
        " conversation_id VARCHAR(128) NOT NULL,"
        " created_at_ms BIGINT UNSIGNED NOT NULL,"
        " updated_at_ms BIGINT UNSIGNED NOT NULL,"
        " PRIMARY KEY (id),"
        " UNIQUE KEY uniq_sid_conv (sid, conversation_id),"
        " KEY idx_updated_at_ms (updated_at_ms)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    static const char *kCreateMessagesSql =
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

    return ExecuteSql(kCreateConversationsSql, error) && ExecuteSql(kCreateMessagesSql, error);
}

bool AsyncMySqlWriter::ExecuteSql(const std::string &sql, std::string &error)
{
    if (mysql_query(m_conn, sql.c_str()) != 0)
    {
        error = mysql_error(m_conn);
        return false;
    }
    return true;
}

bool AsyncMySqlWriter::FlushBatch(std::deque<common::PersistMessage> &batch, std::string &error)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!EnsureConnected(error))
    {
        return false;
    }

    if (!ExecuteSql("BEGIN", error))
    {
        return false;
    }

    for (size_t i = 0; i < batch.size(); ++i)
    {
        const common::PersistMessage &message = batch[i];
        std::ostringstream upsert_conversation;
        upsert_conversation << "INSERT INTO conversations (sid, conversation_id, created_at_ms, updated_at_ms) VALUES ('"
                            << Escape(message.sid)
                            << "', '"
                            << Escape(message.conversation_id)
                            << "', "
                            << message.created_at_ms
                            << ", "
                            << message.created_at_ms
                            << ") ON DUPLICATE KEY UPDATE updated_at_ms=VALUES(updated_at_ms)";

        if (!ExecuteSql(upsert_conversation.str(), error))
        {
            ExecuteSql("ROLLBACK", error);
            return false;
        }

        std::ostringstream insert_message;
        insert_message << "INSERT INTO chat_messages (sid, conversation_id, role, content, created_at_ms) VALUES ('"
                       << Escape(message.sid)
                       << "', '"
                       << Escape(message.conversation_id)
                       << "', '"
                       << Escape(message.role)
                       << "', '"
                       << Escape(message.content)
                       << "', "
                       << message.created_at_ms
                       << ")";

        if (!ExecuteSql(insert_message.str(), error))
        {
            ExecuteSql("ROLLBACK", error);
            return false;
        }
    }

    if (!ExecuteSql("COMMIT", error))
    {
        ExecuteSql("ROLLBACK", error);
        return false;
    }

    return true;
}

std::string AsyncMySqlWriter::Escape(const std::string &value)
{
    std::string escaped;
    escaped.resize(value.size() * 2 + 1);
    unsigned long length = mysql_real_escape_string(m_conn, &escaped[0], value.c_str(), value.size());
    escaped.resize(length);
    return escaped;
}

} // namespace storage
} // namespace ai
