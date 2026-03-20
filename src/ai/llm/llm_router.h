#ifndef __SYLAR_AI_LLM_LLM_ROUTER_H__
#define __SYLAR_AI_LLM_LLM_ROUTER_H__

#include "ai/llm/llm_client_registry.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace ai
{
namespace llm
{

/**
 * @brief 单次请求路由结果。
 */
struct LlmRouteResult
{
    std::string provider_id;
    std::string provider_type;
    std::string model;
    LlmClient::ptr client;
};

/**
 * @brief LLM 路由器（显式 provider > model 映射 > 默认 provider）。
 */
class LlmRouter
{
  public:
    typedef std::shared_ptr<LlmRouter> ptr;

  public:
    LlmRouter(const LlmClientRegistry::ptr& registry,
              const std::string& default_provider_id,
              const std::unordered_map<std::string, std::string>& model_to_provider);

    bool Route(const std::string& requested_provider_id,
               const std::string& requested_model,
               LlmRouteResult& out,
               std::string& error) const;

    bool ResolveProvider(const std::string& provider_id, LlmProviderEntry& out) const;

  private:
    LlmClientRegistry::ptr m_registry;
    std::string m_default_provider_id;
    std::unordered_map<std::string, std::string> m_model_to_provider;
};

} // namespace llm
} // namespace ai

#endif
