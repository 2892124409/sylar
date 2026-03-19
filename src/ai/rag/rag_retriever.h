#ifndef __SYLAR_AI_RAG_RAG_RETRIEVER_H__
#define __SYLAR_AI_RAG_RAG_RETRIEVER_H__

#include "ai/rag/embedding_client.h"
#include "ai/rag/vector_store.h"

#include <memory>
#include <string>
#include <vector>

namespace ai
{
namespace rag
{

/**
 * @brief Semantic retriever for user long-term memory.
 */
class RagRetriever
{
  public:
    typedef std::shared_ptr<RagRetriever> ptr;

    RagRetriever(const EmbeddingClient::ptr& embedding_client,
                 const VectorStore::ptr& vector_store);

    /**
     * @brief Retrieve top-k semantic memory snippets for one user.
     */
    bool Retrieve(const std::string& sid,
                  const std::string& query,
                  size_t top_k,
                  double score_threshold,
                  std::vector<SearchHit>& out,
                  std::string& error) const;

  private:
    EmbeddingClient::ptr m_embedding_client;
    VectorStore::ptr m_vector_store;
};

} // namespace rag
} // namespace ai

#endif
