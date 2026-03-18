#include "ai/service/auth_service.h"

#include "ai/common/ai_utils.h"
#include "log/logger.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ai
{
namespace service
{

/**
 * @file auth_service.cc
 * @brief 认证业务服务实现。
 */

/** @brief 认证模块日志器。 */
static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

/**
 * @brief 构造函数，保存配置与仓储依赖。
 */
AuthService::AuthService(const config::AuthSettings& settings,
                         const storage::AuthRepository::ptr& repository)
    : m_settings(settings)
    , m_repository(repository)
{
}

/**
 * @brief 注册流程：校验参数 -> 生成密码哈希 -> 落库创建用户。
 */
bool AuthService::Register(const std::string& username,
                           const std::string& password,
                           uint64_t& user_id,
                           std::string& error,
                           http::HttpStatus& status)
{
    // 1) 仓储依赖检查。
    if (!m_repository)
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        error = "auth repository is not initialized";
        return false;
    }

    // 2) 基础格式校验。
    if (!IsValidUsername(username))
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "invalid username";
        return false;
    }
    if (!IsValidPassword(password))
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "invalid password";
        return false;
    }

    // 3) 生成带随机 salt 的 PBKDF2 密码哈希。
    std::string salt_hex;
    std::string hash_hex;
    if (!BuildPasswordHash(password, salt_hex, hash_hex, error))
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    // 4) 写入用户记录。
    const uint64_t now_ms = common::NowMs();
    if (!m_repository->CreateUser(username, hash_hex, salt_hex, now_ms, user_id, error))
    {
        if (error == "username already exists")
        {
            status = static_cast<http::HttpStatus>(409);
        }
        else
        {
            status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        }
        return false;
    }

    status = http::HttpStatus::OK;
    return true;
}

/**
 * @brief 登录流程：查用户 -> 校验密码 -> 生成 token -> 持久化 token_hash。
 */
bool AuthService::Login(const std::string& username,
                        const std::string& password,
                        std::string& access_token,
                        AuthIdentity& identity,
                        std::string& error,
                        http::HttpStatus& status)
{
    // 1) 仓储依赖检查。
    if (!m_repository)
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        error = "auth repository is not initialized";
        return false;
    }

    // 2) 查询用户；不区分“用户名不存在/密码错误”的外显错误，减少枚举攻击面。
    storage::AuthRepository::UserRecord user;
    if (!m_repository->GetUserByUsername(username, user, error))
    {
        if (error == "user not found")
        {
            status = static_cast<http::HttpStatus>(401);
            error = "invalid username or password";
        }
        else
        {
            status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        }
        return false;
    }

    // 3) 校验用户状态与密码哈希。
    if (user.status != 1 || !VerifyPassword(password, user.password_salt, user.password_hash))
    {
        status = static_cast<http::HttpStatus>(401);
        error = "invalid username or password";
        return false;
    }

    // 4) 生成 token 明文。
    access_token = GenerateAccessToken(error);
    if (access_token.empty())
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    // 5) 保存 token 的哈希（不保存明文）。
    const std::string token_hash = Sha256Hex(access_token);
    const uint64_t now_ms = common::NowMs();
    const uint64_t expires_at_ms = now_ms + m_settings.token_ttl_seconds * 1000;

    if (!m_repository->SaveToken(user.id, token_hash, expires_at_ms, now_ms, error))
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    // 6) 返回登录身份信息。
    identity.authenticated = true;
    identity.user_id = user.id;
    identity.username = user.username;
    identity.principal_sid = BuildPrincipalSid(user.id);

    status = http::HttpStatus::OK;
    return true;
}

/**
 * @brief Bearer 鉴权：查 token -> 检查过期/撤销/用户状态 -> 返回身份。
 */
bool AuthService::AuthenticateBearerToken(const std::string& token,
                                          AuthIdentity& identity,
                                          std::string& error,
                                          http::HttpStatus& status)
{
    // 1) 基础入参校验。
    if (token.empty())
    {
        status = static_cast<http::HttpStatus>(401);
        error = "missing bearer token";
        return false;
    }

    // 2) 仓储依赖检查。
    if (!m_repository)
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        error = "auth repository is not initialized";
        return false;
    }

    // 3) 用 token 哈希查询数据库记录。
    storage::AuthRepository::TokenRecord token_record;
    if (!m_repository->GetToken(Sha256Hex(token), token_record, error))
    {
        if (error == "token not found")
        {
            status = static_cast<http::HttpStatus>(401);
            error = "invalid token";
        }
        else
        {
            status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        }
        return false;
    }

    // 4) 过期、撤销、禁用用户三类无效状态统一拒绝。
    const uint64_t now_ms = common::NowMs();
    if (token_record.revoked_at_ms != 0 || token_record.expires_at_ms <= now_ms || token_record.user_status != 1)
    {
        status = static_cast<http::HttpStatus>(401);
        error = "token expired or revoked";
        return false;
    }

    // 5) 返回鉴权成功的身份快照。
    identity.authenticated = true;
    identity.user_id = token_record.user_id;
    identity.username = token_record.username;
    identity.principal_sid = BuildPrincipalSid(token_record.user_id);

    status = http::HttpStatus::OK;
    return true;
}

/**
 * @brief 登出：将 token 标记为撤销。
 */
bool AuthService::Logout(const std::string& token,
                         std::string& error,
                         http::HttpStatus& status)
{
    // 1) 基础入参校验。
    if (token.empty())
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "missing bearer token";
        return false;
    }

    // 2) 仓储依赖检查。
    if (!m_repository)
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        error = "auth repository is not initialized";
        return false;
    }

    // 3) 按 token_hash 撤销，撤销后鉴权会被拒绝。
    if (!m_repository->RevokeToken(Sha256Hex(token), common::NowMs(), error))
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    status = http::HttpStatus::OK;
    return true;
}

/**
 * @brief 构造主体 SID：`u:<user_id>`。
 */
std::string AuthService::BuildPrincipalSid(uint64_t user_id)
{
    std::ostringstream ss;
    ss << "u:" << user_id;
    return ss.str();
}

/**
 * @brief 用户名格式校验。
 */
bool AuthService::IsValidUsername(const std::string& username)
{
    if (username.size() < 3 || username.size() > 64)
    {
        return false;
    }

    for (size_t i = 0; i < username.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(username[i]);
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.'))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief 密码长度校验。
 */
bool AuthService::IsValidPassword(const std::string& password)
{
    return password.size() >= 6 && password.size() <= 128;
}

/**
 * @brief 生成密码 salt 与哈希。
 */
bool AuthService::BuildPasswordHash(const std::string& password,
                                    std::string& salt_hex,
                                    std::string& hash_hex,
                                    std::string& error) const
{
    // 1) 随机生成 16 字节 salt。
    unsigned char salt[16];
    if (RAND_bytes(salt, sizeof(salt)) != 1)
    {
        error = "generate password salt failed";
        return false;
    }

    salt_hex = BytesToHex(salt, sizeof(salt));

    // 2) 使用 PBKDF2 派生哈希。
    std::string salt_bytes(reinterpret_cast<const char*>(salt), sizeof(salt));
    return DerivePasswordHash(password, salt_bytes, hash_hex, error);
}

/**
 * @brief 校验明文密码是否匹配期望哈希。
 */
bool AuthService::VerifyPassword(const std::string& password,
                                 const std::string& salt_hex,
                                 const std::string& expected_hash_hex) const
{
    std::string salt_bytes;
    if (!HexToBytes(salt_hex, salt_bytes))
    {
        return false;
    }

    std::string hash_hex;
    std::string error;
    if (!DerivePasswordHash(password, salt_bytes, hash_hex, error))
    {
        BASE_LOG_WARN(g_logger) << "derive password hash failed in verify: " << error;
        return false;
    }

    return hash_hex == expected_hash_hex;
}

/**
 * @brief PBKDF2-HMAC-SHA256 密钥派生实现。
 */
bool AuthService::DerivePasswordHash(const std::string& password,
                                     const std::string& salt_bytes,
                                     std::string& hash_hex,
                                     std::string& error) const
{
    unsigned char output[32];
    if (PKCS5_PBKDF2_HMAC(password.c_str(),
                          static_cast<int>(password.size()),
                          reinterpret_cast<const unsigned char*>(salt_bytes.data()),
                          static_cast<int>(salt_bytes.size()),
                          static_cast<int>(m_settings.password_pbkdf2_iterations),
                          EVP_sha256(),
                          sizeof(output),
                          output) != 1)
    {
        error = "pbkdf2 derive failed";
        return false;
    }

    hash_hex = BytesToHex(output, sizeof(output));
    return true;
}

/**
 * @brief 生成随机访问令牌（32 字节随机数转十六进制）。
 */
std::string AuthService::GenerateAccessToken(std::string& error) const
{
    unsigned char bytes[32];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1)
    {
        error = "generate access token failed";
        return std::string();
    }
    return BytesToHex(bytes, sizeof(bytes));
}

/**
 * @brief 计算字符串 SHA256 并转十六进制。
 */
std::string AuthService::Sha256Hex(const std::string& plain)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(plain.data()), plain.size(), digest);
    return BytesToHex(digest, SHA256_DIGEST_LENGTH);
}

/**
 * @brief 二进制字节串转十六进制字符串。
 */
std::string AuthService::BytesToHex(const unsigned char* data, size_t len)
{
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.resize(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        out[i * 2] = kHex[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return out;
}

/**
 * @brief 十六进制字符串转二进制字节串。
 */
bool AuthService::HexToBytes(const std::string& hex, std::string& out)
{
    if (hex.size() % 2 != 0)
    {
        return false;
    }

    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        const char hi = hex[i];
        const char lo = hex[i + 1];

        if (!std::isxdigit(static_cast<unsigned char>(hi)) ||
            !std::isxdigit(static_cast<unsigned char>(lo)))
        {
            return false;
        }

        int hi_val = std::isdigit(static_cast<unsigned char>(hi))
                         ? hi - '0'
                         : static_cast<int>(std::tolower(static_cast<unsigned char>(hi)) - 'a' + 10);
        int lo_val = std::isdigit(static_cast<unsigned char>(lo))
                         ? lo - '0'
                         : static_cast<int>(std::tolower(static_cast<unsigned char>(lo)) - 'a' + 10);

        out.push_back(static_cast<char>((hi_val << 4) | lo_val));
    }
    return true;
}

} // namespace service
} // namespace ai
