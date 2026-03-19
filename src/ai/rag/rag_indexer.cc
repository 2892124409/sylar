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

/**
 * @brief 计算字符串的 FNV-1a 64 位哈希。
 * @details 用于生成 point id 与去重键，要求稳定且速度快。
 */
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

/**
 * @brief 获取当前系统时间戳（毫秒）。
 */
uint64_t NowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

/**
 * @brief 文本归一化：trim + 合并连续空白为单空格。
 * @details 主要用于去重和规则判断，减少“格式不同内容相同”的干扰。
 */
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

/**
 * @brief ASCII 小写化（仅对英文字符做 tolower）。
 */
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

/**
 * @brief 判断文本是否包含任一特征子串。
 */
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

/**
 * @brief 判断文本中是否出现数字字符。
 */
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

/**
 * @brief 构造异步索引器并保存依赖/配置。
 */
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

/**
 * @brief 析构时确保后台线程安全退出。
 */
RagIndexer::~RagIndexer()
{
    Stop();
}

/**
 * @brief 启动索引后台线程。
 */
bool RagIndexer::Start(std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // 已启动时保持幂等。
    if (m_running)
    {
        return true;
    }

    // 启动前检查依赖完整性。
    if (!m_embedding_client || !m_vector_store)
    {
        error = "rag indexer dependencies are not initialized";
        return false;
    }

    // 启动前检查关键参数有效性。
    if (m_settings.queue_capacity == 0 ||
        m_settings.batch_size == 0 ||
        m_settings.flush_interval_ms == 0)
    {
        error = "rag indexer settings are invalid";
        return false;
    }

    // 标记运行并拉起后台线程。
    m_running = true;
    m_thread = std::thread(&RagIndexer::Run, this);
    return true;
}

/**
 * @brief 停止后台线程（幂等）。
 */
void RagIndexer::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // 已停止时直接返回。
        if (!m_running)
        {
            return;
        }
        m_running = false;
    }

    // 唤醒可能正在 wait 的工作线程，让其感知停止状态。
    m_cond.notify_all();

    // 等待线程收尾，避免悬挂线程。
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

/**
 * @brief 投递消息到异步索引队列。
 */
bool RagIndexer::Enqueue(const common::PersistMessage& message, std::string& error)
{
    // 按角色与内容策略先做轻量筛选，不符合直接跳过。
    if (!ShouldIndexMessage(message))
    {
        return true;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    // 停止状态下拒绝入队。
    if (!m_running)
    {
        error = "rag indexer is not running";
        return false;
    }

    // 命中去重窗口内重复文本则直接忽略。
    if (ShouldSkipByDedup(message))
    {
        return true;
    }

    // 队列满时返回错误，交由上层做降级或监控。
    if (m_queue.size() >= m_settings.queue_capacity)
    {
        error = "rag index queue full";
        return false;
    }

    // 入队并唤醒消费者线程。
    m_queue.push_back(message);
    m_cond.notify_one();
    return true;
}

/**
 * @brief 工作线程主循环：拉取 batch 并 flush 到向量库。
 */
void RagIndexer::Run()
{
    while (true)
    {
        std::deque<common::PersistMessage> batch;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            // 队列为空且仍在运行时，按 flush 周期等待新消息。
            if (m_queue.empty() && m_running)
            {
                m_cond.wait_for(lock, std::chrono::milliseconds(m_settings.flush_interval_ms));
            }

            // 取出一批数据，减小锁持有时间。
            const size_t fetch_count = std::min(m_queue.size(), m_settings.batch_size);
            for (size_t i = 0; i < fetch_count; ++i)
            {
                batch.push_back(m_queue.front());
                m_queue.pop_front();
            }

            // 停止且无待处理消息时，安全退出循环。
            if (batch.empty() && !m_running)
            {
                break;
            }
        }

        // 当前周期无数据则继续下一轮等待。
        if (batch.empty())
        {
            continue;
        }

        // 失败只记日志，不中断线程，避免单批异常拖垮整个索引服务。
        std::string error;
        if (!FlushBatch(batch, error))
        {
            BASE_LOG_ERROR(g_logger) << "flush rag index batch failed: " << error;
        }
    }
}

/**
 * @brief 把 batch 中消息转 embedding 并批量 upsert 到向量库。
 */
bool RagIndexer::FlushBatch(std::deque<common::PersistMessage>& batch, std::string& error)
{
    std::vector<VectorPoint> points;
    points.reserve(batch.size());

    for (size_t i = 0; i < batch.size(); ++i)
    {
        const common::PersistMessage& message = batch[i];

        // 为每条消息生成 embedding。
        std::vector<float> embedding;
        std::string embed_error;
        if (!m_embedding_client->Embed(message.content, embedding, embed_error))
        {
            BASE_LOG_WARN(g_logger) << "embedding message failed sid=" << message.sid
                                    << " conv=" << message.conversation_id
                                    << " error=" << embed_error;
            continue;
        }

        // 空向量视为无效结果，直接跳过。
        if (embedding.empty())
        {
            continue;
        }

        // 首次成功 embedding 时，按维度创建/校验集合并缓存维度。
        if (m_vector_size == 0)
        {
            if (!m_vector_store->EnsureCollection(embedding.size(), error))
            {
                return false;
            }
            m_vector_size = embedding.size();
        }

        // 维度不一致说明模型/集合不匹配，跳过该点并记录告警。
        if (embedding.size() != m_vector_size)
        {
            BASE_LOG_WARN(g_logger) << "skip rag index point due to vector size mismatch expected="
                                    << m_vector_size << " actual=" << embedding.size();
            continue;
        }

        // 组装向量点（id + vector + payload）。
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

    // 整批都被过滤/失败时，按成功处理避免上层反复重试。
    if (points.empty())
    {
        return true;
    }

    // 批量写入向量库。
    return m_vector_store->Upsert(points, error);
}

/**
 * @brief 根据消息关键字段生成稳定 point id。
 * @details 使用 `\0` 作为字段分隔，避免拼接歧义。
 */
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

/**
 * @brief 判断消息是否应该进入索引流程。
 */
bool RagIndexer::ShouldIndexMessage(const common::PersistMessage& message) const
{
    // 空文本不入索引。
    if (message.content.empty())
    {
        return false;
    }

    // 用户消息默认全部入索引。
    if (message.role == "user")
    {
        return true;
    }

    // 非 assistant 且非 user 的角色暂不入索引。
    if (message.role != "assistant")
    {
        return false;
    }

    // assistant 策略：none 直接禁用。
    if (m_settings.assistant_index_mode == "none")
    {
        return false;
    }
    // assistant 策略：all 全量入索引。
    if (m_settings.assistant_index_mode == "all")
    {
        return true;
    }

    // 默认 fact_like：只保留有“信息密度”的回复。
    return IsAssistantMessageUseful(message.content);
}

/**
 * @brief 评估 assistant 消息是否具备“事实/配置/步骤”信息密度。
 */
bool RagIndexer::IsAssistantMessageUseful(const std::string& content) const
{
    // 先归一化空白，减少格式干扰。
    std::string normalized = TrimAndCollapseSpace(content);
    // 长度过短通常为寒暄或低信息文本。
    if (normalized.size() < m_settings.assistant_min_chars)
    {
        return false;
    }

    const std::string lower = ToLowerAscii(normalized);

    // 常见寒暄模板词：命中则判定为噪声。
    static const std::vector<std::string> kNoisePhrases = {
        "很高兴", "有什么我可以帮助", "如果你需要", "随时告诉我", "欢迎继续提问",
        "glad to help", "anything i can help", "feel free to ask", "happy to help"};
    if (ContainsAny(lower, kNoisePhrases))
    {
        return false;
    }

    // 事实信号词：命中则优先保留。
    static const std::vector<std::string> kFactSignals = {
        "http://", "https://", "mysql", "qdrant", "ollama", "curl", "token",
        "配置", "参数", "端口", "路径", "模型", "版本", "超时", "错误", "步骤",
        "```", "c++", "api", "sql"};
    if (ContainsAny(lower, kFactSignals))
    {
        return true;
    }

    // 含数字往往代表版本/端口/步骤/参数，也倾向保留。
    if (ContainsDigit(lower))
    {
        return true;
    }

    // 含列表结构（- / 1.）通常是说明性内容，倾向保留。
    if (lower.find("- ") != std::string::npos || lower.find("1.") != std::string::npos)
    {
        return true;
    }

    // 其余文本按低信息处理。
    return false;
}

/**
 * @brief 文本去重判断：在 TTL 窗口内重复内容不再重复入索引。
 */
bool RagIndexer::ShouldSkipByDedup(const common::PersistMessage& message)
{
    // 任一参数关闭时，直接禁用去重。
    if (m_settings.dedup_ttl_ms == 0 || m_settings.dedup_max_entries == 0)
    {
        return false;
    }

    // 先清理过期/失效条目，控制缓存体积与命中准确性。
    const uint64_t now_ms = NowMs();
    PruneDedupCache(now_ms);

    // 构造归一化文本键，减少空白/大小写差异导致的重复误判。
    const std::string normalized = TrimAndCollapseSpace(ToLowerAscii(message.content));
    if (normalized.empty())
    {
        return false;
    }

    // 去重维度：同一 sid 下的相同文本。
    std::string key;
    key.reserve(message.sid.size() + normalized.size() + 2);
    key.append(message.sid);
    key.push_back('\0');
    key.append(normalized);

    const uint64_t hash = Fnv1a64(key);
    std::unordered_map<uint64_t, uint64_t>::iterator it = m_dedup_last_seen_ms.find(hash);
    // TTL 窗口内重复命中，跳过本次索引。
    if (it != m_dedup_last_seen_ms.end() && now_ms - it->second <= m_settings.dedup_ttl_ms)
    {
        return true;
    }

    // 记录本次出现时间，并维护顺序队列用于后续淘汰。
    m_dedup_last_seen_ms[hash] = now_ms;
    m_dedup_order.push_back(std::make_pair(hash, now_ms));

    // 超过最大容量时，按时间顺序淘汰最旧条目。
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

/**
 * @brief 清理去重缓存中已过期或失效的队首条目。
 */
void RagIndexer::PruneDedupCache(uint64_t now_ms)
{
    while (!m_dedup_order.empty())
    {
        const std::pair<uint64_t, uint64_t>& front = m_dedup_order.front();
        std::unordered_map<uint64_t, uint64_t>::iterator fit = m_dedup_last_seen_ms.find(front.first);

        // 队首若已被更新或删除，直接丢弃旧记录。
        if (fit == m_dedup_last_seen_ms.end() || fit->second != front.second)
        {
            m_dedup_order.pop_front();
            continue;
        }

        // 超过 TTL 的条目删除并继续清理。
        if (now_ms - front.second > m_settings.dedup_ttl_ms)
        {
            m_dedup_last_seen_ms.erase(fit);
            m_dedup_order.pop_front();
            continue;
        }

        // 队首仍有效且未过期，后续条目更“新”，可提前停止。
        break;
    }
}

} // namespace rag
} // namespace ai
