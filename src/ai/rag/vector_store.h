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
 * @brief 向量库连接配置。
 */
struct VectorStoreSettings
{
    /** @brief 向量库 HTTP 基础地址（当前实现为 Qdrant）。 */
    std::string base_url;
    /** @brief 集合名（collection）。 */
    std::string collection;
    /** @brief 请求超时（毫秒）。 */
    uint64_t request_timeout_ms = 5000;
};

/**
 * @brief 每个向量点携带的业务元数据。
 * @details 这些字段用于检索过滤与结果解释。
 */
struct MemoryPayload
{
    /** @brief 主体标识（用户 SID）。 */
    std::string sid;
    /** @brief 对话会话 ID。 */
    std::string conversation_id;
    /** @brief 角色（user/assistant）。 */
    std::string role;
    /** @brief 原始文本内容。 */
    std::string content;
    /** @brief 业务时间戳（毫秒）。 */
    uint64_t created_at_ms = 0;
};

/**
 * @brief 用于 Upsert 的单个向量点。
 */
struct VectorPoint
{
    /** @brief 向量点唯一 ID。 */
    uint64_t id = 0;
    /** @brief 向量数据。 */
    std::vector<float> vector;
    /** @brief 附加业务元数据。 */
    MemoryPayload payload;
};

/**
 * @brief 向量检索命中项。
 */
struct SearchHit
{
    /** @brief 命中点 ID。 */
    uint64_t id = 0;
    /** @brief 相似度分数。 */
    double score = 0;
    /** @brief 命中点元数据。 */
    MemoryPayload payload;
};

/**
 * @brief 向量库抽象接口。
 */
class VectorStore
{
  public:
    typedef std::shared_ptr<VectorStore> ptr;
    virtual ~VectorStore() {}

    /**
     * @brief 确保集合存在（不存在则创建）。
     */
    virtual bool EnsureCollection(size_t vector_size, std::string& error) = 0;
    /**
     * @brief 批量写入/更新向量点。
     */
    virtual bool Upsert(const std::vector<VectorPoint>& points, std::string& error) = 0;
    /**
     * @brief 按用户范围执行语义检索。
     */
    virtual bool Search(const std::string& sid,
                        const std::vector<float>& query,
                        size_t top_k,
                        double score_threshold,
                        std::vector<SearchHit>& out,
                        std::string& error) = 0;
};

/**
 * @brief 基于 Qdrant HTTP API 的向量库实现。
 */
class QdrantVectorStore : public VectorStore
{
  public:
    /**
     * @brief 构造 Qdrant 向量库客户端。
     */
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
    /** @brief 构造集合资源 URL。 */
    std::string BuildCollectionUrl() const;
    /** @brief 构造 points Upsert URL。 */
    std::string BuildPointsUrl() const;
    /** @brief 构造检索 URL。 */
    std::string BuildSearchUrl() const;

  private:
    VectorStoreSettings m_settings;
};

} // namespace rag
} // namespace ai

#endif
