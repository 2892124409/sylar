#include "ai/llm/anthropic_client.h"
#include "ai/llm/fiber_curl_session.h"

#include "log/logger.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <mutex>
#include <sstream>

/**
 * @file anthropic_client.cc
 * @brief Anthropic Messages API 客户端实现。
 */

namespace ai
{
namespace llm
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

namespace
{

/**
 * @brief 同步请求写回调上下文。
 */
struct SyncWriteContext
{
    /** @brief 接收完整 HTTP 响应体。 */
    std::string response;
};

/**
 * @brief 流式请求写回调上下文。
 */
struct StreamWriteContext
{
    /** @brief 上层增量回调。 */
    LlmClient::DeltaCallback on_delta;
    /** @brief SSE 行缓冲（处理半包）。 */
    std::string line_buffer;
    /** @brief 聚合后的完整文本。 */
    std::string assembled;
    /** @brief 是否已经向上层输出过任意 delta 内容。 */
    bool has_output = false;
    /** @brief 响应模型名。 */
    std::string model;
    /** @brief 停止原因。 */
    std::string finish_reason;
    /** @brief 输入 token 数。 */
    uint64_t prompt_tokens = 0;
    /** @brief 输出 token 数。 */
    uint64_t completion_tokens = 0;
    /** @brief 解析错误信息。 */
    std::string parse_error;
    /** @brief 是否由上层主动中断流。 */
    bool callback_aborted = false;
};

/**
 * @brief 进程级 curl 全局初始化（仅一次）。
 */
void EnsureCurlGlobalInit()
{
    static std::once_flag s_init_flag;
    std::call_once(s_init_flag, []()
                   { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

/**
 * @brief 去掉字符串首尾空白字符（空格、Tab、CR）。
 */
std::string Trim(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r'))
    {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r'))
    {
        --end;
    }
    return value.substr(begin, end - begin);
}

/**
 * @brief 构建 Anthropic API Key 请求头。
 */
std::string BuildApiKeyHeader(const std::string& api_key)
{
    return "x-api-key: " + api_key;
}

/**
 * @brief 构建 Anthropic API 版本头。
 */
std::string BuildApiVersionHeader(const std::string& api_version)
{
    return "anthropic-version: " + api_version;
}

ApiKeyFailureType ClassifyHttpFailureType(long http_code)
{
    if (http_code == 429)
    {
        return ApiKeyFailureType::RATE_LIMIT;
    }
    if (http_code == 401 || http_code == 403)
    {
        return ApiKeyFailureType::AUTH_ERROR;
    }
    if (http_code >= 500 && http_code < 600)
    {
        return ApiKeyFailureType::SERVER_ERROR;
    }
    return ApiKeyFailureType::OTHER_ERROR;
}

bool IsRetriableHttpFailure(long http_code)
{
    return http_code == 429 || http_code == 401 || http_code == 403 || (http_code >= 500 && http_code < 600);
}

/**
 * @brief libcurl 同步响应写回调。
 */
size_t SyncWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    SyncWriteContext* ctx = static_cast<SyncWriteContext*>(userp);
    ctx->response.append(static_cast<const char*>(contents), total);
    return total;
}

/**
 * @brief 解析 Anthropic 错误节点（同步或流式事件中的 error）。
 * @param node 待解析 JSON 节点。
 * @param[out] error 解析到的人类可读错误信息。
 * @return true 识别到错误节点；false 非错误节点。
 */
bool TryParseErrorNode(const nlohmann::json& node, std::string& error)
{
    if (!node.is_object())
    {
        return false;
    }

    if (node.contains("error") && node["error"].is_object())
    {
        const nlohmann::json& err = node["error"];
        if (err.contains("message") && err["message"].is_string())
        {
            error = err["message"].get<std::string>();
        }
        else
        {
            error = "anthropic returned error";
        }
        return true;
    }

    if (node.contains("type") && node["type"].is_string() && node["type"].get<std::string>() == "error")
    {
        if (node.contains("message") && node["message"].is_string())
        {
            error = node["message"].get<std::string>();
        }
        else
        {
            error = "anthropic stream error";
        }
        return true;
    }

    return false;
}

/**
 * @brief 从 Anthropic `content[]` 数组提取 text 片段并拼接。
 * @param content Anthropic 响应中的 content 数组。
 * @param[out] out 拼接后的文本。
 * @return true 至少提取到一段文本；false 未提取到有效文本。
 */
bool ExtractTextFromContentArray(const nlohmann::json& content, std::string& out)
{
    if (!content.is_array())
    {
        return false;
    }

    bool has_text = false;
    for (size_t i = 0; i < content.size(); ++i)
    {
        if (!content[i].is_object())
        {
            continue;
        }
        const nlohmann::json& item = content[i];
        if (item.contains("type") && item["type"].is_string() && item["type"].get<std::string>() == "text" && item.contains("text") && item["text"].is_string())
        {
            out.append(item["text"].get<std::string>());
            has_text = true;
        }
    }
    return has_text;
}

/**
 * @brief 从 Anthropic usage 节点提取 token 统计。
 */
void FillUsageFromNode(const nlohmann::json& usage, uint64_t& prompt_tokens, uint64_t& completion_tokens)
{
    if (!usage.is_object())
    {
        return;
    }

    if (usage.contains("input_tokens") && usage["input_tokens"].is_number_unsigned())
    {
        prompt_tokens = usage["input_tokens"].get<uint64_t>();
    }
    if (usage.contains("output_tokens") && usage["output_tokens"].is_number_unsigned())
    {
        completion_tokens = usage["output_tokens"].get<uint64_t>();
    }
}

/**
 * @brief 构建 Anthropic Messages API 请求体。
 * @details
 * - `system` 消息会被聚合到顶层 `system` 字段；
 * - 其余消息映射为 `messages[]` 中的 user/assistant 条目。
 */
nlohmann::json BuildRequestBody(const LlmCompletionRequest& request, bool stream)
{
    nlohmann::json body;
    body["model"] = request.model;
    body["temperature"] = request.temperature;
    body["max_tokens"] = request.max_tokens;
    body["stream"] = stream;

    std::string system_prompt;
    body["messages"] = nlohmann::json::array();
    for (size_t i = 0; i < request.messages.size(); ++i)
    {
        const common::ChatMessage& message = request.messages[i];
        if (message.role == "system")
        {
            if (!system_prompt.empty())
            {
                system_prompt.append("\n\n");
            }
            system_prompt.append(message.content);
            continue;
        }

        nlohmann::json item;
        item["role"] = (message.role == "assistant") ? "assistant" : "user";
        item["content"] = message.content;
        body["messages"].push_back(item);
    }

    if (!system_prompt.empty())
    {
        body["system"] = system_prompt;
    }
    return body;
}

/**
 * @brief 解析 Anthropic 同步响应 JSON。
 */
bool ParseSyncResponse(const std::string& response_text, LlmCompletionResult& result, std::string& error)
{
    nlohmann::json parsed = nlohmann::json::parse(response_text, nullptr, false);
    if (parsed.is_discarded())
    {
        error = "anthropic response is not valid json";
        return false;
    }

    if (TryParseErrorNode(parsed, error))
    {
        return false;
    }

    if (parsed.contains("model") && parsed["model"].is_string())
    {
        result.model = parsed["model"].get<std::string>();
    }

    if (parsed.contains("stop_reason") && parsed["stop_reason"].is_string())
    {
        result.finish_reason = parsed["stop_reason"].get<std::string>();
    }

    if (!parsed.contains("content"))
    {
        error = "anthropic response missing content";
        return false;
    }

    if (!ExtractTextFromContentArray(parsed["content"], result.content))
    {
        error = "anthropic response missing content.text";
        return false;
    }

    if (parsed.contains("usage"))
    {
        FillUsageFromNode(parsed["usage"], result.prompt_tokens, result.completion_tokens);
    }

    return true;
}

/**
 * @brief 处理单行 SSE 数据，抽取增量文本并回调上层。
 */
bool HandleStreamDataLine(const std::string& line, StreamWriteContext& ctx)
{
    if (line.empty())
    {
        return true;
    }

    const std::string prefix = "data:";
    if (line.compare(0, prefix.size(), prefix) != 0)
    {
        return true;
    }

    std::string payload = Trim(line.substr(prefix.size()));
    if (payload.empty() || payload == "[DONE]")
    {
        return true;
    }

    nlohmann::json chunk = nlohmann::json::parse(payload, nullptr, false);
    if (chunk.is_discarded())
    {
        ctx.parse_error = "invalid anthropic stream chunk json";
        return false;
    }

    if (TryParseErrorNode(chunk, ctx.parse_error))
    {
        return false;
    }

    if (chunk.contains("model") && chunk["model"].is_string())
    {
        ctx.model = chunk["model"].get<std::string>();
    }

    if (chunk.contains("message") && chunk["message"].is_object())
    {
        const nlohmann::json& message = chunk["message"];
        if (message.contains("model") && message["model"].is_string())
        {
            ctx.model = message["model"].get<std::string>();
        }
        if (message.contains("usage"))
        {
            FillUsageFromNode(message["usage"], ctx.prompt_tokens, ctx.completion_tokens);
        }
        if (message.contains("stop_reason") && message["stop_reason"].is_string())
        {
            ctx.finish_reason = message["stop_reason"].get<std::string>();
        }
    }

    if (chunk.contains("usage"))
    {
        FillUsageFromNode(chunk["usage"], ctx.prompt_tokens, ctx.completion_tokens);
    }

    if (chunk.contains("stop_reason") && chunk["stop_reason"].is_string())
    {
        ctx.finish_reason = chunk["stop_reason"].get<std::string>();
    }

    if (chunk.contains("delta") && chunk["delta"].is_object())
    {
        const nlohmann::json& delta = chunk["delta"];
        if (delta.contains("stop_reason") && delta["stop_reason"].is_string())
        {
            ctx.finish_reason = delta["stop_reason"].get<std::string>();
        }
        if (delta.contains("text") && delta["text"].is_string())
        {
            const std::string text = delta["text"].get<std::string>();
                if (!text.empty())
                {
                    ctx.assembled.append(text);
                    ctx.has_output = true;
                    if (ctx.on_delta && !ctx.on_delta(text))
                    {
                        ctx.callback_aborted = true;
                    return false;
                }
            }
        }
    }

    if (chunk.contains("content_block") && chunk["content_block"].is_object())
    {
        const nlohmann::json& block = chunk["content_block"];
        if (block.contains("type") && block["type"].is_string() && block["type"].get<std::string>() == "text" && block.contains("text") && block["text"].is_string())
        {
            const std::string text = block["text"].get<std::string>();
            if (!text.empty())
            {
                ctx.assembled.append(text);
                ctx.has_output = true;
                if (ctx.on_delta && !ctx.on_delta(text))
                {
                    ctx.callback_aborted = true;
                    return false;
                }
            }
        }
    }

    return true;
}

/**
 * @brief libcurl 流式写回调：按行切分并委托 HandleStreamDataLine。
 */
size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    StreamWriteContext* ctx = static_cast<StreamWriteContext*>(userp);
    ctx->line_buffer.append(static_cast<const char*>(contents), total);

    size_t pos = 0;
    while (true)
    {
        size_t line_end = ctx->line_buffer.find('\n', pos);
        if (line_end == std::string::npos)
        {
            break;
        }

        std::string line = ctx->line_buffer.substr(pos, line_end - pos);
        if (!line.empty() && line[line.size() - 1] == '\r')
        {
            line.erase(line.size() - 1);
        }

        if (!HandleStreamDataLine(line, *ctx))
        {
            return 0;
        }

        pos = line_end + 1;
    }

    if (pos > 0)
    {
        ctx->line_buffer.erase(0, pos);
    }
    return total;
}

} // namespace

/**
 * @brief 构造 Anthropic 客户端并完成 curl 全局初始化。
 */
AnthropicClient::AnthropicClient(const AnthropicSettings& settings,
                                 const ApiKeyProvider::ptr& key_provider,
                                 uint32_t max_retry_per_request)
    : m_settings(settings)
    , m_key_provider(key_provider)
    , m_max_retry_per_request(std::min<uint32_t>(max_retry_per_request, 8))
{
    EnsureCurlGlobalInit();
}

/**
 * @brief 组装 `.../v1/messages` 目标地址。
 */
std::string AnthropicClient::BuildMessagesUrl() const
{
    if (m_settings.base_url.empty())
    {
        return "https://api.anthropic.com/v1/messages";
    }

    const std::string& base_url = m_settings.base_url;
    if (base_url.size() >= 3 && base_url.compare(base_url.size() - 3, 3, "/v1") == 0)
    {
        return base_url + "/messages";
    }
    if (base_url.size() >= 4 && base_url.compare(base_url.size() - 4, 4, "/v1/") == 0)
    {
        return base_url + "messages";
    }
    if (!base_url.empty() && base_url[base_url.size() - 1] == '/')
    {
        return base_url + "v1/messages";
    }
    return base_url + "/v1/messages";
}

/**
 * @brief Anthropic 同步补全实现。
 * @details
 * 第五阶段后流程与 OpenAI-Compatible 路径保持一致：
 * - 优先走 provider 级 key 池候选；
 * - 候选为空时回退配置单 key；
 * - 失败按错误类型上报，驱动 key 冷却与重试决策。
 */
bool AnthropicClient::Complete(const LlmCompletionRequest& request,
                               LlmCompletionResult& result,
                               std::string& error)
{
    std::vector<ApiKeyCandidate> candidates;
    const size_t attempt_limit = static_cast<size_t>(m_max_retry_per_request) + 1;
    if (m_key_provider)
    {
        candidates = m_key_provider->AcquireCandidates(attempt_limit);
    }
    if (candidates.empty() && !m_settings.api_key.empty())
    {
        ApiKeyCandidate fallback;
        fallback.id = 0;
        fallback.api_key = m_settings.api_key;
        candidates.push_back(fallback);
    }
    if (candidates.empty())
    {
        error = "no available anthropic api key";
        return false;
    }

    const size_t total_attempts = std::min(candidates.size(), attempt_limit);
    std::string last_error;
    for (size_t attempt = 0; attempt < total_attempts; ++attempt)
    {
        const ApiKeyCandidate& candidate = candidates[attempt];
        if (candidate.api_key.empty())
        {
            continue;
        }

        CURL* curl = curl_easy_init();
        if (!curl)
        {
            error = "curl_easy_init failed";
            return false;
        }

        SyncWriteContext write_ctx;
        nlohmann::json body = BuildRequestBody(request, false);
        const std::string payload = body.dump();

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, BuildApiKeyHeader(candidate.api_key).c_str());
        headers = curl_slist_append(headers, BuildApiVersionHeader(m_settings.api_version).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, BuildMessagesUrl().c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(m_settings.connect_timeout_ms));
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(m_settings.request_timeout_ms));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SyncWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);

        FiberCurlSession session(curl);
        CURLcode code = session.Perform();
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        bool should_retry = false;
        ApiKeyFailureType failure_type = ApiKeyFailureType::OTHER_ERROR;
        if (code != CURLE_OK)
        {
            error = std::string("anthropic request failed: ") + curl_easy_strerror(code);
            should_retry = true;
            failure_type = ApiKeyFailureType::NETWORK_ERROR;
        }
        else if (http_code < 200 || http_code >= 300)
        {
            std::ostringstream ss;
            ss << "anthropic http status " << http_code;
            if (!write_ctx.response.empty())
            {
                ss << ", body=" << write_ctx.response;
            }
            error = ss.str();
            should_retry = IsRetriableHttpFailure(http_code);
            failure_type = ClassifyHttpFailureType(http_code);
        }
        else
        {
            if (!ParseSyncResponse(write_ctx.response, result, error))
            {
                BASE_LOG_ERROR(g_logger) << "Parse anthropic sync response failed, body=" << write_ctx.response;
                should_retry = false;
                failure_type = ApiKeyFailureType::OTHER_ERROR;
            }
            else
            {
                if (result.model.empty())
                {
                    result.model = request.model;
                }
                if (m_key_provider && candidate.id != 0)
                {
                    m_key_provider->ReportSuccess(candidate.id);
                }
                return true;
            }
        }

        if (should_retry && m_key_provider && candidate.id != 0)
        {
            m_key_provider->ReportFailure(candidate.id, failure_type);
        }

        last_error = error;
        BASE_LOG_WARN(g_logger) << "anthropic attempt failed, key_id=" << candidate.id
                                << " attempt=" << (attempt + 1) << "/" << total_attempts
                                << " retry=" << (should_retry ? "yes" : "no")
                                << " error=" << error;

        if (!should_retry || attempt + 1 >= total_attempts)
        {
            error = last_error;
            return false;
        }
    }

    error = last_error.empty() ? "anthropic request failed" : last_error;
    return false;
}

/**
 * @brief Anthropic 流式补全实现。
 * @details
 * 与同步模式相同支持 key 池切换；当已输出部分 delta 后若发生错误，
 * 直接终止并返回失败，避免对客户端产生重复流式片段。
 */
bool AnthropicClient::StreamComplete(const LlmCompletionRequest& request,
                                     const DeltaCallback& on_delta,
                                     LlmCompletionResult& result,
                                     std::string& error)
{
    std::vector<ApiKeyCandidate> candidates;
    const size_t attempt_limit = static_cast<size_t>(m_max_retry_per_request) + 1;
    if (m_key_provider)
    {
        candidates = m_key_provider->AcquireCandidates(attempt_limit);
    }
    if (candidates.empty() && !m_settings.api_key.empty())
    {
        ApiKeyCandidate fallback;
        fallback.id = 0;
        fallback.api_key = m_settings.api_key;
        candidates.push_back(fallback);
    }
    if (candidates.empty())
    {
        error = "no available anthropic api key";
        return false;
    }

    const size_t total_attempts = std::min(candidates.size(), attempt_limit);
    std::string last_error;
    for (size_t attempt = 0; attempt < total_attempts; ++attempt)
    {
        const ApiKeyCandidate& candidate = candidates[attempt];
        if (candidate.api_key.empty())
        {
            continue;
        }

        CURL* curl = curl_easy_init();
        if (!curl)
        {
            error = "curl_easy_init failed";
            return false;
        }

        StreamWriteContext write_ctx;
        write_ctx.on_delta = on_delta;

        nlohmann::json body = BuildRequestBody(request, true);
        const std::string payload = body.dump();

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, BuildApiKeyHeader(candidate.api_key).c_str());
        headers = curl_slist_append(headers, BuildApiVersionHeader(m_settings.api_version).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, BuildMessagesUrl().c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(m_settings.connect_timeout_ms));
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(m_settings.request_timeout_ms));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);

        FiberCurlSession session(curl);
        CURLcode code = session.Perform();
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        bool should_retry = false;
        bool should_report_failure = false;
        ApiKeyFailureType failure_type = ApiKeyFailureType::OTHER_ERROR;
        if (code != CURLE_OK)
        {
            if (write_ctx.callback_aborted)
            {
                error = "stream callback aborted";
                should_retry = false;
            }
            else if (!write_ctx.parse_error.empty())
            {
                error = write_ctx.parse_error;
                should_retry = false;
            }
            else
            {
                error = std::string("anthropic stream request failed: ") + curl_easy_strerror(code);
                should_retry = true;
                should_report_failure = true;
                failure_type = ApiKeyFailureType::NETWORK_ERROR;
            }
        }
        else if (http_code < 200 || http_code >= 300)
        {
            std::ostringstream ss;
            ss << "anthropic stream http status " << http_code;
            error = ss.str();
            should_retry = IsRetriableHttpFailure(http_code);
            should_report_failure = should_retry;
            failure_type = ClassifyHttpFailureType(http_code);
        }
        else if (!write_ctx.parse_error.empty())
        {
            error = write_ctx.parse_error;
            should_retry = false;
        }
        else
        {
            result.content = write_ctx.assembled;
            result.model = write_ctx.model.empty() ? request.model : write_ctx.model;
            result.finish_reason = write_ctx.finish_reason;
            result.prompt_tokens = write_ctx.prompt_tokens;
            result.completion_tokens = write_ctx.completion_tokens;
            if (m_key_provider && candidate.id != 0)
            {
                m_key_provider->ReportSuccess(candidate.id);
            }
            return true;
        }

        if (write_ctx.has_output && should_retry)
        {
            should_retry = false;
            error = "anthropic stream interrupted after partial output";
        }

        if (should_report_failure && m_key_provider && candidate.id != 0)
        {
            m_key_provider->ReportFailure(candidate.id, failure_type);
        }

        last_error = error;
        BASE_LOG_WARN(g_logger) << "anthropic stream attempt failed, key_id=" << candidate.id
                                << " attempt=" << (attempt + 1) << "/" << total_attempts
                                << " retry=" << (should_retry ? "yes" : "no")
                                << " error=" << error;

        if (!should_retry || attempt + 1 >= total_attempts)
        {
            error = last_error;
            return false;
        }
    }

    error = last_error.empty() ? "anthropic stream request failed" : last_error;
    return false;
}

} // namespace llm
} // namespace ai
