#include "ai/service/chat_service.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "ai/common/ai_utils.h"
#include "log/logger.h"

namespace ai
{
namespace service
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

ChatService::ChatService(const config::ChatSettings& settings,
                         const llm::LlmClient::ptr& llm_client,
                         const ChatStore::ptr& store,
                         const MessageSink::ptr& sink)
    : m_settings(settings)
    , m_llm_client(llm_client)
    , m_store(store)
    , m_sink(sink)
{
}

bool ChatService::Complete(const common::ChatCompletionRequest& request,
                           common::ChatCompletionResponse& response,
                           std::string& error,
                           http::HttpStatus& status)
{
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

    if (!m_llm_client || !m_store || !m_sink)
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        error = "chat service dependencies are not initialized";
        return false;
    }

    std::string conversation_id = request.conversation_id.empty() ? common::GenerateConversationId() : request.conversation_id;

    if (!EnsureContextLoaded(request.sid, conversation_id, error, status))
    {
        return false;
    }

    ConversationContext context = SnapshotContext(request.sid, conversation_id);

    common::ChatMessage user_message;
    user_message.role = "user";
    user_message.content = request.message;
    user_message.created_at_ms = common::NowMs();

    llm::LlmCompletionRequest llm_request;
    llm_request.model = request.model;
    llm_request.temperature = request.temperature;
    llm_request.max_tokens = request.max_tokens;

    if (!m_settings.system_prompt.empty())
    {
        common::ChatMessage system_message;
        system_message.role = "system";
        system_message.content = m_settings.system_prompt;
        llm_request.messages.push_back(system_message);
    }

    std::vector<common::ChatMessage> budgeted_context = BuildBudgetedContextMessages(context, user_message);
    llm_request.messages.insert(llm_request.messages.end(), budgeted_context.begin(), budgeted_context.end());
    llm_request.messages.push_back(user_message);

    llm::LlmCompletionResult llm_response;
    if (!m_llm_client->Complete(llm_request, llm_response, error))
    {
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

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

    if (!PersistMessage(user_persist, error) || !PersistMessage(assistant_persist, error))
    {
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    AppendContextMessages(request.sid, conversation_id, user_message, assistant_message);

    std::string summary_error;
    if (!MaybeRefreshSummary(request.sid, conversation_id, request.model, summary_error))
    {
        BASE_LOG_WARN(g_logger) << "refresh summary failed sid=" << request.sid
                                << " conv=" << conversation_id
                                << " error=" << summary_error;
    }

    response.conversation_id = conversation_id;
    response.reply = assistant_message.content;
    response.model = llm_response.model;
    response.finish_reason = llm_response.finish_reason;
    response.created_at_ms = assistant_message.created_at_ms;

    status = http::HttpStatus::OK;
    return true;
}

bool ChatService::StreamComplete(const common::ChatCompletionRequest& request,
                                 const StreamEventEmitter& emit,
                                 common::ChatCompletionResponse& response,
                                 std::string& error,
                                 http::HttpStatus& status)
{
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

    if (!m_llm_client || !m_store || !m_sink)
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        error = "chat service dependencies are not initialized";
        return false;
    }

    std::string conversation_id = request.conversation_id.empty() ? common::GenerateConversationId() : request.conversation_id;

    if (!EnsureContextLoaded(request.sid, conversation_id, error, status))
    {
        return false;
    }

    ConversationContext context = SnapshotContext(request.sid, conversation_id);

    common::ChatMessage user_message;
    user_message.role = "user";
    user_message.content = request.message;
    user_message.created_at_ms = common::NowMs();

    llm::LlmCompletionRequest llm_request;
    llm_request.model = request.model;
    llm_request.temperature = request.temperature;
    llm_request.max_tokens = request.max_tokens;

    if (!m_settings.system_prompt.empty())
    {
        common::ChatMessage system_message;
        system_message.role = "system";
        system_message.content = m_settings.system_prompt;
        llm_request.messages.push_back(system_message);
    }

    std::vector<common::ChatMessage> budgeted_context = BuildBudgetedContextMessages(context, user_message);
    llm_request.messages.insert(llm_request.messages.end(), budgeted_context.begin(), budgeted_context.end());
    llm_request.messages.push_back(user_message);

    nlohmann::json start;
    start["conversation_id"] = conversation_id;
    start["created_at_ms"] = user_message.created_at_ms;
    start["model"] = llm_request.model;
    if (!emit("start", start.dump()))
    {
        error = "stream client disconnected before start event";
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    std::string assembled;
    llm::LlmCompletionResult llm_response;
    bool ok = m_llm_client->StreamComplete(
        llm_request,
        [&emit, &assembled](const std::string& delta)
        {
            assembled.append(delta);
            nlohmann::json chunk;
            chunk["delta"] = delta;
            return emit("delta", chunk.dump());
        },
        llm_response, error);

    if (!ok)
    {
        nlohmann::json event_error;
        event_error["message"] = error;
        (void)emit("error", event_error.dump());
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    if (!llm_response.content.empty())
    {
        assembled = llm_response.content;
    }

    common::ChatMessage assistant_message;
    assistant_message.role = "assistant";
    assistant_message.content = assembled;
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

    if (!PersistMessage(user_persist, error) || !PersistMessage(assistant_persist, error))
    {
        nlohmann::json persist_error;
        persist_error["message"] = error;
        (void)emit("error", persist_error.dump());
        status = http::HttpStatus::SERVICE_UNAVAILABLE;
        return false;
    }

    AppendContextMessages(request.sid, conversation_id, user_message, assistant_message);

    std::string summary_error;
    if (!MaybeRefreshSummary(request.sid, conversation_id, request.model, summary_error))
    {
        BASE_LOG_WARN(g_logger) << "refresh summary failed sid=" << request.sid
                                << " conv=" << conversation_id
                                << " error=" << summary_error;
    }

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
        BASE_LOG_WARN(g_logger) << "stream done event send failed";
    }

    response.conversation_id = conversation_id;
    response.reply = assistant_message.content;
    response.model = llm_response.model;
    response.finish_reason = llm_response.finish_reason;
    response.created_at_ms = assistant_message.created_at_ms;

    status = http::HttpStatus::OK;
    return true;
}

bool ChatService::GetHistory(const std::string& sid,
                             const std::string& conversation_id,
                             size_t limit,
                             std::vector<common::ChatMessage>& messages,
                             std::string& error,
                             http::HttpStatus& status)
{
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

    if (!m_store->LoadHistory(sid, conversation_id, limit, messages, error))
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    status = http::HttpStatus::OK;
    return true;
}

std::string ChatService::BuildContextKey(const std::string& sid,
                                         const std::string& conversation_id) const
{
    return sid + "#" + conversation_id;
}

bool ChatService::EnsureContextLoaded(const std::string& sid,
                                      const std::string& conversation_id,
                                      std::string& error,
                                      http::HttpStatus& status)
{
    const std::string key = BuildContextKey(sid, conversation_id);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_contexts.find(key) != m_contexts.end())
        {
            return true;
        }
    }

    std::vector<common::ChatMessage> loaded_messages;
    if (!m_store->LoadRecentMessages(sid, conversation_id,
                                     m_settings.history_load_limit,
                                     loaded_messages, error))
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    std::string summary;
    uint64_t summary_updated_at_ms = 0;
    if (!m_store->LoadConversationSummary(sid, conversation_id, summary, summary_updated_at_ms, error))
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    ConversationContext context;
    context.messages.swap(loaded_messages);
    context.summary = summary;
    context.summary_updated_at_ms = summary_updated_at_ms;
    context.touched_at_ms = common::NowMs();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_contexts[key] = context;
    return true;
}

ChatService::ConversationContext ChatService::SnapshotContext(const std::string& sid,
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

void ChatService::AppendContextMessages(const std::string& sid,
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

    if (context.messages.size() > m_settings.max_context_messages)
    {
        size_t remove_count = context.messages.size() - m_settings.max_context_messages;
        context.messages.erase(context.messages.begin(), context.messages.begin() + remove_count);
    }
}

std::vector<common::ChatMessage> ChatService::BuildBudgetedContextMessages(
    const ConversationContext& context,
    const common::ChatMessage& pending_user_message) const
{
    std::vector<common::ChatMessage> source;
    if (!context.summary.empty())
    {
        common::ChatMessage summary_message;
        summary_message.role = "system";
        summary_message.content = std::string("Conversation summary:\n") + context.summary;
        summary_message.created_at_ms = context.summary_updated_at_ms;
        source.push_back(summary_message);
    }

    source.insert(source.end(), context.messages.begin(), context.messages.end());

    if (m_settings.max_context_tokens == 0 || source.empty())
    {
        return source;
    }

    const size_t reserve_user_tokens = EstimateMessageTokens(pending_user_message) + 16;
    if (reserve_user_tokens >= m_settings.max_context_tokens)
    {
        return std::vector<common::ChatMessage>();
    }

    const size_t budget = m_settings.max_context_tokens - reserve_user_tokens;
    size_t used = 0;
    std::vector<common::ChatMessage> picked;

    for (std::vector<common::ChatMessage>::reverse_iterator it = source.rbegin();
         it != source.rend(); ++it)
    {
        const size_t tokens = EstimateMessageTokens(*it);
        if (!picked.empty() && used + tokens > budget)
        {
            continue;
        }
        if (picked.empty() && tokens > budget)
        {
            continue;
        }

        used += tokens;
        picked.push_back(*it);
    }

    std::reverse(picked.begin(), picked.end());
    return picked;
}

bool ChatService::MaybeRefreshSummary(const std::string& sid,
                                      const std::string& conversation_id,
                                      const std::string& model,
                                      std::string& error)
{
    ConversationContext snapshot = SnapshotContext(sid, conversation_id);
    if (snapshot.messages.size() <= m_settings.recent_window_messages)
    {
        return true;
    }

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

    if (total_tokens <= m_settings.summary_trigger_tokens)
    {
        return true;
    }

    const size_t keep_recent = std::min(m_settings.recent_window_messages, snapshot.messages.size());
    const size_t summarize_count = snapshot.messages.size() - keep_recent;
    if (summarize_count == 0)
    {
        return true;
    }

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
    if (!m_llm_client->Complete(req, result, llm_error))
    {
        error = llm_error;
        return false;
    }

    if (result.content.empty())
    {
        return true;
    }

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

    if (!m_store->SaveConversationSummary(sid, conversation_id, result.content, updated_at_ms, error))
    {
        return false;
    }

    return true;
}

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

bool ChatService::PersistMessage(const common::PersistMessage& message,
                                 std::string& error)
{
    if (!m_sink->Enqueue(message, error))
    {
        BASE_LOG_ERROR(g_logger) << "enqueue persist message failed: " << error;
        return false;
    }
    return true;
}

} // namespace service
} // namespace ai
