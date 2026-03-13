#ifndef __SYLAR_AI_SERVICE_CHAT_INTERFACES_H__
#define __SYLAR_AI_SERVICE_CHAT_INTERFACES_H__

#include "ai/common/ai_types.h"

#include <memory>
#include <string>
#include <vector>

namespace ai
{
namespace service
{

class ChatStore
{
public:
    typedef std::shared_ptr<ChatStore> ptr;
    virtual ~ChatStore() {}

    virtual bool LoadRecentMessages(const std::string &sid,
                                    const std::string &conversation_id,
                                    size_t limit,
                                    std::vector<common::ChatMessage> &out,
                                    std::string &error) = 0;

    virtual bool LoadHistory(const std::string &sid,
                             const std::string &conversation_id,
                             size_t limit,
                             std::vector<common::ChatMessage> &out,
                             std::string &error) = 0;
};

class MessageSink
{
public:
    typedef std::shared_ptr<MessageSink> ptr;
    virtual ~MessageSink() {}

    virtual bool Enqueue(const common::PersistMessage &message, std::string &error) = 0;
};

} // namespace service
} // namespace ai

#endif
