#include "ai/rag/rag_http_client.h"

#include "ai/llm/fiber_curl_session.h"

#include <curl/curl.h>

#include <mutex>

namespace ai
{
namespace rag
{

namespace
{

struct WriteContext
{
    std::string body;
};

void EnsureCurlGlobalInit()
{
    static std::once_flag s_init_flag;
    std::call_once(s_init_flag, []()
                   { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    const size_t total = size * nmemb;
    WriteContext* ctx = static_cast<WriteContext*>(userp);
    ctx->body.append(static_cast<const char*>(contents), total);
    return total;
}

} // namespace

bool PerformHttpRequest(const HttpRequestOptions& options,
                        long& http_status,
                        std::string& response_body,
                        std::string& error)
{
    EnsureCurlGlobalInit();

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        error = "curl_easy_init failed";
        return false;
    }

    WriteContext write_ctx;

    struct curl_slist* curl_headers = nullptr;
    for (size_t i = 0; i < options.headers.size(); ++i)
    {
        curl_headers = curl_slist_append(curl_headers, options.headers[i].c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, options.url.c_str());
    if (curl_headers)
    {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
    }

    if (options.method == "POST")
    {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    }
    else if (options.method != "GET")
    {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, options.method.c_str());
    }

    if (!options.body.empty())
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, options.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, options.body.size());
    }

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(options.connect_timeout_ms));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(options.request_timeout_ms));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);

    llm::FiberCurlSession session(curl);
    CURLcode code = session.Perform();

    http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);

    if (curl_headers)
    {
        curl_slist_free_all(curl_headers);
    }
    curl_easy_cleanup(curl);

    if (code != CURLE_OK)
    {
        error = std::string("http request failed: ") + curl_easy_strerror(code);
        return false;
    }

    response_body.swap(write_ctx.body);
    return true;
}

} // namespace rag
} // namespace ai
