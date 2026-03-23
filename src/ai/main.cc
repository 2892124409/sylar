#include "ai/config/ai_app_config.h"
#include "ai/http/ai_http_api.h"
#include "ai/llm/anthropic_client.h"
#include "ai/llm/llm_client_factory.h"
#include "ai/llm/llm_client_registry.h"
#include "ai/llm/llm_router.h"
#include "ai/llm/openai_compatible_client.h"
#include "ai/llm/provider_key_pool.h"
#include "ai/middleware/auth_middleware.h"
#include "ai/middleware/request_id_middleware.h"
#include "ai/mq/rabbitmq_message_sink.h"
#include "ai/rag/embedding_client.h"
#include "ai/rag/rag_indexer.h"
#include "ai/rag/rag_retriever.h"
#include "ai/rag/vector_store.h"
#include "ai/service/auth_service.h"
#include "ai/service/chat_service.h"
#include "ai/storage/api_key_pool_repository.h"
#include "ai/storage/async_mysql_writer.h"
#include "ai/storage/auth_repository.h"
#include "ai/storage/chat_repository.h"
#include "ai/storage/mysql_connection_pool.h"

#include "http/core/http_framework_config.h"
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
#include <unordered_map>
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
/** @brief IOManager 是否启用 caller 模式。 */
sylar::ConfigVar<bool>::ptr g_iomanager_use_caller =
    sylar::Config::Lookup<bool>("iomanager.use_caller", true, "iomanager use_caller");

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
        sylar::Config::LoadFromYaml(node);
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
    BASE_LOG_ROOT()->setLevel(sylar::LogLevel::INFO);
    BASE_LOG_NAME("system")->setLevel(sylar::LogLevel::INFO);

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
    const ai::config::LlmSettings llm_settings = ai::config::AiAppConfig::GetLlmSettings();
    const ai::config::ChatSettings chat_settings = ai::config::AiAppConfig::GetChatSettings();
    const ai::config::AuthSettings auth_settings = ai::config::AiAppConfig::GetAuthSettings();
    const ai::config::MysqlSettings mysql_settings = ai::config::AiAppConfig::GetMysqlSettings();
    const ai::config::PersistSettings persist_settings = ai::config::AiAppConfig::GetPersistSettings();
    const ai::config::MqSettings mq_settings = ai::config::AiAppConfig::GetMqSettings();
    ai::config::RagSettings rag_settings = ai::config::AiAppConfig::GetRagSettings();
    const ai::config::EmbeddingSettings embedding_settings = ai::config::AiAppConfig::GetEmbeddingSettings();
    const ai::config::QdrantSettings qdrant_settings = ai::config::AiAppConfig::GetQdrantSettings();
    const ai::config::RagIndexerSettings rag_indexer_settings = ai::config::AiAppConfig::GetRagIndexerSettings();

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

    /// @brief Step 5: 初始化账号鉴权仓库（用户/Token 表结构）。
    ai::storage::AuthRepository::ptr auth_repository(new ai::storage::AuthRepository(mysql_pool));
    if (!auth_repository->Init(error))
    {
        std::cerr << "init auth repository failed: " << error << std::endl;
        return 1;
    }

    // 第五阶段：Provider 级 Key 池仓库与运行时池实例集合。
    // key: provider_id, value: ProviderKeyPool
    ai::storage::ApiKeyPoolRepository::ptr api_key_pool_repository;
    std::unordered_map<std::string, ai::llm::ProviderKeyPool::ptr> provider_key_pools;

    // 只有至少一个启用 provider 开启了 key_pool，才初始化 key_pool repository。
    bool need_key_pool_repo = false;
    for (size_t i = 0; i < llm_settings.providers.size(); ++i)
    {
        const ai::config::LlmProviderSettings& provider = llm_settings.providers[i];
        if (provider.enabled && provider.key_pool.enabled)
        {
            need_key_pool_repo = true;
            break;
        }
    }
    if (need_key_pool_repo)
    {
        api_key_pool_repository.reset(new ai::storage::ApiKeyPoolRepository(mysql_pool));
        if (!api_key_pool_repository->Init(error))
        {
            BASE_LOG_WARN(g_logger) << "init api key pool repository failed, fallback to single key for all providers: "
                                    << error;
            api_key_pool_repository.reset();
        }
    }

    /// @brief Step 6: 装配写路径 sink（优先 MQ，失败可回退本地异步写入）。
    ai::service::MessageSink::ptr message_sink;
    ai::mq::RabbitMqMessageSink::ptr mq_sink;
    ai::storage::AsyncMySqlWriter::ptr async_writer;

    if (mq_settings.enabled)
    {
        mq_sink.reset(new ai::mq::RabbitMqMessageSink(mq_settings));
        if (mq_sink->Start(error))
        {
            message_sink = mq_sink;
            BASE_LOG_INFO(g_logger) << "persist sink selected: rabbitmq_amqp";
        }
        else if (mq_settings.fallback_to_local_writer)
        {
            BASE_LOG_WARN(g_logger) << "start rabbitmq sink failed, fallback to local async writer: " << error;
            async_writer.reset(new ai::storage::AsyncMySqlWriter(mysql_pool, persist_settings));
            if (!async_writer->Start(error))
            {
                std::cerr << "start async mysql writer failed: " << error << std::endl;
                return 1;
            }
            message_sink = async_writer;
        }
        else
        {
            std::cerr << "start rabbitmq sink failed: " << error << std::endl;
            return 1;
        }
    }
    else
    {
        async_writer.reset(new ai::storage::AsyncMySqlWriter(mysql_pool, persist_settings));
        if (!async_writer->Start(error))
        {
            std::cerr << "start async mysql writer failed: " << error << std::endl;
            return 1;
        }
        message_sink = async_writer;
        BASE_LOG_INFO(g_logger) << "persist sink selected: local_async_mysql";
    }

    /// @brief Step 7: 按 provider.type 通过注册工厂装配多 Provider 客户端。
    /// @details
    /// 启动装配链路：
    /// 1) 可选创建 provider 级 key 池并注入 BuildOptions；
    /// 2) 由 Factory 按 provider.type 创建具体协议客户端；
    /// 3) 注册到 Registry（provider_id -> client entry）；
    /// 4) 最终构建 Router 供请求期动态路由。
    ai::llm::LlmClientFactory llm_factory = ai::llm::LlmClientFactory::BuildDefault();
    ai::llm::LlmClientRegistry::ptr llm_registry(new ai::llm::LlmClientRegistry());

    for (size_t i = 0; i < llm_settings.providers.size(); ++i)
    {
        const ai::config::LlmProviderSettings& provider = llm_settings.providers[i];
        if (!provider.enabled)
        {
            continue;
        }

        // BuildOptions 是协议无关注入点：key provider、重试预算等。
        ai::llm::LlmClientFactory::BuildOptions build_options;
        if (provider.key_pool.enabled)
        {
            build_options.max_retry_per_request = provider.key_pool.max_retry_per_request;
            if (api_key_pool_repository)
            {
                // 第五阶段：key pool 绑定 provider_id，避免跨 provider 污染状态。
                ai::llm::ProviderKeyPool::ptr key_pool(
                    new ai::llm::ProviderKeyPool(api_key_pool_repository, provider.key_pool, provider.id));
                std::string key_pool_error;
                if (!key_pool->Start(key_pool_error))
                {
                    BASE_LOG_WARN(g_logger) << "start provider key pool failed, provider_id=" << provider.id
                                            << " fallback to single key, error=" << key_pool_error;
                }
                else
                {
                    // 注入到具体客户端后，客户端会在每次请求前动态取 key 候选。
                    build_options.api_key_provider = key_pool;
                    provider_key_pools[provider.id] = key_pool;
                }
            }
        }

        ai::llm::LlmClient::ptr llm_client = llm_factory.Create(provider, build_options, error);
        if (!llm_client)
        {
            std::cerr << "create llm client failed, provider_id=" << provider.id << " error=" << error << std::endl;
            return 1;
        }

        ai::llm::LlmProviderEntry entry;
        entry.provider_id = provider.id;
        entry.provider_type = provider.type;
        entry.default_model = provider.default_model;
        entry.client = llm_client;
        if (!llm_registry->Register(entry, error))
        {
            std::cerr << "register llm provider failed, provider_id=" << provider.id << " error=" << error << std::endl;
            return 1;
        }
    }

    if (llm_registry->Size() == 0)
    {
        std::cerr << "no enabled llm providers loaded" << std::endl;
        return 1;
    }

    ai::llm::LlmRouter::ptr llm_router(new ai::llm::LlmRouter(
        llm_registry, llm_settings.routing.default_provider_id, llm_settings.routing.model_to_provider));

    /// @brief Step 8: 组装账号与对话业务服务。
    ai::service::AuthService::ptr auth_service(new ai::service::AuthService(auth_settings, auth_repository));

    ai::rag::EmbeddingClient::ptr embedding_client;
    ai::rag::VectorStore::ptr vector_store;
    ai::rag::RagRetriever::ptr rag_retriever;
    ai::rag::RagIndexer::ptr rag_indexer;

    if (rag_settings.enabled)
    {
        ai::rag::EmbeddingSettings rag_embedding_settings;
        rag_embedding_settings.base_url = embedding_settings.base_url;
        rag_embedding_settings.model = embedding_settings.model;
        rag_embedding_settings.connect_timeout_ms = embedding_settings.connect_timeout_ms;
        rag_embedding_settings.request_timeout_ms = embedding_settings.request_timeout_ms;
        embedding_client.reset(new ai::rag::OllamaEmbeddingClient(rag_embedding_settings));

        ai::rag::VectorStoreSettings rag_vector_store_settings;
        rag_vector_store_settings.base_url = qdrant_settings.base_url;
        rag_vector_store_settings.collection = qdrant_settings.collection;
        rag_vector_store_settings.request_timeout_ms = qdrant_settings.request_timeout_ms;
        vector_store.reset(new ai::rag::QdrantVectorStore(rag_vector_store_settings));

        rag_retriever.reset(new ai::rag::RagRetriever(embedding_client, vector_store));

        ai::rag::RagIndexerSettings rag_idx_settings;
        rag_idx_settings.queue_capacity = rag_indexer_settings.queue_capacity;
        rag_idx_settings.batch_size = rag_indexer_settings.batch_size;
        rag_idx_settings.flush_interval_ms = rag_indexer_settings.flush_interval_ms;
        rag_idx_settings.assistant_index_mode = rag_indexer_settings.assistant_index_mode;
        rag_idx_settings.assistant_min_chars = rag_indexer_settings.assistant_min_chars;
        rag_idx_settings.dedup_ttl_ms = rag_indexer_settings.dedup_ttl_ms;
        rag_idx_settings.dedup_max_entries = rag_indexer_settings.dedup_max_entries;
        rag_indexer.reset(new ai::rag::RagIndexer(embedding_client, vector_store, rag_idx_settings));

        std::vector<float> probe_embedding;
        if (!embedding_client->Embed("rag_startup_probe", probe_embedding, error) || probe_embedding.empty() ||
            !vector_store->EnsureCollection(probe_embedding.size(), error) || !rag_indexer->Start(error))
        {
            BASE_LOG_WARN(g_logger) << "init rag components failed, disable rag: " << error;
            rag_settings.enabled = false;
            rag_retriever.reset();
            rag_indexer.reset();
        }
    }

    /// @brief 组装业务编排核心 ChatService。
    ai::service::ChatService::ptr chat_service(new ai::service::ChatService(
        chat_settings, llm_router, chat_repository, message_sink, rag_settings, rag_retriever, rag_indexer));

    /// @brief Step 9: 按配置创建 HTTP worker。
    /// @details
    /// TcpServer 约束 accept_worker 与 io_worker 必须是不同实例，因此这里始终分离。
    /// - use_caller=true: io_worker 由 caller 驱动；accept_worker 独立 worker-only。
    /// - use_caller=false: io_worker 与 accept_worker 均为 worker-only。
    const uint32_t io_worker_threads = http::HttpFrameworkConfig::GetIOWorkerThreads();
    const uint32_t accept_worker_threads = http::HttpFrameworkConfig::GetAcceptWorkerThreads();
    const bool use_caller = g_iomanager_use_caller->getValue();

    sylar::IOManager::ptr io_worker;
    sylar::IOManager::ptr accept_worker;
    const uint32_t effective_accept_threads = accept_worker_threads > 0 ? accept_worker_threads : 1;
    if (use_caller)
    {
        io_worker.reset(new sylar::IOManager(io_worker_threads, true, "sylar-http-io-worker"));
    }
    else
    {
        io_worker.reset(new sylar::IOManager(io_worker_threads, false, "sylar-http-io-worker"));
    }
    accept_worker.reset(new sylar::IOManager(effective_accept_threads, false, "sylar-http-accept-worker"));

    /// @brief Step 10: 使用上述 worker 创建 HTTP Server。
    http::HttpServer::ptr server(new http::HttpServer(io_worker.get(), accept_worker.get()));

    /// @brief Step 11: 可选 TLS 配置（由 YAML 决定是否启用）。
    if (server_settings.enable_ssl)
    {
        http::ssl::SslConfig ssl_config;
        ssl_config.setCertificateFile(server_settings.cert_file);
        ssl_config.setPrivateKeyFile(server_settings.key_file);
        ssl_config.setVerifyPeer(false);
        if (!server->setSslConfig(ssl_config))
        {
            std::cerr << "configure server ssl failed" << std::endl;
            if (mq_sink)
            {
                mq_sink->Stop();
            }
            if (async_writer)
            {
                async_writer->Stop();
            }
            return 1;
        }
    }

    /// @brief Step 12: 注入 request_id 中间件，统一链路追踪字段。
    server->addMiddleware(http::Middleware::ptr(new ai::middleware::RequestIdMiddleware()));

    /// @brief Step 13: 注入鉴权中间件（从 main 解耦到独立类）。
    server->addMiddleware(http::Middleware::ptr(new ai::middleware::AuthMiddleware(auth_service)));

    /// @brief Step 14: 注册 AI 业务路由（health/completions/stream/history/auth）。
    ai::api::RegisterAiHttpApi(server, auth_service, chat_service, chat_settings);

    /// @brief Step 15: 解析并校验绑定地址（host:port）。
    sylar::Address::ptr bind_addr =
        sylar::Address::LookupAny(server_settings.host + ":" + std::to_string(server_settings.port));
    if (!bind_addr)
    {
        std::cerr << "invalid bind address: " << server_settings.host << ":" << server_settings.port << std::endl;
        if (mq_sink)
        {
            mq_sink->Stop();
        }
        if (async_writer)
        {
            async_writer->Stop();
        }
        return 1;
    }

    std::vector<sylar::Address::ptr> addrs;
    std::vector<sylar::Address::ptr> fails;
    addrs.push_back(bind_addr);

    /// @brief Step 16: bind + start。
    /// @note 失败时先停异步写线程，避免后台线程泄漏。
    if (!server->bind(addrs, fails) || !server->start())
    {
        std::cerr << "start ai chat server failed at " << server_settings.host << ":" << server_settings.port
                  << std::endl;
        if (mq_sink)
        {
            mq_sink->Stop();
        }
        if (async_writer)
        {
            async_writer->Stop();
        }
        return 1;
    }

    std::cout << "ai_chat_server started on " << server_settings.host << ":" << server_settings.port
              << " ssl=" << (server_settings.enable_ssl ? "on" : "off") << " use_caller=" << (use_caller ? "on" : "off")
              << std::endl;

    /// @brief Step 17: 注册优雅退出信号：Ctrl+C(SIGINT) / kill(SIGTERM)。
    ::signal(SIGINT, HandleSignal);
    ::signal(SIGTERM, HandleSignal);

    if (use_caller)
    {
        /// @brief caller 模式：
        /// 主线程进入 IOM 调度循环，通过定时器轮询停止标志触发关停。
        sylar::Timer::ptr stop_timer;
        stop_timer = io_worker->addTimer(
            200,
            [&]()
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
                }
            },
            true);
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

    /// @brief Step 18: 最终收尾。
    /// @details
    /// 1) 先停异步写线程，尽量把已入队消息刷盘完成；
    /// 2) 再关闭连接池，释放数据库连接资源。
    if (mq_sink)
    {
        mq_sink->Stop();
    }
    if (async_writer)
    {
        async_writer->Stop();
    }
    if (rag_indexer)
    {
        rag_indexer->Stop();
    }
    for (std::unordered_map<std::string, ai::llm::ProviderKeyPool::ptr>::iterator it = provider_key_pools.begin();
        it != provider_key_pools.end();
        ++it)
    {
        if (it->second)
        {
            it->second->Stop();
        }
    }
    mysql_pool->Shutdown();
    return 0;
}
