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
 * @brief 鉴权后身份信息。
 */
struct AuthIdentity
{
    bool authenticated = false;
    uint64_t user_id = 0;
    std::string username;
    std::string principal_sid;
};

/**
 * @brief 账号注册、登录、Token 鉴权服务。
 */
class AuthService
{
  public:
    typedef std::shared_ptr<AuthService> ptr;

    AuthService(const config::AuthSettings& settings,
                const storage::AuthRepository::ptr& repository);

    bool Register(const std::string& username,
                  const std::string& password,
                  uint64_t& user_id,
                  std::string& error,
                  http::HttpStatus& status);

    bool Login(const std::string& username,
               const std::string& password,
               std::string& access_token,
               AuthIdentity& identity,
               std::string& error,
               http::HttpStatus& status);

    bool AuthenticateBearerToken(const std::string& token,
                                 AuthIdentity& identity,
                                 std::string& error,
                                 http::HttpStatus& status);

    bool Logout(const std::string& token,
                std::string& error,
                http::HttpStatus& status);

    static std::string BuildPrincipalSid(uint64_t user_id);

  private:
    static bool IsValidUsername(const std::string& username);
    static bool IsValidPassword(const std::string& password);

    bool BuildPasswordHash(const std::string& password,
                           std::string& salt_hex,
                           std::string& hash_hex,
                           std::string& error) const;

    bool VerifyPassword(const std::string& password,
                        const std::string& salt_hex,
                        const std::string& expected_hash_hex) const;

    bool DerivePasswordHash(const std::string& password,
                            const std::string& salt_bytes,
                            std::string& hash_hex,
                            std::string& error) const;

    std::string GenerateAccessToken(std::string& error) const;
    static std::string Sha256Hex(const std::string& plain);
    static std::string BytesToHex(const unsigned char* data, size_t len);
    static bool HexToBytes(const std::string& hex, std::string& out);

  private:
    config::AuthSettings m_settings;
    storage::AuthRepository::ptr m_repository;
};

} // namespace service
} // namespace ai

#endif
