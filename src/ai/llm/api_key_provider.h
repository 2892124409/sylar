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
 * @details
 * `LlmClient` 在一次请求内可按候选序列逐个尝试（失败切 key 重试），
 * 该结构描述每个候选 key 的元信息。
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
    /** @brief 网络异常（连接/超时/传输失败）。 */
    NETWORK_ERROR,
    /** @brief 限流或配额不足（通常可短暂冷却后重试）。 */
    RATE_LIMIT,
    /** @brief 鉴权失败（通常需要较长冷却或人工介入）。 */
    AUTH_ERROR,
    /** @brief 上游 5xx 服务异常。 */
    SERVER_ERROR,
    /** @brief 其他不可归类错误。 */
    OTHER_ERROR,
};

/**
 * @brief 协议无关 API Key 提供者抽象。
 * @details
 * 该接口把“如何选 key / 如何反馈状态”从具体协议客户端中解耦出来：
 * - OpenAI-Compatible 客户端可复用；
 * - Anthropic 客户端可复用；
 * - 后续新协议客户端（如 Gemini）也可复用。
 */
class ApiKeyProvider
{
  public:
    typedef std::shared_ptr<ApiKeyProvider> ptr;
    virtual ~ApiKeyProvider() {}

    /**
     * @brief 获取本次请求的 key 候选序列。
     * @param max_candidates 最多返回候选数。
     * @return 候选 key 列表（按调用方可直接尝试的顺序排列）。
     */
    virtual std::vector<ApiKeyCandidate> AcquireCandidates(size_t max_candidates) = 0;

    /**
     * @brief 上报 key 使用成功。
     * @param key_id 成功的 key 记录 ID。
     */
    virtual void ReportSuccess(uint64_t key_id) = 0;

    /**
     * @brief 上报 key 使用失败。
     * @param key_id 失败的 key 记录 ID。
     * @param type 失败类型（影响冷却策略）。
     */
    virtual void ReportFailure(uint64_t key_id, ApiKeyFailureType type) = 0;
};

} // namespace llm
} // namespace ai

#endif
