#ifndef __SYLAR_AI_SERVICE_CHAT_SERVICE_H__
#define __SYLAR_AI_SERVICE_CHAT_SERVICE_H__

#include "ai/common/ai_types.h"
#include "ai/config/ai_app_config.h"
#include "ai/llm/llm_client.h"
#include "ai/llm/llm_router.h"
#include "ai/rag/rag_indexer.h"
#include "ai/rag/rag_retriever.h"
#include "ai/service/chat_interfaces.h"
#include "http/core/http.h"

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file chat_service.h
 * @brief AI 对话业务编排核心服务声明。
 */

namespace ai
{
namespace service
{

/**
 * @brief AI 对话业务服务。
 *
 * 该类位于 API 层与基础设施层之间，负责：
 * - 请求参数校验与会话 ID 处理；
 * - 上下文加载（内存缓存 + 存储补齐）；
 * - 调用 LLM 客户端（同步/流式）；
 * - 异步持久化入队；
 * - 历史消息查询。
 */
class ChatService
{
  public:
    typedef std::shared_ptr<ChatService> ptr;
    /** @brief 流式事件发送器，参数分别为 `event` 与 `data`。 */
    typedef std::function<bool(const std::string& event, const std::string& data)> StreamEventEmitter;

    /**
     * @brief 构造 ChatService。
     * @param settings 对话业务配置。
     * @param llm_router 大模型路由器（按 provider/model 选择客户端）。
     * @param store 历史读接口（ChatStore）。
     * @param sink 持久化写接口（MessageSink）。
     */
    ChatService(const config::ChatSettings& settings,
        const llm::LlmRouter::ptr& llm_router,
        const ChatStore::ptr& store,
        const MessageSink::ptr& sink,
        const config::RagSettings& rag_settings = config::RagSettings(),
        const rag::RagRetriever::ptr& rag_retriever = rag::RagRetriever::ptr(),
        const rag::RagIndexer::ptr& rag_indexer = rag::RagIndexer::ptr());

    /**
     * @brief 同步对话调用。
     * @param request 业务请求对象。
     * @param[out] response 业务响应对象。
     * @param[out] error 错误信息。
     * @param[out] status 建议返回的 HTTP 状态码。
     * @return true 成功；false 失败。
     */
    bool Complete(const common::ChatCompletionRequest& request,
        common::ChatCompletionResponse& response,
        std::string& error,
        http::HttpStatus& status);

    /**
     * @brief 流式对话调用（SSE 事件驱动）。
     * @param request 业务请求对象。
     * @param emit 事件发送回调，流式事件发送器。
     * @param[out] response 业务响应对象（聚合结果）。
     * @param[out] error 错误信息。
     * @param[out] status 建议返回的 HTTP 状态码。
     * @return true 成功；false 失败。
     */
    bool StreamComplete(const common::ChatCompletionRequest& request,
        const StreamEventEmitter& emit,
        common::ChatCompletionResponse& response,
        std::string& error,
        http::HttpStatus& status);

    /**
     * @brief 查询会话历史消息。
     * @param sid 会话 SID。
     * @param conversation_id 会话 ID。
     * @param limit 最大返回条数。
     * @param[out] messages 历史消息列表。
     * @param[out] error 错误信息。
     * @param[out] status 建议返回的 HTTP 状态码。
     * @return true 成功；false 失败。
     */
    bool GetHistory(const std::string& sid,
        const std::string& conversation_id,
        size_t limit,
        std::vector<common::ChatMessage>& messages,
        std::string& error,
        http::HttpStatus& status);

  private:
    /**
     * @brief 内存上下文缓存条目。
     */
    struct ConversationContext
    {
        /** @brief 缓存的上下文消息。 */
        std::vector<common::ChatMessage> messages;
        /** @brief 摘要记忆文本。 */
        std::string summary;
        /** @brief 摘要更新时间（毫秒）。 */
        uint64_t summary_updated_at_ms = 0;
        /** @brief 最近访问时间（毫秒时间戳）。 */
        uint64_t touched_at_ms = 0;
    };

  private:
    /**
     * @brief 构建上下文缓存 key。
     * @return 形如 `sid#conversation_id` 的字符串。
     */
    std::string BuildContextKey(const std::string& sid, const std::string& conversation_id) const;

    /**
     * @brief 确保会话上下文已加载到内存缓存。
     * @details 若缓存未命中，则从存储读取 recent messages 并写入缓存。
     */
    bool EnsureContextLoaded(
        const std::string& sid, const std::string& conversation_id, std::string& error, http::HttpStatus& status);

    /**
     * @brief 获取上下文快照。
     * @return 会话上下文消息副本；缓存未命中返回空数组。
     */
    ConversationContext SnapshotContext(const std::string& sid, const std::string& conversation_id);

    /**
     * @brief 追加 user/assistant 消息到上下文缓存并执行窗口裁剪。
     */
    void AppendContextMessages(const std::string& sid,
        const std::string& conversation_id,
        const common::ChatMessage& user_message,
        const common::ChatMessage& assistant_message);

    /**
     * @brief 依据 token 预算构建最终发送给模型的上下文消息。
     */
    std::vector<common::ChatMessage> BuildBudgetedContextMessages(const ConversationContext& context,
        const std::vector<common::ChatMessage>& extra_messages,
        const common::ChatMessage& pending_user_message) const;

    /**
     * @brief 基于用户长时记忆执行语义召回，并生成可注入模型的 memory 消息。
     */
    std::vector<common::ChatMessage> BuildRagMemoryMessages(
        const std::string& sid, const common::ChatMessage& pending_user_message);

    /**
     * @brief 判断当前问题是否需要触发语义召回。
     */
    bool ShouldTriggerRagRecall(const common::ChatMessage& pending_user_message) const;

    /**
     * @brief 在上下文超阈值时生成/更新摘要记忆。
     */
    bool MaybeRefreshSummary(
        const std::string& sid,
        const std::string& conversation_id,
        const llm::LlmClient::ptr& llm_client,
        const std::string& model,
        std::string& error);

    /**
     * @brief 启发式估算一条消息的 token 数量。
     */
    size_t EstimateMessageTokens(const common::ChatMessage& message) const;

    /**
     * @brief 将单条消息提交到异步持久化通道。
     */
    bool PersistMessage(const common::PersistMessage& message, std::string& error);

  private:
    /** @brief 业务配置快照。 */
    config::ChatSettings m_settings;
    /** @brief 大模型路由器（请求级选择 provider/client）。 */
    llm::LlmRouter::ptr m_llm_router;
    /** @brief 历史读取接口。 */
    ChatStore::ptr m_store;
    /** @brief 异步写入接口。 */
    MessageSink::ptr m_sink;
    /** @brief RAG 策略配置。 */
    config::RagSettings m_rag_settings;
    /** @brief RAG 语义召回器。 */
    rag::RagRetriever::ptr m_rag_retriever;
    /** @brief RAG 异步索引器。 */
    rag::RagIndexer::ptr m_rag_indexer;

    /** @brief 上下文缓存互斥锁。 */
    std::mutex m_mutex;
    /** @brief 会话上下文缓存（key = sid#conversation_id）。 */
    std::unordered_map<std::string, ConversationContext> m_contexts;
};

} // namespace service
} // namespace ai

#endif
