#include "ai/service/chat_service.h"

#include "ai/common/ai_utils.h"

#include "log/logger.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace ai
{
namespace service
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

ChatService::ChatService(const config::ChatSettings &settings,
                         const llm::LlmClient::ptr &llm_client,
                         const ChatStore::ptr &store,
                         const MessageSink::ptr &sink)
    : m_settings(settings)
    , m_llm_client(llm_client)
    , m_store(store)
    , m_sink(sink)
{
}

bool ChatService::Complete(const common::ChatCompletionRequest &request,
                           common::ChatCompletionResponse &response,
                           std::string &error,
                           http::HttpStatus &status)
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

    std::string conversation_id = request.conversation_id.empty()
                                      ? common::GenerateConversationId()
                                      : request.conversation_id;

    if (!EnsureContextLoaded(request.sid, conversation_id, error, status))
    {
        return false;
    }

    std::vector<common::ChatMessage> context = SnapshotContext(request.sid, conversation_id);

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

    llm_request.messages.insert(llm_request.messages.end(), context.begin(), context.end());

    common::ChatMessage user_message;
    user_message.role = "user";
    user_message.content = request.message;
    user_message.created_at_ms = common::NowMs();
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

    response.conversation_id = conversation_id;
    response.reply = assistant_message.content;
    response.model = llm_response.model;
    response.finish_reason = llm_response.finish_reason;
    response.created_at_ms = assistant_message.created_at_ms;

    status = http::HttpStatus::OK;
    return true;
}

bool ChatService::StreamComplete(const common::ChatCompletionRequest &request,
                                 const StreamEventEmitter &emit,
                                 common::ChatCompletionResponse &response,
                                 std::string &error,
                                 http::HttpStatus &status)
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

    std::string conversation_id = request.conversation_id.empty()
                                      ? common::GenerateConversationId()
                                      : request.conversation_id;

    if (!EnsureContextLoaded(request.sid, conversation_id, error, status))
    {
        return false;
    }

    std::vector<common::ChatMessage> context = SnapshotContext(request.sid, conversation_id);

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

    llm_request.messages.insert(llm_request.messages.end(), context.begin(), context.end());

    common::ChatMessage user_message;
    user_message.role = "user";
    user_message.content = request.message;
    user_message.created_at_ms = common::NowMs();
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
        [&emit, &assembled](const std::string &delta) {
            assembled.append(delta);
            nlohmann::json chunk;
            chunk["delta"] = delta;
            return emit("delta", chunk.dump());
        },
        llm_response,
        error);

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

bool ChatService::GetHistory(const std::string &sid,
                             const std::string &conversation_id,
                             size_t limit,
                             std::vector<common::ChatMessage> &messages,
                             std::string &error,
                             http::HttpStatus &status)
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

    std::vector<common::ChatMessage> context = SnapshotContext(sid, conversation_id);
    if (!context.empty())
    {
        if (context.size() > limit)
        {
            messages.assign(context.end() - limit, context.end());
        }
        else
        {
            messages.swap(context);
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

std::string ChatService::BuildContextKey(const std::string &sid, const std::string &conversation_id) const
{
    return sid + "#" + conversation_id;
}

bool ChatService::EnsureContextLoaded(const std::string &sid,
                                      const std::string &conversation_id,
                                      std::string &error,
                                      http::HttpStatus &status)
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
    if (!m_store->LoadRecentMessages(sid, conversation_id, m_settings.history_load_limit, loaded_messages, error))
    {
        status = http::HttpStatus::INTERNAL_SERVER_ERROR;
        return false;
    }

    ConversationContext context;
    context.messages.swap(loaded_messages);
    context.touched_at_ms = common::NowMs();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_contexts[key] = context;
    return true;
}

std::vector<common::ChatMessage> ChatService::SnapshotContext(const std::string &sid, const std::string &conversation_id)
{
    const std::string key = BuildContextKey(sid, conversation_id);
    std::lock_guard<std::mutex> lock(m_mutex);
    std::unordered_map<std::string, ConversationContext>::iterator it = m_contexts.find(key);
    if (it == m_contexts.end())
    {
        return std::vector<common::ChatMessage>();
    }

    it->second.touched_at_ms = common::NowMs();
    return it->second.messages;
}

void ChatService::AppendContextMessages(const std::string &sid,
                                        const std::string &conversation_id,
                                        const common::ChatMessage &user_message,
                                        const common::ChatMessage &assistant_message)
{
    const std::string key = BuildContextKey(sid, conversation_id);

    std::lock_guard<std::mutex> lock(m_mutex);
    ConversationContext &context = m_contexts[key];
    context.messages.push_back(user_message);
    context.messages.push_back(assistant_message);
    context.touched_at_ms = common::NowMs();

    if (context.messages.size() > m_settings.max_context_messages)
    {
        size_t remove_count = context.messages.size() - m_settings.max_context_messages;
        context.messages.erase(context.messages.begin(), context.messages.begin() + remove_count);
    }
}

bool ChatService::PersistMessage(const common::PersistMessage &message, std::string &error)
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
