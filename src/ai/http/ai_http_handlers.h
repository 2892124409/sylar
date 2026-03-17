#ifndef __SYLAR_AI_HTTP_AI_HTTP_HANDLERS_H__
#define __SYLAR_AI_HTTP_AI_HTTP_HANDLERS_H__

#include "ai/config/ai_app_config.h"
#include "ai/service/chat_service.h"

#include "http/server/http_server.h"

#include <string>

namespace ai
{
namespace api
{

/**
 * @brief AI V1 HTTP 路由处理器集合。
 */
class AiHttpHandlers
{
  public:
    typedef std::shared_ptr<AiHttpHandlers> ptr;

    AiHttpHandlers(const ai::service::ChatService::ptr& chat_service,
                   const ai::config::ChatSettings& chat_settings,
                   const std::string& default_model);

    int HandleHealthz(http::HttpRequest::ptr request,
                      http::HttpResponse::ptr response,
                      http::HttpSession::ptr session);

    int HandleChatCompletions(http::HttpRequest::ptr request,
                              http::HttpResponse::ptr response,
                              http::HttpSession::ptr session);

    int HandleChatStream(http::HttpRequest::ptr request,
                         http::HttpResponse::ptr response,
                         http::HttpSession::ptr session);

    int HandleHistory(http::HttpRequest::ptr request,
                      http::HttpResponse::ptr response,
                      http::HttpSession::ptr session);

    int HandleNotFound(http::HttpRequest::ptr request,
                       http::HttpResponse::ptr response,
                       http::HttpSession::ptr session);

  private:
    std::string GetRequestId(http::HttpRequest::ptr request) const;

    bool BuildChatRequest(http::HttpRequest::ptr request,
                          http::HttpResponse::ptr response,
                          ai::common::ChatCompletionRequest& out,
                          std::string& error) const;

    void WriteSuccessJson(http::HttpResponse::ptr response,
                          const ai::common::ChatCompletionResponse& chat_response,
                          const std::string& request_id) const;

  private:
    ai::service::ChatService::ptr m_chat_service;
    ai::config::ChatSettings m_chat_settings;
    std::string m_default_model;
};

} // namespace api
} // namespace ai

#endif
