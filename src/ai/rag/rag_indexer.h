#ifndef __SYLAR_AI_RAG_RAG_INDEXER_H__
#define __SYLAR_AI_RAG_RAG_INDEXER_H__

#include "ai/common/ai_types.h"
#include "ai/rag/embedding_client.h"
#include "ai/rag/vector_store.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace ai
{
namespace rag
{

/**
 * @brief RAG 异步索引器配置。
 *
 * @details
 * RagIndexer 负责把入队消息异步转为 embedding 并写入向量库。
 * 该配置用于控制队列容量、批处理节奏、assistant 消息筛选与去重策略。
 */
struct RagIndexerSettings
{
    /** @brief 入队缓冲队列上限（条）。 */
    size_t queue_capacity = 10000;
    /** @brief 单次 flush 最多处理条数。 */
    size_t batch_size = 32;
    /** @brief 空闲时等待并触发 flush 的周期（毫秒）。 */
    uint64_t flush_interval_ms = 200;
    /** @brief assistant 消息入索引策略：`all` | `fact_like` | `none`。 */
    std::string assistant_index_mode = "fact_like";
    /** @brief `fact_like` 模式下，assistant 最小字符数门槛。 */
    size_t assistant_min_chars = 24;
    /** @brief 去重窗口 TTL（毫秒），`0` 表示关闭去重。 */
    uint64_t dedup_ttl_ms = 600000;
    /** @brief 去重缓存最大条数，`0` 表示关闭去重。 */
    size_t dedup_max_entries = 50000;
};

/**
 * @brief RAG 异步索引流水线：`message -> embedding -> vector upsert`。
 *
 * @details
 * 该类通过后台线程消费消息队列，完成以下工作：
 * 1) 筛选可索引消息（用户消息、或符合策略的 assistant 消息）。
 * 2) 文本去重（按 sid + 归一化文本 + TTL）。
 * 3) 批量生成向量并写入 Qdrant。
 */
class RagIndexer
{
  public:
    typedef std::shared_ptr<RagIndexer> ptr;

    /**
     * @brief 构造异步索引器。
     */
    RagIndexer(const EmbeddingClient::ptr& embedding_client,
               const VectorStore::ptr& vector_store,
               const RagIndexerSettings& settings);

    /**
     * @brief 析构时自动停止后台线程并回收资源。
     */
    ~RagIndexer();

    /**
     * @brief 启动索引后台线程。
     * @param[out] error 错误信息。
     * @return true 启动成功或已在运行；
     * @return false 依赖/配置非法导致启动失败。
     */
    bool Start(std::string& error);

    /**
     * @brief 停止索引后台线程（幂等）。
     */
    void Stop();

    /**
     * @brief 投递一条消息进入异步索引队列。
     * @param message 待索引消息（通常来自对话持久化事件）。
     * @param[out] error 错误信息。
     * @return true 入队成功或被策略性跳过；
     * @return false 索引器未运行或队列已满。
     */
    bool Enqueue(const common::PersistMessage& message, std::string& error);

  private:
    /**
     * @brief 后台工作循环：定期/按批消费消息并 flush。
     */
    void Run();

    /**
     * @brief 对一个 batch 执行 embedding 与向量写入。
     */
    bool FlushBatch(std::deque<common::PersistMessage>& batch, std::string& error);

    /**
     * @brief 基于消息关键字段生成稳定 point id（64 位哈希）。
     */
    uint64_t BuildPointId(const common::PersistMessage& message) const;

    /**
     * @brief 判断消息是否应该进入索引流程。
     */
    bool ShouldIndexMessage(const common::PersistMessage& message) const;

    /**
     * @brief 判断 assistant 文本是否“有信息密度”（fact_like）。
     */
    bool IsAssistantMessageUseful(const std::string& content) const;

    /**
     * @brief 去重判断：命中 TTL 窗口内重复文本则跳过。
     */
    bool ShouldSkipByDedup(const common::PersistMessage& message);

    /**
     * @brief 清理过期或失效的去重缓存条目。
     */
    void PruneDedupCache(uint64_t now_ms);

  private:
    /** @brief Embedding 客户端。 */
    EmbeddingClient::ptr m_embedding_client;
    /** @brief 向量库客户端。 */
    VectorStore::ptr m_vector_store;
    /** @brief 索引器配置。 */
    RagIndexerSettings m_settings;

    /** @brief 是否处于运行状态（受 m_mutex 保护）。 */
    bool m_running;
    /** @brief 队列与状态保护锁。 */
    std::mutex m_mutex;
    /** @brief 入队/停止通知条件变量。 */
    std::condition_variable m_cond;
    /** @brief 待索引消息队列。 */
    std::deque<common::PersistMessage> m_queue;
    /** @brief 后台工作线程。 */
    std::thread m_thread;

    /** @brief 向量维度缓存（首批成功 embedding 后确定）。 */
    size_t m_vector_size;

    /** @brief 去重哈希 -> 最近出现时间戳(ms)。 */
    std::unordered_map<uint64_t, uint64_t> m_dedup_last_seen_ms;
    /** @brief 去重条目插入顺序，用于 TTL 与容量淘汰。 */
    std::deque<std::pair<uint64_t, uint64_t>> m_dedup_order;
};

} // namespace rag
} // namespace ai

#endif
