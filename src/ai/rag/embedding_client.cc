#include "ai/rag/embedding_client.h"

#include "ai/rag/rag_http_client.h"

#include <nlohmann/json.hpp>

#include <sstream>

namespace ai
{
namespace rag
{

/**
 * @brief 使用配置初始化 Ollama Embedding 客户端。
 */
OllamaEmbeddingClient::OllamaEmbeddingClient(const EmbeddingSettings& settings)
    : m_settings(settings)
{
}

/**
 * @brief 生成 Ollama embedding 接口地址。
 * @details
 * 兼容两种 base_url：
 * - 末尾不带 `/`：`http://x.x.x.x:11434`
 * - 末尾带 `/`：`http://x.x.x.x:11434/`
 */
std::string OllamaEmbeddingClient::BuildEmbedUrl() const
{
    if (m_settings.base_url.empty())
    {
        // 未配置时回退到本地默认地址。
        return "http://127.0.0.1:11434/api/embed";
    }

    if (m_settings.base_url[m_settings.base_url.size() - 1] == '/')
    {
        return m_settings.base_url + "api/embed";
    }
    return m_settings.base_url + "/api/embed";
}

bool OllamaEmbeddingClient::Embed(const std::string& input,
                                  std::vector<float>& embedding,
                                  std::string& error) const
{
    // 输入为空直接失败，避免无意义请求。
    if (input.empty())
    {
        error = "embedding input is empty";
        return false;
    }

    // 组装 Ollama 请求体：模型名 + 输入文本。
    nlohmann::json body;
    body["model"] = m_settings.model;
    body["input"] = input;

    // 组装 HTTP 请求参数。
    HttpRequestOptions req;
    req.method = "POST";
    req.url = BuildEmbedUrl();
    req.headers.push_back("Content-Type: application/json");
    req.body = body.dump();
    req.connect_timeout_ms = m_settings.connect_timeout_ms;
    req.request_timeout_ms = m_settings.request_timeout_ms;

    long http_status = 0;
    std::string response_body;
    if (!PerformHttpRequest(req, http_status, response_body, error))
    {
        // 网络层失败（连接错误、超时等）。
        return false;
    }

    // HTTP 非 2xx 视为 embedding 调用失败。
    if (http_status < 200 || http_status >= 300)
    {
        std::ostringstream ss;
        ss << "ollama embedding http status " << http_status;
        if (!response_body.empty())
        {
            ss << ", body=" << response_body;
        }
        error = ss.str();
        return false;
    }

    // 解析 JSON 响应。
    nlohmann::json parsed = nlohmann::json::parse(response_body, nullptr, false);
    if (parsed.is_discarded())
    {
        error = "ollama embedding response is not valid json";
        return false;
    }

    if (parsed.contains("error") && parsed["error"].is_string())
    {
        error = parsed["error"].get<std::string>();
        return false;
    }

    // 期望字段：embeddings[0] = float array。
    if (!parsed.contains("embeddings") || !parsed["embeddings"].is_array() || parsed["embeddings"].empty())
    {
        error = "ollama embedding response missing embeddings";
        return false;
    }

    const nlohmann::json& first = parsed["embeddings"][0];
    if (!first.is_array() || first.empty())
    {
        error = "ollama embedding vector is empty";
        return false;
    }

    // 把 JSON 数组安全转换为 std::vector<float>。
    embedding.clear();
    embedding.reserve(first.size());
    for (size_t i = 0; i < first.size(); ++i)
    {
        if (!first[i].is_number())
        {
            error = "ollama embedding vector contains non-numeric value";
            return false;
        }
        embedding.push_back(static_cast<float>(first[i].get<double>()));
    }

    return true;
}

} // namespace rag
} // namespace ai
