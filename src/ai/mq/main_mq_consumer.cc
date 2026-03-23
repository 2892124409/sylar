#include "ai/common/ai_types.h"
#include "ai/config/ai_app_config.h"
#include "ai/mq/rabbitmq_amqp_client.h"
#include "ai/storage/chat_message_persister.h"
#include "ai/storage/mysql_connection_pool.h"

#include "config/config.h"
#include "log/logger.h"
#include "sylar/fiber/hook.h"

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <iostream>
#include <vector>

namespace
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");
std::atomic<bool> g_stop_requested(false);

void HandleSignal(int signo)
{
    (void)signo;
    g_stop_requested.store(true, std::memory_order_release);
}

std::string ParseConfigFilePath(int argc, char** argv)
{
    std::string config_file;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc)
        {
            config_file = argv[++i];
            continue;
        }

        const std::string prefix = "--config=";
        if (arg.compare(0, prefix.size(), prefix) == 0)
        {
            config_file = arg.substr(prefix.size());
        }
    }

    return config_file;
}

bool LoadConfigFromFile(const std::string& config_file, std::string& error)
{
    if (config_file.empty())
    {
        return true;
    }

    try
    {
        YAML::Node node = YAML::LoadFile(config_file);
        sylar::Config::LoadFromYaml(node);
        return true;
    }
    catch (const std::exception& ex)
    {
        error = ex.what();
        return false;
    }
}

bool DecodePersistPayload(const std::string& payload, ai::common::PersistMessage& out, std::string& error)
{
    nlohmann::json parsed = nlohmann::json::parse(payload, nullptr, false);
    if (parsed.is_discarded())
    {
        error = "persist payload is not valid json";
        return false;
    }

    if (!parsed.is_object())
    {
        error = "persist payload must be json object";
        return false;
    }

    if (!parsed.contains("sid") || !parsed["sid"].is_string() || parsed["sid"].get<std::string>().empty())
    {
        error = "persist payload sid is invalid";
        return false;
    }

    if (!parsed.contains("conversation_id") || !parsed["conversation_id"].is_string() ||
        parsed["conversation_id"].get<std::string>().empty())
    {
        error = "persist payload conversation_id is invalid";
        return false;
    }

    if (!parsed.contains("role") || !parsed["role"].is_string() || parsed["role"].get<std::string>().empty())
    {
        error = "persist payload role is invalid";
        return false;
    }

    if (!parsed.contains("content") || !parsed["content"].is_string())
    {
        error = "persist payload content is invalid";
        return false;
    }

    if (!parsed.contains("created_at_ms") || !parsed["created_at_ms"].is_number_unsigned())
    {
        error = "persist payload created_at_ms is invalid";
        return false;
    }

    out.sid = parsed["sid"].get<std::string>();
    out.conversation_id = parsed["conversation_id"].get<std::string>();
    out.role = parsed["role"].get<std::string>();
    out.content = parsed["content"].get<std::string>();
    out.created_at_ms = parsed["created_at_ms"].get<uint64_t>();
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    sylar::set_hook_enable(true);
    BASE_LOG_ROOT()->setLevel(sylar::LogLevel::INFO);
    BASE_LOG_NAME("system")->setLevel(sylar::LogLevel::INFO);

    std::string config_file = ParseConfigFilePath(argc, argv);
    std::string error;
    if (!LoadConfigFromFile(config_file, error))
    {
        std::cerr << "load config failed: " << error << std::endl;
        return 1;
    }

    if (!ai::config::AiAppConfig::Validate(error))
    {
        std::cerr << "invalid ai app config: " << error << std::endl;
        return 1;
    }

    const ai::config::MqSettings mq_settings = ai::config::AiAppConfig::GetMqSettings();
    if (!mq_settings.enabled)
    {
        std::cerr << "mq is disabled, no consumer work to run" << std::endl;
        return 1;
    }

    if (mq_settings.provider != "rabbitmq_amqp")
    {
        std::cerr << "unsupported mq provider: " << mq_settings.provider << std::endl;
        return 1;
    }

    const ai::config::MysqlSettings mysql_settings = ai::config::AiAppConfig::GetMysqlSettings();

    ai::storage::MysqlConnectionPool::ptr mysql_pool(new ai::storage::MysqlConnectionPool());
    if (!mysql_pool->Init(mysql_settings, error))
    {
        std::cerr << "init mysql connection pool failed: " << error << std::endl;
        return 1;
    }

    ai::storage::ChatMessagePersister::ptr persister(new ai::storage::ChatMessagePersister(mysql_pool));
    if (!persister->Init(error))
    {
        std::cerr << "init chat message persister failed: " << error << std::endl;
        return 1;
    }

    ai::mq::RabbitMqAmqpClient mq_client(mq_settings.rabbitmq);
    if (!mq_client.EnsureQueue(error))
    {
        std::cerr << "ensure rabbitmq queue failed: " << error << std::endl;
        return 1;
    }

    ::signal(SIGINT, HandleSignal);
    ::signal(SIGTERM, HandleSignal);

    std::cout << "ai_mq_consumer started, queue=" << mq_settings.rabbitmq.queue
              << " batch_size=" << mq_settings.consumer_batch_size << std::endl;

    while (!g_stop_requested.load(std::memory_order_acquire))
    {
        std::vector<std::string> payloads;
        if (!mq_client.Get(mq_settings.consumer_batch_size, payloads, error))
        {
            BASE_LOG_ERROR(g_logger) << "pull message from rabbitmq failed: " << error;
            usleep(static_cast<useconds_t>(mq_settings.consumer_poll_interval_ms * 1000));
            continue;
        }

        if (payloads.empty())
        {
            usleep(static_cast<useconds_t>(mq_settings.consumer_poll_interval_ms * 1000));
            continue;
        }

        std::vector<ai::common::PersistMessage> batch;
        batch.reserve(payloads.size());

        for (size_t i = 0; i < payloads.size(); ++i)
        {
            ai::common::PersistMessage message;
            std::string decode_error;
            if (!DecodePersistPayload(payloads[i], message, decode_error))
            {
                BASE_LOG_ERROR(g_logger) << "decode persist payload failed: " << decode_error;
                continue;
            }
            batch.push_back(message);
        }

        if (batch.empty())
        {
            continue;
        }

        if (!persister->PersistBatch(batch, error))
        {
            BASE_LOG_ERROR(g_logger) << "persist mq batch to mysql failed: " << error;
            usleep(static_cast<useconds_t>(mq_settings.consumer_poll_interval_ms * 1000));
            continue;
        }
    }

    mysql_pool->Shutdown();
    return 0;
}
