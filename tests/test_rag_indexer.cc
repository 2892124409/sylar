#include "ai/common/ai_types.h"
#include "ai/rag/rag_indexer.h"

#include <assert.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

class FakeEmbeddingClient : public ai::rag::EmbeddingClient
{
  public:
    virtual bool Embed(const std::string& input, std::vector<float>& embedding, std::string& error) const override
    {
        (void)error;
        embedding.clear();
        embedding.push_back(1.0f);
        embedding.push_back(static_cast<float>(input.size() % 97));
        embedding.push_back(3.0f);
        return true;
    }
};

class FakeVectorStore : public ai::rag::VectorStore
{
  public:
    virtual bool EnsureCollection(size_t vector_size, std::string& error) override
    {
        (void)error;
        ++ensure_calls;
        last_vector_size = vector_size;
        return true;
    }

    virtual bool Upsert(const std::vector<ai::rag::VectorPoint>& points, std::string& error) override
    {
        (void)error;
        upsert_calls.push_back(points);
        return true;
    }

    virtual bool Search(const std::string& sid,
                        const std::vector<float>& query,
                        size_t top_k,
                        double score_threshold,
                        std::vector<ai::rag::SearchHit>& out,
                        std::string& error) override
    {
        (void)sid;
        (void)query;
        (void)top_k;
        (void)score_threshold;
        (void)out;
        (void)error;
        return true;
    }

    size_t TotalUpsertPoints() const
    {
        size_t total = 0;
        for (size_t i = 0; i < upsert_calls.size(); ++i)
        {
            total += upsert_calls[i].size();
        }
        return total;
    }

    bool HasContentSubstring(const std::string& needle) const
    {
        for (size_t i = 0; i < upsert_calls.size(); ++i)
        {
            for (size_t j = 0; j < upsert_calls[i].size(); ++j)
            {
                if (upsert_calls[i][j].payload.content.find(needle) != std::string::npos)
                {
                    return true;
                }
            }
        }
        return false;
    }

  public:
    size_t ensure_calls = 0;
    size_t last_vector_size = 0;
    std::vector<std::vector<ai::rag::VectorPoint>> upsert_calls;
};

ai::common::PersistMessage MakeMessage(const std::string& sid,
                                       const std::string& conv,
                                       const std::string& role,
                                       const std::string& content)
{
    ai::common::PersistMessage msg;
    msg.sid = sid;
    msg.conversation_id = conv;
    msg.role = role;
    msg.content = content;
    msg.created_at_ms = 1;
    return msg;
}

void TestFactLikeAssistantAndDedup()
{
    ai::rag::RagIndexerSettings settings;
    settings.queue_capacity = 64;
    settings.batch_size = 16;
    settings.flush_interval_ms = 10;
    settings.assistant_index_mode = "fact_like";
    settings.assistant_min_chars = 12;
    settings.dedup_ttl_ms = 600000;
    settings.dedup_max_entries = 1024;

    ai::rag::EmbeddingClient::ptr embedding(new FakeEmbeddingClient());
    std::shared_ptr<FakeVectorStore> vector_store(new FakeVectorStore());
    ai::rag::RagIndexer indexer(embedding, vector_store, settings);

    std::string error;
    assert(indexer.Start(error));

    assert(indexer.Enqueue(MakeMessage("u:1", "conv-a", "user", "我最喜欢的编程语言是C++"), error));
    assert(indexer.Enqueue(MakeMessage("u:1", "conv-a", "user", "我最喜欢的编程语言是C++"), error)); // duplicate
    assert(indexer.Enqueue(MakeMessage("u:1", "conv-a", "assistant", "很高兴为你服务，有什么我可以帮助你的吗？"), error)); // noise
    assert(indexer.Enqueue(MakeMessage("u:1", "conv-a", "assistant", "MySQL 默认端口是 3306，连接池上限可以设置为 8。"), error)); // useful

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    indexer.Stop();

    assert(vector_store->ensure_calls >= 1);
    assert(vector_store->last_vector_size == 3);
    assert(vector_store->TotalUpsertPoints() == 2);
    assert(vector_store->HasContentSubstring("我最喜欢的编程语言是C++"));
    assert(vector_store->HasContentSubstring("3306"));
    assert(!vector_store->HasContentSubstring("很高兴为你服务"));
}

void TestAssistantModeAll()
{
    ai::rag::RagIndexerSettings settings;
    settings.queue_capacity = 16;
    settings.batch_size = 8;
    settings.flush_interval_ms = 10;
    settings.assistant_index_mode = "all";
    settings.assistant_min_chars = 12;
    settings.dedup_ttl_ms = 0;
    settings.dedup_max_entries = 0;

    ai::rag::EmbeddingClient::ptr embedding(new FakeEmbeddingClient());
    std::shared_ptr<FakeVectorStore> vector_store(new FakeVectorStore());
    ai::rag::RagIndexer indexer(embedding, vector_store, settings);

    std::string error;
    assert(indexer.Start(error));
    assert(indexer.Enqueue(MakeMessage("u:2", "conv-b", "assistant", "很高兴为你服务，有什么我可以帮助你的吗？"), error));

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    indexer.Stop();

    assert(vector_store->TotalUpsertPoints() == 1);
}

int main()
{
    TestFactLikeAssistantAndDedup();
    TestAssistantModeAll();
    std::cout << "test_rag_indexer ok\n";
    return 0;
}
