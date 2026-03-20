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
 * @details
 * 该结构是路由器输出给业务层的“可执行结果”，业务层无需再关心
 * provider 配置细节，只要按该结构调用 `client` 即可。
 */
struct LlmRouteResult
{
    /** @brief 命中的 Provider 实例 ID。 */
    std::string provider_id;
    /** @brief 命中的 Provider 协议类型。 */
    std::string provider_type;
    /** @brief 本次请求最终使用的模型名（可能来自默认模型）。 */
    std::string model;
    /** @brief 对应的客户端实例。 */
    LlmClient::ptr client;
};

/**
 * @brief LLM 路由器（显式 provider > model 映射 > 默认 provider）。
 * @details
 * 路由优先级：
 * 1. 请求显式 `provider_id`；
 * 2. `requested_model` 在 `model_to_provider` 中的映射；
 * 3. `default_provider_id`。
 *
 * 该类只负责“选择目标 Provider/Model”，不负责请求执行。
 */
class LlmRouter
{
  public:
    typedef std::shared_ptr<LlmRouter> ptr;

  public:
    /**
     * @brief 构造路由器。
     * @param registry Provider 注册表。
     * @param default_provider_id 默认 Provider（最终兜底）。
     * @param model_to_provider 模型到 Provider 的显式映射表。
     */
    LlmRouter(const LlmClientRegistry::ptr& registry,
              const std::string& default_provider_id,
              const std::unordered_map<std::string, std::string>& model_to_provider);

    /**
     * @brief 为单次请求执行路由决策。
     * @param requested_provider_id 请求显式 Provider（可空）。
     * @param requested_model 请求显式模型名（可空）。
     * @param[out] out 路由结果。
     * @param[out] error 路由失败原因。
     * @return true 路由成功；false 路由失败。
     */
    bool Route(const std::string& requested_provider_id,
               const std::string& requested_model,
               LlmRouteResult& out,
               std::string& error) const;

    /**
     * @brief 仅按 provider_id 解析 Provider 条目（不做完整路由）。
     * @param provider_id Provider 实例 ID。
     * @param[out] out 命中条目。
     * @return true 命中；false 未命中。
     */
    bool ResolveProvider(const std::string& provider_id, LlmProviderEntry& out) const;

  private:
    /** @brief Provider 注册表（路由查询来源）。 */
    LlmClientRegistry::ptr m_registry;
    /** @brief 默认 Provider ID。 */
    std::string m_default_provider_id;
    /** @brief 模型到 Provider 的静态映射。 */
    std::unordered_map<std::string, std::string> m_model_to_provider;
};

} // namespace llm
} // namespace ai

#endif
