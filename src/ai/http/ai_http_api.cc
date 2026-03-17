#include "ai/http/ai_http_api.h"

#include "ai/common/ai_utils.h"

#include "http/core/http_error.h"
#include "http/stream/sse.h"

#include <nlohmann/json.hpp>

/**
 * @file ai_http_api.cc
 * @brief AI HTTP API 路由注册与协议转换实现。
 */

namespace ai
{
namespace api
{

namespace
{

/**
 * @brief 读取请求链路 ID。
 * @param request HTTP 请求对象。
 * @return `x-request-id` 头值；不存在时返回空字符串。
 */
std::string GetRequestId(http::HttpRequest::ptr request)
{
    return request->getHeader("x-request-id");
}

/**
 * @brief 将 HTTP 请求转换为应用层 struct ChatCompletionRequest 请求，方便后续应用层处理。
 * @details
 * 转换规则：
 * - `sid`：从 Cookie/Set-Cookie 提取；
 * - `message`：必填，且必须为字符串；
 * - `conversation_id/model/temperature/max_tokens`：可选，未提供则回落默认值。
 * @param request HTTP 请求对象。
 * @param response HTTP 响应对象（用于 SID 兜底提取）。
 * @param chat_settings 聊天默认配置。
 * @param default_model 默认模型名。
 * @param[out] out 转换后的业务请求对象。
 * @param[out] error 失败原因。
 * @return true 转换成功；false 参数不合法或 JSON 解析失败。
 */
bool BuildChatRequest(http::HttpRequest::ptr request,
                      http::HttpResponse::ptr response,
                      const ai::config::ChatSettings& chat_settings,
                      const std::string& default_model,
                      ai::common::ChatCompletionRequest& out,
                      std::string& error)
{
    // 从请求上下文提取会话 SID（优先 Cookie，必要时可从响应 Set-Cookie 兜底）。
    const std::string sid = ai::common::ExtractSid(request, response);
    // 将提取到的 SID 写入业务请求对象，供后续会话隔离与历史查询使用。
    out.sid = sid;

    // 准备承接请求体 JSON 的容器。
    nlohmann::json body;
    // 解析 HTTP body 为 JSON；解析失败时 error 已在工具函数中写入原因。
    if (!ai::common::ParseJsonBody(request, body, error))
    {
        // 解析失败直接返回，让上层统一按 BAD_REQUEST 输出错误响应。
        return false;
    }

    // 校验 message 字段存在且类型为字符串（这是最小必填参数）。
    if (!body.contains("message") || !body["message"].is_string())
    {
        // 明确错误信息，便于客户端快速定位入参问题。
        error = "message must be a string";
        // 参数不合法，返回 false 走统一错误处理。
        return false;
    }

    // 提取用户输入消息正文，写入业务请求对象。
    out.message = body["message"].get<std::string>();

    // conversation_id 是可选字段：
    // - 传入时表示续聊已有会话；
    // - 未传时由 service 层生成新会话 ID。
    if (body.contains("conversation_id") && body["conversation_id"].is_string())
    {
        // 仅在类型正确时覆盖，避免脏数据污染请求对象。
        out.conversation_id = body["conversation_id"].get<std::string>();
    }

    // 先给 model 设置默认值（来自服务端配置）。
    out.model = default_model;
    // 若请求显式给出 model 且类型正确，则覆盖默认模型。
    if (body.contains("model") && body["model"].is_string())
    {
        out.model = body["model"].get<std::string>();
    }

    // 先给 temperature 设置默认值（来自服务端配置）。
    out.temperature = chat_settings.default_temperature;
    // 若请求显式给出 temperature 且是数值类型，则覆盖默认值。
    if (body.contains("temperature") && body["temperature"].is_number())
    {
        out.temperature = body["temperature"].get<double>();
    }

    // 先给 max_tokens 设置默认值（来自服务端配置）。
    out.max_tokens = chat_settings.default_max_tokens;
    // 仅接受无符号整数，避免负值或字符串等非法类型进入业务层。
    if (body.contains("max_tokens") && body["max_tokens"].is_number_unsigned())
    {
        out.max_tokens = body["max_tokens"].get<uint32_t>();
    }

    // 所有必填校验和参数合并完成，构造成功。
    return true;
}

/**
 * @brief 输出聊天成功响应 JSON，就是将应用层返回的响应 struct ChatCompletionResponse 转换为 HTTP 响应。
 * @details SSE 流式响应有专门的处理方式，不走这条通路
 * @param response HTTP 响应对象。
 * @param chat_response ChatService 返回的业务结果。
 * @param request_id 请求链路 ID，用于串联日志与排障。
 */
void WriteSuccessJson(http::HttpResponse::ptr response,
                      const ai::common::ChatCompletionResponse& chat_response,
                      const std::string& request_id)
{
    nlohmann::json payload;
    payload["ok"] = true;
    payload["conversation_id"] = chat_response.conversation_id;
    payload["reply"] = chat_response.reply;
    payload["model"] = chat_response.model;
    payload["finish_reason"] = chat_response.finish_reason;
    payload["created_at_ms"] = chat_response.created_at_ms;
    if (!request_id.empty())
    {
        payload["request_id"] = request_id;
    }

    ai::common::WriteJson(response, payload, http::HttpStatus::OK);
}

} // namespace

void RegisterAiHttpApi(const http::HttpServer::ptr& server,
                       const ai::service::ChatService::ptr& chat_service,
                       const ai::config::ChatSettings& chat_settings,
                       const std::string& default_model)
{
    http::ServletDispatch::ptr dispatch = server->getServletDispatch();

    /**
     * @route GET /api/v1/healthz
     * @brief 健康检查接口，返回服务存活状态。
     */
    dispatch->addServlet(http::HttpMethod::GET,
                         "/api/v1/healthz",
                         [](http::HttpRequest::ptr request, http::HttpResponse::ptr response, http::HttpSession::ptr)
                         {
                             // 构建健康检查响应 JSON。
                             nlohmann::json payload;
                             // 标记接口调用成功。
                             payload["ok"] = true;
                             // 固定状态值，表示服务可用。
                             payload["status"] = "up";
                             // 返回当前服务时间戳，便于客户端/网关探活对时。
                             payload["timestamp_ms"] = ai::common::NowMs();
                             // 读取链路 request_id（若中间件已注入）。
                             const std::string request_id = request->getHeader("x-request-id");
                             // 若存在 request_id，则回传给调用方用于链路追踪。
                             if (!request_id.empty())
                             {
                                 payload["request_id"] = request_id;
                             }

                             // 统一输出 HTTP 200 + JSON 响应。
                             ai::common::WriteJson(response, payload, http::HttpStatus::OK);
                             // 返回 0 表示 Servlet 处理成功。
                             return 0;
                         });

    /**
     * @route POST /api/v1/chat/completions
     * @brief 同步对话接口，返回完整回答 JSON。
     * @details
     * 执行顺序：参数翻译 -> ChatService::Complete -> 统一 JSON 输出。
     */
    dispatch->addServlet(
        http::HttpMethod::POST,
        "/api/v1/chat/completions",
        [chat_service, chat_settings, default_model](
            http::HttpRequest::ptr request, http::HttpResponse::ptr response, http::HttpSession::ptr)
        {
            // 获取当前请求链路 ID（由中间件注入）。
            const std::string request_id = GetRequestId(request);

            // 创建应用层请求对象，承接 HTTP 参数转换结果。
            ai::common::ChatCompletionRequest chat_request;
            // 错误信息输出缓冲。
            std::string error;
            // 将 HTTP 请求翻译为业务请求对象；失败通常为参数/JSON 问题。
            if (!BuildChatRequest(request, response, chat_settings, default_model, chat_request, error))
            {
                // 参数错误统一返回 400 JSON，带 request_id 便于定位。
                ai::common::WriteJsonError(response, http::HttpStatus::BAD_REQUEST, error, request_id);
                return 0;
            }

            // 业务层标准响应对象。
            ai::common::ChatCompletionResponse chat_response;
            // 默认状态码先设为 200，若业务失败由 service 填充具体状态。
            http::HttpStatus status = http::HttpStatus::OK;
            // 调用业务编排层执行同步对话。
            if (!chat_service->Complete(chat_request, chat_response, error, status))
            {
                // 业务失败按 service 返回状态码输出统一错误响应。
                ai::common::WriteJsonError(response, status, error, request_id);
                return 0;
            }

            // 业务成功，输出统一成功 JSON 响应。
            WriteSuccessJson(response, chat_response, request_id);
            return 0;
        });

    /**
     * @route POST /api/v1/chat/stream
     * @brief 流式对话接口，以 SSE 事件流返回增量输出。
     * @details
     * 事件类型：`start` / `delta` / `done` / `error`。
     */
    dispatch->addServlet(
        http::HttpMethod::POST,
        "/api/v1/chat/stream",
        [chat_service, chat_settings, default_model](
            http::HttpRequest::ptr request, http::HttpResponse::ptr response, http::HttpSession::ptr session)
        {
            // 获取当前请求链路 ID（用于错误事件透传）。
            const std::string request_id = GetRequestId(request);

            // 创建应用层请求对象，承接 HTTP 参数转换结果。
            ai::common::ChatCompletionRequest chat_request;
            // 错误信息输出缓冲。
            std::string error;
            // 将 HTTP 请求翻译为业务请求对象；失败直接按普通 JSON 返回错误。
            if (!BuildChatRequest(request, response, chat_settings, default_model, chat_request, error))
            {
                ai::common::WriteJsonError(response, http::HttpStatus::BAD_REQUEST, error, request_id);
                return 0;
            }

            // 设置状态码为 200（SSE 连接建立成功后由事件流承载后续语义）。
            response->setStatus(http::HttpStatus::OK);
            // 流式连接按短连接处理，完成后关闭。
            response->setKeepAlive(false);
            // 标记为流式响应，避免框架按普通一次性 body 发送。
            response->setStream(true);
            // 指定 SSE 内容类型。
            response->setHeader("Content-Type", "text/event-stream");
            // 禁止中间缓存，确保事件实时下发。
            response->setHeader("Cache-Control", "no-cache");
            // 显式声明连接关闭策略。
            response->setHeader("Connection", "close");
            // 对 Nginx 等代理禁用缓冲，避免事件被攒包延迟推送。
            response->setHeader("X-Accel-Buffering", "no");

            // 先把响应头发送给客户端，建立 SSE 通道。
            if (session->sendResponse(response) <= 0)
            {
                // 发送头失败通常表示连接已断开。
                return -1;
            }

            // 基于当前 session 创建 SSE 事件写入器。
            http::SSEWriter writer(session);
            // 业务层标准响应对象（流式结束后可用于补充信息）。
            ai::common::ChatCompletionResponse chat_response;
            // 默认状态码，失败时由 service 写入。
            http::HttpStatus status = http::HttpStatus::OK;

            // 执行业务层流式对话，delta 回调中把事件写到 SSE 通道。
            bool ok = chat_service->StreamComplete(
                chat_request,
                [&writer](const std::string& event, const std::string& data)
                { return writer.sendEvent(data, event) > 0; },
                chat_response,
                error,
                status);

            // 业务执行失败时，按 SSE 规范发送 error 事件而非普通 JSON。
            if (!ok)
            {
                nlohmann::json event_error;
                // 透传失败原因。
                event_error["message"] = error;
                // 若有 request_id 一并透传，便于日志关联。
                if (!request_id.empty())
                {
                    event_error["request_id"] = request_id;
                }
                // 下发 error 事件，通知客户端流式失败。
                writer.sendEvent(event_error.dump(), "error");
                return -1;
            }

            // 流式路径成功结束（done 事件由 service 内部发送）。
            return 0;
        });

    /**
     * @route GET /api/v1/chat/history/:conversation_id
     * @brief 按会话 ID 查询历史消息列表。
     * @details
     * 支持 `?limit=`，并受 `history_query_limit_max` 上限约束。
     */
    dispatch->addParamServlet(
        http::HttpMethod::GET,
        "/api/v1/chat/history/:conversation_id",
        [chat_service,
         chat_settings](http::HttpRequest::ptr request, http::HttpResponse::ptr response, http::HttpSession::ptr)
        {
            // 获取当前请求链路 ID。
            const std::string request_id = GetRequestId(request);

            // 提取 SID，用于会话隔离校验。
            std::string sid = ai::common::ExtractSid(request, response);
            // 从路由参数中读取会话 ID：/history/:conversation_id。
            const std::string conversation_id = request->getRouteParam("conversation_id");

            // 读取查询参数 limit（如 ?limit=20）。
            const std::string limit_text = request->getParam("limit", "");
            // 对 limit 执行默认值与上限归一化。
            size_t limit = ai::common::ParseLimit(limit_text,
                                                  static_cast<uint32_t>(chat_settings.history_load_limit),
                                                  static_cast<uint32_t>(chat_settings.history_query_limit_max));

            // 承接历史消息结果集。
            std::vector<ai::common::ChatMessage> messages;
            // 错误信息输出缓冲。
            std::string error;
            // 默认状态码，失败时由 service 设置。
            http::HttpStatus status = http::HttpStatus::OK;

            // 调用业务层查询历史消息。
            if (!chat_service->GetHistory(sid, conversation_id, limit, messages, error, status))
            {
                // 查询失败时统一输出 JSON 错误响应。
                ai::common::WriteJsonError(response, status, error, request_id);
                return 0;
            }

            // 构建成功响应 JSON。
            nlohmann::json payload;
            // 成功标记。
            payload["ok"] = true;
            // 透传会话 ID。
            payload["conversation_id"] = conversation_id;
            // 初始化消息数组。
            payload["messages"] = nlohmann::json::array();
            // 将消息列表逐条转换为 JSON 对象。
            for (size_t i = 0; i < messages.size(); ++i)
            {
                nlohmann::json item;
                // 角色：user/assistant/system。
                item["role"] = messages[i].role;
                // 文本内容。
                item["content"] = messages[i].content;
                // 消息创建时间戳。
                item["created_at_ms"] = messages[i].created_at_ms;
                payload["messages"].push_back(item);
            }
            // 回传 request_id，便于链路排障。
            if (!request_id.empty())
            {
                payload["request_id"] = request_id;
            }

            // 输出 HTTP 200 + JSON 响应。
            ai::common::WriteJson(response, payload, http::HttpStatus::OK);
            return 0;
        });

    /**
     * @brief 默认路由，未命中 API 时返回 404 JSON。
     */
    dispatch->setDefault(http::Servlet::ptr(new http::FunctionServlet(
        [](http::HttpRequest::ptr, http::HttpResponse::ptr response, http::HttpSession::ptr)
        {
            ai::common::WriteJsonError(response, http::HttpStatus::NOT_FOUND, "route not found", "");
            return 0;
        },
        "AiApiNotFoundServlet")));
}

} // namespace api
} // namespace ai
