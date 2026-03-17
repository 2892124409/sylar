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

/**
 * @brief 构造仓库对象，保存配置并初始化连接状态。
 */
ChatRepository::ChatRepository(const config::MysqlSettings& settings)
    : m_settings(settings), m_conn(nullptr), m_initialized(false)
{
}

/**
 * @brief 析构时释放 MySQL 连接。
 */
ChatRepository::~ChatRepository()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_conn)
    {
        mysql_close(m_conn);
        m_conn = nullptr;
    }
}

/**
 * @brief 初始化仓库（连接数据库并确保表结构存在）。
 * @details 该函数是幂等的，重复调用直接返回成功。
 */
bool ChatRepository::Init(std::string& error)
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

/**
 * @brief 加载最近消息（V1 直接复用历史查询实现）。
 */
bool ChatRepository::LoadRecentMessages(const std::string& sid, const std::string& conversation_id, size_t limit, std::vector<common::ChatMessage>& out, std::string& error)
{
    return LoadHistory(sid, conversation_id, limit, out, error);
}

/**
 * @brief 按 sid + conversation_id 查询历史消息，并输出时间正序结果。
 */
bool ChatRepository::LoadHistory(const std::string& sid, const std::string& conversation_id, size_t limit, std::vector<common::ChatMessage>& out, std::string& error)
{
    // 保护共享 MySQL 连接与查询流程，避免并发线程同时操作同一连接句柄。
    std::lock_guard<std::mutex> lock(m_mutex);

    // 确保连接可用；不可用时会在 EnsureConnected 内自动重连。
    if (!EnsureConnected(error))
    {
        return false;
    }

    // limit=0 表示调用方不需要任何数据，直接清空输出并返回成功。
    if (limit == 0)
    {
        out.clear();
        return true;
    }

    // 对字符串做 SQL 转义，避免拼接查询语句时出现注入或语法破坏。
    const std::string safe_sid = Escape(sid);
    const std::string safe_conversation_id = Escape(conversation_id);

    // 组装查询语句：
    // 1) 按 sid + conversation_id 过滤同一会话；
    // 2) 按 created_at_ms 倒序取最近 limit 条；
    // 3) 后面会再 reverse 成正序返回给业务层。
    std::ostringstream sql;
    sql << "SELECT role, content, created_at_ms "
           "FROM chat_messages "
           "WHERE sid='"
        << safe_sid
        << "' AND conversation_id='"
        << safe_conversation_id
        << "' ORDER BY created_at_ms DESC LIMIT " << limit;

    // 向 MySQL 发送 SQL 文本并执行；返回非 0 表示语句执行失败。
    if (mysql_query(m_conn, sql.str().c_str()) != 0)
    {
        error = mysql_error(m_conn);
        return false;
    }

    // 把查询结果从连接句柄中取出为结果集对象。
    // 对 SELECT 来说，成功时通常返回非空 MYSQL_RES*。
    MYSQL_RES* res = mysql_store_result(m_conn);
    if (!res)
    {
        // mysql_field_count==0 代表这次语句没有结果集字段。
        // 在当前查询语境下可按“无数据”处理，返回空 out。
        if (mysql_field_count(m_conn) == 0)
        {
            out.clear();
            return true;
        }

        // 其余情况视为错误（例如连接异常等）。
        error = mysql_error(m_conn);
        return false;
    }

    // 清空调用方传入的 out，避免和旧数据混合。
    out.clear();

    // MYSQL_ROW 本质是 `char**`，每一列是 C 字符串指针。
    MYSQL_ROW row;

    // 每次 mysql_fetch_row 取一行；取完后返回 nullptr。
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        // 构造一条业务消息对象。
        common::ChatMessage message;

        // row[0] 对应 SELECT 的 role 列；空指针时退化为空串。
        message.role = row[0] ? row[0] : "";

        // row[1] 对应 content 列；同样做空指针保护。
        message.content = row[1] ? row[1] : "";

        // row[2] 对应 created_at_ms（字符串），转成 uint64_t 毫秒时间戳。
        // strtoull 解析失败时会得到 0（这里按 0 处理）。
        message.created_at_ms = row[2] ? static_cast<uint64_t>(std::strtoull(row[2], nullptr, 10)) : 0;

        // 把当前行转换后的业务对象追加到 out。
        // 到这里，SQL 结果就完成了“行 -> ChatMessage -> out”的映射。
        out.push_back(message);
    }

    // 释放 MySQL 结果集内存，避免泄漏。
    mysql_free_result(res);

    // 当前 SQL 是 DESC（新到旧），这里反转为 ASC（旧到新）便于上下文拼接。
    std::reverse(out.begin(), out.end());
    return true;
}

/**
 * @brief 确保连接可用，不可用时自动重连。
 */
bool ChatRepository::EnsureConnected(std::string& error)
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

    if (!mysql_real_connect(m_conn, m_settings.host.c_str(), m_settings.user.c_str(), m_settings.password.c_str(), m_settings.database.c_str(), m_settings.port, nullptr, CLIENT_MULTI_STATEMENTS))
    {
        error = mysql_error(m_conn);
        mysql_close(m_conn);
        m_conn = nullptr;
        return false;
    }

    return true;
}

/**
 * @brief 确保最小表结构存在（幂等建表）。
 */
bool ChatRepository::EnsureSchema(std::string& error)
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
 * @brief 执行单条 SQL 语句。
 */
bool ChatRepository::ExecuteSql(const std::string& sql, std::string& error)
{
    if (mysql_query(m_conn, sql.c_str()) != 0)
    {
        error = mysql_error(m_conn);
        BASE_LOG_ERROR(g_logger) << "Execute sql failed: " << sql << " error=" << error;
        return false;
    }
    return true;
}

/**
 * @brief 对字符串做 SQL 转义，避免拼接 SQL 时的注入风险。
 */
std::string ChatRepository::Escape(const std::string& value)
{
    std::string escaped;
    escaped.resize(value.size() * 2 + 1);
    unsigned long length = mysql_real_escape_string(m_conn, &escaped[0], value.c_str(), value.size());
    escaped.resize(length);
    return escaped;
}

} // namespace storage
} // namespace ai
