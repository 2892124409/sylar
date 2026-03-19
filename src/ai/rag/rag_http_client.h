#ifndef __SYLAR_AI_RAG_HTTP_CLIENT_H__
#define __SYLAR_AI_RAG_HTTP_CLIENT_H__

#include <stdint.h>

#include <string>
#include <vector>

namespace ai
{
namespace rag
{

/**
 * @brief Generic HTTP request options for local RAG dependencies.
 */
struct HttpRequestOptions
{
    std::string method;
    std::string url;
    std::vector<std::string> headers;
    std::string body;
    uint64_t connect_timeout_ms = 3000;
    uint64_t request_timeout_ms = 30000;
};

/**
 * @brief Execute one HTTP request and return status/body.
 */
bool PerformHttpRequest(const HttpRequestOptions& options,
                        long& http_status,
                        std::string& response_body,
                        std::string& error);

} // namespace rag
} // namespace ai

#endif
