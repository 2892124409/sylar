#include "ai/llm/llm_client_registry.h"

namespace ai
{
namespace llm
{

bool LlmClientRegistry::Register(const LlmProviderEntry& entry, std::string& error)
{
    if (entry.provider_id.empty())
    {
        error = "provider_id is empty";
        return false;
    }
    if (!entry.client)
    {
        error = "provider client is null, provider_id=" + entry.provider_id;
        return false;
    }
    if (m_entries.find(entry.provider_id) != m_entries.end())
    {
        error = "duplicated provider_id: " + entry.provider_id;
        return false;
    }
    m_entries[entry.provider_id] = entry;
    return true;
}

bool LlmClientRegistry::Find(const std::string& provider_id, LlmProviderEntry& out) const
{
    std::unordered_map<std::string, LlmProviderEntry>::const_iterator it = m_entries.find(provider_id);
    if (it == m_entries.end())
    {
        return false;
    }
    out = it->second;
    return true;
}

size_t LlmClientRegistry::Size() const
{
    return m_entries.size();
}

} // namespace llm
} // namespace ai
