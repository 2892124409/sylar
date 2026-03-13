#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

struct Options
{
    std::string base_url = "http://127.0.0.1:8080";
    std::string model = "";
    std::string conversation_id = "";
    double temperature = 0.7;
    uint32_t max_tokens = 512;
    bool stream = false;
};

struct HttpResult
{
    long status = 0;
    std::string body;
    std::string error;
};

struct StreamResult
{
    long status = 0;
    std::string assistant_reply;
    std::string conversation_id;
    std::string error;
};

std::string Trim(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
    {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
    {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool IsLocalBaseUrl(const std::string &base_url)
{
    return base_url.compare(0, 16, "http://127.0.0.1") == 0
           || base_url.compare(0, 17, "https://127.0.0.1") == 0
           || base_url.compare(0, 16, "http://localhost") == 0
           || base_url.compare(0, 17, "https://localhost") == 0
           || base_url.compare(0, 11, "http://[::1]") == 0
           || base_url.compare(0, 12, "https://[::1]") == 0;
}

std::string DumpJsonSafe(const nlohmann::json &value)
{
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

void PrintUsage(const char *argv0)
{
    std::cout
        << "Usage: " << argv0 << " [options]\n"
        << "  --base-url <url>         Server base url, default http://127.0.0.1:8080\n"
        << "  --model <name>           Model name, default use server config\n"
        << "  --conversation-id <id>   Start with existing conversation id\n"
        << "  --temperature <float>    Sampling temperature, default 0.7\n"
        << "  --max-tokens <int>       Max tokens, default 512\n"
        << "  --stream                 Use /api/v1/chat/stream\n"
        << "  --help                   Show help\n\n"
        << "Interactive commands:\n"
        << "  /exit or /quit           Exit client\n"
        << "  /new                     Start a new conversation\n"
        << "  /history [limit]         Query current conversation history\n"
        << "  /stream on|off           Toggle stream mode\n";
}

bool ParseDouble(const std::string &text, double &out)
{
    char *end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end && *end == '\0';
}

bool ParseUint32(const std::string &text, uint32_t &out)
{
    char *end = nullptr;
    unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (!end || *end != '\0' || value > 0xFFFFFFFFul)
    {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

bool ParseArgs(int argc, char **argv, Options &opts)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help")
        {
            PrintUsage(argv[0]);
            return false;
        }
        if (arg == "--base-url" && i + 1 < argc)
        {
            opts.base_url = argv[++i];
            continue;
        }
        if (arg == "--model" && i + 1 < argc)
        {
            opts.model = argv[++i];
            continue;
        }
        if (arg == "--conversation-id" && i + 1 < argc)
        {
            opts.conversation_id = argv[++i];
            continue;
        }
        if (arg == "--temperature" && i + 1 < argc)
        {
            double value = 0;
            if (!ParseDouble(argv[++i], value))
            {
                std::cerr << "Invalid --temperature\n";
                return false;
            }
            opts.temperature = value;
            continue;
        }
        if (arg == "--max-tokens" && i + 1 < argc)
        {
            uint32_t value = 0;
            if (!ParseUint32(argv[++i], value))
            {
                std::cerr << "Invalid --max-tokens\n";
                return false;
            }
            opts.max_tokens = value;
            continue;
        }
        if (arg == "--stream")
        {
            opts.stream = true;
            continue;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        PrintUsage(argv[0]);
        return false;
    }

    if (!opts.base_url.empty() && opts.base_url[opts.base_url.size() - 1] == '/')
    {
        opts.base_url.erase(opts.base_url.size() - 1);
    }

    return true;
}

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    std::string *buffer = static_cast<std::string *>(userp);
    buffer->append(static_cast<const char *>(contents), total);
    return total;
}

class SseParser
{
public:
    explicit SseParser(StreamResult *result)
        : m_result(result)
    {
    }

    bool OnChunk(const char *data, size_t len)
    {
        m_buffer.append(data, len);
        size_t pos = 0;

        while (true)
        {
            size_t nl = m_buffer.find('\n', pos);
            if (nl == std::string::npos)
            {
                break;
            }

            std::string line = m_buffer.substr(pos, nl - pos);
            if (!line.empty() && line[line.size() - 1] == '\r')
            {
                line.erase(line.size() - 1);
            }

            if (line.empty())
            {
                DispatchEvent();
            }
            else if (line.compare(0, 6, "event:") == 0)
            {
                m_event = Trim(line.substr(6));
            }
            else if (line.compare(0, 5, "data:") == 0)
            {
                if (!m_data.empty())
                {
                    m_data.push_back('\n');
                }
                m_data.append(Trim(line.substr(5)));
            }

            pos = nl + 1;
        }

        if (pos > 0)
        {
            m_buffer.erase(0, pos);
        }

        return true;
    }

private:
    void DispatchEvent()
    {
        if (m_event.empty() && m_data.empty())
        {
            return;
        }

        if (m_event == "start")
        {
            nlohmann::json j = nlohmann::json::parse(m_data, nullptr, false);
            if (!j.is_discarded() && j.contains("conversation_id") && j["conversation_id"].is_string())
            {
                m_result->conversation_id = j["conversation_id"].get<std::string>();
            }
            std::cout << "[start] conversation_id=" << m_result->conversation_id << "\n";
        }
        else if (m_event == "delta")
        {
            nlohmann::json j = nlohmann::json::parse(m_data, nullptr, false);
            if (!j.is_discarded() && j.contains("delta") && j["delta"].is_string())
            {
                std::string delta = j["delta"].get<std::string>();
                m_result->assistant_reply.append(delta);
                std::cout << delta << std::flush;
            }
        }
        else if (m_event == "done")
        {
            std::cout << "\n[done]\n";
        }
        else if (m_event == "error")
        {
            m_result->error = m_data;
            std::cout << "\n[error] " << m_data << "\n";
        }

        m_event.clear();
        m_data.clear();
    }

private:
    StreamResult *m_result;
    std::string m_buffer;
    std::string m_event;
    std::string m_data;
};

size_t SseWriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    SseParser *parser = static_cast<SseParser *>(userp);
    parser->OnChunk(static_cast<const char *>(contents), total);
    return total;
}

class ApiClient
{
public:
    explicit ApiClient(const std::string &base_url)
        : m_base_url(base_url)
    {
        m_curl = curl_easy_init();
        if (!m_curl)
        {
            throw std::runtime_error("curl_easy_init failed");
        }

        // Enable in-memory cookie engine so SID persists across requests.
        curl_easy_setopt(m_curl, CURLOPT_COOKIEFILE, "");
        curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
        curl_easy_setopt(m_curl, CURLOPT_TIMEOUT_MS, 120000L);
        if (IsLocalBaseUrl(m_base_url))
        {
            curl_easy_setopt(m_curl, CURLOPT_NOPROXY, "*");
        }

        if (std::getenv("AI_CHAT_CLIENT_VERBOSE"))
        {
            curl_easy_setopt(m_curl, CURLOPT_VERBOSE, 1L);
        }
    }

    ~ApiClient()
    {
        if (m_curl)
        {
            curl_easy_cleanup(m_curl);
            m_curl = nullptr;
        }
    }

    HttpResult Healthz()
    {
        return RequestJson("GET", m_base_url + "/api/v1/healthz", "");
    }

    HttpResult ChatCompletions(const nlohmann::json &payload)
    {
        return RequestJson("POST", m_base_url + "/api/v1/chat/completions", DumpJsonSafe(payload));
    }

    StreamResult ChatStream(const nlohmann::json &payload)
    {
        StreamResult result;

        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(m_curl, CURLOPT_URL, (m_base_url + "/api/v1/chat/stream").c_str());
        curl_easy_setopt(m_curl, CURLOPT_POST, 1L);
        curl_easy_setopt(m_curl, CURLOPT_HTTPGET, 0L);
        std::string body = DumpJsonSafe(payload);
        curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, body.size());

        SseParser parser(&result);
        curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, SseWriteCallback);
        curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &parser);

        CURLcode code = curl_easy_perform(m_curl);
        curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &result.status);

        if (code != CURLE_OK)
        {
            result.error = std::string("stream request failed: ") + curl_easy_strerror(code);
        }

        curl_slist_free_all(headers);
        curl_easy_setopt(m_curl, CURLOPT_POST, 0L);
        return result;
    }

    HttpResult History(const std::string &conversation_id, uint32_t limit)
    {
        std::ostringstream url;
        url << m_base_url << "/api/v1/chat/history/" << conversation_id << "?limit=" << limit;
        return RequestJson("GET", url.str(), "");
    }

private:
    HttpResult RequestJson(const std::string &method, const std::string &url, const std::string &body)
    {
        HttpResult result;
        result.body.clear();

        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &result.body);

        if (method == "POST")
        {
            curl_easy_setopt(m_curl, CURLOPT_POST, 1L);
            curl_easy_setopt(m_curl, CURLOPT_HTTPGET, 0L);
            curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, body.size());
        }
        else
        {
            curl_easy_setopt(m_curl, CURLOPT_POST, 0L);
            curl_easy_setopt(m_curl, CURLOPT_HTTPGET, 1L);
        }

        CURLcode code = curl_easy_perform(m_curl);
        curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &result.status);

        if (code != CURLE_OK)
        {
            result.error = curl_easy_strerror(code);
        }

        curl_slist_free_all(headers);
        return result;
    }

private:
    std::string m_base_url;
    CURL *m_curl = nullptr;
};

nlohmann::json BuildChatPayload(const std::string &message,
                                const Options &opts,
                                const std::string &conversation_id)
{
    nlohmann::json payload;
    payload["message"] = message;
    payload["temperature"] = opts.temperature;
    payload["max_tokens"] = opts.max_tokens;

    if (!opts.model.empty())
    {
        payload["model"] = opts.model;
    }

    if (!conversation_id.empty())
    {
        payload["conversation_id"] = conversation_id;
    }

    return payload;
}

void PrintJsonIfPossible(const std::string &text)
{
    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (!parsed.is_discarded())
    {
        std::cout << DumpJsonSafe(parsed) << "\n";
    }
    else
    {
        std::cout << text << "\n";
    }
}

} // namespace

int main(int argc, char **argv)
{
    bool curl_initialized = false;

    try
    {
        Options opts;
        if (!ParseArgs(argc, argv, opts))
        {
            return 0;
        }

        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        {
            std::cerr << "curl_global_init failed\n";
            return 1;
        }
        curl_initialized = true;

        ApiClient client(opts.base_url);

        HttpResult healthz = client.Healthz();
        if (!healthz.error.empty() || healthz.status != 200)
        {
            std::cerr << "healthz failed, status=" << healthz.status << " error=" << healthz.error << "\n";
            PrintJsonIfPossible(healthz.body);
            curl_global_cleanup();
            return 1;
        }

        std::cout << "Connected to " << opts.base_url << "\n";
        std::cout << "Mode: " << (opts.stream ? "stream" : "sync") << "\n";
        if (!opts.conversation_id.empty())
        {
            std::cout << "Start with conversation_id=" << opts.conversation_id << "\n";
        }

        std::string conversation_id = opts.conversation_id;

        while (true)
        {
            std::cout << "\nYou> ";
            std::string input;
            if (!std::getline(std::cin, input))
            {
                break;
            }

            input = Trim(input);
            if (input.empty())
            {
                continue;
            }

            if (input == "/exit" || input == "/quit")
            {
                break;
            }

            if (input == "/new")
            {
                conversation_id.clear();
                std::cout << "Started a new conversation.\n";
                continue;
            }

            if (input.compare(0, 8, "/stream ") == 0)
            {
                std::string mode = Trim(input.substr(8));
                if (mode == "on")
                {
                    opts.stream = true;
                    std::cout << "Stream mode enabled.\n";
                }
                else if (mode == "off")
                {
                    opts.stream = false;
                    std::cout << "Stream mode disabled.\n";
                }
                else
                {
                    std::cout << "Usage: /stream on|off\n";
                }
                continue;
            }

            if (input.compare(0, 8, "/history") == 0)
            {
                if (conversation_id.empty())
                {
                    std::cout << "No active conversation_id.\n";
                    continue;
                }

                uint32_t limit = 20;
                std::string rest = Trim(input.substr(8));
                if (!rest.empty())
                {
                    uint32_t parsed = 0;
                    if (ParseUint32(rest, parsed))
                    {
                        limit = parsed;
                    }
                }

                HttpResult history = client.History(conversation_id, limit);
                std::cout << "History status=" << history.status << "\n";
                PrintJsonIfPossible(history.body);
                continue;
            }

            nlohmann::json payload = BuildChatPayload(input, opts, conversation_id);

            if (!opts.stream)
            {
                HttpResult result = client.ChatCompletions(payload);
                if (!result.error.empty())
                {
                    std::cerr << "request error: " << result.error << "\n";
                    continue;
                }

                if (result.status != 200)
                {
                    std::cerr << "server returned status=" << result.status << "\n";
                    PrintJsonIfPossible(result.body);
                    continue;
                }

                nlohmann::json parsed = nlohmann::json::parse(result.body, nullptr, false);
                if (parsed.is_discarded())
                {
                    std::cout << "Raw response: " << result.body << "\n";
                    continue;
                }

                if (parsed.contains("conversation_id") && parsed["conversation_id"].is_string())
                {
                    conversation_id = parsed["conversation_id"].get<std::string>();
                }

                std::string reply = parsed.value("reply", "");
                std::cout << "AI(" << conversation_id << ")> " << reply << "\n";
                continue;
            }

            std::cout << "AI(" << (conversation_id.empty() ? "new" : conversation_id) << ")> " << std::flush;
            StreamResult stream_result = client.ChatStream(payload);
            if (stream_result.status != 200)
            {
                std::cout << "\nStream status=" << stream_result.status << "\n";
                if (!stream_result.error.empty())
                {
                    std::cout << stream_result.error << "\n";
                }
                continue;
            }

            if (!stream_result.conversation_id.empty())
            {
                conversation_id = stream_result.conversation_id;
            }

            if (!stream_result.error.empty())
            {
                std::cout << "\nStream error: " << stream_result.error << "\n";
                continue;
            }

            std::cout << "Conversation id: " << conversation_id << "\n";
        }
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Fatal: " << ex.what() << "\n";
        if (curl_initialized)
        {
            curl_global_cleanup();
        }
        return 1;
    }

    if (curl_initialized)
    {
        curl_global_cleanup();
    }
    return 0;
}
