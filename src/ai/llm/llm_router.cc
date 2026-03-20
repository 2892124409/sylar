#include "ai/llm/llm_router.h"

namespace ai
{
namespace llm
{

LlmRouter::LlmRouter(const LlmClientRegistry::ptr& registry,
                     const std::string& default_provider_id,
                     const std::unordered_map<std::string, std::string>& model_to_provider)
    : m_registry(registry)
    , m_default_provider_id(default_provider_id)
    , m_model_to_provider(model_to_provider)
{
}

bool LlmRouter::Route(const std::string& requested_provider_id,
                      const std::string& requested_model,
                      LlmRouteResult& out,
                      std::string& error) const
{
    if (!m_registry)
    {
        error = "llm registry is null";
        return false;
    }

    std::string provider_id = requested_provider_id;
    if (provider_id.empty() && !requested_model.empty())
    {
        std::unordered_map<std::string, std::string>::const_iterator it = m_model_to_provider.find(requested_model);
        if (it != m_model_to_provider.end())
        {
            provider_id = it->second;
        }
    }
    if (provider_id.empty())
    {
        provider_id = m_default_provider_id;
    }
    if (provider_id.empty())
    {
        error = "provider is empty";
        return false;
    }

    LlmProviderEntry entry;
    if (!m_registry->Find(provider_id, entry))
    {
        error = "provider not found: " + provider_id;
        return false;
    }

    const std::string model = requested_model.empty() ? entry.default_model : requested_model;
    if (model.empty())
    {
        error = "model is empty for provider: " + provider_id;
        return false;
    }

    out.provider_id = entry.provider_id;
    out.provider_type = entry.provider_type;
    out.model = model;
    out.client = entry.client;
    return true;
}

bool LlmRouter::ResolveProvider(const std::string& provider_id, LlmProviderEntry& out) const
{
    if (!m_registry)
    {
        return false;
    }
    return m_registry->Find(provider_id, out);
}

} // namespace llm
} // namespace ai
