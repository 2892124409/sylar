#ifndef __SYLAR_AI_HTTP_AI_HTTP_HANDLERS_H__
#define __SYLAR_AI_HTTP_AI_HTTP_HANDLERS_H__

#include "ai/config/ai_app_config.h"
#include "ai/service/chat_service.h"

#include "http/server/http_server.h"

#include <string>

/**
 * @file ai_http_handlers.h
 * @brief AI HTTP 路由处理器声明。
 */

namespace ai
{
namespace api
{

/**
 * @brief AI V1 HTTP 路由处理器集合。
 * @details
 * 该类负责把 HTTP 协议层与应用层业务对象衔接起来，核心职责包括：
 * - 请求解析与参数校验（HTTP -> ChatCompletionRequest）；
 * - 调用 ChatService 执行业务编排；
 * - 统一 JSON/SSE 响应写回（业务结果 -> HTTP 响应）。
 *
 * 设计边界：
 * - 本类只做“协议转换 + 调度编排”，不直接实现对话业务逻辑；
 * - 对话上下文、模型调用、持久化等由 ChatService 负责。
 */
class AiHttpHandlers
{
  public:
    /** @brief 智能指针别名。 */
    typedef std::shared_ptr<AiHttpHandlers> ptr;

    /**
     * @brief 构造 HTTP 处理器。
     * @param chat_service 对话业务服务。
     * @param chat_settings 对话相关配置快照（默认温度、默认 max_tokens、历史 limit 等）。
     * @param default_model 默认模型名（请求未显式指定 model 时使用）。
     */
    AiHttpHandlers(const ai::service::ChatService::ptr& chat_service,
                   const ai::config::ChatSettings& chat_settings,
                   const std::string& default_model);

    /**
     * @brief 处理健康检查接口：`GET /api/v1/healthz`。
     * @param request HTTP 请求对象。
     * @param response HTTP 响应对象。
     * @param session HTTP 会话对象（本接口中不使用）。
     * @return Servlet 约定返回码，`0` 表示已完成响应。
     */
    int HandleHealthz(http::HttpRequest::ptr request,
                      http::HttpResponse::ptr response,
                      http::HttpSession::ptr session);

    /**
     * @brief 处理同步对话接口：`POST /api/v1/chat/completions`。
     * @details
     * 流程：解析请求 -> 调用 ChatService::Complete -> 写回统一 JSON 响应。
     * @param request HTTP 请求对象。
     * @param response HTTP 响应对象。
     * @param session HTTP 会话对象（本接口中不使用）。
     * @return Servlet 约定返回码，`0` 表示已完成响应。
     */
    int HandleChatCompletions(http::HttpRequest::ptr request,
                              http::HttpResponse::ptr response,
                              http::HttpSession::ptr session);

    /**
     * @brief 处理流式对话接口：`POST /api/v1/chat/stream`。
     * @details
     * 流程：解析请求 -> 发送 SSE 头 -> 调用 ChatService::StreamComplete -> 写出流式事件。
     * @param request HTTP 请求对象。
     * @param response HTTP 响应对象。
     * @param session HTTP 会话对象（用于 SSE 事件写出）。
     * @return `0` 表示处理完成；`-1` 通常表示连接写出失败或流式中断。
     */
    int HandleChatStream(http::HttpRequest::ptr request,
                         http::HttpResponse::ptr response,
                         http::HttpSession::ptr session);

    /**
     * @brief 处理历史查询接口：`GET /api/v1/chat/history/:conversation_id`。
     * @param request HTTP 请求对象。
     * @param response HTTP 响应对象。
     * @param session HTTP 会话对象（本接口中不使用）。
     * @return Servlet 约定返回码，`0` 表示已完成响应。
     */
    int HandleHistory(http::HttpRequest::ptr request,
                      http::HttpResponse::ptr response,
                      http::HttpSession::ptr session);

    /**
     * @brief 处理未命中路由的默认响应。
     * @param request HTTP 请求对象（本接口中不使用）。
     * @param response HTTP 响应对象。
     * @param session HTTP 会话对象（本接口中不使用）。
     * @return Servlet 约定返回码，`0` 表示已完成响应。
     */
    int HandleNotFound(http::HttpRequest::ptr request,
                       http::HttpResponse::ptr response,
                       http::HttpSession::ptr session);

  private:
    /**
     * @brief 从请求头提取 request_id。
     * @param request HTTP 请求对象。
     * @return 请求头中的 `x-request-id` 值；不存在时返回空字符串。
     */
    std::string GetRequestId(http::HttpRequest::ptr request) const;

    /**
     * @brief 将 HTTP 请求转换为业务请求对象。
     * @details
     * 会完成 SID 提取、JSON 解析、必填字段校验、默认值填充。
     * @param request HTTP 请求对象。
     * @param response HTTP 响应对象（用于 SID 下发等副作用）。
     * @param[out] out 转换后的业务请求对象。
     * @param[out] error 失败原因。
     * @return true 转换成功；false 转换失败。
     */
    bool BuildChatRequest(http::HttpRequest::ptr request,
                          http::HttpResponse::ptr response,
                          ai::common::ChatCompletionRequest& out,
                          std::string& error) const;

    /**
     * @brief 写出同步对话成功响应 JSON。
     * @param response HTTP 响应对象。
     * @param chat_response 业务层返回结果。
     * @param request_id 请求链路 ID（可选）。
     * @details 就是把应用层响应（成功的响应）转换为标准 HTTP JSON 响应
     */
    void WriteSuccessJson(http::HttpResponse::ptr response,
                          const ai::common::ChatCompletionResponse& chat_response,
                          const std::string& request_id) const;

  private:
    /** @brief 对话业务服务依赖。 */
    ai::service::ChatService::ptr m_chat_service;
    /** @brief 对话配置快照。 */
    ai::config::ChatSettings m_chat_settings;
    /** @brief 默认模型名（请求未指定 model 时使用）。 */
    std::string m_default_model;
};

} // namespace api
} // namespace ai

#endif
