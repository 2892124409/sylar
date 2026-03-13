#ifndef __SYLAR_AI_SERVICE_CHAT_SERVICE_H__
#define __SYLAR_AI_SERVICE_CHAT_SERVICE_H__

#include "ai/common/ai_types.h"
#include "ai/config/ai_app_config.h"
#include "ai/llm/llm_client.h"
#include "ai/service/chat_interfaces.h"
#include "http/core/http.h"

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ai
{
namespace service
{

class ChatService
{
public:
    typedef std::shared_ptr<ChatService> ptr;
    typedef std::function<bool(const std::string &event, const std::string &data)> StreamEventEmitter;

    ChatService(const config::ChatSettings &settings,
                const llm::LlmClient::ptr &llm_client,
                const ChatStore::ptr &store,
                const MessageSink::ptr &sink);

    bool Complete(const common::ChatCompletionRequest &request,
                  common::ChatCompletionResponse &response,
                  std::string &error,
                  http::HttpStatus &status);

    bool StreamComplete(const common::ChatCompletionRequest &request,
                        const StreamEventEmitter &emit,
                        common::ChatCompletionResponse &response,
                        std::string &error,
                        http::HttpStatus &status);

    bool GetHistory(const std::string &sid,
                    const std::string &conversation_id,
                    size_t limit,
                    std::vector<common::ChatMessage> &messages,
                    std::string &error,
                    http::HttpStatus &status);

private:
    struct ConversationContext
    {
        std::vector<common::ChatMessage> messages;
        uint64_t touched_at_ms = 0;
    };

private:
    std::string BuildContextKey(const std::string &sid, const std::string &conversation_id) const;

    bool EnsureContextLoaded(const std::string &sid,
                             const std::string &conversation_id,
                             std::string &error,
                             http::HttpStatus &status);

    std::vector<common::ChatMessage> SnapshotContext(const std::string &sid, const std::string &conversation_id);

    void AppendContextMessages(const std::string &sid,
                               const std::string &conversation_id,
                               const common::ChatMessage &user_message,
                               const common::ChatMessage &assistant_message);

    bool PersistMessage(const common::PersistMessage &message, std::string &error);

private:
    config::ChatSettings m_settings;
    llm::LlmClient::ptr m_llm_client;
    ChatStore::ptr m_store;
    MessageSink::ptr m_sink;

    std::mutex m_mutex;
    std::unordered_map<std::string, ConversationContext> m_contexts;
};

} // namespace service
} // namespace ai

#endif
