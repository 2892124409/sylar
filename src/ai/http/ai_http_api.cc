#include "ai/http/ai_http_api.h"

#include "ai/http/ai_http_handlers.h"

namespace ai
{
namespace api
{

void RegisterAiHttpApi(const http::HttpServer::ptr& server,
                       const ai::service::ChatService::ptr& chat_service,
                       const ai::config::ChatSettings& chat_settings,
                       const std::string& default_model)
{
    http::ServletDispatch::ptr dispatch = server->getServletDispatch();
    AiHttpHandlers::ptr handlers(new AiHttpHandlers(chat_service, chat_settings, default_model));

    dispatch->addServlet(
        http::HttpMethod::GET,
        "/api/v1/healthz",
        [handlers](http::HttpRequest::ptr request, http::HttpResponse::ptr response, http::HttpSession::ptr session)
        {
            return handlers->HandleHealthz(request, response, session);
        });

    dispatch->addServlet(
        http::HttpMethod::POST,
        "/api/v1/chat/completions",
        [handlers](http::HttpRequest::ptr request, http::HttpResponse::ptr response, http::HttpSession::ptr session)
        {
            return handlers->HandleChatCompletions(request, response, session);
        });

    dispatch->addServlet(
        http::HttpMethod::POST,
        "/api/v1/chat/stream",
        [handlers](http::HttpRequest::ptr request, http::HttpResponse::ptr response, http::HttpSession::ptr session)
        {
            return handlers->HandleChatStream(request, response, session);
        });

    dispatch->addParamServlet(
        http::HttpMethod::GET,
        "/api/v1/chat/history/:conversation_id",
        [handlers](http::HttpRequest::ptr request, http::HttpResponse::ptr response, http::HttpSession::ptr session)
        {
            return handlers->HandleHistory(request, response, session);
        });

    dispatch->setDefault(http::Servlet::ptr(new http::FunctionServlet(
        [handlers](http::HttpRequest::ptr request, http::HttpResponse::ptr response, http::HttpSession::ptr session)
        {
            return handlers->HandleNotFound(request, response, session);
        },
        "AiApiNotFoundServlet")));
}

} // namespace api
} // namespace ai
