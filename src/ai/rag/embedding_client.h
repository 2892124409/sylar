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
 * @brief Embedding service settings.
 */
struct EmbeddingSettings
{
    std::string base_url;
    std::string model;
    uint64_t connect_timeout_ms = 3000;
    uint64_t request_timeout_ms = 30000;
};

/**
 * @brief Abstract embedding client.
 */
class EmbeddingClient
{
  public:
    typedef std::shared_ptr<EmbeddingClient> ptr;
    virtual ~EmbeddingClient() {}

    /**
     * @brief Convert input text to one dense vector.
     */
    virtual bool Embed(const std::string& input,
                       std::vector<float>& embedding,
                       std::string& error) const = 0;
};

/**
 * @brief Ollama embedding client.
 */
class OllamaEmbeddingClient : public EmbeddingClient
{
  public:
    explicit OllamaEmbeddingClient(const EmbeddingSettings& settings);

    virtual bool Embed(const std::string& input,
                       std::vector<float>& embedding,
                       std::string& error) const override;

  private:
    std::string BuildEmbedUrl() const;

  private:
    EmbeddingSettings m_settings;
};

} // namespace rag
} // namespace ai

#endif
