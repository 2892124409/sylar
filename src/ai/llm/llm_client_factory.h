#ifndef __SYLAR_AI_LLM_LLM_CLIENT_FACTORY_H__
#define __SYLAR_AI_LLM_LLM_CLIENT_FACTORY_H__

#include "ai/config/ai_app_config.h"
#include "ai/llm/llm_client.h"
#include "ai/llm/api_key_provider.h"
#include "ai/llm/openai_compatible_client.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace ai
{
namespace llm
{

/**
 * @brief LLM 客户端工厂（按 provider.type 创建具体客户端）。
 */
class LlmClientFactory
{
  public:
    typedef std::shared_ptr<LlmClientFactory> ptr;

    /**
     * @brief Provider 构建附加参数。
     */
    struct BuildOptions
    {
        /** @brief Provider 级 Key 提供者（可选）。 */
        ApiKeyProvider::ptr api_key_provider;
        /** @brief 单请求重试次数（不含首次尝试）。 */
        uint32_t max_retry_per_request = 2;
    };

    /**
     * @brief provider 构造器函数类型。
     */
    typedef std::function<LlmClient::ptr(const config::LlmProviderSettings& provider,
                                         const BuildOptions& options,
                                         std::string& error)>
        Creator;

  public:
    static LlmClientFactory BuildDefault();

    void Register(const std::string& provider_type, const Creator& creator);

    LlmClient::ptr Create(const config::LlmProviderSettings& provider,
                          const BuildOptions& options,
                          std::string& error) const;

  private:
    std::unordered_map<std::string, Creator> m_creators;
};

} // namespace llm
} // namespace ai

#endif
