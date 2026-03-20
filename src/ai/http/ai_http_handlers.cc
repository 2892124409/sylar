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
    const ai::service::AuthService::ptr& auth_service,
    const ai::config::ChatSettings& chat_settings)
    : m_chat_service(chat_service),
      m_auth_service(auth_service),
      m_chat_settings(chat_settings)
{
}

std::string AiHttpHandlers::GetRequestId(http::HttpRequest::ptr request) const
{
    return request->getHeader("x-request-id");
}

bool AiHttpHandlers::BuildChatRequest(
    http::HttpRequest::ptr request,
    ai::common::ChatCompletionRequest& out,
    std::string& error) const
{
    out.sid = request->getHeader("X-Principal-Sid");

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

    if (body.contains("model") && body["model"].is_string())
    {
        out.model = body["model"].get<std::string>();
    }
    if (body.contains("provider") && body["provider"].is_string())
    {
        out.provider = body["provider"].get<std::string>();
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
    payload["provider"] = chat_response.provider;
    payload["finish_reason"] = chat_response.finish_reason;
    payload["created_at_ms"] = chat_response.created_at_ms;
    if (!request_id.empty())
    {
        payload["request_id"] = request_id;
    }

    ai::common::WriteJson(response, payload, http::HttpStatus::OK);
}

std::string AiHttpHandlers::ParseBearerToken(http::HttpRequest::ptr request) const
{
    std::string authorization = request->getHeader("Authorization");
    if (authorization.size() < 8)
    {
        return std::string();
    }

    const std::string prefix = "Bearer ";
    if (authorization.compare(0, prefix.size(), prefix) != 0)
    {
        return std::string();
    }

    return authorization.substr(prefix.size());
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
    if (request->getHeader("X-Principal-Sid").empty())
    {
        ai::common::WriteJsonError(response, static_cast<http::HttpStatus>(401), "authorization required", request_id);
        return 0;
    }

    ai::common::ChatCompletionRequest chat_request;
    std::string error;
    if (!BuildChatRequest(request, chat_request, error))
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
    if (request->getHeader("X-Principal-Sid").empty())
    {
        ai::common::WriteJsonError(response, static_cast<http::HttpStatus>(401), "authorization required", request_id);
        return 0;
    }

    ai::common::ChatCompletionRequest chat_request;
    std::string error;
    if (!BuildChatRequest(request, chat_request, error))
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

    std::string sid = request->getHeader("X-Principal-Sid");
    if (sid.empty())
    {
        ai::common::WriteJsonError(response, static_cast<http::HttpStatus>(401), "authorization required", request_id);
        return 0;
    }
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

int AiHttpHandlers::HandleAuthRegister(http::HttpRequest::ptr request,
                                       http::HttpResponse::ptr response,
                                       http::HttpSession::ptr)
{
    const std::string request_id = GetRequestId(request);
    if (!m_auth_service)
    {
        ai::common::WriteJsonError(response, http::HttpStatus::INTERNAL_SERVER_ERROR, "auth service not initialized", request_id);
        return 0;
    }

    nlohmann::json body;
    std::string error;
    if (!ai::common::ParseJsonBody(request, body, error))
    {
        ai::common::WriteJsonError(response, http::HttpStatus::BAD_REQUEST, error, request_id);
        return 0;
    }

    if (!body.contains("username") || !body["username"].is_string() ||
        !body.contains("password") || !body["password"].is_string())
    {
        ai::common::WriteJsonError(response, http::HttpStatus::BAD_REQUEST, "username/password must be string", request_id);
        return 0;
    }

    const std::string username = body["username"].get<std::string>();
    const std::string password = body["password"].get<std::string>();

    uint64_t user_id = 0;
    http::HttpStatus status = http::HttpStatus::OK;
    if (!m_auth_service->Register(username, password, user_id, error, status))
    {
        ai::common::WriteJsonError(response, status, error, request_id);
        return 0;
    }

    nlohmann::json payload;
    payload["ok"] = true;
    payload["user"] = {
        {"id", user_id},
        {"username", username},
    };
    if (!request_id.empty())
    {
        payload["request_id"] = request_id;
    }
    ai::common::WriteJson(response, payload, http::HttpStatus::OK);
    return 0;
}

int AiHttpHandlers::HandleAuthLogin(http::HttpRequest::ptr request,
                                    http::HttpResponse::ptr response,
                                    http::HttpSession::ptr)
{
    const std::string request_id = GetRequestId(request);
    if (!m_auth_service)
    {
        ai::common::WriteJsonError(response, http::HttpStatus::INTERNAL_SERVER_ERROR, "auth service not initialized", request_id);
        return 0;
    }

    nlohmann::json body;
    std::string error;
    if (!ai::common::ParseJsonBody(request, body, error))
    {
        ai::common::WriteJsonError(response, http::HttpStatus::BAD_REQUEST, error, request_id);
        return 0;
    }

    if (!body.contains("username") || !body["username"].is_string() ||
        !body.contains("password") || !body["password"].is_string())
    {
        ai::common::WriteJsonError(response, http::HttpStatus::BAD_REQUEST, "username/password must be string", request_id);
        return 0;
    }

    const std::string username = body["username"].get<std::string>();
    const std::string password = body["password"].get<std::string>();

    std::string access_token;
    ai::service::AuthIdentity identity;
    http::HttpStatus status = http::HttpStatus::OK;
    if (!m_auth_service->Login(username, password, access_token, identity, error, status))
    {
        ai::common::WriteJsonError(response, status, error, request_id);
        return 0;
    }

    nlohmann::json payload;
    payload["ok"] = true;
    payload["token_type"] = "Bearer";
    payload["access_token"] = access_token;
    payload["principal_sid"] = identity.principal_sid;
    payload["user"] = {
        {"id", identity.user_id},
        {"username", identity.username},
    };
    if (!request_id.empty())
    {
        payload["request_id"] = request_id;
    }

    ai::common::WriteJson(response, payload, http::HttpStatus::OK);
    return 0;
}

int AiHttpHandlers::HandleAuthLogout(http::HttpRequest::ptr request,
                                     http::HttpResponse::ptr response,
                                     http::HttpSession::ptr)
{
    const std::string request_id = GetRequestId(request);
    if (!m_auth_service)
    {
        ai::common::WriteJsonError(response, http::HttpStatus::INTERNAL_SERVER_ERROR, "auth service not initialized", request_id);
        return 0;
    }

    const std::string token = ParseBearerToken(request);
    std::string error;
    http::HttpStatus status = http::HttpStatus::OK;
    if (!m_auth_service->Logout(token, error, status))
    {
        ai::common::WriteJsonError(response, status, error, request_id);
        return 0;
    }

    nlohmann::json payload;
    payload["ok"] = true;
    if (!request_id.empty())
    {
        payload["request_id"] = request_id;
    }
    ai::common::WriteJson(response, payload, http::HttpStatus::OK);
    return 0;
}

int AiHttpHandlers::HandleAuthMe(http::HttpRequest::ptr request,
                                 http::HttpResponse::ptr response,
                                 http::HttpSession::ptr)
{
    const std::string request_id = GetRequestId(request);
    if (!m_auth_service)
    {
        ai::common::WriteJsonError(response, http::HttpStatus::INTERNAL_SERVER_ERROR, "auth service not initialized", request_id);
        return 0;
    }

    const std::string token = ParseBearerToken(request);
    std::string error;
    http::HttpStatus status = http::HttpStatus::OK;
    ai::service::AuthIdentity identity;
    if (!m_auth_service->AuthenticateBearerToken(token, identity, error, status))
    {
        ai::common::WriteJsonError(response, status, error, request_id);
        return 0;
    }

    nlohmann::json payload;
    payload["ok"] = true;
    payload["authenticated"] = true;
    payload["principal_sid"] = identity.principal_sid;
    payload["user"] = {
        {"id", identity.user_id},
        {"username", identity.username},
    };
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
