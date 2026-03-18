#include "ai/storage/auth_repository.h"

#include "log/logger.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace ai
{
namespace storage
{

/**
 * @file auth_repository.cc
 * @brief 账号与鉴权仓库实现。
 */

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

/// @brief 构造函数，保存连接池并初始化状态位。
AuthRepository::AuthRepository(const MysqlConnectionPool::ptr& pool)
    : m_pool(pool)
    , m_initialized(false)
{
}

/// @brief 析构函数（无额外资源释放逻辑）。
AuthRepository::~AuthRepository()
{
}

/// @brief 初始化仓库并确保 users/auth_tokens 表存在。
bool AuthRepository::Init(std::string& error)
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

/// @brief 写入一条用户记录，返回新用户 ID。
bool AuthRepository::CreateUser(const std::string& username,
                                const std::string& password_hash,
                                const std::string& password_salt,
                                uint64_t now_ms,
                                uint64_t& user_id,
                                std::string& error)
{
    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    const std::string safe_username = Escape(conn.get(), username);
    const std::string safe_hash = Escape(conn.get(), password_hash);
    const std::string safe_salt = Escape(conn.get(), password_salt);

    std::ostringstream sql;
    sql << "INSERT INTO users (username, password_hash, password_salt, status, created_at_ms, updated_at_ms) VALUES ('"
        << safe_username
        << "', '"
        << safe_hash
        << "', '"
        << safe_salt
        << "', 1, "
        << now_ms
        << ", "
        << now_ms
        << ")";

    if (mysql_query(conn.get(), sql.str().c_str()) != 0)
    {
        if (mysql_errno(conn.get()) == 1062)
        {
            error = "username already exists";
        }
        else
        {
            error = mysql_error(conn.get());
        }
        return false;
    }

    user_id = static_cast<uint64_t>(mysql_insert_id(conn.get()));
    return true;
}

/// @brief 按用户名查询用户记录（供登录鉴权使用）。
bool AuthRepository::GetUserByUsername(const std::string& username,
                                       UserRecord& out,
                                       std::string& error)
{
    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    const std::string safe_username = Escape(conn.get(), username);

    std::ostringstream sql;
    sql << "SELECT id, username, password_hash, password_salt, status "
           "FROM users WHERE username='"
        << safe_username
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
            error = "user not found";
            return false;
        }
        error = mysql_error(conn.get());
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row)
    {
        mysql_free_result(res);
        error = "user not found";
        return false;
    }

    out.id = row[0] ? static_cast<uint64_t>(std::strtoull(row[0], nullptr, 10)) : 0;
    out.username = row[1] ? row[1] : "";
    out.password_hash = row[2] ? row[2] : "";
    out.password_salt = row[3] ? row[3] : "";
    out.status = row[4] ? static_cast<uint32_t>(std::strtoul(row[4], nullptr, 10)) : 0;

    mysql_free_result(res);
    return true;
}

/// @brief 保存 token 哈希与过期时间。
bool AuthRepository::SaveToken(uint64_t user_id,
                               const std::string& token_hash,
                               uint64_t expires_at_ms,
                               uint64_t now_ms,
                               std::string& error)
{
    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    const std::string safe_token_hash = Escape(conn.get(), token_hash);

    std::ostringstream sql;
    sql << "INSERT INTO auth_tokens (user_id, token_hash, expires_at_ms, revoked_at_ms, created_at_ms, last_seen_at_ms) VALUES ("
        << user_id
        << ", '"
        << safe_token_hash
        << "', "
        << expires_at_ms
        << ", 0, "
        << now_ms
        << ", "
        << now_ms
        << ")";

    if (mysql_query(conn.get(), sql.str().c_str()) != 0)
    {
        error = mysql_error(conn.get());
        return false;
    }

    return true;
}

/// @brief 按 token_hash 查询 token + 用户状态（联表）。
bool AuthRepository::GetToken(const std::string& token_hash,
                              TokenRecord& out,
                              std::string& error)
{
    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    const std::string safe_token_hash = Escape(conn.get(), token_hash);

    std::ostringstream sql;
    sql << "SELECT t.user_id, u.username, t.expires_at_ms, t.revoked_at_ms, u.status "
           "FROM auth_tokens t "
           "JOIN users u ON u.id=t.user_id "
           "WHERE t.token_hash='"
        << safe_token_hash
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
            error = "token not found";
            return false;
        }
        error = mysql_error(conn.get());
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row)
    {
        mysql_free_result(res);
        error = "token not found";
        return false;
    }

    out.user_id = row[0] ? static_cast<uint64_t>(std::strtoull(row[0], nullptr, 10)) : 0;
    out.username = row[1] ? row[1] : "";
    out.expires_at_ms = row[2] ? static_cast<uint64_t>(std::strtoull(row[2], nullptr, 10)) : 0;
    out.revoked_at_ms = row[3] ? static_cast<uint64_t>(std::strtoull(row[3], nullptr, 10)) : 0;
    out.user_status = row[4] ? static_cast<uint32_t>(std::strtoul(row[4], nullptr, 10)) : 0;

    mysql_free_result(res);
    return true;
}

/// @brief 将 token 标记为已撤销。
bool AuthRepository::RevokeToken(const std::string& token_hash,
                                 uint64_t revoked_at_ms,
                                 std::string& error)
{
    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    const std::string safe_token_hash = Escape(conn.get(), token_hash);

    std::ostringstream sql;
    sql << "UPDATE auth_tokens SET revoked_at_ms="
        << revoked_at_ms
        << " WHERE token_hash='"
        << safe_token_hash
        << "' AND revoked_at_ms=0";

    if (mysql_query(conn.get(), sql.str().c_str()) != 0)
    {
        error = mysql_error(conn.get());
        return false;
    }

    return true;
}

/// @brief 幂等建表：users 与 auth_tokens。
bool AuthRepository::EnsureSchema(std::string& error)
{
    ScopedMysqlConn conn(m_pool, 0, error);
    if (!conn)
    {
        return false;
    }

    static const char* kCreateUsersSql =
        "CREATE TABLE IF NOT EXISTS users ("
        " id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
        " username VARCHAR(64) NOT NULL,"
        " password_hash VARCHAR(128) NOT NULL,"
        " password_salt VARCHAR(64) NOT NULL,"
        " status TINYINT UNSIGNED NOT NULL DEFAULT 1,"
        " created_at_ms BIGINT UNSIGNED NOT NULL,"
        " updated_at_ms BIGINT UNSIGNED NOT NULL,"
        " PRIMARY KEY (id),"
        " UNIQUE KEY uniq_username (username)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    static const char* kCreateAuthTokensSql =
        "CREATE TABLE IF NOT EXISTS auth_tokens ("
        " id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
        " user_id BIGINT UNSIGNED NOT NULL,"
        " token_hash VARCHAR(128) NOT NULL,"
        " expires_at_ms BIGINT UNSIGNED NOT NULL,"
        " revoked_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        " created_at_ms BIGINT UNSIGNED NOT NULL,"
        " last_seen_at_ms BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        " PRIMARY KEY (id),"
        " UNIQUE KEY uniq_token_hash (token_hash),"
        " KEY idx_user_id (user_id),"
        " KEY idx_expires_at_ms (expires_at_ms)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    return ExecuteSql(conn.get(), kCreateUsersSql, error) &&
           ExecuteSql(conn.get(), kCreateAuthTokensSql, error);
}

/// @brief 执行通用 SQL，并在失败时统一记录错误日志。
bool AuthRepository::ExecuteSql(MYSQL* conn, const std::string& sql, std::string& error)
{
    if (mysql_query(conn, sql.c_str()) != 0)
    {
        error = mysql_error(conn);
        BASE_LOG_ERROR(g_logger) << "Execute sql failed: " << sql << " error=" << error;
        return false;
    }
    return true;
}

/// @brief 调用 mysql_real_escape_string 对字符串做 SQL 转义。
std::string AuthRepository::Escape(MYSQL* conn, const std::string& value)
{
    std::string escaped;
    escaped.resize(value.size() * 2 + 1);
    unsigned long length = mysql_real_escape_string(conn, &escaped[0], value.c_str(), value.size());
    escaped.resize(length);
    return escaped;
}

} // namespace storage
} // namespace ai
