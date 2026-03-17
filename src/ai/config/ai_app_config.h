#ifndef __SYLAR_AI_CONFIG_AI_APP_CONFIG_H__
#define __SYLAR_AI_CONFIG_AI_APP_CONFIG_H__

#include <stddef.h>
#include <stdint.h>

#include <string>

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
    uint64_t connect_timeout_ms;
    /** @brief 请求超时（毫秒）。 */
    uint64_t request_timeout_ms;
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
    uint64_t connect_timeout_ms;
    /** @brief 请求超时（毫秒）。 */
    uint64_t request_timeout_ms;
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
    /** @brief 获取 Anthropic 客户端配置。 */
    static AnthropicSettings GetAnthropicSettings();
    /** @brief 获取对话业务配置。 */
    static ChatSettings GetChatSettings();
    /** @brief 获取数据库连接配置。 */
    static MysqlSettings GetMysqlSettings();
    /** @brief 获取异步持久化配置。 */
    static PersistSettings GetPersistSettings();

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
