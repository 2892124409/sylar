#ifndef __SYLAR_AI_LLM_OPENAI_COMPATIBLE_CLIENT_H__
#define __SYLAR_AI_LLM_OPENAI_COMPATIBLE_CLIENT_H__

#include "ai/llm/api_key_provider.h"
#include "ai/config/ai_app_config.h"
#include "ai/llm/llm_client.h"

#include <memory>
#include <string>
#include <vector>

/**
 * @file openai_compatible_client.h
 * @brief OpenAI-Compatible 协议客户端实现声明。
 */

namespace ai
{
namespace llm
{

/**
 * @brief 基于 OpenAI-Compatible 接口的通用 LLM 客户端。
 * @details
 * 当前可直接对接 DeepSeek、OpenAI 兼容网关等实现了 `chat/completions` 的服务。
 */
class OpenAICompatibleClient : public LlmClient
{
  public:
    /**
     * @brief 构造 OpenAI-Compatible 客户端。
     * @details
     * 使用 `ai.openai_compatible.*` 配置中的
     * `base_url/api_key/default_model/timeout` 等通用字段。
     */
    explicit OpenAICompatibleClient(const config::OpenAICompatibleSettings& settings,
                                    const ApiKeyProvider::ptr& key_provider = ApiKeyProvider::ptr(),
                                    uint32_t max_retry_per_request = 2);

    /**
     * @brief 调用同步补全接口。
     */
    virtual bool Complete(const LlmCompletionRequest& request,
                          LlmCompletionResult& result,
                          std::string& error) override;

    /**
     * @brief 调用流式补全接口。
     */
    virtual bool StreamComplete(const LlmCompletionRequest& request,
                                const DeltaCallback& on_delta,
                                LlmCompletionResult& result,
                                std::string& error) override;

  private:
    /**
     * @brief 组装 `chat/completions` 请求地址。
     */
    std::string BuildCompletionsUrl() const;

  private:
    /** @brief OpenAI-Compatible 客户端配置快照。 */
    config::OpenAICompatibleSettings m_settings;
    /** @brief 动态 key 提供者（可选）。 */
    ApiKeyProvider::ptr m_key_provider;
    /** @brief 单请求最大重试次数（不含首次尝试）。 */
    uint32_t m_max_retry_per_request;
};

} // namespace llm
} // namespace ai

#endif
