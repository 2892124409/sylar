#ifndef __SYLAR_AI_LLM_ANTHROPIC_CLIENT_H__
#define __SYLAR_AI_LLM_ANTHROPIC_CLIENT_H__

#include "ai/llm/api_key_provider.h"
#include "ai/llm/llm_client.h"

#include <stdint.h>

#include <string>

/**
 * @file anthropic_client.h
 * @brief Anthropic Messages API 客户端实现声明。
 */

namespace ai
{
namespace llm
{

/**
 * @brief Anthropic 客户端配置。
 * @details
 * 对应 `ai.anthropic.*` 配置项，用于构造 Anthropic Messages API 请求。
 */
struct AnthropicSettings
{
    /** @brief API 基础地址，默认 `https://api.anthropic.com`。 */
    std::string base_url = "https://api.anthropic.com";
    /** @brief API Key。 */
    std::string api_key;
    /** @brief 默认模型名。 */
    std::string default_model;
    /** @brief Anthropic API 版本头。 */
    std::string api_version = "2023-06-01";
    /** @brief 连接超时（毫秒）。 */
    uint64_t connect_timeout_ms = 3000;
    /** @brief 请求超时（毫秒）。 */
    uint64_t request_timeout_ms = 120000;
};

/**
 * @brief 基于 Anthropic Messages API 的 LLM 客户端。
 * @details
 * 该实现对外仍暴露统一的 `LlmClient` 接口，内部负责：
 * - 组装 Anthropic 协议请求体与请求头；
 * - 解析同步/流式响应结构；
 * - 把 Anthropic 协议字段映射为统一 `LlmCompletionResult`。
 */
class AnthropicClient : public LlmClient
{
  public:
    /**
     * @brief 构造 Anthropic 客户端。
     * @param settings Anthropic 客户端配置快照。
     * @param key_provider Provider 级 Key 提供者（可选）。
     * @param max_retry_per_request 单请求最大重试次数（不含首次尝试）。
     */
    explicit AnthropicClient(const AnthropicSettings& settings,
                             const ApiKeyProvider::ptr& key_provider = ApiKeyProvider::ptr(),
                             uint32_t max_retry_per_request = 2);

    /**
     * @brief 发起同步补全请求。
     * @param request 统一补全请求对象。
     * @param[out] result 统一补全结果对象。
     * @param[out] error 失败原因描述。
     * @return true 请求成功；false 请求失败。
     */
    virtual bool Complete(const LlmCompletionRequest& request,
                          LlmCompletionResult& result,
                          std::string& error) override;

    /**
     * @brief 发起流式补全请求。
     * @param request 统一补全请求对象。
     * @param on_delta 增量回调，返回 false 时主动中断流。
     * @param[out] result 聚合后的统一补全结果对象。
     * @param[out] error 失败原因描述。
     * @return true 请求成功；false 请求失败或流被中断。
     */
    virtual bool StreamComplete(const LlmCompletionRequest& request,
                                const DeltaCallback& on_delta,
                                LlmCompletionResult& result,
                                std::string& error) override;

  private:
    /**
     * @brief 构建 Anthropic Messages API 地址（`.../v1/messages`）。
     * @return 完整请求 URL。
     */
    std::string BuildMessagesUrl() const;

  private:
    /** @brief Anthropic 客户端配置快照。 */
    AnthropicSettings m_settings;
    /** @brief 动态 key 提供者（可选）。 */
    ApiKeyProvider::ptr m_key_provider;
    /** @brief 单请求最大重试次数（不含首次尝试）。 */
    uint32_t m_max_retry_per_request;
};

} // namespace llm
} // namespace ai

#endif
