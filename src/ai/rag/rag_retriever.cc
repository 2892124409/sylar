#include "ai/rag/rag_retriever.h"

namespace ai
{
namespace rag
{

RagRetriever::RagRetriever(const EmbeddingClient::ptr& embedding_client,
                           const VectorStore::ptr& vector_store)
    : m_embedding_client(embedding_client)
    , m_vector_store(vector_store)
{
}

bool RagRetriever::Retrieve(const std::string& sid,
                            const std::string& query,
                            size_t top_k,
                            double score_threshold,
                            std::vector<SearchHit>& out,
                            std::string& error) const
{
    out.clear();

    if (sid.empty() || query.empty() || top_k == 0)
    {
        return true;
    }

    if (!m_embedding_client || !m_vector_store)
    {
        error = "rag retriever dependencies are not initialized";
        return false;
    }

    std::vector<float> query_embedding;
    if (!m_embedding_client->Embed(query, query_embedding, error))
    {
        return false;
    }

    return m_vector_store->Search(sid, query_embedding, top_k, score_threshold, out, error);
}

} // namespace rag
} // namespace ai
