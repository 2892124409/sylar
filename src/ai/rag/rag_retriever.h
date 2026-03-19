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
 * @brief 用户长期记忆语义检索器。
 *
 * @details
 * 负责把用户 query 串成完整检索链路：
 * 1) query -> embedding
 * 2) embedding -> vector search（按 sid 过滤）
 * 3) 返回命中片段给上层进行 prompt 注入
 */
class RagRetriever
{
  public:
    typedef std::shared_ptr<RagRetriever> ptr;

    /**
     * @brief 构造检索器。
     */
    RagRetriever(const EmbeddingClient::ptr& embedding_client,
                 const VectorStore::ptr& vector_store);

    /**
     * @brief 检索指定用户的语义记忆片段。
     *
     * @param sid 用户主体 SID。
     * @param query 当前问题文本。
     * @param top_k 召回条数上限。
     * @param score_threshold 分数阈值（<=0 表示不启用阈值）。
     * @param[out] out 检索命中结果。
     * @param[out] error 错误信息。
     * @return true 成功（包含“无命中”的成功）；
     * @return false 失败（依赖未初始化/embedding失败/vector search失败）。
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
