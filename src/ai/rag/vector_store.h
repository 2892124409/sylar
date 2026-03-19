#ifndef __SYLAR_AI_RAG_VECTOR_STORE_H__
#define __SYLAR_AI_RAG_VECTOR_STORE_H__

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

namespace ai
{
namespace rag
{

/**
 * @brief Vector store connection settings.
 */
struct VectorStoreSettings
{
    std::string base_url;
    std::string collection;
    uint64_t request_timeout_ms = 5000;
};

/**
 * @brief Metadata persisted with each vector point.
 */
struct MemoryPayload
{
    std::string sid;
    std::string conversation_id;
    std::string role;
    std::string content;
    uint64_t created_at_ms = 0;
};

/**
 * @brief One vector point for upsert.
 */
struct VectorPoint
{
    uint64_t id = 0;
    std::vector<float> vector;
    MemoryPayload payload;
};

/**
 * @brief One retrieval hit from vector search.
 */
struct SearchHit
{
    uint64_t id = 0;
    double score = 0;
    MemoryPayload payload;
};

/**
 * @brief Abstract vector store.
 */
class VectorStore
{
  public:
    typedef std::shared_ptr<VectorStore> ptr;
    virtual ~VectorStore() {}

    virtual bool EnsureCollection(size_t vector_size, std::string& error) = 0;
    virtual bool Upsert(const std::vector<VectorPoint>& points, std::string& error) = 0;
    virtual bool Search(const std::string& sid,
                        const std::vector<float>& query,
                        size_t top_k,
                        double score_threshold,
                        std::vector<SearchHit>& out,
                        std::string& error) = 0;
};

/**
 * @brief Qdrant implementation of vector store.
 */
class QdrantVectorStore : public VectorStore
{
  public:
    explicit QdrantVectorStore(const VectorStoreSettings& settings);

    virtual bool EnsureCollection(size_t vector_size, std::string& error) override;
    virtual bool Upsert(const std::vector<VectorPoint>& points, std::string& error) override;
    virtual bool Search(const std::string& sid,
                        const std::vector<float>& query,
                        size_t top_k,
                        double score_threshold,
                        std::vector<SearchHit>& out,
                        std::string& error) override;

  private:
    std::string BuildCollectionUrl() const;
    std::string BuildPointsUrl() const;
    std::string BuildSearchUrl() const;

  private:
    VectorStoreSettings m_settings;
};

} // namespace rag
} // namespace ai

#endif
