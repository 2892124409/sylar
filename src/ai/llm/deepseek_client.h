#ifndef __SYLAR_AI_LLM_DEEPSEEK_CLIENT_H__
#define __SYLAR_AI_LLM_DEEPSEEK_CLIENT_H__

#include "ai/config/ai_app_config.h"
#include "ai/llm/llm_client.h"

#include <string>

namespace ai
{
    namespace llm
    {

        class DeepSeekClient : public LlmClient
        {
        public:
            explicit DeepSeekClient(const config::DeepSeekSettings &settings);

            virtual bool Complete(const LlmCompletionRequest &request,
                                  LlmCompletionResult &result,
                                  std::string &error) override;

            virtual bool StreamComplete(const LlmCompletionRequest &request,
                                        const DeltaCallback &on_delta,
                                        LlmCompletionResult &result,
                                        std::string &error) override;

        private:
            std::string BuildCompletionsUrl() const;

        private:
            config::DeepSeekSettings m_settings;
        };

    } // namespace llm
} // namespace ai

#endif
