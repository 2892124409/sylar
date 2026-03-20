#ifndef __SYLAR_AI_LLM_LLM_CLIENT_FACTORY_H__
#define __SYLAR_AI_LLM_LLM_CLIENT_FACTORY_H__

#include "ai/config/ai_app_config.h"
#include "ai/llm/llm_client.h"
#include "ai/llm/api_key_provider.h"
#include "ai/llm/openai_compatible_client.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace ai
{
namespace llm
{

/**
 * @brief LLM 客户端工厂（按 provider.type 构建具体客户端实例）。
 * @details
 * 该工厂是第五阶段多 Provider 装配链路的“创建入口”，负责把配置层的
 * `LlmProviderSettings` 转换成运行时可调用的 `LlmClient` 对象。
 *
 * 关键点：
 * - 通过 `Register()` 建立 `provider.type -> Creator` 映射；
 * - 通过 `Create()` 在启动期按配置逐个构建客户端；
 * - 通过 `BuildOptions` 注入协议无关的附加能力（如 provider 级 Key 池）。
 */
class LlmClientFactory
{
  public:
    typedef std::shared_ptr<LlmClientFactory> ptr;

    /**
     * @brief Provider 构建附加参数。
     * @details
     * 该结构用于把“与具体协议无关”的能力在构造阶段注入到各类 Client 中，
     * 避免在不同协议实现里重复读取和传递同一类控制参数。
     */
    struct BuildOptions
    {
        /** @brief Provider 级 Key 提供者（可选）。 */
        ApiKeyProvider::ptr api_key_provider;
        /** @brief 单请求重试次数（不含首次尝试）。 */
        uint32_t max_retry_per_request = 2;
    };

    /**
     * @brief Provider 构造器函数签名。
     * @param provider 单个 Provider 的配置快照。
     * @param options 构建附加参数（如 Key 提供者、重试预算）。
     * @param[out] error 构造失败时返回错误信息。
     * @return 构建好的客户端对象；失败返回空指针。
     */
    typedef std::function<LlmClient::ptr(const config::LlmProviderSettings& provider,const BuildOptions& options,std::string& error)> Creator;

  public:
    /**
     * @brief 构建默认工厂并注册内置协议实现。
     * @details
     * 当前默认注册：
     * - `openai_compatible`
     * - `anthropic`
     *
     * 后续新增协议时，通常在该函数中追加 `Register(...)` 条目即可。
     */
    static LlmClientFactory BuildDefault();

    /**
     * @brief 注册某类 Provider 的构造器。
     * @param provider_type Provider 类型名（来自配置 `provider.type`）。
     * @param creator 该类型对应的客户端构造器。
     */
    void Register(const std::string& provider_type, const Creator& creator);

    /**
     * @brief 按 Provider 配置创建客户端实例。
     * @param provider 单个 Provider 的配置。
     * @param options 构建附加参数。
     * @param[out] error 失败信息。
     * @return 构建成功返回客户端指针；失败返回空指针。
     * @note 若 `provider.type` 未注册，会返回 `unsupported provider type`。
     */
    LlmClient::ptr Create(const config::LlmProviderSettings& provider,
                          const BuildOptions& options,
                          std::string& error) const;

  private:
    std::unordered_map<std::string, Creator> m_creators;
};

} // namespace llm
} // namespace ai

#endif
