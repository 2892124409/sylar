#ifndef __SYLAR_AI_COMMON_AI_UTILS_H__
#define __SYLAR_AI_COMMON_AI_UTILS_H__

#include "http/core/http_request.h"
#include "http/core/http_response.h"

#include <stdint.h>

#include <string>

#include <nlohmann/json.hpp>

namespace ai
{
namespace common
{

uint64_t NowMs();

std::string GenerateConversationId();

std::string GenerateRequestId();

std::string ExtractSid(http::HttpRequest::ptr request, http::HttpResponse::ptr response);

bool ParseJsonBody(http::HttpRequest::ptr request, nlohmann::json &out, std::string &error);

uint32_t ParseLimit(const std::string &text, uint32_t default_value, uint32_t max_value);

void WriteJson(http::HttpResponse::ptr response,
               const nlohmann::json &body,
               http::HttpStatus status = http::HttpStatus::OK,
               const std::string &reason = "");

void WriteJsonError(http::HttpResponse::ptr response,
                    http::HttpStatus status,
                    const std::string &message,
                    const std::string &request_id,
                    const std::string &details = "");

} // namespace common
} // namespace ai

#endif
