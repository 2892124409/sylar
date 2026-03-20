#include "ai/storage/api_key_pool_repository.h"

#include "log/logger.h"

#include <cstdlib>
#include <sstream>

namespace ai
{
namespace storage
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

/**
 * @brief 构造 API Key 池仓库。
 */
ApiKeyPoolRepository::ApiKeyPoolRepository(const MysqlConnectionPool::ptr& pool)
    : m_pool(pool)
    , m_initialized(false)
{
}

/**
 * @brief 析构函数。
 */
ApiKeyPoolRepository::~ApiKeyPoolRepository()
{
}

/**
 * @brief 初始化仓库并确保表结构存在。
 */
bool ApiKeyPoolRepository::Init(std::string& error)
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

/**
 * @brief 加载指定 provider_id 的启用 key。
 * @details
 * 查询按 `priority DESC, id ASC` 排序，方便上层优先选择高优先级 key。
 */
bool ApiKeyPoolRepository::LoadEnabledKeys(const std::string& provider_id,
                                           std::vector<ApiKeyRecord>& out,
                                           std::string& error)
{
    out.clear();
    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    const std::string safe_provider = Escape(conn.get(), provider_id);
    std::ostringstream sql;
    sql << "SELECT id, provider, name, api_key, enabled, priority, weight, cooldown_until_ms, fail_count "
           "FROM llm_api_keys WHERE provider='"
        << safe_provider
        << "' AND enabled=1 ORDER BY priority DESC, id ASC";

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
            return true;
        }
        error = mysql_error(conn.get());
        return false;
    }

    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        ApiKeyRecord record;
        record.id = row[0] ? static_cast<uint64_t>(std::strtoull(row[0], nullptr, 10)) : 0;
        record.provider_id = row[1] ? row[1] : "";
        record.name = row[2] ? row[2] : "";
        record.api_key = row[3] ? row[3] : "";
        record.enabled = row[4] && std::strtoul(row[4], nullptr, 10) == 1;
        record.priority = row[5] ? static_cast<int>(std::strtol(row[5], nullptr, 10)) : 0;
        record.weight = row[6] ? static_cast<int>(std::strtol(row[6], nullptr, 10)) : 1;
        record.cooldown_until_ms = row[7] ? static_cast<uint64_t>(std::strtoull(row[7], nullptr, 10)) : 0;
        record.fail_count = row[8] ? static_cast<uint64_t>(std::strtoull(row[8], nullptr, 10)) : 0;
        if (!record.api_key.empty())
        {
            out.push_back(record);
        }
    }

    mysql_free_result(res);
    return true;
}

/**
 * @brief 标记 key 成功。
 * @details
 * 成功后清空失败上下文，恢复该 key 的正常可用状态。
 */
bool ApiKeyPoolRepository::MarkKeySuccess(uint64_t key_id, uint64_t now_ms, std::string& error)
{
    if (key_id == 0)
    {
        return true;
    }

    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    std::ostringstream sql;
    sql << "UPDATE llm_api_keys SET fail_count=0, last_error_code='', last_error_at_ms=0, cooldown_until_ms=0, updated_at_ms="
        << now_ms
        << " WHERE id="
        << key_id;

    if (mysql_query(conn.get(), sql.str().c_str()) != 0)
    {
        error = mysql_error(conn.get());
        return false;
    }
    return true;
}

/**
 * @brief 标记 key 失败。
 * @details
 * - fail_count 自增；
 * - 记录 last_error_code / last_error_at_ms；
 * - cooldown_until_ms 取最大值，避免较短冷却覆盖较长冷却。
 */
bool ApiKeyPoolRepository::MarkKeyFailure(uint64_t key_id,
                                          const std::string& error_code,
                                          uint64_t cooldown_until_ms,
                                          uint64_t now_ms,
                                          std::string& error)
{
    if (key_id == 0)
    {
        return true;
    }

    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    const std::string safe_error_code = Escape(conn.get(), error_code);
    std::ostringstream sql;
    sql << "UPDATE llm_api_keys SET fail_count=fail_count+1, last_error_code='"
        << safe_error_code
        << "', last_error_at_ms="
        << now_ms
        << ", cooldown_until_ms=GREATEST(cooldown_until_ms,"
        << cooldown_until_ms
        << "), updated_at_ms="
        << now_ms
        << " WHERE id="
        << key_id;

    if (mysql_query(conn.get(), sql.str().c_str()) != 0)
    {
        error = mysql_error(conn.get());
        return false;
    }
    return true;
}

/**
 * @brief 幂等建表。
 * @details
 * `llm_api_keys.provider` 列在业务语义上存放 `provider_id`。
 */
bool ApiKeyPoolRepository::EnsureSchema(std::string& error)
{
    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    static const char* kCreateKeysSql =
        "CREATE TABLE IF NOT EXISTS llm_api_keys ("
        " id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
        " provider VARCHAR(32) NOT NULL,"
        " name VARCHAR(128) NOT NULL DEFAULT '',"
        " api_key TEXT NOT NULL,"
        " enabled TINYINT(1) NOT NULL DEFAULT 1,"
        " priority INT NOT NULL DEFAULT 0,"
        " weight INT NOT NULL DEFAULT 1,"
        " cooldown_until_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        " fail_count BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        " last_error_code VARCHAR(64) NOT NULL DEFAULT '',"
        " last_error_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        " created_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        " updated_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        " PRIMARY KEY (id),"
        " KEY idx_provider_enabled_priority (provider, enabled, priority),"
        " KEY idx_provider_cooldown (provider, cooldown_until_ms)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    return ExecuteSql(conn.get(), kCreateKeysSql, error);
}

/**
 * @brief 执行 SQL 并统一错误处理。
 */
bool ApiKeyPoolRepository::ExecuteSql(MYSQL* conn, const std::string& sql, std::string& error)
{
    if (mysql_query(conn, sql.c_str()) != 0)
    {
        error = mysql_error(conn);
        BASE_LOG_ERROR(g_logger) << "Execute sql failed: " << sql << " error=" << error;
        return false;
    }
    return true;
}

/**
 * @brief SQL 字符串转义。
 * @details
 * 使用 `mysql_real_escape_string`，避免 SQL 拼接时注入风险。
 */
std::string ApiKeyPoolRepository::Escape(MYSQL* conn, const std::string& value)
{
    std::string escaped;
    escaped.resize(value.size() * 2 + 1);
    unsigned long length = mysql_real_escape_string(conn, &escaped[0], value.c_str(), value.size());
    escaped.resize(length);
    return escaped;
}

} // namespace storage
} // namespace ai
