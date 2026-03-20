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
 */
class LlmClientRegistry
{
  public:
    typedef std::shared_ptr<LlmClientRegistry> ptr;

  public:
    bool Register(const LlmProviderEntry& entry, std::string& error);

    bool Find(const std::string& provider_id, LlmProviderEntry& out) const;

    size_t Size() const;

  private:
    std::unordered_map<std::string, LlmProviderEntry> m_entries;
};

} // namespace llm
} // namespace ai

#endif
