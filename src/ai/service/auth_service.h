#ifndef __SYLAR_AI_SERVICE_AUTH_SERVICE_H__
#define __SYLAR_AI_SERVICE_AUTH_SERVICE_H__

#include "ai/config/ai_app_config.h"
#include "ai/storage/auth_repository.h"
#include "http/core/http.h"

#include <memory>
#include <string>

namespace ai
{
namespace service
{

/**
 * @file auth_service.h
 * @brief 认证业务服务声明。
 */

/**
 * @brief 鉴权成功后的身份信息快照。
 * @details
 * 该结构由 AuthService 输出，供中间件/路由层向下游传递主体信息。
 */
struct AuthIdentity
{
    /** @brief 是否已通过鉴权。 */
    bool authenticated = false;
    /** @brief 用户 ID。 */
    uint64_t user_id = 0;
    /** @brief 用户名。 */
    std::string username;
    /** @brief 统一主体 SID，格式 `u:<user_id>`。 */
    std::string principal_sid;
};

/**
 * @brief 账号认证业务服务。
 * @details
 * 职责边界：
 * - 负责注册、登录、Bearer 鉴权、登出的业务规则；
 * - 负责密码哈希派生、token 生成与哈希；
 * - 通过 AuthRepository 完成数据读写。
 *
 * 非职责：
 * - 不直接处理 HTTP 协议（由 handler/middleware 负责）；
 * - 不直接管理数据库连接（由 repository/pool 负责）。
 */
class AuthService
{
  public:
    /** @brief 智能指针别名。 */
    typedef std::shared_ptr<AuthService> ptr;

    /**
     * @brief 构造认证服务。
     * @param settings 认证配置（token TTL、PBKDF2 迭代次数）。
     * @param repository 认证仓储依赖。
     */
    AuthService(const config::AuthSettings& settings,
                const storage::AuthRepository::ptr& repository);

    /**
     * @brief 用户注册。
     * @details
     * 流程：
     * 1. 校验用户名/密码格式；
     * 2. 生成随机 salt + PBKDF2 密码哈希；
     * 3. 写入 users 表。
     * @param username 用户名。
     * @param password 明文密码（仅在内存中参与派生，不直接落库）。
     * @param[out] user_id 新注册用户 ID。
     * @param[out] error 失败原因。
     * @param[out] status 对应 HTTP 状态码语义。
     * @return true 成功；false 失败。
     */
    bool Register(const std::string& username,
                  const std::string& password,
                  uint64_t& user_id,
                  std::string& error,
                  http::HttpStatus& status);

    /**
     * @brief 用户登录并签发访问令牌。
     * @details
     * 流程：
     * 1. 按用户名查用户；
     * 2. 校验用户状态与密码；
     * 3. 生成随机 token；
     * 4. 存储 token_hash 与过期时间；
     * 5. 返回身份信息（含 principal_sid）。
     * @param username 用户名。
     * @param password 明文密码。
     * @param[out] access_token 返回 token 明文（仅返回给客户端）。
     * @param[out] identity 返回鉴权主体信息。
     * @param[out] error 失败原因。
     * @param[out] status 对应 HTTP 状态码语义。
     * @return true 成功；false 失败。
     */
    bool Login(const std::string& username,
               const std::string& password,
               std::string& access_token,
               AuthIdentity& identity,
               std::string& error,
               http::HttpStatus& status);

    /**
     * @brief Bearer token 鉴权。
     * @details
     * 使用 token 明文计算 SHA256 后查询数据库，再检查：
     * - token 是否存在；
     * - 是否过期；
     * - 是否已撤销；
     * - 用户状态是否可用。
     * @param token Bearer token 明文。
     * @param[out] identity 鉴权成功时返回身份信息。
     * @param[out] error 失败原因。
     * @param[out] status 对应 HTTP 状态码语义。
     * @return true 鉴权通过；false 鉴权失败。
     */
    bool AuthenticateBearerToken(const std::string& token,
                                 AuthIdentity& identity,
                                 std::string& error,
                                 http::HttpStatus& status);

    /**
     * @brief 用户登出（撤销 token）。
     * @param token Bearer token 明文。
     * @param[out] error 失败原因。
     * @param[out] status 对应 HTTP 状态码语义。
     * @return true 成功；false 失败。
     */
    bool Logout(const std::string& token,
                std::string& error,
                http::HttpStatus& status);

    /**
     * @brief 按用户 ID 构造主体 SID。
     * @param user_id 用户 ID。
     * @return `u:<user_id>` 形式字符串。
     */
    static std::string BuildPrincipalSid(uint64_t user_id);

  private:
    /**
     * @brief 校验用户名格式。
     * @details 允许字符：字母、数字、`_`、`-`、`.`，长度 3~64。
     */
    static bool IsValidUsername(const std::string& username);
    /**
     * @brief 校验密码长度。
     * @details 当前策略仅做长度校验，范围 6~128。
     */
    static bool IsValidPassword(const std::string& password);

    /**
     * @brief 生成带随机 salt 的密码哈希。
     * @param password 明文密码。
     * @param[out] salt_hex 生成的 salt（十六进制）。
     * @param[out] hash_hex PBKDF2 输出哈希（十六进制）。
     * @param[out] error 失败原因。
     * @return true 成功；false 失败。
     */
    bool BuildPasswordHash(const std::string& password,
                           std::string& salt_hex,
                           std::string& hash_hex,
                           std::string& error) const;

    /**
     * @brief 校验明文密码是否与目标哈希匹配。
     * @param password 明文密码。
     * @param salt_hex 盐值（十六进制）。
     * @param expected_hash_hex 目标哈希（十六进制）。
     * @return true 匹配；false 不匹配或校验失败。
     */
    bool VerifyPassword(const std::string& password,
                        const std::string& salt_hex,
                        const std::string& expected_hash_hex) const;

    /**
     * @brief 执行 PBKDF2-HMAC-SHA256 派生。
     * @param password 明文密码。
     * @param salt_bytes 二进制盐值。
     * @param[out] hash_hex 派生哈希（十六进制）。
     * @param[out] error 失败原因。
     * @return true 成功；false 失败。
     */
    bool DerivePasswordHash(const std::string& password,
                            const std::string& salt_bytes,
                            std::string& hash_hex,
                            std::string& error) const;

    /**
     * @brief 生成随机访问令牌明文。
     * @param[out] error 失败原因。
     * @return 生成成功返回十六进制 token；失败返回空串。
     */
    std::string GenerateAccessToken(std::string& error) const;
    /**
     * @brief 计算字符串的 SHA256 十六进制摘要。
     */
    static std::string Sha256Hex(const std::string& plain);
    /**
     * @brief 二进制转十六进制字符串。
     */
    static std::string BytesToHex(const unsigned char* data, size_t len);
    /**
     * @brief 十六进制字符串转二进制字节串。
     */
    static bool HexToBytes(const std::string& hex, std::string& out);

  private:
    /** @brief 认证配置快照。 */
    config::AuthSettings m_settings;
    /** @brief 认证仓储依赖。 */
    storage::AuthRepository::ptr m_repository;
};

} // namespace service 
} // namespace ai

#endif
