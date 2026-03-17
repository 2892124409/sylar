#ifndef __SYLAR_AI_LLM_OPENAI_COMPATIBLE_CLIENT_H__
#define __SYLAR_AI_LLM_OPENAI_COMPATIBLE_CLIENT_H__

#include "ai/config/ai_app_config.h"
#include "ai/llm/llm_client.h"

#include <string>

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
    explicit OpenAICompatibleClient(const config::OpenAICompatibleSettings& settings);

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
};

} // namespace llm
} // namespace ai

#endif
