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

ProviderKeyPool::ProviderKeyPool(const storage::ApiKeyPoolRepository::ptr& repository,
                                 const config::ApiKeyPoolSettings& settings,
                                 const std::string& provider_id)
    : m_repository(repository)
    , m_settings(settings)
    , m_provider_id(provider_id)
    , m_running(false)
{
}

ProviderKeyPool::~ProviderKeyPool()
{
    Stop();
}

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

uint64_t ProviderKeyPool::NowMs() const
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

uint64_t ProviderKeyPool::GetCooldownMs(ApiKeyFailureType type) const
{
    if (type == ApiKeyFailureType::AUTH_ERROR)
    {
        return m_settings.cooldown_long_ms;
    }
    return m_settings.cooldown_short_ms;
}

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
