#ifndef __SYLAR_AI_LLM_API_KEY_PROVIDER_H__
#define __SYLAR_AI_LLM_API_KEY_PROVIDER_H__

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

namespace ai
{
namespace llm
{

/**
 * @brief 单个 API Key 候选项。
 */
struct ApiKeyCandidate
{
    /** @brief Key 记录 ID（0 表示非池化兜底 key）。 */
    uint64_t id = 0;
    /** @brief 实际 API Key。 */
    std::string api_key;
    /** @brief 优先级（数值越大优先级越高）。 */
    int priority = 0;
    /** @brief 权重（用于同优先级内轮询分配）。 */
    int weight = 1;
};

/**
 * @brief 请求失败类型（用于 key 池冷却策略）。
 */
enum class ApiKeyFailureType
{
    NETWORK_ERROR,
    RATE_LIMIT,
    AUTH_ERROR,
    SERVER_ERROR,
    OTHER_ERROR,
};

/**
 * @brief 协议无关 API Key 提供者抽象。
 */
class ApiKeyProvider
{
  public:
    typedef std::shared_ptr<ApiKeyProvider> ptr;
    virtual ~ApiKeyProvider() {}

    /**
     * @brief 获取本次请求的 key 候选序列。
     * @param max_candidates 最多返回候选数。
     */
    virtual std::vector<ApiKeyCandidate> AcquireCandidates(size_t max_candidates) = 0;

    /**
     * @brief 上报 key 使用成功。
     */
    virtual void ReportSuccess(uint64_t key_id) = 0;

    /**
     * @brief 上报 key 使用失败。
     */
    virtual void ReportFailure(uint64_t key_id, ApiKeyFailureType type) = 0;
};

} // namespace llm
} // namespace ai

#endif
