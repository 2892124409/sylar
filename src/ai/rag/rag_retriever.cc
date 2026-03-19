#include "ai/rag/rag_retriever.h"

namespace ai
{
namespace rag
{

/**
 * @brief 构造检索器，注入 embedding 与向量检索依赖。
 */
RagRetriever::RagRetriever(const EmbeddingClient::ptr& embedding_client, const VectorStore::ptr& vector_store)
    : m_embedding_client(embedding_client)
    , m_vector_store(vector_store)
{
}

/**
 * @brief 执行一次用户侧语义检索。
 *
 * @details
 * 调用链：
 * 1) query -> embedding
 * 2) embedding -> vector search（按 sid 过滤）
 * 3) 返回命中片段给上层拼装上下文
 */
bool RagRetriever::Retrieve(const std::string& sid,
    const std::string& query,
    size_t top_k,
    double score_threshold,
    std::vector<SearchHit>& out,
    std::string& error) const
{
    // 每次调用先清空输出，避免调用方拿到脏数据。
    out.clear();

    // 参数空值场景按“无结果”成功返回，避免干扰主链路。
    if (sid.empty() || query.empty() || top_k == 0)
    {
        return true;
    }

    // 依赖未初始化属于配置/启动问题，直接失败并返回错误。
    if (!m_embedding_client || !m_vector_store)
    {
        error = "rag retriever dependencies are not initialized";
        return false;
    }

    // Step 1: 把 query 转成向量。
    std::vector<float> query_embedding;
    if (!m_embedding_client->Embed(query, query_embedding, error))
    {
        return false;
    }

    // Step 2: 在向量库中执行相似度检索并返回命中。
    return m_vector_store->Search(sid, query_embedding, top_k, score_threshold, out, error);
}

} // namespace rag
} // namespace ai
