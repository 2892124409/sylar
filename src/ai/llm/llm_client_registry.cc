#include "ai/llm/llm_client_registry.h"

namespace ai
{
namespace llm
{

/**
 * @brief 注册 Provider 条目。
 * @param entry 待注册条目。
 * @param[out] error 失败信息。
 * @return true 成功；false 失败。
 */
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

/**
 * @brief 按 provider_id 查找条目。
 * @param provider_id 目标 Provider ID。
 * @param[out] out 命中条目输出。
 * @return true 命中；false 未命中。
 */
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

/**
 * @brief 返回注册表大小。
 */
size_t LlmClientRegistry::Size() const
{
    return m_entries.size();
}

} // namespace llm
} // namespace ai
