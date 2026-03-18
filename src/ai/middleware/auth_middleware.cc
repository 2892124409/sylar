#include "ai/middleware/auth_middleware.h"

#include "ai/common/ai_utils.h"

namespace ai
{
namespace middleware
{

/**
 * @file auth_middleware.cc
 * @brief AI 鉴权中间件实现。
 */

/**
 * @brief 构造函数，保存依赖对象。
 */
AuthMiddleware::AuthMiddleware(const ai::service::AuthService::ptr& auth_service)
    : m_auth_service(auth_service)
{
}

/**
 * @brief 鉴权前置逻辑。
 * @details 执行顺序：
 * 1) 公共路径直接放行；
 * 2) 无 Token 时直接返回 401；
 * 3) 有 Token 时调用 AuthService 鉴权；
 * 4) 鉴权成功后把身份信息写入请求头，供下游业务统一使用。
 */
bool AuthMiddleware::before(http::HttpRequest::ptr request,
                            http::HttpResponse::ptr response,
                            http::HttpSession::ptr)
{
    // Step 1: 先读取路径，公共接口不做强制鉴权。
    const std::string path = request->getPath();
    if (IsPublicAuthPath(path))
    {
        // 例如 healthz/register/login，直接进入路由处理。
        return true;
    }

    // Step 2: 尝试解析 Bearer Token。
    const std::string token = ParseBearerTokenFromAuthorization(request->getHeader("Authorization"));
    if (token.empty())
    {
        // 非公共路径统一要求登录态。
        ai::common::WriteJsonError(
            response,
            static_cast<http::HttpStatus>(401),
            "authorization required",
            request->getHeader("x-request-id"));
        // 返回 false 告诉中间件链“请求到此结束”。
        return false;
    }

    // Step 3: 有 token 但服务未注入，属于服务端装配错误，返回 500。
    if (!m_auth_service)
    {
        ai::common::WriteJsonError(
            response,
            http::HttpStatus::INTERNAL_SERVER_ERROR,
            "auth service unavailable",
            request->getHeader("x-request-id"));
        return false;
    }

    // Step 4: 调用认证服务解析 token。
    ai::service::AuthIdentity identity;
    std::string auth_error;
    http::HttpStatus auth_status = http::HttpStatus::OK;
    if (!m_auth_service->AuthenticateBearerToken(token, identity, auth_error, auth_status))
    {
        // 认证失败：透传状态码与错误信息（401/403 等）。
        ai::common::WriteJsonError(response, auth_status, auth_error, request->getHeader("x-request-id"));
        return false;
    }

    // Step 5: 认证成功，写入统一身份头，供下游 handler/service 使用。
    request->setHeader("X-Auth-User-Id", std::to_string(identity.user_id));
    request->setHeader("X-Auth-Username", identity.username);
    request->setHeader("X-Principal-Sid", identity.principal_sid);
    // 放行到具体路由处理逻辑。
    return true;
}

/**
 * @brief 公共路径判断。
 */
bool AuthMiddleware::IsPublicAuthPath(const std::string& path)
{
    return path == "/api/v1/healthz"
        || path == "/api/v1/auth/register"
        || path == "/api/v1/auth/login";
}

/**
 * @brief 解析 Bearer Token。
 */
std::string AuthMiddleware::ParseBearerTokenFromAuthorization(const std::string& authorization)
{
    const std::string prefix = "Bearer ";
    if (authorization.size() <= prefix.size())
    {
        return std::string();
    }

    if (authorization.compare(0, prefix.size(), prefix) != 0)
    {
        return std::string();
    }

    return authorization.substr(prefix.size());
}

} // namespace middleware
} // namespace ai
