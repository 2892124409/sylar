#include "ai/common/ai_types.h"
#include "ai/config/ai_app_config.h"
#include "ai/llm/llm_client.h"
#include "ai/service/chat_interfaces.h"
#include "ai/service/chat_service.h"

#include <assert.h>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace
{

class FakeLlmClient : public ai::llm::LlmClient
{
public:
    virtual bool Complete(const ai::llm::LlmCompletionRequest &request,
                          ai::llm::LlmCompletionResult &result,
                          std::string &error) override
    {
        (void)error;
        last_request = request;
        result.content = "assistant reply";
        result.model = request.model;
        result.finish_reason = "stop";
        result.prompt_tokens = 10;
        result.completion_tokens = 5;
        return true;
    }

    virtual bool StreamComplete(const ai::llm::LlmCompletionRequest &request,
                                const DeltaCallback &on_delta,
                                ai::llm::LlmCompletionResult &result,
                                std::string &error) override
    {
        (void)error;
        last_request = request;
        if (!on_delta("hello ") || !on_delta("world"))
        {
            return false;
        }
        result.content = "hello world";
        result.model = request.model;
        result.finish_reason = "stop";
        result.prompt_tokens = 8;
        result.completion_tokens = 3;
        return true;
    }

    ai::llm::LlmCompletionRequest last_request;
};

class FakeStore : public ai::service::ChatStore
{
public:
    virtual bool LoadRecentMessages(const std::string &sid,
                                    const std::string &conversation_id,
                                    size_t limit,
                                    std::vector<ai::common::ChatMessage> &out,
                                    std::string &error) override
    {
        (void)sid;
        (void)conversation_id;
        (void)limit;
        (void)error;
        out = seed_messages;
        return true;
    }

    virtual bool LoadHistory(const std::string &sid,
                             const std::string &conversation_id,
                             size_t limit,
                             std::vector<ai::common::ChatMessage> &out,
                             std::string &error) override
    {
        (void)sid;
        (void)conversation_id;
        (void)error;
        out = seed_messages;
        if (out.size() > limit)
        {
            out.erase(out.begin(), out.end() - limit);
        }
        return true;
    }

    std::vector<ai::common::ChatMessage> seed_messages;
};

class FakeSink : public ai::service::MessageSink
{
public:
    virtual bool Enqueue(const ai::common::PersistMessage &message, std::string &error) override
    {
        (void)error;
        records.push_back(message);
        return true;
    }

    std::vector<ai::common::PersistMessage> records;
};

ai::config::ChatSettings BuildDefaultChatSettings()
{
    ai::config::ChatSettings settings;
    settings.require_sid = true;
    settings.max_context_messages = 20;
    settings.history_load_limit = 20;
    settings.history_query_limit_max = 200;
    settings.default_temperature = 0.7;
    settings.default_max_tokens = 1024;
    settings.system_prompt = "";
    return settings;
}

void TestCompleteAndHistory()
{
    ai::config::ChatSettings settings = BuildDefaultChatSettings();

    std::shared_ptr<FakeLlmClient> llm(new FakeLlmClient());
    std::shared_ptr<FakeStore> store(new FakeStore());
    std::shared_ptr<FakeSink> sink(new FakeSink());

    ai::service::ChatService service(settings, llm, store, sink);

    ai::common::ChatCompletionRequest request;
    request.sid = "sid-1";
    request.message = "hello";
    request.model = "deepseek-chat";
    request.temperature = 0.2;
    request.max_tokens = 256;

    ai::common::ChatCompletionResponse response;
    std::string error;
    http::HttpStatus status = http::HttpStatus::OK;

    bool ok = service.Complete(request, response, error, status);
    assert(ok);
    assert(status == http::HttpStatus::OK);
    assert(response.conversation_id.size() > 0);
    assert(response.reply == "assistant reply");
    assert(sink->records.size() == 2);
    assert(sink->records[0].role == "user");
    assert(sink->records[1].role == "assistant");

    std::vector<ai::common::ChatMessage> history;
    ok = service.GetHistory(request.sid, response.conversation_id, 20, history, error, status);
    assert(ok);
    assert(history.size() == 2);
    assert(history[0].role == "user");
    assert(history[1].role == "assistant");
}

void TestStream()
{
    ai::config::ChatSettings settings = BuildDefaultChatSettings();

    std::shared_ptr<FakeLlmClient> llm(new FakeLlmClient());
    std::shared_ptr<FakeStore> store(new FakeStore());
    std::shared_ptr<FakeSink> sink(new FakeSink());

    ai::service::ChatService service(settings, llm, store, sink);

    ai::common::ChatCompletionRequest request;
    request.sid = "sid-2";
    request.message = "stream";
    request.conversation_id = "conv-stream";
    request.model = "deepseek-chat";

    std::vector<std::pair<std::string, std::string> > events;

    ai::common::ChatCompletionResponse response;
    std::string error;
    http::HttpStatus status = http::HttpStatus::OK;

    bool ok = service.StreamComplete(
        request,
        [&events](const std::string &event, const std::string &data) {
            events.push_back(std::make_pair(event, data));
            return true;
        },
        response,
        error,
        status);

    assert(ok);
    assert(status == http::HttpStatus::OK);
    assert(sink->records.size() == 2);

    bool has_start = false;
    bool has_done = false;
    size_t delta_count = 0;

    for (size_t i = 0; i < events.size(); ++i)
    {
        if (events[i].first == "start")
        {
            has_start = true;
            nlohmann::json parsed = nlohmann::json::parse(events[i].second, nullptr, false);
            assert(!parsed.is_discarded());
            assert(parsed["conversation_id"].is_string());
        }
        else if (events[i].first == "delta")
        {
            ++delta_count;
        }
        else if (events[i].first == "done")
        {
            has_done = true;
        }
    }

    assert(has_start);
    assert(has_done);
    assert(delta_count == 2);
    assert(response.reply == "hello world");
}

void TestMissingSidRejected()
{
    ai::config::ChatSettings settings = BuildDefaultChatSettings();

    std::shared_ptr<FakeLlmClient> llm(new FakeLlmClient());
    std::shared_ptr<FakeStore> store(new FakeStore());
    std::shared_ptr<FakeSink> sink(new FakeSink());

    ai::service::ChatService service(settings, llm, store, sink);

    ai::common::ChatCompletionRequest request;
    request.sid = "";
    request.message = "hello";
    request.model = "deepseek-chat";

    ai::common::ChatCompletionResponse response;
    std::string error;
    http::HttpStatus status = http::HttpStatus::OK;
    bool ok = service.Complete(request, response, error, status);

    assert(!ok);
    assert(status == http::HttpStatus::BAD_REQUEST);
}

} // namespace

int main()
{
    TestCompleteAndHistory();
    TestStream();
    TestMissingSidRejected();
    return 0;
}
