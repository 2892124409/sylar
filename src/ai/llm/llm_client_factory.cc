#include "ai/llm/llm_client_factory.h"

#include "ai/llm/anthropic_client.h"
#include "ai/llm/openai_compatible_client.h"

namespace ai
{
namespace llm
{

LlmClientFactory LlmClientFactory::BuildDefault()
{
    LlmClientFactory factory;

    factory.Register(
        "openai_compatible",
        [](const config::LlmProviderSettings& provider, const BuildOptions& options, std::string&)
        {
            config::OpenAICompatibleSettings settings = provider.openai_compatible;
            settings.default_model = provider.default_model;
            return LlmClient::ptr(new OpenAICompatibleClient(
                settings,
                options.api_key_provider,
                options.max_retry_per_request));
        });

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

void LlmClientFactory::Register(const std::string& provider_type, const Creator& creator)
{
    if (provider_type.empty() || !creator)
    {
        return;
    }
    m_creators[provider_type] = creator;
}

LlmClient::ptr LlmClientFactory::Create(const config::LlmProviderSettings& provider,
                                        const BuildOptions& options,
                                        std::string& error) const
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
