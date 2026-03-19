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
 * @brief RAG 依赖服务通用 HTTP 请求参数。
 *
 * @details
 * 该结构用于统一描述一次 HTTP 请求，供 `PerformHttpRequest()` 消费。
 * 当前主要服务于：
 * - Ollama（embedding）
 * - Qdrant（collection / upsert / search）
 */
struct HttpRequestOptions
{
    /** @brief HTTP 方法，如 `GET` / `POST` / `PUT`。 */
    std::string method;
    /** @brief 完整请求 URL。 */
    std::string url;
    /** @brief HTTP 请求头列表，每一项形如 `"Key: Value"`。 */
    std::vector<std::string> headers;
    /** @brief 请求体（通常为 JSON 字符串）。 */
    std::string body;
    /** @brief 建连超时（毫秒），仅作用于连接阶段。 */
    uint64_t connect_timeout_ms = 3000;
    /** @brief 请求总超时（毫秒），覆盖连接+收发全过程。 */
    uint64_t request_timeout_ms = 30000;
};

/**
 * @brief 发起一次 HTTP 请求并返回状态码与响应体。
 *
 * @param options 请求参数。
 * @param[out] http_status HTTP 状态码。
 * @param[out] response_body 响应体字符串。
 * @param[out] error 错误信息（仅失败时填写）。
 * @return true 请求成功（包含 4xx/5xx 这种“业务失败但网络成功”的情况）；
 * @return false 网络层或执行层失败（如 curl 初始化失败/超时/连接错误）。
 */
bool PerformHttpRequest(const HttpRequestOptions& options,
                        long& http_status,
                        std::string& response_body,
                        std::string& error);

} // namespace rag
} // namespace ai

#endif
