#ifndef __SYLAR_AI_HTTP_AI_HTTP_API_H__
#define __SYLAR_AI_HTTP_AI_HTTP_API_H__

#include "ai/service/auth_service.h"
#include "ai/service/chat_service.h"

#include "http/server/http_server.h"

#include <string>

/**
 * @file ai_http_api.h
 * @brief AI HTTP API 路由注册入口声明。
 *
 * 本模块职责是 HTTP 协议适配：
 * 1) 将 HTTP 请求转换为 ChatService 可消费的业务请求对象；
 * 2) 将业务结果统一转换为 JSON/SSE 响应；
 * 3) 统一错误响应语义。
 */

namespace ai
{
namespace api
{

/**
 * @brief 向 HTTP Server 注册 AI V1 路由集合。
 * @param server HTTP 服务器实例，内部持有 ServletDispatch。
 * @param chat_service AI 应用服务，承载核心编排逻辑。
 * @param chat_settings 聊天配置，用于默认参数与 limit 约束。
 */
void RegisterAiHttpApi(const http::HttpServer::ptr& server,
                       const ai::service::AuthService::ptr& auth_service,
                       const ai::service::ChatService::ptr& chat_service,
                       const ai::config::ChatSettings& chat_settings);

} // namespace api
} // namespace ai

#endif
