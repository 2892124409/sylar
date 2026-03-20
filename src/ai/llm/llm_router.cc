#include "ai/llm/llm_router.h"

namespace ai
{
namespace llm
{

/**
 * @brief 构造路由器。
 */
LlmRouter::LlmRouter(const LlmClientRegistry::ptr& registry,
                     const std::string& default_provider_id,
                     const std::unordered_map<std::string, std::string>& model_to_provider)
    : m_registry(registry)
    , m_default_provider_id(default_provider_id)
    , m_model_to_provider(model_to_provider)
{
}

/**
 * @brief 按优先级执行 Provider/Model 路由。
 * @details
 * 决策顺序：
 * 1) requested_provider_id
 * 2) model_to_provider[requested_model]
 * 3) default_provider_id
 *
 * 选定 provider 后，再确定最终 model：
 * - 若请求带 model，用请求值；
 * - 否则用 provider.default_model。
 */
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
        // 若请求未显式指定 provider，尝试按 model 映射自动选 provider。
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

/**
 * @brief 直接解析 provider_id 对应条目。
 */
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
