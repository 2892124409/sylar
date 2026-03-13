#include "ai/llm/deepseek_client.h"

#include "log/logger.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <mutex>
#include <sstream>

namespace ai
{
    namespace llm
    {

        static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

        namespace
        {

            struct SyncWriteContext
            {
                std::string response;
            };

            struct StreamWriteContext
            {
                LlmClient::DeltaCallback on_delta;
                std::string line_buffer;
                std::string assembled;
                std::string model;
                std::string finish_reason;
                uint64_t prompt_tokens = 0;
                uint64_t completion_tokens = 0;
                std::string parse_error;
                bool callback_aborted = false;
            };

            void EnsureCurlGlobalInit()
            {
                static std::once_flag s_init_flag;
                std::call_once(s_init_flag, []()
                               { curl_global_init(CURL_GLOBAL_DEFAULT); });
            }

            std::string BuildAuthorizationHeader(const std::string &api_key)
            {
                return "Authorization: Bearer " + api_key;
            }

            size_t SyncWriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
            {
                size_t total = size * nmemb;
                SyncWriteContext *ctx = static_cast<SyncWriteContext *>(userp);
                ctx->response.append(static_cast<const char *>(contents), total);
                return total;
            }

            static std::string Trim(const std::string &value)
            {
                size_t begin = 0;
                while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r'))
                {
                    ++begin;
                }
                size_t end = value.size();
                while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r'))
                {
                    --end;
                }
                return value.substr(begin, end - begin);
            }

            bool HandleStreamDataLine(const std::string &line, StreamWriteContext &ctx)
            {
                if (line.empty())
                {
                    return true;
                }

                const std::string prefix = "data:";
                if (line.compare(0, prefix.size(), prefix) != 0)
                {
                    return true;
                }

                std::string payload = Trim(line.substr(prefix.size()));
                if (payload.empty())
                {
                    return true;
                }

                if (payload == "[DONE]")
                {
                    return true;
                }

                nlohmann::json chunk = nlohmann::json::parse(payload, nullptr, false);
                if (chunk.is_discarded())
                {
                    ctx.parse_error = "invalid stream chunk json";
                    return false;
                }

                if (chunk.contains("model") && chunk["model"].is_string())
                {
                    ctx.model = chunk["model"].get<std::string>();
                }

                if (chunk.contains("choices") && chunk["choices"].is_array() && !chunk["choices"].empty())
                {
                    const nlohmann::json &choice = chunk["choices"][0];
                    if (choice.contains("delta") && choice["delta"].is_object())
                    {
                        const nlohmann::json &delta = choice["delta"];
                        if (delta.contains("content") && delta["content"].is_string())
                        {
                            const std::string delta_text = delta["content"].get<std::string>();
                            if (!delta_text.empty())
                            {
                                ctx.assembled.append(delta_text);
                                if (ctx.on_delta && !ctx.on_delta(delta_text))
                                {
                                    ctx.callback_aborted = true;
                                    return false;
                                }
                            }
                        }
                    }

                    if (choice.contains("finish_reason") && choice["finish_reason"].is_string())
                    {
                        ctx.finish_reason = choice["finish_reason"].get<std::string>();
                    }
                }

                if (chunk.contains("usage") && chunk["usage"].is_object())
                {
                    const nlohmann::json &usage = chunk["usage"];
                    if (usage.contains("prompt_tokens") && usage["prompt_tokens"].is_number_unsigned())
                    {
                        ctx.prompt_tokens = usage["prompt_tokens"].get<uint64_t>();
                    }
                    if (usage.contains("completion_tokens") && usage["completion_tokens"].is_number_unsigned())
                    {
                        ctx.completion_tokens = usage["completion_tokens"].get<uint64_t>();
                    }
                }

                return true;
            }

            size_t StreamWriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
            {
                size_t total = size * nmemb;
                StreamWriteContext *ctx = static_cast<StreamWriteContext *>(userp);
                ctx->line_buffer.append(static_cast<const char *>(contents), total);

                size_t pos = 0;
                while (true)
                {
                    size_t line_end = ctx->line_buffer.find('\n', pos);
                    if (line_end == std::string::npos)
                    {
                        break;
                    }

                    std::string line = ctx->line_buffer.substr(pos, line_end - pos);
                    if (!line.empty() && line[line.size() - 1] == '\r')
                    {
                        line.erase(line.size() - 1);
                    }

                    if (!HandleStreamDataLine(line, *ctx))
                    {
                        return 0;
                    }

                    pos = line_end + 1;
                }

                if (pos > 0)
                {
                    ctx->line_buffer.erase(0, pos);
                }

                return total;
            }

            nlohmann::json BuildRequestBody(const LlmCompletionRequest &request, bool stream)
            {
                nlohmann::json body;
                body["model"] = request.model;
                body["temperature"] = request.temperature;
                body["max_tokens"] = request.max_tokens;
                body["stream"] = stream;

                body["messages"] = nlohmann::json::array();
                for (size_t i = 0; i < request.messages.size(); ++i)
                {
                    nlohmann::json item;
                    item["role"] = request.messages[i].role;
                    item["content"] = request.messages[i].content;
                    body["messages"].push_back(item);
                }

                return body;
            }

            bool ParseSyncResponse(const std::string &response_text, LlmCompletionResult &result, std::string &error)
            {
                nlohmann::json parsed = nlohmann::json::parse(response_text, nullptr, false);
                if (parsed.is_discarded())
                {
                    error = "deepseek response is not valid json";
                    return false;
                }

                if (parsed.contains("error") && parsed["error"].is_object())
                {
                    const nlohmann::json &error_node = parsed["error"];
                    if (error_node.contains("message") && error_node["message"].is_string())
                    {
                        error = error_node["message"].get<std::string>();
                    }
                    else
                    {
                        error = "deepseek returned error";
                    }
                    return false;
                }

                if (parsed.contains("model") && parsed["model"].is_string())
                {
                    result.model = parsed["model"].get<std::string>();
                }

                if (!parsed.contains("choices") || !parsed["choices"].is_array() || parsed["choices"].empty())
                {
                    error = "deepseek response missing choices";
                    return false;
                }

                const nlohmann::json &choice = parsed["choices"][0];
                if (choice.contains("finish_reason") && choice["finish_reason"].is_string())
                {
                    result.finish_reason = choice["finish_reason"].get<std::string>();
                }

                if (!choice.contains("message") || !choice["message"].is_object())
                {
                    error = "deepseek response missing message";
                    return false;
                }

                const nlohmann::json &message = choice["message"];
                if (!message.contains("content") || !message["content"].is_string())
                {
                    error = "deepseek response missing message.content";
                    return false;
                }

                result.content = message["content"].get<std::string>();

                if (parsed.contains("usage") && parsed["usage"].is_object())
                {
                    const nlohmann::json &usage = parsed["usage"];
                    if (usage.contains("prompt_tokens") && usage["prompt_tokens"].is_number_unsigned())
                    {
                        result.prompt_tokens = usage["prompt_tokens"].get<uint64_t>();
                    }
                    if (usage.contains("completion_tokens") && usage["completion_tokens"].is_number_unsigned())
                    {
                        result.completion_tokens = usage["completion_tokens"].get<uint64_t>();
                    }
                }

                return true;
            }

        } // namespace

        DeepSeekClient::DeepSeekClient(const config::DeepSeekSettings &settings)
            : m_settings(settings)
        {
            EnsureCurlGlobalInit();
        }

        std::string DeepSeekClient::BuildCompletionsUrl() const
        {
            if (m_settings.base_url.empty())
            {
                return "https://api.deepseek.com/v1/chat/completions";
            }

            if (m_settings.base_url[m_settings.base_url.size() - 1] == '/')
            {
                return m_settings.base_url + "chat/completions";
            }
            return m_settings.base_url + "/chat/completions";
        }

        bool DeepSeekClient::Complete(const LlmCompletionRequest &request,
                                      LlmCompletionResult &result,
                                      std::string &error)
        {
            CURL *curl = curl_easy_init();
            if (!curl)
            {
                error = "curl_easy_init failed";
                return false;
            }

            SyncWriteContext write_ctx;

            nlohmann::json body = BuildRequestBody(request, false);
            const std::string payload = body.dump();

            struct curl_slist *headers = nullptr;
            const std::string auth = BuildAuthorizationHeader(m_settings.api_key);
            headers = curl_slist_append(headers, auth.c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, BuildCompletionsUrl().c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(m_settings.connect_timeout_ms));
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(m_settings.request_timeout_ms));
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SyncWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);

            CURLcode code = curl_easy_perform(curl);

            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (code != CURLE_OK)
            {
                error = std::string("deepseek request failed: ") + curl_easy_strerror(code);
                return false;
            }

            if (http_code < 200 || http_code >= 300)
            {
                std::ostringstream ss;
                ss << "deepseek http status " << http_code;
                if (!write_ctx.response.empty())
                {
                    ss << ", body=" << write_ctx.response;
                }
                error = ss.str();
                return false;
            }

            if (!ParseSyncResponse(write_ctx.response, result, error))
            {
                BASE_LOG_ERROR(g_logger) << "Parse deepseek sync response failed, body=" << write_ctx.response;
                return false;
            }

            if (result.model.empty())
            {
                result.model = request.model;
            }

            return true;
        }

        bool DeepSeekClient::StreamComplete(const LlmCompletionRequest &request,
                                            const DeltaCallback &on_delta,
                                            LlmCompletionResult &result,
                                            std::string &error)
        {
            CURL *curl = curl_easy_init();
            if (!curl)
            {
                error = "curl_easy_init failed";
                return false;
            }

            StreamWriteContext write_ctx;
            write_ctx.on_delta = on_delta;

            nlohmann::json body = BuildRequestBody(request, true);
            const std::string payload = body.dump();

            struct curl_slist *headers = nullptr;
            const std::string auth = BuildAuthorizationHeader(m_settings.api_key);
            headers = curl_slist_append(headers, auth.c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, BuildCompletionsUrl().c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(m_settings.connect_timeout_ms));
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(m_settings.request_timeout_ms));
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);

            CURLcode code = curl_easy_perform(curl);

            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (code != CURLE_OK)
            {
                if (write_ctx.callback_aborted)
                {
                    error = "stream callback aborted";
                }
                else if (!write_ctx.parse_error.empty())
                {
                    error = write_ctx.parse_error;
                }
                else
                {
                    error = std::string("deepseek stream request failed: ") + curl_easy_strerror(code);
                }
                return false;
            }

            if (http_code < 200 || http_code >= 300)
            {
                std::ostringstream ss;
                ss << "deepseek stream http status " << http_code;
                error = ss.str();
                return false;
            }

            if (!write_ctx.parse_error.empty())
            {
                error = write_ctx.parse_error;
                return false;
            }

            result.content = write_ctx.assembled;
            result.model = write_ctx.model.empty() ? request.model : write_ctx.model;
            result.finish_reason = write_ctx.finish_reason;
            result.prompt_tokens = write_ctx.prompt_tokens;
            result.completion_tokens = write_ctx.completion_tokens;
            return true;
        }

    } // namespace llm
} // namespace ai
