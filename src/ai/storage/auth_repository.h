#ifndef __SYLAR_AI_STORAGE_AUTH_REPOSITORY_H__
#define __SYLAR_AI_STORAGE_AUTH_REPOSITORY_H__

#include "ai/storage/mysql_connection_pool.h"

#include <memory>
#include <string>

namespace ai
{
namespace storage
{

/**
 * @brief 账号与鉴权相关持久化仓库（MySQL 实现）。
 */
class AuthRepository
{
  public:
    typedef std::shared_ptr<AuthRepository> ptr;

    /**
     * @brief 用户记录。
     */
    struct UserRecord
    {
        uint64_t id = 0;
        std::string username;
        std::string password_hash;
        std::string password_salt;
        uint32_t status = 0;
    };

    /**
     * @brief Token 记录。
     */
    struct TokenRecord
    {
        uint64_t user_id = 0;
        std::string username;
        uint64_t expires_at_ms = 0;
        uint64_t revoked_at_ms = 0;
        uint32_t user_status = 0;
    };

  public:
    explicit AuthRepository(const MysqlConnectionPool::ptr& pool);
    ~AuthRepository();

    bool Init(std::string& error);

    bool CreateUser(const std::string& username,
                    const std::string& password_hash,
                    const std::string& password_salt,
                    uint64_t now_ms,
                    uint64_t& user_id,
                    std::string& error);

    bool GetUserByUsername(const std::string& username,
                           UserRecord& out,
                           std::string& error);

    bool SaveToken(uint64_t user_id,
                   const std::string& token_hash,
                   uint64_t expires_at_ms,
                   uint64_t now_ms,
                   std::string& error);

    bool GetToken(const std::string& token_hash,
                  TokenRecord& out,
                  std::string& error);

    bool RevokeToken(const std::string& token_hash,
                     uint64_t revoked_at_ms,
                     std::string& error);

  private:
    bool EnsureSchema(std::string& error);
    bool ExecuteSql(MYSQL* conn, const std::string& sql, std::string& error);
    std::string Escape(MYSQL* conn, const std::string& value);

  private:
    MysqlConnectionPool::ptr m_pool;
    bool m_initialized;
};

} // namespace storage
} // namespace ai

#endif
