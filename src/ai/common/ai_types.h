#ifndef __SYLAR_AI_COMMON_AI_TYPES_H__
#define __SYLAR_AI_COMMON_AI_TYPES_H__

#include <stdint.h>

#include <string>
#include <vector>

namespace ai
{
namespace common
{

struct ChatMessage
{
    std::string role;
    std::string content;
    uint64_t created_at_ms = 0;
};

struct ChatCompletionRequest
{
    std::string sid;
    std::string conversation_id;
    std::string message;

    std::string model;
    double temperature = 0.7;
    uint32_t max_tokens = 1024;
};

struct ChatCompletionResponse
{
    std::string conversation_id;
    std::string reply;
    std::string model;
    std::string finish_reason;
    uint64_t created_at_ms = 0;
};

struct PersistMessage
{
    std::string sid;
    std::string conversation_id;
    std::string role;
    std::string content;
    uint64_t created_at_ms = 0;
};

} // namespace common
} // namespace ai

#endif
