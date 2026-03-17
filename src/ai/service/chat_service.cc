#include "ai/service/chat_service.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "ai/common/ai_utils.h"
#include "log/logger.h"

/**
 * @file chat_service.cc
 * @brief AI 对话业务编排核心服务实现。
 */

namespace ai
{
namespace service
{

/** @brief ChatService 使用的系统日志器。 */
static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

/**
 * @brief 构造函数，注入配置与依赖。
 */
ChatService::ChatService(const config::ChatSettings& settings,
                         const llm::LlmClient::ptr& llm_client,
                         const ChatStore::ptr& store,
                         const MessageSink::ptr& sink)
    // 注入业务配置快照。
    : m_settings(settings)
      // 注入大模型客户端抽象（当前通常是 OpenAICompatibleClient）。
      ,
      m_llm_client(llm_client)
      // 注入历史读取接口。
      ,
      m_store(store)
      // 注入异步持久化写入接口。
      ,
      m_sink(sink)
{
    // 构造函数只做依赖绑定，不做重逻辑初始化。
}

/**
 * @brief 同步对话主流程。
 */
bool ChatService::Complete(const common::ChatCompletionRequest& request,
                           common::ChatCompletionResponse& response,
                           std::string& error, http::HttpStatus& status)
{
    // 若配置要求必须携带 SID，则在入口处做强校验。
    if (request.sid.empty() && m_settings.require_sid)
    {
        // 参数错误映射为 400。
        status = http::HttpStatus::BAD_REQUEST;
        error = "missing sid";
        return false;
    }

    // message 是对话最小必填字段，不能为空。
    if (request.message.empty())
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "message can not be empty";
        return false;
    }

    // 依赖必须完整：LLM、Store、Sink 任一缺失都属于服务端错误。
    if (!m_llm_client || !m_store || !m_sink)
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        error = "chat service dependencies are not initialized";
        return false;
    }

    // 若请求未给 conversation_id，则生成新会话 ID；否则沿用传入会话。
    std::string conversation_id = request.conversation_id.empty()
                                      ? common::GenerateConversationId()
                                      : request.conversation_id;

    // 确保上下文已就绪：内存命中直接用，未命中则从存储加载 recent messages。
    if (!EnsureContextLoaded(request.sid, conversation_id, error, status))
    {
        return false;
    }

    // 取出当前会话上下文快照，避免长时间持锁影响并发。
    std::vector<common::ChatMessage> context =
        SnapshotContext(request.sid, conversation_id);

    // 组装 LLM 请求基础参数（模型与采样配置）。
    llm::LlmCompletionRequest llm_request;
    llm_request.model = request.model;
    llm_request.temperature = request.temperature;
    llm_request.max_tokens = request.max_tokens;

    // 若配置了 system_prompt，则将其作为首条 system 消息注入。
    if (!m_settings.system_prompt.empty())
    {
        common::ChatMessage system_message;
        system_message.role = "system";
        system_message.content = m_settings.system_prompt;
        llm_request.messages.push_back(system_message);
    }

    // 拼接历史上下文消息。
    llm_request.messages.insert(llm_request.messages.end(), context.begin(),
                                context.end());

    // 构造当前轮用户输入消息，并追加到 LLM 请求末尾。
    common::ChatMessage user_message;
    user_message.role = "user";
    user_message.content = request.message;
    user_message.created_at_ms = common::NowMs();
    llm_request.messages.push_back(user_message);

    // 调用大模型同步接口获取完整回答。
    llm::LlmCompletionResult llm_response;
    if (!m_llm_client->Complete(llm_request, llm_response, error))
    {
        // 外部依赖不可用通常映射为 503。
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    // 组装 assistant 消息（用于持久化和上下文追加）。
    common::ChatMessage assistant_message;
    assistant_message.role = "assistant";
    assistant_message.content = llm_response.content;
    assistant_message.created_at_ms = common::NowMs();

    // 转为持久化写模型（用户消息）。
    common::PersistMessage user_persist;
    user_persist.sid = request.sid;
    user_persist.conversation_id = conversation_id;
    user_persist.role = user_message.role;
    user_persist.content = user_message.content;
    user_persist.created_at_ms = user_message.created_at_ms;

    // 转为持久化写模型（助手消息）。
    common::PersistMessage assistant_persist;
    assistant_persist.sid = request.sid;
    assistant_persist.conversation_id = conversation_id;
    assistant_persist.role = assistant_message.role;
    assistant_persist.content = assistant_message.content;
    assistant_persist.created_at_ms = assistant_message.created_at_ms;

    // 提交到异步写通道；当前语义为“入队成功”，非“事务已提交”。
    if (!PersistMessage(user_persist, error) ||
        !PersistMessage(assistant_persist, error))
    {
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    // 更新内存上下文窗口，便于下一轮请求直接复用。
    AppendContextMessages(request.sid, conversation_id, user_message,
                          assistant_message);

    // 组装业务响应对象返回给 API 层。
    response.conversation_id = conversation_id;
    response.reply = assistant_message.content;
    response.model = llm_response.model;
    response.finish_reason = llm_response.finish_reason;
    response.created_at_ms = assistant_message.created_at_ms;

    // 成功路径返回 200。
    status = http::HttpStatus::OK;
    return true;
}

/**
 * @brief 流式对话主流程。
 */
bool ChatService::StreamComplete(const common::ChatCompletionRequest& request,
                                 const StreamEventEmitter& emit,
                                 common::ChatCompletionResponse& response,
                                 std::string& error, http::HttpStatus& status)
{
    // 与同步接口一致：先做 SID 合法性检查。
    if (request.sid.empty() && m_settings.require_sid)
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "missing sid";
        return false;
    }

    // 与同步接口一致：message 必填。
    if (request.message.empty())
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "message can not be empty";
        return false;
    }

    // 检查关键依赖是否完整注入。
    if (!m_llm_client || !m_store || !m_sink)
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        error = "chat service dependencies are not initialized";
        return false;
    }

    // 生成或复用会话 ID。
    std::string conversation_id = request.conversation_id.empty()
                                      ? common::GenerateConversationId()
                                      : request.conversation_id;

    // 确保上下文已装载。
    if (!EnsureContextLoaded(request.sid, conversation_id, error, status))
    {
        return false;
    }

    // 获取上下文快照用于本轮请求。
    std::vector<common::ChatMessage> context =
        SnapshotContext(request.sid, conversation_id);

    // 组装 LLM 请求基础参数。
    llm::LlmCompletionRequest llm_request;
    llm_request.model = request.model;
    llm_request.temperature = request.temperature;
    llm_request.max_tokens = request.max_tokens;

    // 注入 system prompt（若配置了）。
    if (!m_settings.system_prompt.empty())
    {
        common::ChatMessage system_message;
        system_message.role = "system";
        system_message.content = m_settings.system_prompt;
        llm_request.messages.push_back(system_message);
    }

    // 附加历史上下文。
    llm_request.messages.insert(llm_request.messages.end(), context.begin(),
                                context.end());

    // 附加当前用户消息。
    common::ChatMessage user_message;
    user_message.role = "user";
    user_message.content = request.message;
    user_message.created_at_ms = common::NowMs();
    llm_request.messages.push_back(user_message);

    // 流式开始前先发送 start 事件，告知会话 ID 与模型信息。
    nlohmann::json start;
    start["conversation_id"] = conversation_id;
    start["created_at_ms"] = user_message.created_at_ms;
    start["model"] = llm_request.model;
    if (!emit("start", start.dump()))
    {
        // 客户端若在 start 前已断开，直接失败返回。
        error = "stream client disconnected before start event";
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    // 用于聚合最终 assistant 文本（边流边拼接）。
    std::string assembled;

    // 调用流式大模型接口，并把每个 delta 透传为 SSE delta 事件。
    llm::LlmCompletionResult llm_response;
    bool ok = m_llm_client->StreamComplete(
        llm_request,
        [&emit, &assembled](const std::string& delta)
        {
            // 先拼接本地完整文本缓存。
            assembled.append(delta);
            nlohmann::json chunk;
            // 每个增量片段以 {"delta": "..."} 输出。
            chunk["delta"] = delta;
            return emit("delta", chunk.dump());
        },
        llm_response, error);

    // 流式调用失败时，向客户端补发 error 事件。
    if (!ok)
    {
        nlohmann::json event_error;
        event_error["message"] = error;
        (void)emit("error", event_error.dump());
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    // 若底层返回了完整 content，以其为准覆盖拼接结果。
    if (!llm_response.content.empty())
    {
        assembled = llm_response.content;
    }

    // 组装 assistant 消息。
    common::ChatMessage assistant_message;
    assistant_message.role = "assistant";
    assistant_message.content = assembled;
    assistant_message.created_at_ms = common::NowMs();

    // 组装用户消息持久化对象。
    common::PersistMessage user_persist;
    user_persist.sid = request.sid;
    user_persist.conversation_id = conversation_id;
    user_persist.role = user_message.role;
    user_persist.content = user_message.content;
    user_persist.created_at_ms = user_message.created_at_ms;

    // 组装助手消息持久化对象。
    common::PersistMessage assistant_persist;
    assistant_persist.sid = request.sid;
    assistant_persist.conversation_id = conversation_id;
    assistant_persist.role = assistant_message.role;
    assistant_persist.content = assistant_message.content;
    assistant_persist.created_at_ms = assistant_message.created_at_ms;

    // 入异步写队列失败时，也通过 SSE error 事件通知客户端。
    if (!PersistMessage(user_persist, error) ||
        !PersistMessage(assistant_persist, error))
    {
        nlohmann::json persist_error;
        persist_error["message"] = error;
        (void)emit("error", persist_error.dump());
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    // 更新内存上下文窗口。
    AppendContextMessages(request.sid, conversation_id, user_message,
                          assistant_message);

    // 构造 done 事件，携带会话、模型、finish_reason、usage 等收尾信息。
    nlohmann::json done;
    done["conversation_id"] = conversation_id;
    done["model"] = llm_response.model;
    done["finish_reason"] = llm_response.finish_reason;
    done["created_at_ms"] = assistant_message.created_at_ms;
    done["usage"] = {
        {"prompt_tokens", llm_response.prompt_tokens},
        {"completion_tokens", llm_response.completion_tokens},
    };
    if (!emit("done", done.dump()))
    {
        // done 发送失败通常是客户端已断开，这里仅告警不再回滚业务流程。
        BASE_LOG_WARN(g_logger) << "stream done event send failed";
    }

    // 回填统一业务响应对象（便于上层保留聚合结果）。
    response.conversation_id = conversation_id;
    response.reply = assistant_message.content;
    response.model = llm_response.model;
    response.finish_reason = llm_response.finish_reason;
    response.created_at_ms = assistant_message.created_at_ms;

    // 成功路径返回 200。
    status = http::HttpStatus::OK;
    return true;
}

/**
 * @brief 历史查询流程。
 */
bool ChatService::GetHistory(const std::string& sid,
                             const std::string& conversation_id, size_t limit,
                             std::vector<common::ChatMessage>& messages,
                             std::string& error, http::HttpStatus& status)
{
    // require_sid 开启时，历史查询同样要求 SID。
    if (sid.empty() && m_settings.require_sid)
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "missing sid";
        return false;
    }

    // conversation_id 是历史查询的必填路由参数。
    if (conversation_id.empty())
    {
        status = http::HttpStatus::BAD_REQUEST;
        error = "conversation_id can not be empty";
        return false;
    }

    // 存储依赖未注入属于服务端错误。
    if (!m_store)
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        error = "chat store is not initialized";
        return false;
    }

    // 优先走内存上下文缓存，降低 DB 查询频率。
    std::vector<common::ChatMessage> context =
        SnapshotContext(sid, conversation_id);
    if (!context.empty())
    {
        // 缓存命中时按 limit 截断末尾 N 条（最近消息）。
        if (context.size() > limit)
        {
            messages.assign(context.end() - limit, context.end());
        }
        else
        {
            // 不超过 limit 时直接返回全部缓存快照。
            messages.swap(context);
        }
        status = http::HttpStatus::OK;
        return true;
    }

    // 缓存未命中时回源存储层查询历史。
    if (!m_store->LoadHistory(sid, conversation_id, limit, messages, error))
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    status = http::HttpStatus::OK;
    return true;
}

/**
 * @brief 构建上下文缓存 key。
 */
std::string
ChatService::BuildContextKey(const std::string& sid,
                             const std::string& conversation_id) const
{
    // 使用 sid + conversation_id 组合作为上下文唯一键。
    return sid + "#" + conversation_id;
}

/**
 * @brief 确保上下文已加载到缓存（未命中时从存储补齐）。
 */
bool ChatService::EnsureContextLoaded(const std::string& sid,
                                      const std::string& conversation_id,
                                      std::string& error,
                                      http::HttpStatus& status)
{
    // 先构建缓存 key。
    const std::string key = BuildContextKey(sid, conversation_id);
    {
        // 先在锁保护下检查缓存是否已存在，避免重复加载。
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_contexts.find(key) != m_contexts.end())
        {
            // 已命中，直接返回。
            return true;
        }
    }

    // 缓存未命中，从存储读取最近历史用于上下文补齐。
    std::vector<common::ChatMessage> loaded_messages;
    if (!m_store->LoadRecentMessages(sid, conversation_id,
                                     m_settings.history_load_limit,
                                     loaded_messages, error))
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    // 组装新的缓存上下文条目。
    ConversationContext context;
    context.messages.swap(loaded_messages);
    context.touched_at_ms = common::NowMs();

    // 写入缓存（加锁保护）。
    std::lock_guard<std::mutex> lock(m_mutex);
    m_contexts[key] = context;
    return true;
}

/**
 * @brief 获取上下文快照并刷新触达时间。
 */
std::vector<common::ChatMessage>
ChatService::SnapshotContext(const std::string& sid,
                             const std::string& conversation_id)
{
    // 构建缓存 key。
    const std::string key = BuildContextKey(sid, conversation_id);
    // 读取缓存时全程加锁，保证线程安全。
    std::lock_guard<std::mutex> lock(m_mutex);
    std::unordered_map<std::string, ConversationContext>::iterator it =
        m_contexts.find(key);
    if (it == m_contexts.end())
    {
        // 未命中返回空数组。
        return std::vector<common::ChatMessage>();
    }

    // 命中时刷新最近访问时间，用于后续淘汰策略扩展。
    it->second.touched_at_ms = common::NowMs();
    // 返回消息副本，避免把内部容器暴露到锁外。
    return it->second.messages;
}

/**
 * @brief 追加 user/assistant 消息并按窗口上限裁剪。
 */
void ChatService::AppendContextMessages(
    const std::string& sid, const std::string& conversation_id,
    const common::ChatMessage& user_message,
    const common::ChatMessage& assistant_message)
{
    // 计算缓存 key。
    const std::string key = BuildContextKey(sid, conversation_id);

    // 持锁更新上下文缓存。
    std::lock_guard<std::mutex> lock(m_mutex);
    ConversationContext& context = m_contexts[key];
    // 追加本轮 user / assistant 两条消息。
    context.messages.push_back(user_message);
    context.messages.push_back(assistant_message);
    // 刷新触达时间。
    context.touched_at_ms = common::NowMs();

    // 若超过窗口上限，裁剪最旧的消息。
    if (context.messages.size() > m_settings.max_context_messages)
    {
        size_t remove_count =
            context.messages.size() - m_settings.max_context_messages;
        context.messages.erase(context.messages.begin(),
                               context.messages.begin() + remove_count);
    }
}

/**
 * @brief 提交单条消息到异步持久化通道。
 */
bool ChatService::PersistMessage(const common::PersistMessage& message,
                                 std::string& error)
{
    // 交给异步写接口入队，失败时记录日志并向上返回。
    if (!m_sink->Enqueue(message, error))
    {
        BASE_LOG_ERROR(g_logger) << "enqueue persist message failed: " << error;
        return false;
    }
    // 入队成功。
    return true;
}

} // namespace service
} // namespace ai
