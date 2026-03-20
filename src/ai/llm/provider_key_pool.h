#ifndef __SYLAR_AI_LLM_PROVIDER_KEY_POOL_H__
#define __SYLAR_AI_LLM_PROVIDER_KEY_POOL_H__

#include "ai/config/ai_app_config.h"
#include "ai/llm/api_key_provider.h"
#include "ai/storage/api_key_pool_repository.h"

#include <stdint.h>

#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ai
{
namespace llm
{

/**
 * @brief Provider 级 API Key 池（DB 热加载 + 运行时选择 + 冷却）。
 */
class ProviderKeyPool : public ApiKeyProvider
{
  public:
    typedef std::shared_ptr<ProviderKeyPool> ptr;

  public:
    ProviderKeyPool(const storage::ApiKeyPoolRepository::ptr& repository,
                    const config::ApiKeyPoolSettings& settings,
                    const std::string& provider_id);
    ~ProviderKeyPool();

    bool Start(std::string& error);
    void Stop();

    virtual std::vector<ApiKeyCandidate> AcquireCandidates(size_t max_candidates) override;
    virtual void ReportSuccess(uint64_t key_id) override;
    virtual void ReportFailure(uint64_t key_id, ApiKeyFailureType type) override;

  private:
    struct RuntimeKey
    {
        uint64_t id = 0;
        std::string api_key;
        int priority = 0;
        int weight = 1;
        uint64_t cooldown_until_ms = 0;
    };

  private:
    void ReloadLoop();
    bool ReloadOnce(std::string& error);
    uint64_t NowMs() const;
    uint64_t GetCooldownMs(ApiKeyFailureType type) const;
    void MarkFailureInMemory(uint64_t key_id, uint64_t cooldown_until_ms);
    std::vector<ApiKeyCandidate> BuildCandidatesLocked(size_t max_candidates, uint64_t now_ms);

  private:
    storage::ApiKeyPoolRepository::ptr m_repository;
    config::ApiKeyPoolSettings m_settings;
    std::string m_provider_id;

    bool m_running;
    std::thread m_reload_thread;
    std::mutex m_mutex;
    std::vector<RuntimeKey> m_keys;
    std::unordered_map<int, size_t> m_priority_rr_cursor;
};

} // namespace llm
} // namespace ai

#endif
