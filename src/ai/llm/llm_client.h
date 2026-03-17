#ifndef __SYLAR_AI_LLM_LLM_CLIENT_H__
#define __SYLAR_AI_LLM_LLM_CLIENT_H__

#include "ai/common/ai_types.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * @file llm_client.h
 * @brief 大模型客户端抽象接口与通用请求/响应结构定义。
 * @details 上层不关心倒是用的是 OpenAI 格式还是 Anthropic 格式的模型 API，上层只调用需要这个基类的两个接口 Complete() 和 StreamComplete().
 */

namespace ai
{
namespace llm
{

/**
 * @brief 大模型补全请求统一结构。
 * @details
 * 由业务层组装，避免业务代码依赖具体厂商协议格式。
 */
struct LlmCompletionRequest
{
    /** @brief 模型名称，例如 `gpt-4o-mini`。 */
    std::string model;
    /** @brief 采样温度，数值越高通常随机性越强。 */
    double temperature = 0.7;
    /** @brief 输出 token 上限。 */
    uint32_t max_tokens = 1024;
    /** @brief 输入消息列表（system/user/assistant）。 */
    std::vector<common::ChatMessage> messages;
};

/**
 * @brief 大模型补全结果统一结构。
 * @details
 * 同步和流式调用最终都回填到该结构，供上层统一处理。
 */
struct LlmCompletionResult
{
    /** @brief 模型输出的完整文本。 */
    std::string content;
    /** @brief 实际响应模型名。 */
    std::string model;
    /** @brief 结束原因，例如 `stop`、`length`。 */
    std::string finish_reason;
    /** @brief 输入 token 数（若上游返回）。 */
    uint64_t prompt_tokens = 0;
    /** @brief 输出 token 数（若上游返回）。 */
    uint64_t completion_tokens = 0;
};

/**
 * @brief 大模型客户端抽象基类。
 * @details
 * - `Complete`：同步一次性返回完整回复。
 * - `StreamComplete`：通过回调返回增量片段，并在结束后回填聚合结果。
 */
class LlmClient
{
  public:
    /** @brief 智能指针别名。 */
    typedef std::shared_ptr<LlmClient> ptr;
    /**
     * @brief 流式增量回调。
     * @param delta 本次新增文本片段。
     * @return true 继续；false 主动中断流式处理。
     */
    typedef std::function<bool(const std::string& delta)> DeltaCallback;

    virtual ~LlmClient() {}

    /**
     * @brief 发起同步补全请求。
     * @param request 请求参数。
     * @param[out] result 成功时返回完整结果。
     * @param[out] error 失败原因。
     * @return true 成功；false 失败。
     */
    virtual bool Complete(const LlmCompletionRequest& request, LlmCompletionResult& result, std::string& error) = 0;

    /**
     * @brief 发起流式补全请求。
     * @param request 请求参数。
     * @param on_delta 增量回调，每收到一段文本触发一次。
     * @param[out] result 成功时返回聚合后的完整结果。
     * @param[out] error 失败原因。
     * @return true 成功；false 失败或被回调中断。
     */
    virtual bool StreamComplete(const LlmCompletionRequest& request, const DeltaCallback& on_delta, LlmCompletionResult& result, std::string& error) = 0;
};

} // namespace llm
} // namespace ai

#endif
