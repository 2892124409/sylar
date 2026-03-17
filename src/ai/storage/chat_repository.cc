#include "ai/storage/chat_repository.h"

#include "ai/common/ai_utils.h"
#include "log/logger.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace ai
{
namespace storage
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

ChatRepository::ChatRepository(const MysqlConnectionPool::ptr& pool)
    : m_pool(pool)
    , m_initialized(false)
{
}

ChatRepository::~ChatRepository()
{
}

bool ChatRepository::Init(std::string& error)
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

bool ChatRepository::LoadRecentMessages(const std::string& sid,
                                        const std::string& conversation_id,
                                        size_t limit,
                                        std::vector<common::ChatMessage>& out,
                                        std::string& error)
{
    return LoadHistory(sid, conversation_id, limit, out, error);
}

bool ChatRepository::LoadHistory(const std::string& sid,
                                 const std::string& conversation_id,
                                 size_t limit,
                                 std::vector<common::ChatMessage>& out,
                                 std::string& error)
{
    if (limit == 0)
    {
        out.clear();
        return true;
    }

    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    const std::string safe_sid = Escape(conn.get(), sid);
    const std::string safe_conversation_id = Escape(conn.get(), conversation_id);

    std::ostringstream sql;
    sql << "SELECT role, content, created_at_ms "
           "FROM chat_messages "
           "WHERE sid='"
        << safe_sid
        << "' AND conversation_id='"
        << safe_conversation_id
        << "' ORDER BY created_at_ms DESC LIMIT " << limit;

    if (mysql_query(conn.get(), sql.str().c_str()) != 0)
    {
        error = mysql_error(conn.get());
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn.get());
    if (!res)
    {
        if (mysql_field_count(conn.get()) == 0)
        {
            out.clear();
            return true;
        }

        error = mysql_error(conn.get());
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

bool ChatRepository::LoadConversationSummary(const std::string& sid,
                                             const std::string& conversation_id,
                                             std::string& summary,
                                             uint64_t& updated_at_ms,
                                             std::string& error)
{
    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    const std::string safe_sid = Escape(conn.get(), sid);
    const std::string safe_conversation_id = Escape(conn.get(), conversation_id);

    std::ostringstream sql;
    sql << "SELECT summary_text, summary_updated_at_ms "
           "FROM conversations WHERE sid='"
        << safe_sid
        << "' AND conversation_id='"
        << safe_conversation_id
        << "' LIMIT 1";

    if (mysql_query(conn.get(), sql.str().c_str()) != 0)
    {
        error = mysql_error(conn.get());
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn.get());
    if (!res)
    {
        if (mysql_field_count(conn.get()) == 0)
        {
            summary.clear();
            updated_at_ms = 0;
            return true;
        }

        error = mysql_error(conn.get());
        return false;
    }

    summary.clear();
    updated_at_ms = 0;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row)
    {
        summary = row[0] ? row[0] : "";
        updated_at_ms = row[1] ? static_cast<uint64_t>(std::strtoull(row[1], nullptr, 10)) : 0;
    }

    mysql_free_result(res);
    return true;
}

bool ChatRepository::SaveConversationSummary(const std::string& sid,
                                             const std::string& conversation_id,
                                             const std::string& summary,
                                             uint64_t updated_at_ms,
                                             std::string& error)
{
    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    const std::string safe_sid = Escape(conn.get(), sid);
    const std::string safe_conversation_id = Escape(conn.get(), conversation_id);
    const std::string safe_summary = Escape(conn.get(), summary);

    const uint64_t now_ms = common::NowMs();

    std::ostringstream sql;
    sql << "INSERT INTO conversations (sid, conversation_id, created_at_ms, updated_at_ms, summary_text, summary_updated_at_ms) VALUES ('"
        << safe_sid
        << "', '"
        << safe_conversation_id
        << "', "
        << now_ms
        << ", "
        << now_ms
        << ", '"
        << safe_summary
        << "', "
        << updated_at_ms
        << ") ON DUPLICATE KEY UPDATE "
           "updated_at_ms=VALUES(updated_at_ms), "
           "summary_text=VALUES(summary_text), "
           "summary_updated_at_ms=VALUES(summary_updated_at_ms)";

    if (mysql_query(conn.get(), sql.str().c_str()) != 0)
    {
        error = mysql_error(conn.get());
        return false;
    }

    return true;
}

bool ChatRepository::EnsureSchema(std::string& error)
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

bool ChatRepository::ExecuteSql(MYSQL* conn, const std::string& sql, std::string& error)
{
    if (mysql_query(conn, sql.c_str()) != 0)
    {
        error = mysql_error(conn);
        BASE_LOG_ERROR(g_logger) << "Execute sql failed: " << sql << " error=" << error;
        return false;
    }

    return true;
}

std::string ChatRepository::Escape(MYSQL* conn, const std::string& value)
{
    std::string escaped;
    escaped.resize(value.size() * 2 + 1);
    unsigned long length = mysql_real_escape_string(conn, &escaped[0], value.c_str(), value.size());
    escaped.resize(length);
    return escaped;
}

} // namespace storage
} // namespace ai
