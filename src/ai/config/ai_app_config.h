#ifndef __SYLAR_AI_CONFIG_AI_APP_CONFIG_H__
#define __SYLAR_AI_CONFIG_AI_APP_CONFIG_H__

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file ai_app_config.h
 * @brief AI 应用层配置结构与配置读取入口。
 */

namespace ai
{
namespace config
{

/**
 * @brief HTTP 服务监听与 TLS 配置。
 */
struct ServerSettings
{
    /** @brief 服务监听地址，例如 `0.0.0.0`。 */
    std::string host;
    /** @brief 服务监听端口。 */
    uint16_t port;
    /** @brief 是否启用 HTTPS。 */
    bool enable_ssl;
    /** @brief TLS 证书文件路径。 */
    std::string cert_file;
    /** @brief TLS 私钥文件路径。 */
    std::string key_file;
};

/**
 * @brief OpenAI-Compatible 客户端配置。
 * @details
 * 通用于实现了 OpenAI 兼容 `chat/completions` 接口的模型服务。
 */
struct OpenAICompatibleSettings
{
    /** @brief API 基础地址。 */
    std::string base_url;
    /** @brief API Key。 */
    std::string api_key;
    /** @brief 默认模型名。 */
    std::string default_model;
    /** @brief 连接超时（毫秒）。 */
    uint64_t connect_timeout_ms = 3000;
    /** @brief 请求超时（毫秒）。 */
    uint64_t request_timeout_ms = 120000;
};

/**
 * @brief 通用 API Key 池配置（可用于任意 Provider）。
 */
struct ApiKeyPoolSettings
{
    /** @brief 是否启用全局 Key 池。 */
    bool enabled = false;
    /** @brief 是否启用后台热加载线程（可在运行时暂停/恢复轮询）。 */
    bool hot_reload_enabled = true;
    /** @brief Key 池热加载周期（毫秒）。 */
    uint64_t reload_interval_ms = 5000;
    /** @brief 单请求最大重试次数（不含首次尝试）。 */
    uint32_t max_retry_per_request = 2;
    /** @brief 短冷却时长（毫秒），用于限流/网络/5xx。 */
    uint64_t cooldown_short_ms = 60000;
    /** @brief 长冷却时长（毫秒），用于鉴权失败。 */
    uint64_t cooldown_long_ms = 600000;
};

/**
 * @brief Provider 选择配置。
 */
struct ProviderSettings
{
    /** @brief provider 类型：`openai_compatible` 或 `anthropic`。 */
    std::string type;
};

/**
 * @brief Anthropic 客户端配置。
 */
struct AnthropicSettings
{
    /** @brief API 基础地址。 */
    std::string base_url;
    /** @brief API Key。 */
    std::string api_key;
    /** @brief 默认模型名。 */
    std::string default_model;
    /** @brief API 版本头。 */
    std::string api_version;
    /** @brief 连接超时（毫秒）。 */
    uint64_t connect_timeout_ms = 3000;
    /** @brief 请求超时（毫秒）。 */
    uint64_t request_timeout_ms = 120000;
};

/**
 * @brief LLM Provider 实例配置（可同时存在多个）。
 */
struct LlmProviderSettings
{
    /** @brief Provider 实例唯一 ID（如 `deepseek_main`）。 */
    std::string id;
    /** @brief Provider 类型：`openai_compatible` / `anthropic`。 */
    std::string type;
    /** @brief 是否启用该 Provider 实例。 */
    bool enabled = true;
    /** @brief 请求未显式给 model 时使用的默认模型。 */
    std::string default_model;
    /** @brief OpenAI-Compatible 协议配置。 */
    OpenAICompatibleSettings openai_compatible;
    /** @brief Provider Key 池配置（协议无关）。 */
    ApiKeyPoolSettings key_pool;
    /** @brief Anthropic 协议配置。 */
    AnthropicSettings anthropic;
};

/**
 * @brief LLM 路由配置。
 */
struct LlmRoutingSettings
{
    /** @brief 默认 Provider 实例 ID。 */
    std::string default_provider_id;
    /** @brief model -> provider_id 映射。 */
    std::unordered_map<std::string, std::string> model_to_provider;
};

/**
 * @brief LLM 总配置：Provider 列表 + 路由规则。
 */
struct LlmSettings
{
    std::vector<LlmProviderSettings> providers;
    LlmRoutingSettings routing;
};

/**
 * @brief 对话业务配置。
 */
struct ChatSettings
{
    /** @brief 是否强制要求 SID。 */
    bool require_sid;
    /** @brief 内存上下文最多保留的消息条数。 */
    size_t max_context_messages;
    /** @brief 上下文补齐时单次从存储加载的条数。 */
    size_t history_load_limit;
    /** @brief 历史查询接口允许的 limit 上限。 */
    size_t history_query_limit_max;
    /** @brief 输入上下文 token 预算上限（启发式估算）。 */
    size_t max_context_tokens;
    /** @brief 摘要后保留最近原文消息条数。 */
    size_t recent_window_messages;
    /** @brief 触发摘要的 token 阈值（启发式估算）。 */
    size_t summary_trigger_tokens;
    /** @brief 摘要生成最大 token 上限。 */
    uint32_t summary_max_tokens;
    /** @brief 默认采样温度。 */
    double default_temperature;
    /** @brief 默认最大输出 token 数。 */
    uint32_t default_max_tokens;
    /** @brief 可选系统提示词。 */
    std::string system_prompt;
    /** @brief 摘要模型提示词模板。 */
    std::string summary_prompt;
};

/**
 * @brief 账号与鉴权配置。
 */
struct AuthSettings
{
    /** @brief 访问令牌有效期（秒）。 */
    uint64_t token_ttl_seconds;
    /** @brief PBKDF2 密码哈希迭代次数。 */
    uint32_t password_pbkdf2_iterations;
};

/**
 * @brief MySQL/MariaDB 连接配置。
 */
struct MysqlSettings
{
    /** @brief 数据库地址。 */
    std::string host;
    /** @brief 数据库端口。 */
    uint16_t port;
    /** @brief 数据库用户名。 */
    std::string user;
    /** @brief 数据库密码。 */
    std::string password;
    /** @brief 数据库名。 */
    std::string database;
    /** @brief 连接字符集。 */
    std::string charset;
    /** @brief 连接超时（秒）。 */
    uint32_t connect_timeout_seconds;
    /** @brief 连接池最小连接数。 */
    size_t pool_min_size;
    /** @brief 连接池最大连接数。 */
    size_t pool_max_size;
    /** @brief 获取连接超时（毫秒）。 */
    uint64_t pool_acquire_timeout_ms;
};

/**
 * @brief 异步持久化写入器配置。
 */
struct PersistSettings
{
    /** @brief 写入队列容量。 */
    size_t queue_capacity;
    /** @brief 批量刷盘间隔（毫秒）。 */
    uint64_t flush_interval_ms;
    /** @brief 每批最大写入条数。 */
    size_t batch_size;
};

/**
 * @brief RAG 检索策略配置。
 */
struct RagSettings
{
    /** @brief 是否启用 RAG。 */
    bool enabled = false;
    /** @brief 召回触发模式：`always`（每轮检索）或 `intent`（按记忆意图触发）。 */
    std::string recall_trigger_mode = "intent";
    /** @brief `intent` 模式下，触发判定的最小问题长度（字符数）。 */
    size_t recall_intent_min_chars = 6;
    /** @brief 向量召回条数。 */
    size_t top_k = 6;
    /** @brief 最低相似度分数阈值（<=0 表示不启用阈值过滤）。 */
    double score_threshold = 0.45;
    /** @brief 单条召回片段最大字符数。 */
    size_t max_snippet_chars = 400;
};

/**
 * @brief Embedding 服务配置。
 */
struct EmbeddingSettings
{
    /** @brief Embedding provider 类型，当前仅支持 `ollama`。 */
    std::string provider;
    /** @brief Embedding 服务基础地址。 */
    std::string base_url;
    /** @brief Embedding 模型名。 */
    std::string model;
    /** @brief 连接超时（毫秒）。 */
    uint64_t connect_timeout_ms = 3000;
    /** @brief 请求超时（毫秒）。 */
    uint64_t request_timeout_ms = 30000;
};

/**
 * @brief Qdrant 向量库配置。
 */
struct QdrantSettings
{
    /** @brief Qdrant HTTP 基础地址。 */
    std::string base_url;
    /** @brief 向量集合名。 */
    std::string collection;
    /** @brief 请求超时（毫秒）。 */
    uint64_t request_timeout_ms = 5000;
};

/**
 * @brief RAG 索引异步队列配置。
 */
struct RagIndexerSettings
{
    /** @brief 索引队列容量。 */
    size_t queue_capacity = 10000;
    /** @brief 单批次最大索引消息数。 */
    size_t batch_size = 32;
    /** @brief 刷盘间隔（毫秒）。 */
    uint64_t flush_interval_ms = 200;
    /** @brief assistant 入库模式：`all` / `fact_like` / `none`。 */
    std::string assistant_index_mode = "fact_like";
    /** @brief `fact_like` 模式下 assistant 文本最小长度（字符数）。 */
    size_t assistant_min_chars = 24;
    /** @brief 去重窗口（毫秒），0 表示关闭去重。 */
    uint64_t dedup_ttl_ms = 600000;
    /** @brief 去重缓存最大条目数，0 表示关闭去重。 */
    size_t dedup_max_entries = 50000;
};

/**
 * @brief AI 应用配置读取与校验入口。
 *
 * 负责把配置中心中的 `ai.*` 键转换为强类型 Settings，
 * 并在服务启动前执行必要的 fail-fast 校验。
 */
class AiAppConfig
{
  public:
    /** @brief 获取服务监听配置。 */
    static ServerSettings GetServerSettings();
    /** @brief 获取 provider 选择配置。 */
    static ProviderSettings GetProviderSettings();
    /** @brief 获取 OpenAI-Compatible 客户端配置。 */
    static OpenAICompatibleSettings GetOpenAICompatibleSettings();
    /** @brief 获取通用 API Key 池配置（兼容旧键 ai.openai_compatible.key_pool.*）。 */
    static ApiKeyPoolSettings GetApiKeyPoolSettings();
    /** @brief 兼容旧接口名。 */
    static ApiKeyPoolSettings GetOpenAIKeyPoolSettings();
    /** @brief 获取 LLM 多 Provider 配置。 */
    static LlmSettings GetLlmSettings();
    /** @brief 获取 Anthropic 客户端配置。 */
    static AnthropicSettings GetAnthropicSettings();
    /** @brief 获取对话业务配置。 */
    static ChatSettings GetChatSettings();
    /** @brief 获取账号鉴权配置。 */
    static AuthSettings GetAuthSettings();
    /** @brief 获取数据库连接配置。 */
    static MysqlSettings GetMysqlSettings();
    /** @brief 获取异步持久化配置。 */
    static PersistSettings GetPersistSettings();
    /** @brief 获取 RAG 配置。 */
    static RagSettings GetRagSettings();
    /** @brief 获取 Embedding 配置。 */
    static EmbeddingSettings GetEmbeddingSettings();
    /** @brief 获取 Qdrant 配置。 */
    static QdrantSettings GetQdrantSettings();
    /** @brief 获取 RAG 索引器配置。 */
    static RagIndexerSettings GetRagIndexerSettings();

    /**
     * @brief 解析 OpenAI-Compatible API Key。
     * @details
     * 优先读取 `ai.openai_compatible.api_key`，
     * 为空时回退环境变量 `OPENAI_COMPATIBLE_API_KEY` / `OPENAI_API_KEY`。
     */
    static std::string ResolveOpenAICompatibleApiKey();
    /** @brief 解析 Anthropic API Key。 */
    static std::string ResolveAnthropicApiKey();

    /**
     * @brief 启动前配置合法性校验。
     * @param[out] error 校验失败原因。
     * @return true 校验通过；false 校验失败。
     */
    static bool Validate(std::string& error);
};

} // namespace config
} // namespace ai

#endif
