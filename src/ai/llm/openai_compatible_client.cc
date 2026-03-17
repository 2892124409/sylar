#include "ai/llm/openai_compatible_client.h"
#include "ai/llm/fiber_curl_session.h"

#include "log/logger.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <mutex>
#include <sstream>

/**
 * @file openai_compatible_client.cc
 * @brief OpenAI-Compatible 客户端实现：同步补全与流式补全。
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
    /** @brief 上层注册的 delta 回调。 */
    LlmClient::DeltaCallback on_delta;
    /** @brief 按行解析 SSE 时的残留缓冲。 */
    std::string line_buffer;
    /** @brief 聚合后的完整回答文本。 */
    std::string assembled;
    /** @brief 上游回传的模型名称。 */
    std::string model;
    /** @brief 结束原因。 */
    std::string finish_reason;
    /** @brief usage.prompt_tokens。 */
    uint64_t prompt_tokens = 0;
    /** @brief usage.completion_tokens。 */
    uint64_t completion_tokens = 0;
    /** @brief 解析失败原因。 */
    std::string parse_error;
    /** @brief 是否因为上层回调返回 false 主动中断。 */
    bool callback_aborted = false;
};

/**
 * @brief 确保 curl 全局初始化仅执行一次。
 */
void EnsureCurlGlobalInit()
{
    static std::once_flag s_init_flag;
    std::call_once(s_init_flag, []()
                   { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

/**
 * @brief 组装 HTTP Authorization 请求头。
 */
std::string BuildAuthorizationHeader(const std::string& api_key)
{
    return "Authorization: Bearer " + api_key;
}

/**
 * @brief libcurl 同步响应写回调，把字节流累计到 response。
 */
size_t SyncWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    SyncWriteContext* ctx = static_cast<SyncWriteContext*>(userp);
    ctx->response.append(static_cast<const char*>(contents), total);
    return total;
}

/**
 * @brief 去除字符串首尾空白字符（空格、Tab、CR）。
 */
static std::string Trim(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r'))
    {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin &&
           (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r'))
    {
        --end;
    }
    return value.substr(begin, end - begin);
}

/**
 * @brief 处理单行 SSE 数据。
 * @details
 * 仅处理 `data:` 行；解析出 `choices[0].delta.content` 后回调上层。
 */
bool HandleStreamDataLine(const std::string& line, StreamWriteContext& ctx)
{
    // 空行是 SSE 事件分隔符，忽略即可。
    if (line.empty())
    {
        return true;
    }

    const std::string prefix = "data:";
    // 非 data 行（如注释行）不参与业务解析。
    if (line.compare(0, prefix.size(), prefix) != 0)
    {
        return true;
    }

    std::string payload = Trim(line.substr(prefix.size()));
    if (payload.empty())
    {
        return true;
    }

    // [DONE] 表示上游流结束。
    if (payload == "[DONE]")
    {
        return true;
    }

    nlohmann::json chunk = nlohmann::json::parse(payload, nullptr, false);
    if (chunk.is_discarded())
    {
        ctx.parse_error = "invalid stream chunk json";
        return false;
    }

    // 记录模型信息（若返回）。
    if (chunk.contains("model") && chunk["model"].is_string())
    {
        ctx.model = chunk["model"].get<std::string>();
    }

    if (chunk.contains("choices") && chunk["choices"].is_array() && !chunk["choices"].empty())
    {
        const nlohmann::json& choice = chunk["choices"][0];
        if (choice.contains("delta") && choice["delta"].is_object())
        {
            const nlohmann::json& delta = choice["delta"];
            if (delta.contains("content") && delta["content"].is_string())
            {
                const std::string delta_text = delta["content"].get<std::string>();
                if (!delta_text.empty())
                {
                    // 本地聚合完整文本，供最终 result.content 使用。
                    ctx.assembled.append(delta_text);
                    // 立即回调上层输出增量。
                    if (ctx.on_delta && !ctx.on_delta(delta_text))
                    {
                        // 上层返回 false 表示主动中止流。
                        ctx.callback_aborted = true;
                        return false;
                    }
                }
            }
        }

        if (choice.contains("finish_reason") && choice["finish_reason"].is_string())
        {
            ctx.finish_reason = choice["finish_reason"].get<std::string>();
        }
    }

    // 记录 token 统计（若上游返回）。
    if (chunk.contains("usage") && chunk["usage"].is_object())
    {
        const nlohmann::json& usage = chunk["usage"];
        if (usage.contains("prompt_tokens") && usage["prompt_tokens"].is_number_unsigned())
        {
            ctx.prompt_tokens = usage["prompt_tokens"].get<uint64_t>();
        }
        if (usage.contains("completion_tokens") && usage["completion_tokens"].is_number_unsigned())
        {
            ctx.completion_tokens = usage["completion_tokens"].get<uint64_t>();
        }
    }

    return true;
}

/**
 * @brief libcurl 流式写回调，按行切分并交给 HandleStreamDataLine。
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
            // 返回 0 告诉 libcurl 终止传输。
            return 0;
        }

        pos = line_end + 1;
    }

    if (pos > 0)
    {
        // 移除已处理行，仅保留尾部半行数据。
        ctx->line_buffer.erase(0, pos);
    }

    return total;
}

/**
 * @brief 构建 OpenAI-Compatible 请求体。
 */
nlohmann::json BuildRequestBody(const LlmCompletionRequest& request, bool stream)
{
    nlohmann::json body;
    body["model"] = request.model;
    body["temperature"] = request.temperature;
    body["max_tokens"] = request.max_tokens;
    body["stream"] = stream;

    body["messages"] = nlohmann::json::array();
    for (size_t i = 0; i < request.messages.size(); ++i)
    {
        nlohmann::json item;
        item["role"] = request.messages[i].role;
        item["content"] = request.messages[i].content;
        body["messages"].push_back(item);
    }

    return body;
}

/**
 * @brief 解析 OpenAI-Compatible 同步响应 JSON。
 */
bool ParseSyncResponse(const std::string& response_text, LlmCompletionResult& result, std::string& error)
{
    // 尝试把上游响应文本解析为 JSON；第三个参数=false 表示解析失败不抛异常。
    nlohmann::json parsed = nlohmann::json::parse(response_text, nullptr, false);
    // 解析失败时直接返回错误，避免后续字段访问触发未定义语义。
    if (parsed.is_discarded())
    {
        error = "openai-compatible response is not valid json";
        return false;
    }

    // OpenAI-Compatible 错误响应通常形如 {"error": {...}}，优先识别该分支。
    if (parsed.contains("error") && parsed["error"].is_object())
    {
        // 读取 error 节点，尽量提取可读 message 透传给上层。
        const nlohmann::json& error_node = parsed["error"];
        if (error_node.contains("message") && error_node["message"].is_string())
        {
            error = error_node["message"].get<std::string>();
        }
        else
        {
            // 若缺失 message，给统一兜底错误，避免返回空字符串。
            error = "openai-compatible upstream returned error";
        }
        return false;
    }

    // model 字段是可选元信息；若存在则回填，缺失不视为失败。
    if (parsed.contains("model") && parsed["model"].is_string())
    {
        result.model = parsed["model"].get<std::string>();
    }

    // choices 是同步补全的核心字段，必须是非空数组。
    if (!parsed.contains("choices") || !parsed["choices"].is_array() || parsed["choices"].empty())
    {
        error = "openai-compatible response missing choices";
        return false;
    }

    // V1 只消费第一条候选结果。
    const nlohmann::json& choice = parsed["choices"][0];
    // finish_reason 可选，存在时用于上层判断生成结束原因。
    if (choice.contains("finish_reason") && choice["finish_reason"].is_string())
    {
        result.finish_reason = choice["finish_reason"].get<std::string>();
    }

    // message 对象是同步结果正文容器，缺失则响应结构不合法。
    if (!choice.contains("message") || !choice["message"].is_object())
    {
        error = "openai-compatible response missing message";
        return false;
    }

    const nlohmann::json& message = choice["message"];
    // message.content 是最关键输出字段，必须存在且为字符串。
    if (!message.contains("content") || !message["content"].is_string())
    {
        error = "openai-compatible response missing message.content";
        return false;
    }

    // 提取最终回答文本。
    result.content = message["content"].get<std::string>();

    // usage 是可选统计字段，存在时读取 token 计数。
    if (parsed.contains("usage") && parsed["usage"].is_object())
    {
        const nlohmann::json& usage = parsed["usage"];
        // prompt_tokens（输入 token 数）可选。
        if (usage.contains("prompt_tokens") && usage["prompt_tokens"].is_number_unsigned())
        {
            result.prompt_tokens = usage["prompt_tokens"].get<uint64_t>();
        }
        // completion_tokens（输出 token 数）可选。
        if (usage.contains("completion_tokens") && usage["completion_tokens"].is_number_unsigned())
        {
            result.completion_tokens = usage["completion_tokens"].get<uint64_t>();
        }
    }

    // 所有必要字段校验与提取完成。
    return true;
}

} // namespace

/**
 * @brief 构造 OpenAI-Compatible 客户端并完成 curl 全局初始化。
 */
OpenAICompatibleClient::OpenAICompatibleClient(const config::OpenAICompatibleSettings& settings)
    : m_settings(settings)
{
    EnsureCurlGlobalInit();
}

/**
 * @brief 生成 completions 请求地址。
 */
std::string OpenAICompatibleClient::BuildCompletionsUrl() const
{
    if (m_settings.base_url.empty())
    {
        return "https://api.openai.com/v1/chat/completions";
    }

    if (m_settings.base_url[m_settings.base_url.size() - 1] == '/')
    {
        return m_settings.base_url + "chat/completions";
    }
    return m_settings.base_url + "/chat/completions";
}

/**
 * @brief 同步补全实现。
 */
bool OpenAICompatibleClient::Complete(const LlmCompletionRequest& request, LlmCompletionResult& result, std::string& error)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        error = "curl_easy_init failed";
        return false;
    }

    // 把 HTTP 响应体攒完整的临时容器
    SyncWriteContext write_ctx;

    // 构建非流式 JSON 请求体。
    nlohmann::json body = BuildRequestBody(request, false);
    const std::string payload = body.dump();

    // 设置鉴权头与 JSON 类型头。
    struct curl_slist* headers = nullptr;
    const std::string auth = BuildAuthorizationHeader(m_settings.api_key);
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // 配置请求 URL（目标接口地址：.../chat/completions）。
    curl_easy_setopt(curl, CURLOPT_URL, BuildCompletionsUrl().c_str());
    // 挂载请求头链表（Authorization、Content-Type 等）。
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // 开启 POST 方法（1L 表示 true）。
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    // 设置请求体数据起始地址（payload 是序列化后的 JSON 字符串）。
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    // 显式设置请求体长度，避免依赖 '\0' 推断长度。
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
    // 连接超时（毫秒）：只约束建连阶段（TCP/TLS 握手）。
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(m_settings.connect_timeout_ms));
    // 总超时（毫秒）：覆盖整次请求（建连 + 发送 + 接收）。
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(m_settings.request_timeout_ms));
    // 注册写回调函数：每收到一块响应数据就调用一次。
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SyncWriteCallback);
    // 传入写回调上下文，回调会把数据累积到 write_ctx.response。
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);

    // 之前是直接的 curl_easy_perform是阻塞调用，会导致工作线程阻塞
    FiberCurlSession session(curl);
    CURLcode code = session.Perform();

    long http_code = 0;
    // 取HTTP状态码
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    // 无论成功失败都释放资源。
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // curl失败
    if (code != CURLE_OK)
    {
        error = std::string("openai-compatible request failed: ") + curl_easy_strerror(code);
        return false;
    }

    if (http_code < 200 || http_code >= 300)
    {
        std::ostringstream ss;
        ss << "openai-compatible http status " << http_code;
        if (!write_ctx.response.empty())
        {
            ss << ", body=" << write_ctx.response;
        }
        error = ss.str();
        return false;
    }

    // 解析 JSON 响应到 result
    if (!ParseSyncResponse(write_ctx.response, result, error))
    {
        BASE_LOG_ERROR(g_logger) << "Parse openai-compatible sync response failed, body=" << write_ctx.response;
        return false;
    }

    // 若上游未回传模型名，回退为请求模型。
    if (result.model.empty())
    {
        result.model = request.model;
    }

    return true;
}

/**
 * @brief 流式补全实现。
 */
bool OpenAICompatibleClient::StreamComplete(const LlmCompletionRequest& request, const DeltaCallback& on_delta, LlmCompletionResult& result, std::string& error)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        error = "curl_easy_init failed";
        return false;
    }

    StreamWriteContext write_ctx;
    write_ctx.on_delta = on_delta;

    // 构建流式 JSON 请求体（stream=true）。
    nlohmann::json body = BuildRequestBody(request, true);
    const std::string payload = body.dump();

    struct curl_slist* headers = nullptr;
    const std::string auth = BuildAuthorizationHeader(m_settings.api_key);
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // 配置请求 URL（目标接口地址：.../chat/completions）。
    curl_easy_setopt(curl, CURLOPT_URL, BuildCompletionsUrl().c_str());
    // 挂载请求头链表（Authorization、Content-Type 等）。
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // 开启 POST 方法（1L 表示 true）。
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    // 设置请求体数据起始地址（payload 是序列化后的 JSON 字符串）。
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    // 显式设置请求体长度，避免依赖 '\0' 推断长度。
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
    // 连接超时（毫秒）：只约束建连阶段（TCP/TLS 握手）。
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(m_settings.connect_timeout_ms));
    // 总超时（毫秒）：覆盖整次请求（建连 + 发送 + 接收）。
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(m_settings.request_timeout_ms));
    // 注册流式写回调：每收到一块响应数据就调用 StreamWriteCallback。
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
    // 传入流式回调上下文，回调会解析 SSE 并累计到 write_ctx。
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);

    FiberCurlSession session(curl);
    CURLcode code = session.Perform();

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // curl 失败要区分：回调主动中断、解析错误、网络错误。
    if (code != CURLE_OK)
    {
        if (write_ctx.callback_aborted)
        {
            error = "stream callback aborted";
        }
        else if (!write_ctx.parse_error.empty())
        {
            error = write_ctx.parse_error;
        }
        else
        {
            error = std::string("openai-compatible stream request failed: ") + curl_easy_strerror(code);
        }
        return false;
    }

    if (http_code < 200 || http_code >= 300)
    {
        std::ostringstream ss;
        ss << "openai-compatible stream http status " << http_code;
        error = ss.str();
        return false;
    }

    if (!write_ctx.parse_error.empty())
    {
        error = write_ctx.parse_error;
        return false;
    }

    // 回填流式聚合结果。
    result.content = write_ctx.assembled;
    result.model = write_ctx.model.empty() ? request.model : write_ctx.model;
    result.finish_reason = write_ctx.finish_reason;
    result.prompt_tokens = write_ctx.prompt_tokens;
    result.completion_tokens = write_ctx.completion_tokens;
    return true;
}

} // namespace llm
} // namespace ai
