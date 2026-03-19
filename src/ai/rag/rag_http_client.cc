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

/**
 * @brief curl 写回调上下文。
 * @details 把分块返回的数据累计到 `body` 中。
 */
struct WriteContext
{
    std::string body;
};

/**
 * @brief 进程级初始化 libcurl（只执行一次）。
 */
void EnsureCurlGlobalInit()
{
    static std::once_flag s_init_flag;
    std::call_once(s_init_flag, []()
                   { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

/**
 * @brief libcurl 写回调：把收到的响应分块追加到缓冲区。
 */
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
    // 确保 libcurl 全局状态就绪。
    EnsureCurlGlobalInit();

    // 创建单次请求的 easy handle。
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        error = "curl_easy_init failed";
        return false;
    }

    // 接收响应体的上下文。
    WriteContext write_ctx;

    // 构建请求头链表。
    struct curl_slist* curl_headers = nullptr;
    for (size_t i = 0; i < options.headers.size(); ++i)
    {
        curl_headers = curl_slist_append(curl_headers, options.headers[i].c_str());
    }

    // 基础请求参数：URL + Header。
    curl_easy_setopt(curl, CURLOPT_URL, options.url.c_str());
    if (curl_headers)
    {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
    }

    // 方法设置：POST 特殊处理，其它非 GET 方法走 CUSTOMREQUEST。
    if (options.method == "POST")
    {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    }
    else if (options.method != "GET")
    {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, options.method.c_str());
    }

    // 挂载请求体（为空则不设置）。
    if (!options.body.empty())
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, options.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, options.body.size());
    }

    // 超时与写回调设置。
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(options.connect_timeout_ms));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(options.request_timeout_ms));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);

    // 通过 FiberCurlSession 让 curl 请求挂到协程调度器上，避免阻塞 worker 线程。
    llm::FiberCurlSession session(curl);
    CURLcode code = session.Perform();

    // 即使业务返回 4xx/5xx，也可以读取到 HTTP 状态码供上层判断。
    http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);

    // 释放 curl 资源（请求头链表 + easy handle）。
    if (curl_headers)
    {
        curl_slist_free_all(curl_headers);
    }
    curl_easy_cleanup(curl);

    // 网络执行失败（超时/连接失败等）直接返回 false。
    if (code != CURLE_OK)
    {
        error = std::string("http request failed: ") + curl_easy_strerror(code);
        return false;
    }

    // 返回响应体给调用方。
    response_body.swap(write_ctx.body);
    return true;
}

} // namespace rag
} // namespace ai
