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
 * @file auth_repository.h
 * @brief 账号与鉴权持久化仓库声明（MySQL 实现）。
 */

/**
 * @brief 账号体系持久化仓库。
 * @details
 * 本类只负责“数据落库与查询”，不负责业务规则判断：
 * - 用户注册写入/按用户名读取；
 * - Token 写入/查询/撤销；
 * - 启动时确保 `users` 与 `auth_tokens` 表存在。
 *
 * 业务层（AuthService）通过本类完成鉴权数据访问。
 */
class AuthRepository
{
  public:
    /** @brief 智能指针别名。 */
    typedef std::shared_ptr<AuthRepository> ptr;

    /**
     * @brief 用户记录（对应 users 表的一行核心字段）。
     */
    struct UserRecord
    {
        /** @brief 用户 ID（主键）。 */
        uint64_t id = 0;
        /** @brief 用户名（唯一）。 */
        std::string username;
        /** @brief 密码哈希（PBKDF2 结果的十六进制字符串）。 */
        std::string password_hash;
        /** @brief 密码盐值（十六进制字符串）。 */
        std::string password_salt;
        /** @brief 用户状态（1=可用，其他值按禁用处理）。 */
        uint32_t status = 0;
    };

    /**
     * @brief Token 记录（auth_tokens 与 users 关联查询结果）。
     */
    struct TokenRecord
    {
        /** @brief token 所属用户 ID。 */
        uint64_t user_id = 0;
        /** @brief token 所属用户名（联表获取）。 */
        std::string username;
        /** @brief token 过期时间（毫秒时间戳）。 */
        uint64_t expires_at_ms = 0;
        /** @brief token 撤销时间（0 表示未撤销）。 */
        uint64_t revoked_at_ms = 0;
        /** @brief token 对应用户状态（联表获取）。 */
        uint32_t user_status = 0;
    };

  public:
    /**
     * @brief 构造仓库对象。
     * @param pool MySQL 连接池。
     */
    explicit AuthRepository(const MysqlConnectionPool::ptr& pool);
    /** @brief 析构函数。 */
    ~AuthRepository();

    /**
     * @brief 初始化仓库。
     * @details 幂等初始化；内部会检查并创建必要表结构。
     * @param[out] error 失败原因。
     * @return true 成功；false 失败。
     */
    bool Init(std::string& error);

    /**
     * @brief 创建用户。
     * @param username 用户名（应由上层完成格式校验）。
     * @param password_hash 密码哈希（十六进制字符串）。
     * @param password_salt 密码盐值（十六进制字符串）。
     * @param now_ms 当前毫秒时间戳（写入 created_at_ms/updated_at_ms）。
     * @param[out] user_id 新创建用户 ID。
     * @param[out] error 失败原因。
     * @return true 创建成功；false 创建失败。
     * @note 当用户名冲突时，error 约定为 `"username already exists"`。
     */
    bool CreateUser(const std::string& username,
                    const std::string& password_hash,
                    const std::string& password_salt,
                    uint64_t now_ms,
                    uint64_t& user_id,
                    std::string& error);

    /**
     * @brief 按用户名查询用户记录。
     * @param username 用户名。
     * @param[out] out 查询到的用户记录。
     * @param[out] error 失败原因。
     * @return true 查询成功；false 查询失败。
     * @note 未找到时，error 约定为 `"user not found"`。
     */
    bool GetUserByUsername(const std::string& username,
                           UserRecord& out,
                           std::string& error);

    /**
     * @brief 保存访问令牌（仅保存哈希，不保存明文）。
     * @param user_id 所属用户 ID。
     * @param token_hash token 的 SHA256 哈希。
     * @param expires_at_ms 过期时间（毫秒时间戳）。
     * @param now_ms 当前毫秒时间戳（写入 created_at_ms/last_seen_at_ms）。
     * @param[out] error 失败原因。
     * @return true 保存成功；false 保存失败。
     */
    bool SaveToken(uint64_t user_id,
                   const std::string& token_hash,
                   uint64_t expires_at_ms,
                   uint64_t now_ms,
                   std::string& error);

    /**
     * @brief 按 token 哈希查询 token 记录。
     * @details 该查询会联表 users，返回用户名与用户状态，供上层一次性做鉴权判定。
     * @param token_hash token 的 SHA256 哈希。
     * @param[out] out 查询结果。
     * @param[out] error 失败原因。
     * @return true 查询成功；false 查询失败。
     * @note 未找到时，error 约定为 `"token not found"`。
     */
    bool GetToken(const std::string& token_hash,
                  TokenRecord& out,
                  std::string& error);

    /**
     * @brief 撤销 token（逻辑失效）。
     * @param token_hash token 的 SHA256 哈希。
     * @param revoked_at_ms 撤销时间（毫秒时间戳）。
     * @param[out] error 失败原因。
     * @return true 执行成功；false 执行失败。
     */
    bool RevokeToken(const std::string& token_hash,
                     uint64_t revoked_at_ms,
                     std::string& error);

  private:
    /**
     * @brief 确保鉴权相关表结构存在。
     * @param[out] error 失败原因。
     * @return true 成功；false 失败。
     */
    bool EnsureSchema(std::string& error);
    /**
     * @brief 执行一条 SQL 语句。
     * @param conn MySQL 连接。
     * @param sql 待执行 SQL。
     * @param[out] error 失败原因。
     * @return true 成功；false 失败。
     */
    bool ExecuteSql(MYSQL* conn, const std::string& sql, std::string& error);
    /**
     * @brief 对字符串做 SQL 转义，降低拼接 SQL 注入风险。
     * @param conn MySQL 连接。
     * @param value 原始字符串。
     * @return 转义后的字符串。
     */
    std::string Escape(MYSQL* conn, const std::string& value);

  private:
    /** @brief MySQL 连接池依赖。 */
    MysqlConnectionPool::ptr m_pool;
    /** @brief 初始化标记，避免重复建表。 */
    bool m_initialized;
};

} // namespace storage
} // namespace ai

#endif
