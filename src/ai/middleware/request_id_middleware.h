#ifndef __SYLAR_AI_MIDDLEWARE_REQUEST_ID_MIDDLEWARE_H__
#define __SYLAR_AI_MIDDLEWARE_REQUEST_ID_MIDDLEWARE_H__

#include "http/middleware/middleware.h"

#include <memory>

namespace ai
{
namespace middleware
{

/**
 * @file request_id_middleware.h
 * @brief RequestId 中间件声明。
 */

/**
 * @brief 为每个请求注入并回写统一 request_id。
 * @details
 * 在请求进入业务处理前生成 request_id，并同时写入：
 * 1. Request Header: `X-Request-Id`
 * 2. Response Header: `X-Request-Id`
 *
 * 这样可以把客户端响应、服务端日志和下游错误信息做同一条链路关联。
 */
class RequestIdMiddleware : public http::Middleware
{
  public:
    typedef std::shared_ptr<RequestIdMiddleware> ptr;

    /**
     * @brief 前置处理：生成并注入 request_id。
     * @param request HTTP 请求对象。
     * @param response HTTP 响应对象。
     * @param session HTTP 会话对象（当前实现不直接使用）。
     * @return 始终返回 true，不拦截请求。
     */
    virtual bool before(http::HttpRequest::ptr request,
                        http::HttpResponse::ptr response,
                        http::HttpSession::ptr session) override;
};

} // namespace middleware
} // namespace ai

#endif
