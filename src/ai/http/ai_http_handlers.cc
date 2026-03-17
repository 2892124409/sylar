#include "ai/http/ai_http_handlers.h"

#include "ai/common/ai_utils.h"

#include "http/stream/sse.h"

#include <nlohmann/json.hpp>

namespace ai
{
namespace api
{

AiHttpHandlers::AiHttpHandlers(
    const ai::service::ChatService::ptr& chat_service,
    const ai::config::ChatSettings& chat_settings,
    const std::string& default_model)
    : m_chat_service(chat_service),
      m_chat_settings(chat_settings),
      m_default_model(default_model)
{
}

std::string AiHttpHandlers::GetRequestId(http::HttpRequest::ptr request) const
{
    return request->getHeader("x-request-id");
}

bool AiHttpHandlers::BuildChatRequest(
    http::HttpRequest::ptr request,
    http::HttpResponse::ptr response,
    ai::common::ChatCompletionRequest& out,
    std::string& error) const
{
    out.sid = ai::common::ExtractSid(request, response);

    nlohmann::json body;
    if (!ai::common::ParseJsonBody(request, body, error))
    {
        return false;
    }

    if (!body.contains("message") || !body["message"].is_string())
    {
        error = "message must be a string";
        return false;
    }

    out.message = body["message"].get<std::string>();

    if (body.contains("conversation_id") && body["conversation_id"].is_string())
    {
        out.conversation_id = body["conversation_id"].get<std::string>();
    }

    out.model = m_default_model;
    if (body.contains("model") && body["model"].is_string())
    {
        out.model = body["model"].get<std::string>();
    }

    out.temperature = m_chat_settings.default_temperature;
    if (body.contains("temperature") && body["temperature"].is_number())
    {
        out.temperature = body["temperature"].get<double>();
    }

    out.max_tokens = m_chat_settings.default_max_tokens;
    if (body.contains("max_tokens") && body["max_tokens"].is_number_unsigned())
    {
        out.max_tokens = body["max_tokens"].get<uint32_t>();
    }

    return true;
}

void AiHttpHandlers::WriteSuccessJson(
    http::HttpResponse::ptr response,
    const ai::common::ChatCompletionResponse& chat_response,
    const std::string& request_id) const
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

int AiHttpHandlers::HandleHealthz(http::HttpRequest::ptr request,
                                  http::HttpResponse::ptr response,
                                  http::HttpSession::ptr)
{
    nlohmann::json payload;
    payload["ok"] = true;
    payload["status"] = "up";
    payload["timestamp_ms"] = ai::common::NowMs();

    const std::string request_id = request->getHeader("x-request-id");
    if (!request_id.empty())
    {
        payload["request_id"] = request_id;
    }

    ai::common::WriteJson(response, payload, http::HttpStatus::OK);
    return 0;
}

int AiHttpHandlers::HandleChatCompletions(
    http::HttpRequest::ptr request,
    http::HttpResponse::ptr response,
    http::HttpSession::ptr)
{
    const std::string request_id = GetRequestId(request);

    ai::common::ChatCompletionRequest chat_request;
    std::string error;
    if (!BuildChatRequest(request, response, chat_request, error))
    {
        ai::common::WriteJsonError(response, http::HttpStatus::BAD_REQUEST, error, request_id);
        return 0;
    }

    ai::common::ChatCompletionResponse chat_response;
    http::HttpStatus status = http::HttpStatus::OK;
    if (!m_chat_service->Complete(chat_request, chat_response, error, status))
    {
        ai::common::WriteJsonError(response, status, error, request_id);
        return 0;
    }

    WriteSuccessJson(response, chat_response, request_id);
    return 0;
}

int AiHttpHandlers::HandleChatStream(http::HttpRequest::ptr request,
                                     http::HttpResponse::ptr response,
                                     http::HttpSession::ptr session)
{
    const std::string request_id = GetRequestId(request);

    ai::common::ChatCompletionRequest chat_request;
    std::string error;
    if (!BuildChatRequest(request, response, chat_request, error))
    {
        ai::common::WriteJsonError(response, http::HttpStatus::BAD_REQUEST, error, request_id);
        return 0;
    }

    response->setStatus(http::HttpStatus::OK);
    response->setKeepAlive(false);
    response->setStream(true);
    response->setHeader("Content-Type", "text/event-stream");
    response->setHeader("Cache-Control", "no-cache");
    response->setHeader("Connection", "close");
    response->setHeader("X-Accel-Buffering", "no");

    if (session->sendResponse(response) <= 0)
    {
        return -1;
    }

    http::SSEWriter writer(session);
    ai::common::ChatCompletionResponse chat_response;
    http::HttpStatus status = http::HttpStatus::OK;

    bool ok = m_chat_service->StreamComplete(
        chat_request,
        [&writer](const std::string& event, const std::string& data)
        { return writer.sendEvent(data, event) > 0; },
        chat_response,
        error,
        status);

    if (!ok)
    {
        nlohmann::json event_error;
        event_error["message"] = error;
        if (!request_id.empty())
        {
            event_error["request_id"] = request_id;
        }
        writer.sendEvent(event_error.dump(), "error");
        return -1;
    }

    return 0;
}

int AiHttpHandlers::HandleHistory(http::HttpRequest::ptr request,
                                  http::HttpResponse::ptr response,
                                  http::HttpSession::ptr)
{
    const std::string request_id = GetRequestId(request);

    std::string sid = ai::common::ExtractSid(request, response);
    const std::string conversation_id = request->getRouteParam("conversation_id");

    const std::string limit_text = request->getParam("limit", "");
    size_t limit = ai::common::ParseLimit(
        limit_text,
        static_cast<uint32_t>(m_chat_settings.history_load_limit),
        static_cast<uint32_t>(m_chat_settings.history_query_limit_max));

    std::vector<ai::common::ChatMessage> messages;
    std::string error;
    http::HttpStatus status = http::HttpStatus::OK;

    if (!m_chat_service->GetHistory(sid, conversation_id, limit, messages, error, status))
    {
        ai::common::WriteJsonError(response, status, error, request_id);
        return 0;
    }

    nlohmann::json payload;
    payload["ok"] = true;
    payload["conversation_id"] = conversation_id;
    payload["messages"] = nlohmann::json::array();
    for (size_t i = 0; i < messages.size(); ++i)
    {
        nlohmann::json item;
        item["role"] = messages[i].role;
        item["content"] = messages[i].content;
        item["created_at_ms"] = messages[i].created_at_ms;
        payload["messages"].push_back(item);
    }
    if (!request_id.empty())
    {
        payload["request_id"] = request_id;
    }

    ai::common::WriteJson(response, payload, http::HttpStatus::OK);
    return 0;
}

int AiHttpHandlers::HandleNotFound(http::HttpRequest::ptr,
                                   http::HttpResponse::ptr response,
                                   http::HttpSession::ptr)
{
    ai::common::WriteJsonError(response, http::HttpStatus::NOT_FOUND, "route not found", "");
    return 0;
}

} // namespace api
} // namespace ai
