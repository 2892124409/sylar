#include "ai/llm/llm_client_factory.h"

#include "ai/llm/anthropic_client.h"
#include "ai/llm/openai_compatible_client.h"

namespace ai
{
namespace llm
{

/**
 * @brief 构建默认工厂并注册当前内置 Provider 构造器。
 * @return 完成默认注册的工厂对象。
 */
LlmClientFactory LlmClientFactory::BuildDefault()
{
    LlmClientFactory factory;

    // OpenAI-Compatible 协议：直接复用配置块并补入 provider 级默认模型。
    factory.Register(
        "openai_compatible",
        [](const config::LlmProviderSettings& provider, const BuildOptions& options, std::string&)
        {
            config::OpenAICompatibleSettings settings = provider.openai_compatible;
            settings.default_model = provider.default_model;
            // 注入 provider 级 Key 提供者与重试预算，实现协议无关的 key 池复用。
            return LlmClient::ptr(new OpenAICompatibleClient(
                settings,
                options.api_key_provider,
                options.max_retry_per_request));
        });

    // Anthropic 协议：把 provider.anthropic 子配置显式映射到 AnthropicSettings。
    factory.Register(
        "anthropic",
        [](const config::LlmProviderSettings& provider, const BuildOptions& options, std::string&)
        {
            AnthropicSettings settings;
            settings.base_url = provider.anthropic.base_url;
            settings.api_key = provider.anthropic.api_key;
            settings.default_model = provider.default_model;
            settings.api_version = provider.anthropic.api_version;
            settings.connect_timeout_ms = provider.anthropic.connect_timeout_ms;
            settings.request_timeout_ms = provider.anthropic.request_timeout_ms;
            return LlmClient::ptr(new AnthropicClient(
                settings,
                options.api_key_provider,
                options.max_retry_per_request));
        });

    return factory;
}

/**
 * @brief 注册 Provider 构造器。
 * @param provider_type Provider 类型标识（例如 openai_compatible）。
 * @param creator 对应的构造器函数。
 * @note 对同一 `provider_type` 再次注册会覆盖旧构造器。
 */
void LlmClientFactory::Register(const std::string& provider_type, const Creator& creator)
{
    if (provider_type.empty() || !creator)
    {
        return;
    }
    m_creators[provider_type] = creator;
}

/**
 * @brief 按配置创建客户端。
 * @param provider Provider 配置。
 * @param options 构造附加参数。
 * @param[out] error 失败信息。
 * @return 构建好的客户端；失败返回空指针。
 */
LlmClient::ptr LlmClientFactory::Create(const config::LlmProviderSettings& provider,const BuildOptions& options,std::string& error) const
{
    std::unordered_map<std::string, Creator>::const_iterator it = m_creators.find(provider.type);
    if (it == m_creators.end())
    {
        error = "unsupported provider type: " + provider.type;
        return LlmClient::ptr();
    }
    return it->second(provider, options, error);
}

} // namespace llm
} // namespace ai
