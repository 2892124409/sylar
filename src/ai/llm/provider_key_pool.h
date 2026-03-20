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
 * @details
 * 该类是 `ApiKeyProvider` 的默认实现，以 `provider_id` 为隔离维度管理 key：
 * - 启动时首轮加载 DB key；
 * - 后台线程周期热加载，感知启停与配置变化；
 * - 请求期按 priority/weight 产出候选；
 * - 失败时按错误类型冷却并回写 DB。
 *
 * 并发模型：
 * - `AcquireCandidates/ReportSuccess/ReportFailure` 可并发调用；
 * - 通过互斥锁保护 `m_keys` 与轮询游标。
 */
class ProviderKeyPool : public ApiKeyProvider
{
  public:
    typedef std::shared_ptr<ProviderKeyPool> ptr;

  public:
    /**
     * @brief 构造 Provider 级 Key 池。
     * @param repository Key 池仓库（DB 访问）。
     * @param settings Key 池运行参数（冷却、重载周期、重试预算等）。
     * @param provider_id 绑定的 Provider 实例 ID。
     */
    ProviderKeyPool(const storage::ApiKeyPoolRepository::ptr& repository,
                    const config::ApiKeyPoolSettings& settings,
                    const std::string& provider_id);
    ~ProviderKeyPool();

    /**
     * @brief 启动 Key 池。
     * @details
     * 启动流程：
     * 1) 首次同步加载，确保启动后立刻可用；
     * 2) 启动后台重载线程，按间隔刷新 key 列表。
     */
    bool Start(std::string& error);
    /**
     * @brief 停止 Key 池并回收后台线程。
     */
    void Stop();

    /**
     * @brief 获取本次请求的候选 key 列表。
     */
    virtual std::vector<ApiKeyCandidate> AcquireCandidates(size_t max_candidates) override;
    /**
     * @brief 上报 key 请求成功，清理其冷却状态。
     */
    virtual void ReportSuccess(uint64_t key_id) override;
    /**
     * @brief 上报 key 请求失败，按失败类型设置冷却并回写 DB。
     */
    virtual void ReportFailure(uint64_t key_id, ApiKeyFailureType type) override;

  private:
    /**
     * @brief 运行时 key 快照。
     * @details
     * 与 DB 记录相比，多了“内存态冷却时间”字段，用于请求期快速过滤。
     */
    struct RuntimeKey
    {
        uint64_t id = 0;
        std::string api_key;
        int priority = 0;
        int weight = 1;
        uint64_t cooldown_until_ms = 0;
    };

  private:
    /**
     * @brief 后台重载线程主循环。
     */
    void ReloadLoop();
    /**
     * @brief 执行一次 DB -> 内存 key 快照刷新。
     * @details
     * 会保留旧快照中更长的冷却截止时间，避免因 DB 延迟回写导致冷却倒退。
     */
    bool ReloadOnce(std::string& error);
    /**
     * @brief 获取当前时间戳（毫秒）。
     */
    uint64_t NowMs() const;
    /**
     * @brief 按失败类型返回冷却时长。
     */
    uint64_t GetCooldownMs(ApiKeyFailureType type) const;
    /**
     * @brief 在内存快照中标记某 key 失败冷却。
     */
    void MarkFailureInMemory(uint64_t key_id, uint64_t cooldown_until_ms);
    /**
     * @brief 在持锁状态下构建候选 key 序列。
     * @details
     * 选择策略：
     * - 先按 priority 分组（高优先级优先）；
     * - 组内按 weight 扩展调度表；
     * - 组内通过游标做轮转，避免总是命中同一 key。
     */
    std::vector<ApiKeyCandidate> BuildCandidatesLocked(size_t max_candidates, uint64_t now_ms);

  private:
    /** @brief DB 仓库依赖。 */
    storage::ApiKeyPoolRepository::ptr m_repository;
    /** @brief Key 池配置。 */
    config::ApiKeyPoolSettings m_settings;
    /** @brief 绑定 Provider ID。 */
    std::string m_provider_id;

    /** @brief 是否处于运行态。 */
    bool m_running;
    /** @brief 后台重载线程。 */
    std::thread m_reload_thread;
    /** @brief 运行时状态互斥锁。 */
    std::mutex m_mutex;
    /** @brief 当前可用 key 快照。 */
    std::vector<RuntimeKey> m_keys;
    /** @brief 各优先级组的轮询游标。 */
    std::unordered_map<int, size_t> m_priority_rr_cursor;
};

} // namespace llm
} // namespace ai

#endif
