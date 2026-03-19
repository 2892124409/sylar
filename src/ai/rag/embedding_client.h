#ifndef __SYLAR_AI_RAG_EMBEDDING_CLIENT_H__
#define __SYLAR_AI_RAG_EMBEDDING_CLIENT_H__

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

namespace ai
{
namespace rag
{

/**
 * @brief Embedding 服务配置。
 */
struct EmbeddingSettings
{
    /** @brief Embedding 服务基础地址（例如 `http://127.0.0.1:11434`）。 */
    std::string base_url;
    /** @brief Embedding 模型名（例如 `mxbai-embed-large`）。 */
    std::string model;
    /** @brief 建连超时（毫秒）。 */
    uint64_t connect_timeout_ms = 3000;
    /** @brief 请求总超时（毫秒）。 */
    uint64_t request_timeout_ms = 30000;
};

/**
 * @brief Embedding 客户端抽象接口。
 */
class EmbeddingClient
{
  public:
    typedef std::shared_ptr<EmbeddingClient> ptr;
    virtual ~EmbeddingClient() {}

    /**
     * @brief 把输入文本转换为稠密向量。
     *
     * @param input 输入文本。
     * @param[out] embedding 输出向量。
     * @param[out] error 错误信息。
     * @return true 成功；false 失败。
     */
    virtual bool Embed(const std::string& input,
                       std::vector<float>& embedding,
                       std::string& error) const = 0;
};

/**
 * @brief 基于 Ollama `/api/embed` 的 Embedding 客户端实现。
 */
class OllamaEmbeddingClient : public EmbeddingClient
{
  public:
    /**
     * @brief 构造 Ollama embedding 客户端。
     */
    explicit OllamaEmbeddingClient(const EmbeddingSettings& settings);

    virtual bool Embed(const std::string& input,
                       std::vector<float>& embedding,
                       std::string& error) const override;

  private:
    /**
     * @brief 构造完整的 `/api/embed` 请求地址。
     */
    std::string BuildEmbedUrl() const;

  private:
    EmbeddingSettings m_settings;
};

} // namespace rag
} // namespace ai

#endif
