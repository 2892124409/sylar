#ifndef __SYLAR_AI_LLM_ANTHROPIC_CLIENT_H__
#define __SYLAR_AI_LLM_ANTHROPIC_CLIENT_H__

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
 */
class AnthropicClient : public LlmClient
{
  public:
    explicit AnthropicClient(const AnthropicSettings& settings);

    virtual bool Complete(const LlmCompletionRequest& request,
                          LlmCompletionResult& result,
                          std::string& error) override;

    virtual bool StreamComplete(const LlmCompletionRequest& request,
                                const DeltaCallback& on_delta,
                                LlmCompletionResult& result,
                                std::string& error) override;

  private:
    std::string BuildMessagesUrl() const;

  private:
    AnthropicSettings m_settings;
};

} // namespace llm
} // namespace ai

#endif
