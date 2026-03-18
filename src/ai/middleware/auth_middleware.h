#ifndef __SYLAR_AI_MIDDLEWARE_AUTH_MIDDLEWARE_H__
#define __SYLAR_AI_MIDDLEWARE_AUTH_MIDDLEWARE_H__

#include "ai/service/auth_service.h"

#include "http/middleware/middleware.h"

#include <memory>
#include <string>

namespace ai
{
namespace middleware
{

/**
 * @file auth_middleware.h
 * @brief AI 鉴权中间件声明。
 */

/**
 * @brief AI 服务鉴权中间件。
 * @details
 * 职责：
 * 1. 放行公共路径（healthz/register/login）；
 * 2. 其余路径统一强制要求 Bearer Token；
 * 3. 对携带 Token 的请求执行鉴权并注入主体身份头。
 *
 * 该中间件运行在路由处理前：
 * - 返回 `true`：放行，进入后续路由处理；
 * - 返回 `false`：拦截，请求在中间件阶段结束。
 */
class AuthMiddleware : public http::Middleware
{
  public:
    typedef std::shared_ptr<AuthMiddleware> ptr;

    /**
     * @brief 构造鉴权中间件。
     * @param auth_service 认证服务，负责 Token -> 身份解析。
     */
    explicit AuthMiddleware(const ai::service::AuthService::ptr& auth_service);

    /**
     * @brief 前置鉴权逻辑。
     * @param request HTTP 请求对象。
     * @param response HTTP 响应对象（拦截时写错误响应）。
     * @param session HTTP 会话对象（当前实现不直接使用）。
     * @return `true` 放行，`false` 拦截。
     */
    virtual bool before(http::HttpRequest::ptr request,
                        http::HttpResponse::ptr response,
                        http::HttpSession::ptr session) override;

  private:
    /**
     * @brief 判断是否公共路径（无需强制鉴权）。
     * @param path 请求路径。
     * @return 是公共路径返回 true。
     */
    static bool IsPublicAuthPath(const std::string& path);
    /**
     * @brief 从 `Authorization` 头解析 Bearer Token。
     * @param authorization 请求头原始值（示例：`Bearer xxxxx`）。
     * @return 解析成功返回 token；失败返回空字符串。
     */
    static std::string ParseBearerTokenFromAuthorization(const std::string& authorization);

  private:
    /** @brief 认证服务实例。 */
    ai::service::AuthService::ptr m_auth_service;
};

} // namespace middleware
} // namespace ai

#endif
