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
 * @brief Async indexer settings.
 */
struct RagIndexerSettings
{
    size_t queue_capacity = 10000;
    size_t batch_size = 32;
    uint64_t flush_interval_ms = 200;
    // assistant message indexing policy: all | fact_like | none
    std::string assistant_index_mode = "fact_like";
    // minimum chars for assistant message in fact_like mode
    size_t assistant_min_chars = 24;
    // duplicate suppression TTL in milliseconds, 0 disables dedup
    uint64_t dedup_ttl_ms = 600000;
    // max dedup cache entries, 0 disables dedup
    size_t dedup_max_entries = 50000;
};

/**
 * @brief Async pipeline: message -> embedding -> vector upsert.
 */
class RagIndexer
{
  public:
    typedef std::shared_ptr<RagIndexer> ptr;

    RagIndexer(const EmbeddingClient::ptr& embedding_client,
               const VectorStore::ptr& vector_store,
               const RagIndexerSettings& settings);

    ~RagIndexer();

    bool Start(std::string& error);
    void Stop();

    bool Enqueue(const common::PersistMessage& message, std::string& error);

  private:
    void Run();
    bool FlushBatch(std::deque<common::PersistMessage>& batch, std::string& error);
    uint64_t BuildPointId(const common::PersistMessage& message) const;
    bool ShouldIndexMessage(const common::PersistMessage& message) const;
    bool IsAssistantMessageUseful(const std::string& content) const;
    bool ShouldSkipByDedup(const common::PersistMessage& message);
    void PruneDedupCache(uint64_t now_ms);

  private:
    EmbeddingClient::ptr m_embedding_client;
    VectorStore::ptr m_vector_store;
    RagIndexerSettings m_settings;

    bool m_running;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::deque<common::PersistMessage> m_queue;
    std::thread m_thread;

    size_t m_vector_size;

    std::unordered_map<uint64_t, uint64_t> m_dedup_last_seen_ms;
    std::deque<std::pair<uint64_t, uint64_t>> m_dedup_order;
};

} // namespace rag
} // namespace ai

#endif
