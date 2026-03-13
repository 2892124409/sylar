#ifndef __SYLAR_AI_HTTP_AI_HTTP_API_H__
#define __SYLAR_AI_HTTP_AI_HTTP_API_H__

#include "ai/service/chat_service.h"

#include "http/server/http_server.h"

#include <string>

namespace ai
{
namespace api
{

void RegisterAiHttpApi(const http::HttpServer::ptr &server,
                       const ai::service::ChatService::ptr &chat_service,
                       const ai::config::ChatSettings &chat_settings,
                       const std::string &default_model);

} // namespace api
} // namespace ai

#endif
