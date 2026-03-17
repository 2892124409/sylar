#include "ai/common/ai_utils.h"
#include "ai/config/ai_app_config.h"
#include "ai/http/ai_http_api.h"
#include "ai/llm/anthropic_client.h"
#include "ai/llm/openai_compatible_client.h"
#include "ai/service/chat_service.h"
#include "ai/storage/async_mysql_writer.h"
#include "ai/storage/chat_repository.h"
#include "ai/storage/mysql_connection_pool.h"

#include "http/core/http_framework_config.h"
#include "http/middleware/middleware.h"
#include "http/server/http_server.h"
#include "http/ssl/ssl_config.h"
#include "log/logger.h"
#include "sylar/fiber/fiber_framework_config.h"
#include "sylar/fiber/hook.h"
#include "sylar/net/address.h"

#include "config/config.h"

#include <yaml-cpp/yaml.h>

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <iostream>
#include <vector>

/**
 * @file main.cc
 * @brief AI 对话服务端入口，负责配置加载、依赖装配、HTTP 启动与优雅停机。
 */

namespace
{

/** @brief 当前编译单元使用的系统日志器。 */
static base::Logger::ptr g_logger = BASE_LOG_NAME("system");
/** @brief 进程停止标志，由信号处理函数置位，主循环轮询读取。 */
std::atomic<bool> g_stop_requested(false);

/**
 * @brief POSIX 信号处理函数，仅设置停止标志。
 * @param signo 接收到的信号值（当前未使用）。
 */
void HandleSignal(int signo)
{
    (void)signo;
    g_stop_requested.store(true, std::memory_order_release);
}

/**
 * @brief 从 YAML 文件加载配置并合并到全局配置中心。
 * @param config_file 配置文件路径。为空时使用默认注册配置。
 * @param[out] error 加载失败时返回异常信息。
 * @return true 加载成功；false 加载失败。
 */
bool LoadConfigFromFile(const std::string& config_file, std::string& error)
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
    catch (const std::exception& ex)
    {
        error = ex.what();
        return false;
    }
}

/**
 * @brief 从命令行解析配置文件路径。
 * @details 支持三种格式：
 *   - `-c /path/to/config.yml`
 *   - `--config /path/to/config.yml`
 *   - `--config=/path/to/config.yml`
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组。
 * @return 配置文件路径；未提供时返回空字符串。
 */
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

} // namespace

/**
 * @brief AI 对话服务端主入口。
 * @details 执行流程：
 *   1) 启用 hook；
 *   2) 加载并校验配置；
 *   3) 初始化存储仓库与异步写入线程；
 *   4) 组装 LLM 与 ChatService；
 *   5) 注册路由并启动 HTTP 服务；
 *   6) 等待 SIGINT/SIGTERM；
 *   7) 执行优雅停机。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组。
 * @return 0 正常退出；非 0 启动或运行失败。
 */
int main(int argc, char** argv)
{
    /// @brief Step 0: 启用 sylar hook。
    /// @details
    /// 让后续网络/定时等待等阻塞点在 IOManager 线程内可被 fiber 化调度，
    /// 降低“一个阻塞调用占死一个工作线程”的风险。
    sylar::set_hook_enable(true);

    /// @brief Step 1: 解析命令行配置路径并加载 YAML 到全局配置中心。
    std::string config_file = ParseConfigFilePath(argc, argv);
    std::string error;
    if (!LoadConfigFromFile(config_file, error))
    {
        /// @note 配置加载失败直接退出，避免带默认值误启动。
        std::cerr << "load config failed: " << error << std::endl;
        return 1;
    }

    /// @brief Step 2: 启动前配置合法性校验（含 provider 分支校验）。
    if (!ai::config::AiAppConfig::Validate(error))
    {
        /// @note fail-fast：配置不合法不进入任何后续初始化步骤。
        std::cerr << "invalid ai app config: " << error << std::endl;
        return 1;
    }

    /// @brief 聊天接口依赖 SID，会话机制必须启用。
    http::HttpFrameworkConfig::SetSessionEnabled(true);

    const ai::config::ServerSettings server_settings = ai::config::AiAppConfig::GetServerSettings();
    const ai::config::ProviderSettings provider_settings = ai::config::AiAppConfig::GetProviderSettings();
    const ai::config::OpenAICompatibleSettings openai_settings = ai::config::AiAppConfig::GetOpenAICompatibleSettings();
    const ai::config::AnthropicSettings anthropic_settings = ai::config::AiAppConfig::GetAnthropicSettings();
    const ai::config::ChatSettings chat_settings = ai::config::AiAppConfig::GetChatSettings();
    const ai::config::MysqlSettings mysql_settings = ai::config::AiAppConfig::GetMysqlSettings();
    const ai::config::PersistSettings persist_settings = ai::config::AiAppConfig::GetPersistSettings();

    /// @brief Step 3: 初始化 MySQL 连接池（读写共享）。
    ai::storage::MysqlConnectionPool::ptr mysql_pool(new ai::storage::MysqlConnectionPool());
    if (!mysql_pool->Init(mysql_settings, error))
    {
        /// @note 数据层基础设施不可用，直接退出。
        std::cerr << "init mysql connection pool failed: " << error << std::endl;
        return 1;
    }

    /// @brief Step 4: 初始化查询仓库（建表/迁移检查在这里做）。
    ai::storage::ChatRepository::ptr chat_repository(new ai::storage::ChatRepository(mysql_pool));
    if (!chat_repository->Init(error))
    {
        std::cerr << "init chat repository failed: " << error << std::endl;
        return 1;
    }

    /// @brief Step 5: 启动异步写入器（请求线程只入队，后台批量落库）。
    ai::storage::AsyncMySqlWriter::ptr async_writer(new ai::storage::AsyncMySqlWriter(mysql_pool, persist_settings));
    if (!async_writer->Start(error))
    {
        std::cerr << "start async mysql writer failed: " << error << std::endl;
        return 1;
    }

    /// @brief Step 6: 按 provider 类型创建具体 LLM Client。
    /// @details
    /// 业务层只依赖 LlmClient 抽象，provider 差异在 main 装配期收敛。
    ai::llm::LlmClient::ptr llm_client;
    std::string default_model;
    if (provider_settings.type == "anthropic")
    {
        /// @brief 把应用层配置映射到 Anthropic 客户端配置对象。
        ai::llm::AnthropicSettings settings;
        settings.base_url = anthropic_settings.base_url;
        settings.api_key = anthropic_settings.api_key;
        settings.default_model = anthropic_settings.default_model;
        settings.api_version = anthropic_settings.api_version;
        settings.connect_timeout_ms = anthropic_settings.connect_timeout_ms;
        settings.request_timeout_ms = anthropic_settings.request_timeout_ms;

        llm_client.reset(new ai::llm::AnthropicClient(settings));
        /// @brief API 默认模型用于未显式传 model 的请求。
        default_model = anthropic_settings.default_model;
    }
    else
    {
        /// @note Validate 已保证 type 合法；非 anthropic 分支即 openai_compatible。
        llm_client.reset(new ai::llm::OpenAICompatibleClient(openai_settings));
        default_model = openai_settings.default_model;
    }

    /// @brief Step 7: 组装业务编排核心 ChatService。
    ai::service::ChatService::ptr chat_service(new ai::service::ChatService(chat_settings, llm_client, chat_repository, async_writer));

    /// @brief Step 8: 按配置创建 HTTP worker（benchmark 同款模式）。
    /// @details
    /// - use_caller=true: 单 IOM，io/accept 共用同一个调度器。
    /// - use_caller=false: 双 IOM，accept 与 io 分离。
    const uint32_t io_worker_threads = http::HttpFrameworkConfig::GetIOWorkerThreads();
    const uint32_t accept_worker_threads = http::HttpFrameworkConfig::GetAcceptWorkerThreads();
    const bool use_caller = sylar::FiberFrameworkConfig::GetIOManagerUseCaller();

    sylar::IOManager::ptr io_worker;
    sylar::IOManager::ptr accept_worker;
    if (use_caller)
    {
        io_worker.reset(new sylar::IOManager(io_worker_threads, true, "sylar-http-worker"));
        accept_worker = io_worker;
    }
    else
    {
        io_worker.reset(new sylar::IOManager(io_worker_threads, false, "sylar-http-io-worker"));
        accept_worker.reset(new sylar::IOManager(accept_worker_threads, false, "sylar-http-accept-worker"));
    }

    /// @brief Step 9: 使用上述 worker 创建 HTTP Server。
    http::HttpServer::ptr server(new http::HttpServer(io_worker.get(), accept_worker.get()));

    /// @brief Step 10: 可选 TLS 配置（由 YAML 决定是否启用）。
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

    /// @brief Step 11: 注入 request_id 中间件，统一链路追踪字段。
    /// @details
    /// 同时写入 request/response 头，便于服务端日志与客户端响应做对应。
    server->addMiddleware(http::Middleware::ptr(new http::CallbackMiddleware(
        [](http::HttpRequest::ptr request, http::HttpResponse::ptr response, http::HttpSession::ptr)
        {
            const std::string request_id = ai::common::GenerateRequestId();
            request->setHeader("X-Request-Id", request_id);
            response->setHeader("X-Request-Id", request_id);
            return true;
        },
        http::CallbackMiddleware::AfterCallback())));

    /// @brief Step 12: 注册 AI 业务路由（health/completions/stream/history）。
    ai::api::RegisterAiHttpApi(server, chat_service, chat_settings, default_model);

    /// @brief Step 13: 解析并校验绑定地址（host:port）。
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

    /// @brief Step 14: bind + start。
    /// @note 失败时先停异步写线程，避免后台线程泄漏。
    if (!server->bind(addrs, fails) || !server->start())
    {
        std::cerr << "start ai chat server failed at " << server_settings.host << ":" << server_settings.port << std::endl;
        async_writer->Stop();
        return 1;
    }

    std::cout << "ai_chat_server started on " << server_settings.host << ":" << server_settings.port
              << " ssl=" << (server_settings.enable_ssl ? "on" : "off")
              << " use_caller=" << (use_caller ? "on" : "off") << std::endl;

    /// @brief Step 15: 注册优雅退出信号：Ctrl+C(SIGINT) / kill(SIGTERM)。
    ::signal(SIGINT, HandleSignal);
    ::signal(SIGTERM, HandleSignal);

    if (use_caller)
    {
        /// @brief caller 模式：
        /// 主线程进入 IOM 调度循环，通过定时器轮询停止标志触发关停。
        sylar::Timer::ptr stop_timer;
        stop_timer = io_worker->addTimer(200, [&]()
                                         {
            if (!g_stop_requested.load(std::memory_order_acquire))
            {
                return;
            }

            /// @brief 收到停止信号后按顺序停服与停 worker。
            BASE_LOG_INFO(g_logger) << "ai_chat_server stopping (caller mode)";
            server->stop();
            if (accept_worker && accept_worker != io_worker)
            {
                accept_worker->stop();
            }
            if (stop_timer)
            {
                /// @brief 关停触发后取消轮询定时器，避免重复执行关停逻辑。
                stop_timer->cancel();
            } }, true);
        /// @brief stop() 会阻塞直到调度器完成收尾并退出。
        io_worker->stop();
    }
    else
    {
        /// @brief 非 caller 模式：
        /// 主线程以 200ms 周期轮询停止标志，避免 busy loop。
        while (!g_stop_requested.load(std::memory_order_acquire))
        {
            usleep(200 * 1000);
        }

        /// @brief 停机顺序：先停 HTTP 服务，再停 accept/io worker。
        BASE_LOG_INFO(g_logger) << "ai_chat_server stopping";
        server->stop();
        if (accept_worker)
        {
            accept_worker->stop();
        }
        if (io_worker && io_worker != accept_worker)
        {
            io_worker->stop();
        }
    }

    /// @brief Step 16: 最终收尾。
    /// @details
    /// 1) 先停异步写线程，尽量把已入队消息刷盘完成；
    /// 2) 再关闭连接池，释放数据库连接资源。
    async_writer->Stop();
    mysql_pool->Shutdown();
    return 0;
}
