#ifndef __SYLAR_AI_LLM_LLM_CLIENT_H__
#define __SYLAR_AI_LLM_LLM_CLIENT_H__

#include "ai/common/ai_types.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ai
{
namespace llm
{

struct LlmCompletionRequest
{
    std::string model;
    double temperature = 0.7;
    uint32_t max_tokens = 1024;
    std::vector<common::ChatMessage> messages;
};

struct LlmCompletionResult
{
    std::string content;
    std::string model;
    std::string finish_reason;
    uint64_t prompt_tokens = 0;
    uint64_t completion_tokens = 0;
};

class LlmClient
{
public:
    typedef std::shared_ptr<LlmClient> ptr;
    typedef std::function<bool(const std::string &delta)> DeltaCallback;

    virtual ~LlmClient() {}

    virtual bool Complete(const LlmCompletionRequest &request,
                          LlmCompletionResult &result,
                          std::string &error) = 0;

    virtual bool StreamComplete(const LlmCompletionRequest &request,
                                const DeltaCallback &on_delta,
                                LlmCompletionResult &result,
                                std::string &error) = 0;
};

} // namespace llm
} // namespace ai

#endif
