#include "ai/middleware/request_id_middleware.h"

#include "ai/common/ai_utils.h"

namespace ai
{
namespace middleware
{

/**
 * @file request_id_middleware.cc
 * @brief RequestId 中间件实现。
 */

/**
 * @brief 生成 request_id 并写入 request/response 头。
 */
bool RequestIdMiddleware::before(http::HttpRequest::ptr request,
                                 http::HttpResponse::ptr response,
                                 http::HttpSession::ptr)
{
    // 为当前请求生成全局唯一（或近似唯一）的链路 ID。
    const std::string request_id = ai::common::GenerateRequestId();
    // 写入请求头：供后续中间件/handler/业务层读取。
    request->setHeader("X-Request-Id", request_id);
    // 写入响应头：客户端可直接拿到该 ID 用于问题定位。
    response->setHeader("X-Request-Id", request_id);
    // request_id 中间件不做鉴权/限流，不拦截请求。
    return true;
}

} // namespace middleware
} // namespace ai
