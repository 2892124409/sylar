#ifndef __SYLAR_AI_LLM_LLM_CLIENT_REGISTRY_H__
#define __SYLAR_AI_LLM_LLM_CLIENT_REGISTRY_H__

#include "ai/llm/llm_client.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace ai
{
namespace llm
{

/**
 * @brief 已装配 Provider 客户端条目。
 * @details
 * 一个条目描述“某个 provider_id 在运行时对应的完整调用信息”：
 * - provider_id：路由主键；
 * - provider_type：协议类型（用于可观测性/调试）；
 * - default_model：当请求未显式指定 model 时的兜底模型；
 * - client：真正执行网络调用的客户端对象。
 */
struct LlmProviderEntry
{
    std::string provider_id;
    std::string provider_type;
    std::string default_model;
    LlmClient::ptr client;
};

/**
 * @brief LLM 客户端注册表（provider_id -> LlmClient）。
 * @details
 * 该类是多 Provider 架构中的运行时只读目录：
 * - 启动期由 main 完成注册；
 * - 请求期由 Router/Service 查询；
 * - 注册阶段会阻止重复 provider_id，避免路由歧义。
 */
class LlmClientRegistry
{
  public:
    typedef std::shared_ptr<LlmClientRegistry> ptr;

  public:
    /**
     * @brief 注册一个 Provider 条目。
     * @param entry Provider 条目。
     * @param[out] error 失败原因。
     * @return true 注册成功；false 注册失败。
     */
    bool Register(const LlmProviderEntry& entry, std::string& error);

    /**
     * @brief 按 provider_id 查找已注册条目。
     * @param provider_id Provider 实例 ID。
     * @param[out] out 命中的条目。
     * @return true 命中；false 未命中。
     */
    bool Find(const std::string& provider_id, LlmProviderEntry& out) const;

    /**
     * @brief 返回当前注册的 Provider 数量。
     */
    size_t Size() const;

  private:
    std::unordered_map<std::string, LlmProviderEntry> m_entries;
};

} // namespace llm
} // namespace ai

#endif
