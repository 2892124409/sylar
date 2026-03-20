#include "ai/llm/provider_key_pool.h"

#include "log/logger.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>

namespace ai
{
namespace llm
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

/**
 * @brief 构造 ProviderKeyPool。
 */
ProviderKeyPool::ProviderKeyPool(const storage::ApiKeyPoolRepository::ptr& repository,
                                 const config::ApiKeyPoolSettings& settings,
                                 const std::string& provider_id)
    : m_repository(repository)
    , m_settings(settings)
    , m_provider_id(provider_id)
    , m_running(false)
{
}

/**
 * @brief 析构时确保后台线程退出。
 */
ProviderKeyPool::~ProviderKeyPool()
{
    Stop();
}

/**
 * @brief 启动 Key 池。
 * @details
 * 必须先完成一次同步 Reload，确保 `Start` 返回后即可获取候选 key。
 */
bool ProviderKeyPool::Start(std::string& error)
{
    if (m_running)
    {
        return true;
    }
    if (!m_repository)
    {
        error = "api key pool repository is null";
        return false;
    }
    if (!ReloadOnce(error))
    {
        return false;
    }

    m_running = true;
    m_reload_thread = std::thread(&ProviderKeyPool::ReloadLoop, this);
    return true;
}

/**
 * @brief 停止 Key 池并等待重载线程退出。
 */
void ProviderKeyPool::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running)
        {
            return;
        }
        m_running = false;
    }
    if (m_reload_thread.joinable())
    {
        m_reload_thread.join();
    }
}

/**
 * @brief 获取候选 key 序列。
 * @param max_candidates 最多返回候选数量。
 * @return 候选 key 列表。
 */
std::vector<ApiKeyCandidate> ProviderKeyPool::AcquireCandidates(size_t max_candidates)
{
    if (max_candidates == 0)
    {
        return std::vector<ApiKeyCandidate>();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const uint64_t now_ms = NowMs();
    return BuildCandidatesLocked(max_candidates, now_ms);
}

/**
 * @brief 上报 key 成功。
 * @details
 * 成功后会：
 * 1) 清空内存快照中的冷却截止时间；
 * 2) 回写 DB（重置 fail_count/last_error/cooldown）。
 */
void ProviderKeyPool::ReportSuccess(uint64_t key_id)
{
    if (key_id == 0)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < m_keys.size(); ++i)
        {
            if (m_keys[i].id == key_id)
            {
                m_keys[i].cooldown_until_ms = 0;
                break;
            }
        }
    }

    std::string error;
    if (!m_repository->MarkKeySuccess(key_id, NowMs(), error))
    {
        BASE_LOG_WARN(g_logger) << "mark key success failed, key_id=" << key_id
                                << " error=" << error;
    }
}

/**
 * @brief 上报 key 失败并设置冷却。
 * @param key_id 失败 key ID。
 * @param type 失败类型（决定短冷却/长冷却）。
 */
void ProviderKeyPool::ReportFailure(uint64_t key_id, ApiKeyFailureType type)
{
    if (key_id == 0)
    {
        return;
    }

    const uint64_t now_ms = NowMs();
    const uint64_t cooldown_until_ms = now_ms + GetCooldownMs(type);
    MarkFailureInMemory(key_id, cooldown_until_ms);

    std::string error_code = "other";
    if (type == ApiKeyFailureType::NETWORK_ERROR)
    {
        error_code = "network";
    }
    else if (type == ApiKeyFailureType::RATE_LIMIT)
    {
        error_code = "rate_limit";
    }
    else if (type == ApiKeyFailureType::AUTH_ERROR)
    {
        error_code = "auth";
    }
    else if (type == ApiKeyFailureType::SERVER_ERROR)
    {
        error_code = "server";
    }

    std::string error;
    if (!m_repository->MarkKeyFailure(key_id, error_code, cooldown_until_ms, now_ms, error))
    {
        BASE_LOG_WARN(g_logger) << "mark key failure failed, key_id=" << key_id
                                << " error=" << error;
    }
}

/**
 * @brief 后台重载循环。
 * @details
 * 每个周期从 DB 重新加载启用 key，并替换内存快照；
 * 若加载失败仅记录告警，不终止线程。
 */
void ProviderKeyPool::ReloadLoop()
{
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running)
            {
                break;
            }
        }

        std::string error;
        if (!ReloadOnce(error))
        {
            BASE_LOG_WARN(g_logger) << "reload provider key pool failed, provider_id="
                                    << m_provider_id << " error=" << error;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(m_settings.reload_interval_ms));
    }
}

/**
 * @brief 执行一次 key 刷新。
 * @details
 * 刷新策略说明：
 * - 新快照来源于 DB 当前启用记录；
 * - 若旧快照里某 key 冷却更长，则保留更长冷却，防止冷却倒退。
 */
bool ProviderKeyPool::ReloadOnce(std::string& error)
{
    std::vector<storage::ApiKeyPoolRepository::ApiKeyRecord> records;
    if (!m_repository->LoadEnabledKeys(m_provider_id, records, error))
    {
        return false;
    }

    std::unordered_map<uint64_t, uint64_t> old_cooldown;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < m_keys.size(); ++i)
        {
            old_cooldown[m_keys[i].id] = m_keys[i].cooldown_until_ms;
        }
    }

    std::vector<RuntimeKey> next_keys;
    next_keys.reserve(records.size());
    for (size_t i = 0; i < records.size(); ++i)
    {
        RuntimeKey key;
        key.id = records[i].id;
        key.api_key = records[i].api_key;
        key.priority = records[i].priority;
        key.weight = std::max(1, records[i].weight);
        key.cooldown_until_ms = records[i].cooldown_until_ms;
        std::unordered_map<uint64_t, uint64_t>::const_iterator it = old_cooldown.find(key.id);
        if (it != old_cooldown.end() && it->second > key.cooldown_until_ms)
        {
            key.cooldown_until_ms = it->second;
        }
        next_keys.push_back(key);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_keys.swap(next_keys);
    return true;
}

/**
 * @brief 获取当前时间（毫秒）。
 */
uint64_t ProviderKeyPool::NowMs() const
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

/**
 * @brief 按失败类型映射冷却时长。
 * @details
 * 认证类错误通常表示 key 失效或权限问题，使用长冷却；
 * 其他错误默认使用短冷却。
 */
uint64_t ProviderKeyPool::GetCooldownMs(ApiKeyFailureType type) const
{
    if (type == ApiKeyFailureType::AUTH_ERROR)
    {
        return m_settings.cooldown_long_ms;
    }
    return m_settings.cooldown_short_ms;
}

/**
 * @brief 在内存快照中标记失败冷却。
 * @note 使用 `max(old, new)` 避免并发/重复上报导致冷却回退。
 */
void ProviderKeyPool::MarkFailureInMemory(uint64_t key_id, uint64_t cooldown_until_ms)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (size_t i = 0; i < m_keys.size(); ++i)
    {
        if (m_keys[i].id == key_id)
        {
            m_keys[i].cooldown_until_ms = std::max(m_keys[i].cooldown_until_ms, cooldown_until_ms);
            break;
        }
    }
}

/**
 * @brief 构建本次请求候选 key（持锁调用）。
 * @param max_candidates 候选上限。
 * @param now_ms 当前毫秒时间。
 * @return 可尝试候选序列。
 * @details
 * 算法分三步：
 * 1) 过滤空 key 与冷却中 key；
 * 2) 按 priority 分组（高优先级优先）；
 * 3) 组内基于 weight 扩展调度表，并按游标轮转取样，避免热点 key 固化。
 */
std::vector<ApiKeyCandidate> ProviderKeyPool::BuildCandidatesLocked(size_t max_candidates, uint64_t now_ms)
{
    std::map<int, std::vector<size_t>, std::greater<int> > groups;
    for (size_t i = 0; i < m_keys.size(); ++i)
    {
        if (m_keys[i].api_key.empty())
        {
            continue;
        }
        if (m_keys[i].cooldown_until_ms > now_ms)
        {
            continue;
        }
        groups[m_keys[i].priority].push_back(i);
    }

    std::vector<ApiKeyCandidate> out;
    out.reserve(max_candidates);
    std::set<uint64_t> selected_ids;

    for (std::map<int, std::vector<size_t>, std::greater<int> >::iterator git = groups.begin();
         git != groups.end();
         ++git)
    {
        const int priority = git->first;
        const std::vector<size_t>& indexes = git->second;
        if (indexes.empty())
        {
            continue;
        }

        std::vector<size_t> schedule;
        for (size_t i = 0; i < indexes.size(); ++i)
        {
            const RuntimeKey& key = m_keys[indexes[i]];
            // 通过重复下标实现权重：weight 越大，在调度表里出现次数越多。
            for (int w = 0; w < std::max(1, key.weight); ++w)
            {
                schedule.push_back(indexes[i]);
            }
        }
        if (schedule.empty())
        {
            continue;
        }

        size_t& cursor = m_priority_rr_cursor[priority];
        const size_t start = cursor % schedule.size();
        // 从上次游标位置开始轮询，提升同优先级 key 的负载均衡效果。
        for (size_t i = 0; i < schedule.size() && out.size() < max_candidates; ++i)
        {
            const RuntimeKey& key = m_keys[schedule[(start + i) % schedule.size()]];
            if (selected_ids.insert(key.id).second)
            {
                ApiKeyCandidate candidate;
                candidate.id = key.id;
                candidate.api_key = key.api_key;
                candidate.priority = key.priority;
                candidate.weight = key.weight;
                out.push_back(candidate);
            }
        }
        // 每次调用后游标后移，下一次请求换一个起点。
        cursor = (start + 1) % schedule.size();

        if (out.size() >= max_candidates)
        {
            break;
        }
    }

    return out;
}

} // namespace llm
} // namespace ai
