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

/**
 * @brief 构造函数，保存配置并初始化运行状态。
 */
AsyncMySqlWriter::AsyncMySqlWriter(const config::MysqlSettings& mysql_settings,
                                   const config::PersistSettings& persist_settings)
    : m_mysql_settings(mysql_settings), m_persist_settings(persist_settings), m_running(false), m_conn(nullptr)
{
}

/**
 * @brief 析构函数，确保线程和连接被正确回收。
 */
AsyncMySqlWriter::~AsyncMySqlWriter()
{
    Stop();
}

/**
 * @brief 启动异步写入线程。
 * @details 启动前先进行连库与建表，避免线程启动后立即失败。
 */
bool AsyncMySqlWriter::Start(std::string& error)
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

/**
 * @brief 停止异步线程并释放连接。
 */
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

/**
 * @brief 将消息放入内存队列，供后台线程后续刷盘。
 */
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

/**
 * @brief 后台线程主循环：等待事件、取批次、调用 FlushBatch。
 */
void AsyncMySqlWriter::Run()
{
    while (true)
    {
        std::deque<common::PersistMessage> batch;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_queue.empty() && m_running)
            {
                // 队列为空时按 flush_interval_ms 周期唤醒，兼顾实时性与批量合并。
                m_cond.wait_for(lock, std::chrono::milliseconds(m_persist_settings.flush_interval_ms));
            }

            // 每轮最多提取 batch_size 条，避免单次事务过大。
            size_t fetch_count = std::min(m_queue.size(), m_persist_settings.batch_size);
            for (size_t i = 0; i < fetch_count; ++i)
            {
                batch.push_back(m_queue.front());
                m_queue.pop_front();
            }

            // 停止后若没有待刷数据则退出线程；有数据则先刷完再退出。
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

/**
 * @brief 确保 MySQL 连接可用，不可用时自动重连。
 */
bool AsyncMySqlWriter::EnsureConnected(std::string& error)
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

/**
 * @brief 幂等建表，确保 conversations/chat_messages 存在。
 */
bool AsyncMySqlWriter::EnsureSchema(std::string& error)
{
    static const char* kCreateConversationsSql =
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

    return ExecuteSql(kCreateConversationsSql, error) && ExecuteSql(kCreateMessagesSql, error);
}

/**
 * @brief 执行单条 SQL。
 */
bool AsyncMySqlWriter::ExecuteSql(const std::string& sql, std::string& error)
{
    if (mysql_query(m_conn, sql.c_str()) != 0)
    {
        error = mysql_error(m_conn);
        return false;
    }
    return true;
}

/**
 * @brief 刷写一个批次消息到数据库（事务语义）。
 */
bool AsyncMySqlWriter::FlushBatch(std::deque<common::PersistMessage>& batch, std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!EnsureConnected(error))
    {
        return false;
    }

    // 开启事务，保证一个 batch 内 conversations/chat_messages 同步落地。
    if (!ExecuteSql("BEGIN", error))
    {
        return false;
    }

    for (size_t i = 0; i < batch.size(); ++i)
    {
        const common::PersistMessage& message = batch[i];
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

        // 任一 SQL 失败则回滚整个批次，避免部分写入。
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

    // 提交事务；提交失败同样执行回滚。
    if (!ExecuteSql("COMMIT", error))
    {
        ExecuteSql("ROLLBACK", error);
        return false;
    }

    return true;
}

/**
 * @brief SQL 转义，防止拼接语句时特殊字符破坏语义。
 */
std::string AsyncMySqlWriter::Escape(const std::string& value)
{
    std::string escaped;
    escaped.resize(value.size() * 2 + 1);
    unsigned long length = mysql_real_escape_string(m_conn, &escaped[0], value.c_str(), value.size());
    escaped.resize(length);
    return escaped;
}

} // namespace storage
} // namespace ai
