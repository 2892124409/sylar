#include "ai/rag/rag_indexer.h"

#include "log/logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <vector>

namespace ai
{
namespace rag
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

namespace
{

uint64_t Fnv1a64(const std::string& data)
{
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < data.size(); ++i)
    {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t NowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string TrimAndCollapseSpace(const std::string& input)
{
    std::string out;
    out.reserve(input.size());

    bool prev_is_space = true;
    for (size_t i = 0; i < input.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (std::isspace(c))
        {
            if (!prev_is_space)
            {
                out.push_back(' ');
                prev_is_space = true;
            }
            continue;
        }
        out.push_back(static_cast<char>(c));
        prev_is_space = false;
    }

    if (!out.empty() && out[out.size() - 1] == ' ')
    {
        out.resize(out.size() - 1);
    }
    return out;
}

std::string ToLowerAscii(const std::string& input)
{
    std::string out = input;
    for (size_t i = 0; i < out.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(out[i]);
        out[i] = static_cast<char>(std::tolower(c));
    }
    return out;
}

bool ContainsAny(const std::string& text, const std::vector<std::string>& patterns)
{
    for (size_t i = 0; i < patterns.size(); ++i)
    {
        if (!patterns[i].empty() && text.find(patterns[i]) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

bool ContainsDigit(const std::string& text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isdigit(c))
        {
            return true;
        }
    }
    return false;
}

} // namespace

RagIndexer::RagIndexer(const EmbeddingClient::ptr& embedding_client,
                       const VectorStore::ptr& vector_store,
                       const RagIndexerSettings& settings)
    : m_embedding_client(embedding_client)
    , m_vector_store(vector_store)
    , m_settings(settings)
    , m_running(false)
    , m_vector_size(0)
{
}

RagIndexer::~RagIndexer()
{
    Stop();
}

bool RagIndexer::Start(std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running)
    {
        return true;
    }

    if (!m_embedding_client || !m_vector_store)
    {
        error = "rag indexer dependencies are not initialized";
        return false;
    }

    if (m_settings.queue_capacity == 0 ||
        m_settings.batch_size == 0 ||
        m_settings.flush_interval_ms == 0)
    {
        error = "rag indexer settings are invalid";
        return false;
    }

    m_running = true;
    m_thread = std::thread(&RagIndexer::Run, this);
    return true;
}

void RagIndexer::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running)
        {
            return;
        }
        m_running = false;
    }

    m_cond.notify_all();

    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

bool RagIndexer::Enqueue(const common::PersistMessage& message, std::string& error)
{
    if (!ShouldIndexMessage(message))
    {
        return true;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running)
    {
        error = "rag indexer is not running";
        return false;
    }

    if (ShouldSkipByDedup(message))
    {
        return true;
    }

    if (m_queue.size() >= m_settings.queue_capacity)
    {
        error = "rag index queue full";
        return false;
    }

    m_queue.push_back(message);
    m_cond.notify_one();
    return true;
}

void RagIndexer::Run()
{
    while (true)
    {
        std::deque<common::PersistMessage> batch;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_queue.empty() && m_running)
            {
                m_cond.wait_for(lock, std::chrono::milliseconds(m_settings.flush_interval_ms));
            }

            const size_t fetch_count = std::min(m_queue.size(), m_settings.batch_size);
            for (size_t i = 0; i < fetch_count; ++i)
            {
                batch.push_back(m_queue.front());
                m_queue.pop_front();
            }

            if (batch.empty() && !m_running)
            {
                break;
            }
        }

        if (batch.empty())
        {
            continue;
        }

        std::string error;
        if (!FlushBatch(batch, error))
        {
            BASE_LOG_ERROR(g_logger) << "flush rag index batch failed: " << error;
        }
    }
}

bool RagIndexer::FlushBatch(std::deque<common::PersistMessage>& batch, std::string& error)
{
    std::vector<VectorPoint> points;
    points.reserve(batch.size());

    for (size_t i = 0; i < batch.size(); ++i)
    {
        const common::PersistMessage& message = batch[i];

        std::vector<float> embedding;
        std::string embed_error;
        if (!m_embedding_client->Embed(message.content, embedding, embed_error))
        {
            BASE_LOG_WARN(g_logger) << "embedding message failed sid=" << message.sid
                                    << " conv=" << message.conversation_id
                                    << " error=" << embed_error;
            continue;
        }

        if (embedding.empty())
        {
            continue;
        }

        if (m_vector_size == 0)
        {
            if (!m_vector_store->EnsureCollection(embedding.size(), error))
            {
                return false;
            }
            m_vector_size = embedding.size();
        }

        if (embedding.size() != m_vector_size)
        {
            BASE_LOG_WARN(g_logger) << "skip rag index point due to vector size mismatch expected="
                                    << m_vector_size << " actual=" << embedding.size();
            continue;
        }

        VectorPoint point;
        point.id = BuildPointId(message);
        point.vector.swap(embedding);
        point.payload.sid = message.sid;
        point.payload.conversation_id = message.conversation_id;
        point.payload.role = message.role;
        point.payload.content = message.content;
        point.payload.created_at_ms = message.created_at_ms;
        points.push_back(point);
    }

    if (points.empty())
    {
        return true;
    }

    return m_vector_store->Upsert(points, error);
}

uint64_t RagIndexer::BuildPointId(const common::PersistMessage& message) const
{
    std::string raw;
    raw.reserve(message.sid.size() + message.conversation_id.size() + message.role.size() + message.content.size() + 48);
    raw.append(message.sid);
    raw.push_back('\0');
    raw.append(message.conversation_id);
    raw.push_back('\0');
    raw.append(message.role);
    raw.push_back('\0');
    raw.append(std::to_string(message.created_at_ms));
    raw.push_back('\0');
    raw.append(message.content);
    return Fnv1a64(raw);
}

bool RagIndexer::ShouldIndexMessage(const common::PersistMessage& message) const
{
    if (message.content.empty())
    {
        return false;
    }

    if (message.role == "user")
    {
        return true;
    }

    if (message.role != "assistant")
    {
        return false;
    }

    if (m_settings.assistant_index_mode == "none")
    {
        return false;
    }
    if (m_settings.assistant_index_mode == "all")
    {
        return true;
    }

    return IsAssistantMessageUseful(message.content);
}

bool RagIndexer::IsAssistantMessageUseful(const std::string& content) const
{
    std::string normalized = TrimAndCollapseSpace(content);
    if (normalized.size() < m_settings.assistant_min_chars)
    {
        return false;
    }

    const std::string lower = ToLowerAscii(normalized);

    static const std::vector<std::string> kNoisePhrases = {
        "很高兴", "有什么我可以帮助", "如果你需要", "随时告诉我", "欢迎继续提问",
        "glad to help", "anything i can help", "feel free to ask", "happy to help"};
    if (ContainsAny(lower, kNoisePhrases))
    {
        return false;
    }

    static const std::vector<std::string> kFactSignals = {
        "http://", "https://", "mysql", "qdrant", "ollama", "curl", "token",
        "配置", "参数", "端口", "路径", "模型", "版本", "超时", "错误", "步骤",
        "```", "c++", "api", "sql"};
    if (ContainsAny(lower, kFactSignals))
    {
        return true;
    }

    if (ContainsDigit(lower))
    {
        return true;
    }

    if (lower.find("- ") != std::string::npos || lower.find("1.") != std::string::npos)
    {
        return true;
    }

    return false;
}

bool RagIndexer::ShouldSkipByDedup(const common::PersistMessage& message)
{
    if (m_settings.dedup_ttl_ms == 0 || m_settings.dedup_max_entries == 0)
    {
        return false;
    }

    const uint64_t now_ms = NowMs();
    PruneDedupCache(now_ms);

    const std::string normalized = TrimAndCollapseSpace(ToLowerAscii(message.content));
    if (normalized.empty())
    {
        return false;
    }

    std::string key;
    key.reserve(message.sid.size() + normalized.size() + 2);
    key.append(message.sid);
    key.push_back('\0');
    key.append(normalized);

    const uint64_t hash = Fnv1a64(key);
    std::unordered_map<uint64_t, uint64_t>::iterator it = m_dedup_last_seen_ms.find(hash);
    if (it != m_dedup_last_seen_ms.end() && now_ms - it->second <= m_settings.dedup_ttl_ms)
    {
        return true;
    }

    m_dedup_last_seen_ms[hash] = now_ms;
    m_dedup_order.push_back(std::make_pair(hash, now_ms));

    while (m_dedup_last_seen_ms.size() > m_settings.dedup_max_entries && !m_dedup_order.empty())
    {
        const std::pair<uint64_t, uint64_t>& front = m_dedup_order.front();
        std::unordered_map<uint64_t, uint64_t>::iterator fit = m_dedup_last_seen_ms.find(front.first);
        if (fit != m_dedup_last_seen_ms.end() && fit->second == front.second)
        {
            m_dedup_last_seen_ms.erase(fit);
        }
        m_dedup_order.pop_front();
    }

    return false;
}

void RagIndexer::PruneDedupCache(uint64_t now_ms)
{
    while (!m_dedup_order.empty())
    {
        const std::pair<uint64_t, uint64_t>& front = m_dedup_order.front();
        std::unordered_map<uint64_t, uint64_t>::iterator fit = m_dedup_last_seen_ms.find(front.first);

        if (fit == m_dedup_last_seen_ms.end() || fit->second != front.second)
        {
            m_dedup_order.pop_front();
            continue;
        }

        if (now_ms - front.second > m_settings.dedup_ttl_ms)
        {
            m_dedup_last_seen_ms.erase(fit);
            m_dedup_order.pop_front();
            continue;
        }

        break;
    }
}

} // namespace rag
} // namespace ai
