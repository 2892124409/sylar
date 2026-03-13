#include "ai/storage/chat_repository.h"

#include "log/logger.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace ai
{
namespace storage
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

ChatRepository::ChatRepository(const config::MysqlSettings &settings)
    : m_settings(settings)
    , m_conn(nullptr)
    , m_initialized(false)
{
}

ChatRepository::~ChatRepository()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_conn)
    {
        mysql_close(m_conn);
        m_conn = nullptr;
    }
}

bool ChatRepository::Init(std::string &error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized)
    {
        return true;
    }

    if (!EnsureConnected(error))
    {
        return false;
    }

    if (!EnsureSchema(error))
    {
        return false;
    }

    m_initialized = true;
    return true;
}

bool ChatRepository::LoadRecentMessages(const std::string &sid,
                                        const std::string &conversation_id,
                                        size_t limit,
                                        std::vector<common::ChatMessage> &out,
                                        std::string &error)
{
    return LoadHistory(sid, conversation_id, limit, out, error);
}

bool ChatRepository::LoadHistory(const std::string &sid,
                                 const std::string &conversation_id,
                                 size_t limit,
                                 std::vector<common::ChatMessage> &out,
                                 std::string &error)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!EnsureConnected(error))
    {
        return false;
    }

    if (limit == 0)
    {
        out.clear();
        return true;
    }

    const std::string safe_sid = Escape(sid);
    const std::string safe_conversation_id = Escape(conversation_id);

    std::ostringstream sql;
    sql << "SELECT role, content, created_at_ms "
           "FROM chat_messages "
           "WHERE sid='"
        << safe_sid
        << "' AND conversation_id='"
        << safe_conversation_id
        << "' ORDER BY created_at_ms DESC LIMIT " << limit;

    if (mysql_query(m_conn, sql.str().c_str()) != 0)
    {
        error = mysql_error(m_conn);
        return false;
    }

    MYSQL_RES *res = mysql_store_result(m_conn);
    if (!res)
    {
        if (mysql_field_count(m_conn) == 0)
        {
            out.clear();
            return true;
        }

        error = mysql_error(m_conn);
        return false;
    }

    out.clear();
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        common::ChatMessage message;
        message.role = row[0] ? row[0] : "";
        message.content = row[1] ? row[1] : "";
        message.created_at_ms = row[2] ? static_cast<uint64_t>(std::strtoull(row[2], nullptr, 10)) : 0;
        out.push_back(message);
    }
    mysql_free_result(res);

    std::reverse(out.begin(), out.end());
    return true;
}

bool ChatRepository::EnsureConnected(std::string &error)
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

    mysql_options(m_conn, MYSQL_SET_CHARSET_NAME, m_settings.charset.c_str());
    mysql_options(m_conn, MYSQL_OPT_CONNECT_TIMEOUT, &m_settings.connect_timeout_seconds);

    if (!mysql_real_connect(m_conn,
                            m_settings.host.c_str(),
                            m_settings.user.c_str(),
                            m_settings.password.c_str(),
                            m_settings.database.c_str(),
                            m_settings.port,
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

bool ChatRepository::EnsureSchema(std::string &error)
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

bool ChatRepository::ExecuteSql(const std::string &sql, std::string &error)
{
    if (mysql_query(m_conn, sql.c_str()) != 0)
    {
        error = mysql_error(m_conn);
        BASE_LOG_ERROR(g_logger) << "Execute sql failed: " << sql << " error=" << error;
        return false;
    }
    return true;
}

std::string ChatRepository::Escape(const std::string &value)
{
    std::string escaped;
    escaped.resize(value.size() * 2 + 1);
    unsigned long length = mysql_real_escape_string(m_conn, &escaped[0], value.c_str(), value.size());
    escaped.resize(length);
    return escaped;
}

} // namespace storage
} // namespace ai
