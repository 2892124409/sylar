#include "ai/service/chat_service.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

#include "ai/common/ai_utils.h"
#include "log/logger.h"

/**
 * @file chat_service.cc
 * @brief ChatService 业务编排实现：上下文管理、LLM 调用、持久化与 RAG 接入。
 */

namespace ai
{
namespace service
{

/** @brief ChatService 运行日志器。 */
static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

namespace
{

/**
 * @brief 仅对 ASCII 字符做小写化转换。
 * @details 用于英文关键词匹配与轻量归一化。
 */
std::string ToLowerAscii(const std::string& input)
{
    std::string output = input;
    for (size_t i = 0; i < output.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(output[i]);
        output[i] = static_cast<char>(std::tolower(c));
    }
    return output;
}

/**
 * @brief 判断文本是否包含任意关键词。
 */
bool ContainsAny(const std::string& text, const std::vector<std::string>& keywords)
{
    for (size_t i = 0; i < keywords.size(); ++i)
    {
        if (!keywords[i].empty() && text.find(keywords[i]) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

} // namespace

/**
 * @brief 构造 ChatService，注入依赖与策略配置。
 */
ChatService::ChatService(const config::ChatSettings& settings,
                         const llm::LlmRouter::ptr& llm_router,
                         const ChatStore::ptr& store,
                         const MessageSink::ptr& sink,
                         const config::RagSettings& rag_settings,
                         const rag::RagRetriever::ptr& rag_retriever,
                         const rag::RagIndexer::ptr& rag_indexer)
    : m_settings(settings)
    , m_llm_router(llm_router)
    , m_store(store)
    , m_sink(sink)
    , m_rag_settings(rag_settings)
    , m_rag_retriever(rag_retriever)
    , m_rag_indexer(rag_indexer)
{
}

/**
 * @brief 同步对话主流程。
 * @details
 * 主要步骤：校验 -> 上下文加载 -> 构建 prompt（含 RAG）-> LLM 调用 ->
 *          持久化 + RAG 索引入队 -> 上下文更新 -> 摘要刷新。
 */
bool ChatService::Complete(const common::ChatCompletionRequest& request,
                           common::ChatCompletionResponse& response,
                           std::string& error,
                           http::HttpStatus& status)
{
    // Step 1: 基础参数校验。
    if (request.sid.empty() && m_settings.require_sid)
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "missing sid";
        return false;
    }

    if (request.message.empty())
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "message can not be empty";
        return false;
    }

    if (!m_llm_router || !m_store || !m_sink)
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        error = "chat service dependencies are not initialized";
        return false;
    }

    // Step 2: 生成/复用会话 ID，并确保上下文已加载。
    std::string conversation_id = request.conversation_id.empty() ? common::GenerateConversationId() : request.conversation_id;

    if (!EnsureContextLoaded(request.sid, conversation_id, error, status))
    {
        return false;
    }

    ConversationContext context = SnapshotContext(request.sid, conversation_id);

    // 第五阶段：按请求 provider/model 动态路由到目标客户端。
    llm::LlmRouteResult route;
    if (!m_llm_router->Route(request.provider, request.model, route, error))
    {
        status = http::HttpStatus::BAD_REQUEST;
        return false;
    }

    // Step 3: 组装用户消息与 LLM 请求基础参数。
    common::ChatMessage user_message;
    user_message.role = "user";
    user_message.content = request.message;
    user_message.created_at_ms = common::NowMs();

    llm::LlmCompletionRequest llm_request;
    llm_request.model = route.model;
    llm_request.temperature = request.temperature;
    llm_request.max_tokens = request.max_tokens;

    if (!m_settings.system_prompt.empty())
    {
        common::ChatMessage system_message;
        system_message.role = "system";
        system_message.content = m_settings.system_prompt;
        llm_request.messages.push_back(system_message);
    }

    // Step 4: 构建 RAG 召回记忆并做预算裁剪。
    std::vector<common::ChatMessage> rag_memory_messages = BuildRagMemoryMessages(request.sid, user_message);
    std::vector<common::ChatMessage> budgeted_context = BuildBudgetedContextMessages(context, rag_memory_messages, user_message);
    llm_request.messages.insert(llm_request.messages.end(), budgeted_context.begin(), budgeted_context.end());
    llm_request.messages.push_back(user_message);

    // Step 5: 调用 LLM 完成补全。
    llm::LlmCompletionResult llm_response;
    if (!route.client->Complete(llm_request, llm_response, error))
    {
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    // Step 6: 组装 assistant 消息与持久化对象。
    common::ChatMessage assistant_message;
    assistant_message.role = "assistant";
    assistant_message.content = llm_response.content;
    assistant_message.created_at_ms = common::NowMs();

    common::PersistMessage user_persist;
    user_persist.sid = request.sid;
    user_persist.conversation_id = conversation_id;
    user_persist.role = user_message.role;
    user_persist.content = user_message.content;
    user_persist.created_at_ms = user_message.created_at_ms;

    common::PersistMessage assistant_persist;
    assistant_persist.sid = request.sid;
    assistant_persist.conversation_id = conversation_id;
    assistant_persist.role = assistant_message.role;
    assistant_persist.content = assistant_message.content;
    assistant_persist.created_at_ms = assistant_message.created_at_ms;

    // Step 7: 写入持久化通道（并触发 RAG 索引入队）。
    if (!PersistMessage(user_persist, error) || !PersistMessage(assistant_persist, error))
    {
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    // Step 8: 更新内存上下文并尝试刷新摘要。
    AppendContextMessages(request.sid, conversation_id, user_message, assistant_message);

    std::string summary_error;
    if (!MaybeRefreshSummary(request.sid, conversation_id, route.client, route.model, summary_error))
    {
        BASE_LOG_WARN(g_logger) << "refresh summary failed sid=" << request.sid
                                << " conv=" << conversation_id
                                << " error=" << summary_error;
    }

    // Step 9: 回填响应对象。
    response.conversation_id = conversation_id;
    response.reply = assistant_message.content;
    response.model = llm_response.model;
    // 回填本次命中的 provider_id，供调用方确认路由结果。
    response.provider = route.provider_id;
    response.finish_reason = llm_response.finish_reason;
    response.created_at_ms = assistant_message.created_at_ms;

    status = http::HttpStatus::OK;
    return true;
}

/**
 * @brief 流式对话主流程（SSE）。
 * @details
 * 与 Complete 的差异：
 * - LLM 调用采用流式回调；
 * - 通过 emit 输出 start/delta/error/done 事件；
 * - 完成后同样执行持久化、上下文更新与摘要刷新。
 */
bool ChatService::StreamComplete(const common::ChatCompletionRequest& request,
                                 const StreamEventEmitter& emit,
                                 common::ChatCompletionResponse& response,
                                 std::string& error,
                                 http::HttpStatus& status)
{
    // Step 1: 参数校验与依赖校验。
    if (request.sid.empty() && m_settings.require_sid)
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "missing sid";
        return false;
    }

    if (request.message.empty())
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "message can not be empty";
        return false;
    }

    if (!m_llm_router || !m_store || !m_sink)
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        error = "chat service dependencies are not initialized";
        return false;
    }

    // Step 2: 上下文准备（会话 ID、缓存预热、快照）。
    std::string conversation_id = request.conversation_id.empty() ? common::GenerateConversationId() : request.conversation_id;

    if (!EnsureContextLoaded(request.sid, conversation_id, error, status))
    {
        return false;
    }

    ConversationContext context = SnapshotContext(request.sid, conversation_id);

    // 第五阶段：流式路径与同步路径复用同一套 provider/model 路由策略。
    llm::LlmRouteResult route;
    if (!m_llm_router->Route(request.provider, request.model, route, error))
    {
        status = http::HttpStatus::BAD_REQUEST;
        return false;
    }

    // Step 3: 构建请求消息（system + budgeted_context + user）。
    common::ChatMessage user_message;
    user_message.role = "user";
    user_message.content = request.message;
    user_message.created_at_ms = common::NowMs();

    llm::LlmCompletionRequest llm_request;
    llm_request.model = route.model;
    llm_request.temperature = request.temperature;
    llm_request.max_tokens = request.max_tokens;

    if (!m_settings.system_prompt.empty())
    {
        common::ChatMessage system_message;
        system_message.role = "system";
        system_message.content = m_settings.system_prompt;
        llm_request.messages.push_back(system_message);
    }

    // 注入 RAG 召回消息，并与 recent/summary 一起参与预算裁剪。
    std::vector<common::ChatMessage> rag_memory_messages = BuildRagMemoryMessages(request.sid, user_message);
    std::vector<common::ChatMessage> budgeted_context = BuildBudgetedContextMessages(context, rag_memory_messages, user_message);
    llm_request.messages.insert(llm_request.messages.end(), budgeted_context.begin(), budgeted_context.end());
    llm_request.messages.push_back(user_message);

    // Step 4: 发送 start 事件，通知客户端流式开始。
    nlohmann::json start;
    start["conversation_id"] = conversation_id;
    start["created_at_ms"] = user_message.created_at_ms;
    // 在 stream start 事件回传命中的 provider/model，方便前端做链路展示。
    start["provider"] = route.provider_id;
    start["model"] = llm_request.model;
    if (!emit("start", start.dump()))
    {
        error = "stream client disconnected before start event";
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    // Step 5: 流式拉取 delta，并实时透传 SSE chunk。
    std::string assembled;
    llm::LlmCompletionResult llm_response;
    bool ok = route.client->StreamComplete(
        llm_request,
        [&emit, &assembled](const std::string& delta)
        {
            assembled.append(delta);
            nlohmann::json chunk;
            chunk["delta"] = delta;
            return emit("delta", chunk.dump());
        },
        llm_response, error);

    // Step 6: 流式失败，发送 error 事件并返回。
    if (!ok)
    {
        nlohmann::json event_error;
        event_error["message"] = error;
        (void)emit("error", event_error.dump());
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    // 部分 provider 会回填完整 content，优先采用完整值。
    if (!llm_response.content.empty())
    {
        assembled = llm_response.content;
    }

    common::ChatMessage assistant_message;
    assistant_message.role = "assistant";
    assistant_message.content = assembled;
    assistant_message.created_at_ms = common::NowMs();

    // Step 7: 落持久化（user+assistant），失败则发 error 事件。
    common::PersistMessage user_persist;
    user_persist.sid = request.sid;
    user_persist.conversation_id = conversation_id;
    user_persist.role = user_message.role;
    user_persist.content = user_message.content;
    user_persist.created_at_ms = user_message.created_at_ms;

    common::PersistMessage assistant_persist;
    assistant_persist.sid = request.sid;
    assistant_persist.conversation_id = conversation_id;
    assistant_persist.role = assistant_message.role;
    assistant_persist.content = assistant_message.content;
    assistant_persist.created_at_ms = assistant_message.created_at_ms;

    if (!PersistMessage(user_persist, error) || !PersistMessage(assistant_persist, error))
    {
        nlohmann::json persist_error;
        persist_error["message"] = error;
        (void)emit("error", persist_error.dump());
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    // Step 8: 更新上下文并尝试刷新摘要（失败不阻断）。
    AppendContextMessages(request.sid, conversation_id, user_message, assistant_message);

    std::string summary_error;
    if (!MaybeRefreshSummary(request.sid, conversation_id, route.client, route.model, summary_error))
    {
        BASE_LOG_WARN(g_logger) << "refresh summary failed sid=" << request.sid
                                << " conv=" << conversation_id
                                << " error=" << summary_error;
    }

    // Step 9: 发送 done 事件（包含 usage/finish_reason）。
    nlohmann::json done;
    done["conversation_id"] = conversation_id;
    done["provider"] = route.provider_id;
    done["model"] = llm_response.model;
    done["finish_reason"] = llm_response.finish_reason;
    done["created_at_ms"] = assistant_message.created_at_ms;
    done["usage"] = {
        {"prompt_tokens", llm_response.prompt_tokens},
        {"completion_tokens", llm_response.completion_tokens},
    };
    if (!emit("done", done.dump()))
    {
        BASE_LOG_WARN(g_logger) << "stream done event send failed";
    }

    // Step 10: 回填响应对象，便于上层统一处理。
    response.conversation_id = conversation_id;
    response.reply = assistant_message.content;
    response.model = llm_response.model;
    response.provider = route.provider_id;
    response.finish_reason = llm_response.finish_reason;
    response.created_at_ms = assistant_message.created_at_ms;

    status = http::HttpStatus::OK;
    return true;
}

/**
 * @brief 获取会话历史。
 * @details
 * 读取顺序：先内存上下文快照，未命中再回源存储层。
 */
bool ChatService::GetHistory(const std::string& sid,
                             const std::string& conversation_id,
                             size_t limit,
                             std::vector<common::ChatMessage>& messages,
                             std::string& error,
                             http::HttpStatus& status)
{
    // Step 1: 参数与依赖校验。
    if (sid.empty() && m_settings.require_sid)
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "missing sid";
        return false;
    }

    if (conversation_id.empty())
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "conversation_id can not be empty";
        return false;
    }

    if (!m_store)
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        error = "chat store is not initialized";
        return false;
    }

    // Step 2: 优先使用内存上下文（低延迟）。
    ConversationContext context = SnapshotContext(sid, conversation_id);
    if (!context.messages.empty())
    {
        if (context.messages.size() > limit)
        {
            messages.assign(context.messages.end() - limit, context.messages.end());
        }
        else
        {
            messages.swap(context.messages);
        }
        status = http::HttpStatus::OK;
        return true;
    }

    // Step 3: 内存未命中时回源 DB。
    if (!m_store->LoadHistory(sid, conversation_id, limit, messages, error))
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    status = http::HttpStatus::OK;
    return true;
}

/**
 * @brief 生成上下文缓存键：`sid#conversation_id`。
 */
std::string ChatService::BuildContextKey(const std::string& sid,
                                         const std::string& conversation_id) const
{
    return sid + "#" + conversation_id;
}

/**
 * @brief 确保会话上下文已加载到内存缓存。
 * @details
 * - 命中缓存：直接返回；
 * - 未命中：加载 recent 消息与摘要并写入缓存。
 */
bool ChatService::EnsureContextLoaded(const std::string& sid,
                                      const std::string& conversation_id,
                                      std::string& error,
                                      http::HttpStatus& status)
{
    const std::string key = BuildContextKey(sid, conversation_id);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // 缓存命中直接返回。
        if (m_contexts.find(key) != m_contexts.end())
        {
            return true;
        }
    }

    // 缓存未命中：回源加载 recent 消息。
    std::vector<common::ChatMessage> loaded_messages;
    if (!m_store->LoadRecentMessages(sid, conversation_id,
                                     m_settings.history_load_limit,
                                     loaded_messages, error))
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    // 加载会话摘要（若有）。
    std::string summary;
    uint64_t summary_updated_at_ms = 0;
    if (!m_store->LoadConversationSummary(sid, conversation_id, summary, summary_updated_at_ms, error))
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    // 写入上下文缓存。
    ConversationContext context;
    context.messages.swap(loaded_messages);
    context.summary = summary;
    context.summary_updated_at_ms = summary_updated_at_ms;
    context.touched_at_ms = common::NowMs();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_contexts[key] = context;
    return true;
}

/**
 * @brief 获取会话上下文快照，并刷新最近触达时间。
 */
ChatService::ConversationContext ChatService::SnapshotContext(
    const std::string& sid,
    const std::string& conversation_id)
{
    const std::string key = BuildContextKey(sid, conversation_id);
    std::lock_guard<std::mutex> lock(m_mutex);
    std::unordered_map<std::string, ConversationContext>::iterator it =
        m_contexts.find(key);
    if (it == m_contexts.end())
    {
        return ConversationContext();
    }

    it->second.touched_at_ms = common::NowMs();
    return it->second;
}

/**
 * @brief 追加 user/assistant 到内存上下文并按窗口裁剪。
 */
void ChatService::AppendContextMessages(
    const std::string& sid,
    const std::string& conversation_id,
    const common::ChatMessage& user_message,
    const common::ChatMessage& assistant_message)
{
    const std::string key = BuildContextKey(sid, conversation_id);

    std::lock_guard<std::mutex> lock(m_mutex);
    ConversationContext& context = m_contexts[key];
    context.messages.push_back(user_message);
    context.messages.push_back(assistant_message);
    context.touched_at_ms = common::NowMs();

    // 超过上限时移除最旧消息，控制内存占用。
    if (context.messages.size() > m_settings.max_context_messages)
    {
        size_t remove_count = context.messages.size() - m_settings.max_context_messages;
        context.messages.erase(context.messages.begin(), context.messages.begin() + remove_count);
    }
}

/**
 * @brief 按 token 预算构建最终上下文消息列表。
 * @details
 * 优先保留 summary，再从近到远选择 recent + recall，
 * 并为当前 user 消息预留预算。
 */
std::vector<common::ChatMessage> ChatService::BuildBudgetedContextMessages(
    const ConversationContext& context,
    const std::vector<common::ChatMessage>& extra_messages,
    const common::ChatMessage& pending_user_message) const
{
    // Step 1: 把摘要封装为 system 消息（若存在）。
    common::ChatMessage summary_message;
    bool has_summary = false;
    if (!context.summary.empty())
    {
        summary_message.role = "system";
        summary_message.content = std::string("Conversation summary:\n") + context.summary;
        summary_message.created_at_ms = context.summary_updated_at_ms;
        has_summary = true;
    }

    // Step 2: 合并候选上下文源（recent + recall）。
    std::vector<common::ChatMessage> source;
    source.insert(source.end(), context.messages.begin(), context.messages.end());
    source.insert(source.end(), extra_messages.begin(), extra_messages.end());

    // Step 3: 未启用预算限制时直接返回全部候选（可选附带 summary）。
    if (m_settings.max_context_tokens == 0)
    {
        if (has_summary)
        {
            source.insert(source.begin(), summary_message);
        }
        return source;
    }

    // Step 4: 预留 pending user message 的 token 预算。
    const size_t reserve_user_tokens = EstimateMessageTokens(pending_user_message) + 16;
    if (reserve_user_tokens >= m_settings.max_context_tokens)
    {
        return std::vector<common::ChatMessage>();
    }

    // Step 5: 在剩余预算内挑选消息。
    const size_t budget = m_settings.max_context_tokens - reserve_user_tokens;
    size_t used = 0;
    std::vector<common::ChatMessage> picked;

    // 先尝试保留 summary（信息密度最高），再让 recent/recall 竞争剩余额度。
    if (has_summary)
    {
        const size_t summary_tokens = EstimateMessageTokens(summary_message);
        if (summary_tokens <= budget)
        {
            used += summary_tokens;
            picked.push_back(summary_message);
        }
    }

    // Step 6: 从尾到头选择（优先最近消息）。
    std::vector<common::ChatMessage> picked_tail;
    for (std::vector<common::ChatMessage>::reverse_iterator it = source.rbegin();
         it != source.rend(); ++it)
    {
        const size_t tokens = EstimateMessageTokens(*it);
        if (used + tokens > budget)
        {
            continue;
        }

        used += tokens;
        picked_tail.push_back(*it);
    }

    // Step 7: 反转回正序，拼接最终结果。
    std::reverse(picked_tail.begin(), picked_tail.end());
    picked.insert(picked.end(), picked_tail.begin(), picked_tail.end());
    return picked;
}

/**
 * @brief 执行 RAG 召回并组装为一条 system 记忆消息。
 * @details
 * 若未触发 recall、召回失败或无命中，返回空数组；
 * 命中后将片段聚合为可直接注入 prompt 的 memory 内容。
 */
std::vector<common::ChatMessage> ChatService::BuildRagMemoryMessages(const std::string& sid,
                                                                     const common::ChatMessage& pending_user_message)
{
    // Step 1: RAG 开关、依赖和输入校验。
    if (!m_rag_settings.enabled || !m_rag_retriever)
    {
        return std::vector<common::ChatMessage>();
    }
    if (sid.empty() || pending_user_message.content.empty())
    {
        return std::vector<common::ChatMessage>();
    }

    // Step 2: 先判定本轮是否需要触发 recall。
    if (!ShouldTriggerRagRecall(pending_user_message))
    {
        return std::vector<common::ChatMessage>();
    }

    // Step 3: 执行检索（query -> embedding -> vector search）。
    std::vector<rag::SearchHit> hits;
    std::string error;
    if (!m_rag_retriever->Retrieve(sid, pending_user_message.content,
                                   m_rag_settings.top_k,
                                   m_rag_settings.score_threshold,
                                   hits, error))
    {
        BASE_LOG_WARN(g_logger) << "rag retrieve failed sid=" << sid
                                << " error=" << error;
        return std::vector<common::ChatMessage>();
    }

    // Step 4: 无命中直接返回空。
    if (hits.empty())
    {
        return std::vector<common::ChatMessage>();
    }

    // Step 5: 把命中结果拼成系统记忆片段。
    std::ostringstream content;
    content << "Long-term memory snippets for current user (use only when relevant):\n";

    for (size_t i = 0; i < hits.size(); ++i)
    {
        std::string snippet = hits[i].payload.content;
        if (m_rag_settings.max_snippet_chars > 0 &&
            snippet.size() > m_rag_settings.max_snippet_chars)
        {
            snippet.resize(m_rag_settings.max_snippet_chars);
            snippet.append("...");
        }

        // 附带 score/conv/role/ts，提升模型使用这些记忆时的可解释性。
        content << "[" << (i + 1) << "] "
                << "score=" << std::fixed << std::setprecision(4) << hits[i].score
                << ", conv=" << hits[i].payload.conversation_id
                << ", role=" << hits[i].payload.role
                << ", ts=" << hits[i].payload.created_at_ms
                << "\n"
                << snippet << "\n";
    }

    // Step 6: 作为 system 消息返回给预算裁剪器。
    common::ChatMessage memory_message;
    memory_message.role = "system";
    memory_message.content = content.str();
    memory_message.created_at_ms = common::NowMs();

    return std::vector<common::ChatMessage>(1, memory_message);
}

/**
 * @brief 判断当前消息是否触发 RAG recall。
 * @details
 * 支持：
 * - always：每轮都召回；
 * - intent：命中“历史/记忆”意图关键词才召回。
 */
bool ChatService::ShouldTriggerRagRecall(const common::ChatMessage& pending_user_message) const
{
    if (!m_rag_settings.enabled)
    {
        return false;
    }
    if (m_rag_settings.recall_trigger_mode == "always")
    {
        return true;
    }

    // 默认 intent 模式：只有“用户明确在问历史/记忆”时才触发 recall。
    const std::string& query = pending_user_message.content;
    if (query.size() < m_rag_settings.recall_intent_min_chars)
    {
        return false;
    }

    static const std::vector<std::string> kZhIntentKeywords = {
        "记得", "还记得", "之前", "上次", "刚才", "以前", "曾经", "历史", "聊过", "说过", "提过"};
    if (ContainsAny(query, kZhIntentKeywords))
    {
        return true;
    }

    const std::string query_lower = ToLowerAscii(query);
    static const std::vector<std::string> kEnIntentKeywords = {
        "remember", "earlier", "previous", "before", "history", "we discussed", "you said", "i said"};
    return ContainsAny(query_lower, kEnIntentKeywords);
}

/**
 * @brief 在上下文超过阈值时刷新摘要并回写存储。
 */
bool ChatService::MaybeRefreshSummary(const std::string& sid,
                                      const std::string& conversation_id,
                                      const llm::LlmClient::ptr& llm_client,
                                      const std::string& model,
                                      std::string& error)
{
    // Step 1: 读取快照并判定是否超过“触发摘要”门槛。
    ConversationContext snapshot = SnapshotContext(sid, conversation_id);
    if (snapshot.messages.size() <= m_settings.recent_window_messages)
    {
        return true;
    }

    // Step 2: 估算当前上下文 token 总量。
    size_t total_tokens = 0;
    if (!snapshot.summary.empty())
    {
        common::ChatMessage summary_message;
        summary_message.role = "system";
        summary_message.content = snapshot.summary;
        total_tokens += EstimateMessageTokens(summary_message);
    }
    for (size_t i = 0; i < snapshot.messages.size(); ++i)
    {
        total_tokens += EstimateMessageTokens(snapshot.messages[i]);
    }

    // 未超过阈值：无需更新摘要。
    if (total_tokens <= m_settings.summary_trigger_tokens)
    {
        return true;
    }

    // Step 3: 切分“待摘要旧消息”和“保留 recent 消息”。
    const size_t keep_recent = std::min(m_settings.recent_window_messages, snapshot.messages.size());
    const size_t summarize_count = snapshot.messages.size() - keep_recent;
    if (summarize_count == 0)
    {
        return true;
    }

    // Step 4: 构造摘要输入（旧摘要 + 待摘要旧消息）。
    std::ostringstream summary_input;
    summary_input << "现有摘要:\n";
    if (snapshot.summary.empty())
    {
        summary_input << "(无)\n\n";
    }
    else
    {
        summary_input << snapshot.summary << "\n\n";
    }
    summary_input << "请基于下面的旧对话更新摘要：\n";

    for (size_t i = 0; i < summarize_count; ++i)
    {
        summary_input << "[" << snapshot.messages[i].role << "] "
                      << snapshot.messages[i].content << "\n";
    }

    summary_input << "\n输出要求：保留关键信息、事实、用户偏好、约束条件和未完成任务。";

    // Step 5: 调用 LLM 生成新摘要。
    llm::LlmCompletionRequest req;
    req.model = model;
    req.temperature = 0.2;
    req.max_tokens = m_settings.summary_max_tokens;

    common::ChatMessage system_message;
    system_message.role = "system";
    system_message.content = m_settings.summary_prompt;
    req.messages.push_back(system_message);

    common::ChatMessage user_message;
    user_message.role = "user";
    user_message.content = summary_input.str();
    req.messages.push_back(user_message);

    llm::LlmCompletionResult result;
    std::string llm_error;
    if (!llm_client)
    {
        error = "summary llm client is null";
        return false;
    }
    if (!llm_client->Complete(req, result, llm_error))
    {
        error = llm_error;
        return false;
    }

    // 空结果不更新摘要。
    if (result.content.empty())
    {
        return true;
    }

    // Step 6: 更新内存上下文（刷新 summary 并裁剪 old messages）。
    const uint64_t updated_at_ms = common::NowMs();

    {
        const std::string key = BuildContextKey(sid, conversation_id);
        std::lock_guard<std::mutex> lock(m_mutex);
        std::unordered_map<std::string, ConversationContext>::iterator it = m_contexts.find(key);
        if (it == m_contexts.end())
        {
            return true;
        }

        ConversationContext& context = it->second;
        context.summary = result.content;
        context.summary_updated_at_ms = updated_at_ms;
        if (context.messages.size() > keep_recent)
        {
            context.messages.erase(context.messages.begin(), context.messages.end() - keep_recent);
        }
    }

    // Step 7: 回写摘要到存储层，保证冷启动可恢复。
    if (!m_store->SaveConversationSummary(sid, conversation_id, result.content, updated_at_ms, error))
    {
        return false;
    }

    return true;
}

/**
 * @brief 轻量 token 估算（启发式）。
 * @details
 * 估算规则：`(role+content 字节数 / 4) + 固定开销`。
 * 用于预算裁剪，不追求 tokenizer 级精确。
 */
size_t ChatService::EstimateMessageTokens(const common::ChatMessage& message) const
{
    const size_t bytes = message.role.size() + message.content.size();
    if (bytes == 0)
    {
        return 0;
    }

    const size_t payload_tokens = (bytes + 3) / 4;
    return payload_tokens + 4;
}

/**
 * @brief 持久化消息，并尝试触发 RAG 索引。
 * @details
 * - m_sink 写入失败：返回 false（主流程失败）；
 * - rag_indexer 入队失败：仅 WARN（fail-open，不阻断主流程）。
 */
bool ChatService::PersistMessage(const common::PersistMessage& message,
                                 std::string& error)
{
    // Step 1: 进入异步持久化写通道。
    if (!m_sink->Enqueue(message, error))
    {
        BASE_LOG_ERROR(g_logger) << "enqueue persist message failed: " << error;
        return false;
    }

    // Step 2: 若启用 RAG，额外入队索引任务（增强链路）。
    if (m_rag_settings.enabled && m_rag_indexer)
    {
        std::string index_error;
        if (!m_rag_indexer->Enqueue(message, index_error))
        {
            BASE_LOG_WARN(g_logger) << "enqueue rag index task failed sid=" << message.sid
                                    << " conv=" << message.conversation_id
                                    << " error=" << index_error;
        }
    }
    return true;
}

} // namespace service
} // namespace ai
