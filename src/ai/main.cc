#include "ai/common/ai_utils.h"
#include "ai/config/ai_app_config.h"
#include "ai/http/ai_http_api.h"
#include "ai/llm/deepseek_client.h"
#include "ai/service/chat_service.h"
#include "ai/storage/async_mysql_writer.h"
#include "ai/storage/chat_repository.h"

#include "http/core/http_framework_config.h"
#include "http/middleware/middleware.h"
#include "http/server/http_server.h"
#include "http/ssl/ssl_config.h"
#include "log/logger.h"
#include "sylar/fiber/hook.h"
#include "sylar/net/address.h"

#include "config/config.h"

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

    bool LoadConfigFromFile(const std::string &config_file, std::string &error)
    {
        if (config_file.empty())
        {
            return true;
        }

        try
        {
            YAML::Node node = YAML::LoadFile(config_file);
            base::Config::LoadFromYaml(node);
            return true;
        }
        catch (const std::exception &ex)
        {
            error = ex.what();
            return false;
        }
    }

    std::string ParseConfigFilePath(int argc, char **argv)
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

} // namespace

int main(int argc, char **argv)
{
    sylar::set_hook_enable(true);

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

    http::HttpFrameworkConfig::SetSessionEnabled(true);

    const ai::config::ServerSettings server_settings = ai::config::AiAppConfig::GetServerSettings();
    const ai::config::DeepSeekSettings deepseek_settings = ai::config::AiAppConfig::GetDeepSeekSettings();
    const ai::config::ChatSettings chat_settings = ai::config::AiAppConfig::GetChatSettings();
    const ai::config::MysqlSettings mysql_settings = ai::config::AiAppConfig::GetMysqlSettings();
    const ai::config::PersistSettings persist_settings = ai::config::AiAppConfig::GetPersistSettings();

    ai::storage::ChatRepository::ptr chat_repository(new ai::storage::ChatRepository(mysql_settings));
    if (!chat_repository->Init(error))
    {
        std::cerr << "init chat repository failed: " << error << std::endl;
        return 1;
    }

    ai::storage::AsyncMySqlWriter::ptr async_writer(new ai::storage::AsyncMySqlWriter(mysql_settings, persist_settings));
    if (!async_writer->Start(error))
    {
        std::cerr << "start async mysql writer failed: " << error << std::endl;
        return 1;
    }

    ai::llm::LlmClient::ptr llm_client(new ai::llm::DeepSeekClient(deepseek_settings));
    ai::service::ChatService::ptr chat_service(new ai::service::ChatService(
        chat_settings, llm_client, chat_repository, async_writer));

    http::HttpServer::ptr server = http::HttpServer::CreateWithConfig();

    if (server_settings.enable_ssl)
    {
        http::ssl::SslConfig ssl_config;
        ssl_config.setCertificateFile(server_settings.cert_file);
        ssl_config.setPrivateKeyFile(server_settings.key_file);
        ssl_config.setVerifyPeer(false);
        if (!server->setSslConfig(ssl_config))
        {
            std::cerr << "configure server ssl failed" << std::endl;
            async_writer->Stop();
            return 1;
        }
    }

    server->addMiddleware(http::Middleware::ptr(new http::CallbackMiddleware(
        [](http::HttpRequest::ptr request, http::HttpResponse::ptr response, http::HttpSession::ptr)
        {
            const std::string request_id = ai::common::GenerateRequestId();
            request->setHeader("X-Request-Id", request_id);
            response->setHeader("X-Request-Id", request_id);
            return true;
        },
        http::CallbackMiddleware::AfterCallback())));

    ai::api::RegisterAiHttpApi(server, chat_service, chat_settings, deepseek_settings.default_model);

    sylar::Address::ptr bind_addr =
        sylar::Address::LookupAny(server_settings.host + ":" + std::to_string(server_settings.port));
    if (!bind_addr)
    {
        std::cerr << "invalid bind address: " << server_settings.host << ":" << server_settings.port << std::endl;
        async_writer->Stop();
        return 1;
    }

    std::vector<sylar::Address::ptr> addrs;
    std::vector<sylar::Address::ptr> fails;
    addrs.push_back(bind_addr);

    if (!server->bind(addrs, fails) || !server->start())
    {
        std::cerr << "start ai chat server failed at " << server_settings.host << ":" << server_settings.port << std::endl;
        async_writer->Stop();
        return 1;
    }

    std::cout << "ai_chat_server started on " << server_settings.host << ":" << server_settings.port
              << " ssl=" << (server_settings.enable_ssl ? "on" : "off") << std::endl;

    ::signal(SIGINT, HandleSignal);
    ::signal(SIGTERM, HandleSignal);

    while (!g_stop_requested.load(std::memory_order_acquire))
    {
        sleep(1);
    }

    BASE_LOG_INFO(g_logger) << "ai_chat_server stopping";
    server->stop();
    async_writer->Stop();
    return 0;
}
