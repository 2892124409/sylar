#include "ai/config/ai_app_config.h"

#include "config/config.h"

#include <stdlib.h>
#include <set>
#include <sstream>

namespace ai
{
namespace config
{

namespace
{

http::ConfigVar<std::string>::ptr g_ai_server_host =
    http::Config::Lookup<std::string>("ai.server.host", "0.0.0.0", "ai server bind host");

http::ConfigVar<uint32_t>::ptr g_ai_server_port =
    http::Config::Lookup<uint32_t>("ai.server.port", 8080, "ai server bind port");

http::ConfigVar<bool>::ptr g_ai_server_enable_ssl =
    http::Config::Lookup<bool>("ai.server.enable_ssl", false, "ai server ssl enabled");

http::ConfigVar<std::string>::ptr g_ai_server_ssl_cert_file =
    http::Config::Lookup<std::string>("ai.server.ssl_cert_file", "", "ai server ssl cert file path");

http::ConfigVar<std::string>::ptr g_ai_server_ssl_key_file =
    http::Config::Lookup<std::string>("ai.server.ssl_key_file", "", "ai server ssl key file path");

http::ConfigVar<std::string>::ptr g_ai_provider_type =
    http::Config::Lookup<std::string>("ai.provider.type", "openai_compatible", "ai provider type");

http::ConfigVar<std::string>::ptr g_ai_llm_providers_yaml =
    http::Config::Lookup<std::string>("ai.llm.providers", "[]", "llm provider list");

http::ConfigVar<std::string>::ptr g_ai_llm_routing_default_provider_id =
    http::Config::Lookup<std::string>("ai.llm.routing.default_provider_id", "", "llm default provider id");

http::ConfigVar<std::unordered_map<std::string, std::string>>::ptr g_ai_llm_routing_model_map =
    http::Config::Lookup<std::unordered_map<std::string, std::string>>("ai.llm.routing.model_map",
                                                                        std::unordered_map<std::string, std::string>(),
                                                                        "llm model to provider map");

// OpenAI-Compatible 配置键：ai.openai_compatible.*
http::ConfigVar<std::string>::ptr g_ai_openai_base_url =
    http::Config::Lookup<std::string>("ai.openai_compatible.base_url", "", "openai-compatible base url");

http::ConfigVar<std::string>::ptr g_ai_openai_api_key =
    http::Config::Lookup<std::string>("ai.openai_compatible.api_key", "", "openai-compatible api key");

http::ConfigVar<std::string>::ptr g_ai_openai_default_model =
    http::Config::Lookup<std::string>("ai.openai_compatible.default_model", "", "openai-compatible default model");

http::ConfigVar<uint64_t>::ptr g_ai_openai_connect_timeout_ms =
    http::Config::Lookup<uint64_t>("ai.openai_compatible.connect_timeout_ms", 3000, "openai-compatible connect timeout ms");

http::ConfigVar<uint64_t>::ptr g_ai_openai_request_timeout_ms =
    http::Config::Lookup<uint64_t>("ai.openai_compatible.request_timeout_ms", 120000, "openai-compatible request timeout ms");

http::ConfigVar<bool>::ptr g_ai_openai_key_pool_enabled =
    http::Config::Lookup<bool>("ai.openai_compatible.key_pool.enabled", false, "openai-compatible key pool enabled");

http::ConfigVar<bool>::ptr g_ai_openai_key_pool_hot_reload_enabled =
    http::Config::Lookup<bool>("ai.openai_compatible.key_pool.hot_reload_enabled", true, "openai-compatible key pool hot reload enabled");

http::ConfigVar<uint64_t>::ptr g_ai_openai_key_pool_reload_interval_ms =
    http::Config::Lookup<uint64_t>("ai.openai_compatible.key_pool.reload_interval_ms", 5000, "openai-compatible key pool reload interval ms");

http::ConfigVar<uint32_t>::ptr g_ai_openai_key_pool_max_retry_per_request =
    http::Config::Lookup<uint32_t>("ai.openai_compatible.key_pool.max_retry_per_request", 2, "openai-compatible key pool max retry per request");

http::ConfigVar<uint64_t>::ptr g_ai_openai_key_pool_cooldown_short_ms =
    http::Config::Lookup<uint64_t>("ai.openai_compatible.key_pool.cooldown_short_ms", 60000, "openai-compatible key pool short cooldown ms");

http::ConfigVar<uint64_t>::ptr g_ai_openai_key_pool_cooldown_long_ms =
    http::Config::Lookup<uint64_t>("ai.openai_compatible.key_pool.cooldown_long_ms", 600000, "openai-compatible key pool long cooldown ms");

http::ConfigVar<std::string>::ptr g_ai_anthropic_base_url =
    http::Config::Lookup<std::string>("ai.anthropic.base_url", "https://api.anthropic.com", "anthropic base url");

http::ConfigVar<std::string>::ptr g_ai_anthropic_api_key =
    http::Config::Lookup<std::string>("ai.anthropic.api_key", "", "anthropic api key");

http::ConfigVar<std::string>::ptr g_ai_anthropic_default_model =
    http::Config::Lookup<std::string>("ai.anthropic.default_model", "", "anthropic default model");

http::ConfigVar<std::string>::ptr g_ai_anthropic_api_version =
    http::Config::Lookup<std::string>("ai.anthropic.api_version", "2023-06-01", "anthropic api version");

http::ConfigVar<uint64_t>::ptr g_ai_anthropic_connect_timeout_ms =
    http::Config::Lookup<uint64_t>("ai.anthropic.connect_timeout_ms", 3000, "anthropic connect timeout ms");

http::ConfigVar<uint64_t>::ptr g_ai_anthropic_request_timeout_ms =
    http::Config::Lookup<uint64_t>("ai.anthropic.request_timeout_ms", 120000, "anthropic request timeout ms");

http::ConfigVar<bool>::ptr g_ai_chat_require_sid =
    http::Config::Lookup<bool>("ai.chat.require_sid", true, "whether sid is required");

http::ConfigVar<uint64_t>::ptr g_ai_chat_max_context_messages =
    http::Config::Lookup<uint64_t>("ai.chat.max_context_messages", 20, "max messages kept in memory context");

http::ConfigVar<uint64_t>::ptr g_ai_chat_history_load_limit =
    http::Config::Lookup<uint64_t>("ai.chat.history_load_limit", 20, "history count loaded for context hydration");

http::ConfigVar<uint64_t>::ptr g_ai_chat_history_query_limit_max =
    http::Config::Lookup<uint64_t>("ai.chat.history_query_limit_max", 200, "max history query limit");

http::ConfigVar<uint64_t>::ptr g_ai_chat_max_context_tokens =
    http::Config::Lookup<uint64_t>("ai.chat.max_context_tokens", 4096, "max prompt context tokens by heuristic");

http::ConfigVar<uint64_t>::ptr g_ai_chat_recent_window_messages =
    http::Config::Lookup<uint64_t>("ai.chat.recent_window_messages", 20, "recent window message count after summarization");

http::ConfigVar<uint64_t>::ptr g_ai_chat_summary_trigger_tokens =
    http::Config::Lookup<uint64_t>("ai.chat.summary_trigger_tokens", 3072, "token threshold to trigger summarization");

http::ConfigVar<uint32_t>::ptr g_ai_chat_summary_max_tokens =
    http::Config::Lookup<uint32_t>("ai.chat.summary_max_tokens", 512, "max tokens for summary generation");

http::ConfigVar<double>::ptr g_ai_chat_default_temperature =
    http::Config::Lookup<double>("ai.chat.default_temperature", 0.7, "default model temperature");

http::ConfigVar<uint32_t>::ptr g_ai_chat_default_max_tokens =
    http::Config::Lookup<uint32_t>("ai.chat.default_max_tokens", 1024, "default max tokens");

http::ConfigVar<std::string>::ptr g_ai_chat_system_prompt =
    http::Config::Lookup<std::string>("ai.chat.system_prompt", "", "default system prompt");

http::ConfigVar<std::string>::ptr g_ai_chat_summary_prompt =
    http::Config::Lookup<std::string>(
        "ai.chat.summary_prompt",
        "你是对话摘要助手。请把旧对话压缩成简洁、可延续上下文的中文摘要，保留人物偏好、事实、约束、未完成任务。",
        "summary prompt");

http::ConfigVar<uint64_t>::ptr g_ai_auth_token_ttl_seconds =
    http::Config::Lookup<uint64_t>("ai.auth.token_ttl_seconds", 2592000, "auth token ttl seconds");

http::ConfigVar<uint32_t>::ptr g_ai_auth_password_pbkdf2_iterations =
    http::Config::Lookup<uint32_t>("ai.auth.password_pbkdf2_iterations", 150000, "password pbkdf2 iterations");

http::ConfigVar<std::string>::ptr g_ai_mysql_host =
    http::Config::Lookup<std::string>("ai.mysql.host", "127.0.0.1", "mysql host");

http::ConfigVar<uint32_t>::ptr g_ai_mysql_port =
    http::Config::Lookup<uint32_t>("ai.mysql.port", 3306, "mysql port");

http::ConfigVar<std::string>::ptr g_ai_mysql_user =
    http::Config::Lookup<std::string>("ai.mysql.user", "root", "mysql user");

http::ConfigVar<std::string>::ptr g_ai_mysql_password =
    http::Config::Lookup<std::string>("ai.mysql.password", "", "mysql password");

http::ConfigVar<std::string>::ptr g_ai_mysql_database =
    http::Config::Lookup<std::string>("ai.mysql.database", "ai_chat", "mysql database");

http::ConfigVar<std::string>::ptr g_ai_mysql_charset =
    http::Config::Lookup<std::string>("ai.mysql.charset", "utf8mb4", "mysql charset");

http::ConfigVar<uint32_t>::ptr g_ai_mysql_connect_timeout_seconds =
    http::Config::Lookup<uint32_t>("ai.mysql.connect_timeout_seconds", 5, "mysql connect timeout seconds");

http::ConfigVar<uint64_t>::ptr g_ai_mysql_pool_min_size =
    http::Config::Lookup<uint64_t>("ai.mysql.pool_min_size", 2, "mysql connection pool minimum size");

http::ConfigVar<uint64_t>::ptr g_ai_mysql_pool_max_size =
    http::Config::Lookup<uint64_t>("ai.mysql.pool_max_size", 8, "mysql connection pool maximum size");

http::ConfigVar<uint64_t>::ptr g_ai_mysql_pool_acquire_timeout_ms =
    http::Config::Lookup<uint64_t>("ai.mysql.pool_acquire_timeout_ms", 3000, "mysql connection pool acquire timeout ms");

http::ConfigVar<uint64_t>::ptr g_ai_persist_queue_capacity =
    http::Config::Lookup<uint64_t>("ai.persist.queue_capacity", 10000, "persist queue capacity");

http::ConfigVar<uint64_t>::ptr g_ai_persist_flush_interval_ms =
    http::Config::Lookup<uint64_t>("ai.persist.flush_interval_ms", 200, "persist flush interval ms");

http::ConfigVar<uint64_t>::ptr g_ai_persist_batch_size =
    http::Config::Lookup<uint64_t>("ai.persist.batch_size", 128, "persist batch size");

http::ConfigVar<bool>::ptr g_ai_rag_enabled =
    http::Config::Lookup<bool>("ai.rag.enabled", true, "whether rag retrieval is enabled");

http::ConfigVar<std::string>::ptr g_ai_rag_recall_trigger_mode =
    http::Config::Lookup<std::string>("ai.rag.recall_trigger_mode", "intent", "rag recall trigger mode: always/intent");

http::ConfigVar<uint64_t>::ptr g_ai_rag_recall_intent_min_chars =
    http::Config::Lookup<uint64_t>("ai.rag.recall_intent_min_chars", 6, "rag intent trigger min query chars");

http::ConfigVar<uint64_t>::ptr g_ai_rag_top_k =
    http::Config::Lookup<uint64_t>("ai.rag.top_k", 6, "rag retrieval top-k");

http::ConfigVar<double>::ptr g_ai_rag_score_threshold =
    http::Config::Lookup<double>("ai.rag.score_threshold", 0.45, "rag retrieval score threshold");

http::ConfigVar<uint64_t>::ptr g_ai_rag_max_snippet_chars =
    http::Config::Lookup<uint64_t>("ai.rag.max_snippet_chars", 400, "rag snippet max chars");

http::ConfigVar<std::string>::ptr g_ai_embedding_provider =
    http::Config::Lookup<std::string>("ai.embedding.provider", "ollama", "embedding provider");

http::ConfigVar<std::string>::ptr g_ai_embedding_base_url =
    http::Config::Lookup<std::string>("ai.embedding.base_url", "http://127.0.0.1:11434", "embedding base url");

http::ConfigVar<std::string>::ptr g_ai_embedding_model =
    http::Config::Lookup<std::string>("ai.embedding.model", "mxbai-embed-large", "embedding model");

http::ConfigVar<uint64_t>::ptr g_ai_embedding_connect_timeout_ms =
    http::Config::Lookup<uint64_t>("ai.embedding.connect_timeout_ms", 3000, "embedding connect timeout ms");

http::ConfigVar<uint64_t>::ptr g_ai_embedding_request_timeout_ms =
    http::Config::Lookup<uint64_t>("ai.embedding.request_timeout_ms", 30000, "embedding request timeout ms");

http::ConfigVar<std::string>::ptr g_ai_qdrant_base_url =
    http::Config::Lookup<std::string>("ai.vector_store.qdrant.base_url", "http://127.0.0.1:6333", "qdrant base url");

http::ConfigVar<std::string>::ptr g_ai_qdrant_collection =
    http::Config::Lookup<std::string>("ai.vector_store.qdrant.collection", "chat_memory", "qdrant collection name");

http::ConfigVar<uint64_t>::ptr g_ai_qdrant_request_timeout_ms =
    http::Config::Lookup<uint64_t>("ai.vector_store.qdrant.request_timeout_ms", 5000, "qdrant request timeout ms");

http::ConfigVar<uint64_t>::ptr g_ai_rag_indexer_queue_capacity =
    http::Config::Lookup<uint64_t>("ai.rag_indexer.queue_capacity", 10000, "rag indexer queue capacity");

http::ConfigVar<uint64_t>::ptr g_ai_rag_indexer_batch_size =
    http::Config::Lookup<uint64_t>("ai.rag_indexer.batch_size", 32, "rag indexer batch size");

http::ConfigVar<uint64_t>::ptr g_ai_rag_indexer_flush_interval_ms =
    http::Config::Lookup<uint64_t>("ai.rag_indexer.flush_interval_ms", 200, "rag indexer flush interval ms");

http::ConfigVar<std::string>::ptr g_ai_rag_indexer_assistant_index_mode =
    http::Config::Lookup<std::string>("ai.rag_indexer.assistant_index_mode", "fact_like", "rag indexer assistant index mode");

http::ConfigVar<uint64_t>::ptr g_ai_rag_indexer_assistant_min_chars =
    http::Config::Lookup<uint64_t>("ai.rag_indexer.assistant_min_chars", 24, "rag indexer assistant min chars");

http::ConfigVar<uint64_t>::ptr g_ai_rag_indexer_dedup_ttl_ms =
    http::Config::Lookup<uint64_t>("ai.rag_indexer.dedup_ttl_ms", 600000, "rag indexer dedup ttl ms");

http::ConfigVar<uint64_t>::ptr g_ai_rag_indexer_dedup_max_entries =
    http::Config::Lookup<uint64_t>("ai.rag_indexer.dedup_max_entries", 50000, "rag indexer dedup max entries");

std::string ResolveOpenAICompatibleApiKeyFrom(const std::string& key_from_config)
{
    if (!key_from_config.empty())
    {
        return key_from_config;
    }

    const char* env_key = std::getenv("OPENAI_COMPATIBLE_API_KEY");
    if (env_key && env_key[0] != '\0')
    {
        return std::string(env_key);
    }

    env_key = std::getenv("OPENAI_API_KEY");
    return env_key ? std::string(env_key) : std::string();
}

std::string ResolveAnthropicApiKeyFrom(const std::string& key_from_config)
{
    if (!key_from_config.empty())
    {
        return key_from_config;
    }

    const char* env_key = std::getenv("ANTHROPIC_API_KEY");
    return env_key ? std::string(env_key) : std::string();
}

OpenAICompatibleSettings ParseOpenAICompatibleSettings(const YAML::Node& node)
{
    OpenAICompatibleSettings settings;
    if (!node || !node.IsMap())
    {
        return settings;
    }

    if (node["base_url"] && node["base_url"].IsScalar())
    {
        settings.base_url = node["base_url"].as<std::string>();
    }
    if (node["api_key"] && node["api_key"].IsScalar())
    {
        settings.api_key = ResolveOpenAICompatibleApiKeyFrom(node["api_key"].as<std::string>());
    }
    else
    {
        settings.api_key = ResolveOpenAICompatibleApiKeyFrom(std::string());
    }
    if (node["default_model"] && node["default_model"].IsScalar())
    {
        settings.default_model = node["default_model"].as<std::string>();
    }
    if (node["connect_timeout_ms"] && node["connect_timeout_ms"].IsScalar())
    {
        settings.connect_timeout_ms = node["connect_timeout_ms"].as<uint64_t>();
    }
    if (node["request_timeout_ms"] && node["request_timeout_ms"].IsScalar())
    {
        settings.request_timeout_ms = node["request_timeout_ms"].as<uint64_t>();
    }
    return settings;
}

ApiKeyPoolSettings ParseApiKeyPoolSettings(const YAML::Node& node)
{
    ApiKeyPoolSettings settings;
    if (!node || !node.IsMap())
    {
        return settings;
    }
    if (node["enabled"] && node["enabled"].IsScalar())
    {
        settings.enabled = node["enabled"].as<bool>();
    }
    if (node["hot_reload_enabled"] && node["hot_reload_enabled"].IsScalar())
    {
        settings.hot_reload_enabled = node["hot_reload_enabled"].as<bool>();
    }
    if (node["reload_interval_ms"] && node["reload_interval_ms"].IsScalar())
    {
        settings.reload_interval_ms = node["reload_interval_ms"].as<uint64_t>();
    }
    if (node["max_retry_per_request"] && node["max_retry_per_request"].IsScalar())
    {
        settings.max_retry_per_request = node["max_retry_per_request"].as<uint32_t>();
    }
    if (node["cooldown_short_ms"] && node["cooldown_short_ms"].IsScalar())
    {
        settings.cooldown_short_ms = node["cooldown_short_ms"].as<uint64_t>();
    }
    if (node["cooldown_long_ms"] && node["cooldown_long_ms"].IsScalar())
    {
        settings.cooldown_long_ms = node["cooldown_long_ms"].as<uint64_t>();
    }
    return settings;
}

AnthropicSettings ParseAnthropicSettings(const YAML::Node& node)
{
    AnthropicSettings settings;
    if (!node || !node.IsMap())
    {
        return settings;
    }
    if (node["base_url"] && node["base_url"].IsScalar())
    {
        settings.base_url = node["base_url"].as<std::string>();
    }
    if (node["api_key"] && node["api_key"].IsScalar())
    {
        settings.api_key = ResolveAnthropicApiKeyFrom(node["api_key"].as<std::string>());
    }
    else
    {
        settings.api_key = ResolveAnthropicApiKeyFrom(std::string());
    }
    if (node["default_model"] && node["default_model"].IsScalar())
    {
        settings.default_model = node["default_model"].as<std::string>();
    }
    if (node["api_version"] && node["api_version"].IsScalar())
    {
        settings.api_version = node["api_version"].as<std::string>();
    }
    if (node["connect_timeout_ms"] && node["connect_timeout_ms"].IsScalar())
    {
        settings.connect_timeout_ms = node["connect_timeout_ms"].as<uint64_t>();
    }
    if (node["request_timeout_ms"] && node["request_timeout_ms"].IsScalar())
    {
        settings.request_timeout_ms = node["request_timeout_ms"].as<uint64_t>();
    }
    return settings;
}

LlmProviderSettings ParseLlmProviderSettings(const YAML::Node& node)
{
    LlmProviderSettings settings;
    if (!node || !node.IsMap())
    {
        return settings;
    }

    if (node["id"] && node["id"].IsScalar())
    {
        settings.id = node["id"].as<std::string>();
    }
    if (node["type"] && node["type"].IsScalar())
    {
        settings.type = node["type"].as<std::string>();
    }
    if (node["enabled"] && node["enabled"].IsScalar())
    {
        settings.enabled = node["enabled"].as<bool>();
    }

    if (settings.type == "openai_compatible")
    {
        settings.openai_compatible = ParseOpenAICompatibleSettings(node);
        settings.key_pool = ParseApiKeyPoolSettings(node["key_pool"]);
        settings.default_model = settings.openai_compatible.default_model;
    }
    else if (settings.type == "anthropic")
    {
        settings.anthropic = ParseAnthropicSettings(node);
        settings.key_pool = ParseApiKeyPoolSettings(node["key_pool"]);
        settings.default_model = settings.anthropic.default_model;
    }

    if (node["default_model"] && node["default_model"].IsScalar())
    {
        settings.default_model = node["default_model"].as<std::string>();
    }

    return settings;
}

} // namespace

ServerSettings AiAppConfig::GetServerSettings()
{
    ServerSettings settings;
    settings.host = g_ai_server_host->getValue();
    settings.port = static_cast<uint16_t>(g_ai_server_port->getValue());
    settings.enable_ssl = g_ai_server_enable_ssl->getValue();
    settings.cert_file = g_ai_server_ssl_cert_file->getValue();
    settings.key_file = g_ai_server_ssl_key_file->getValue();
    return settings;
}

ProviderSettings AiAppConfig::GetProviderSettings()
{
    ProviderSettings settings;
    settings.type = g_ai_provider_type->getValue();
    return settings;
}

OpenAICompatibleSettings AiAppConfig::GetOpenAICompatibleSettings()
{
    OpenAICompatibleSettings settings;
    settings.base_url = g_ai_openai_base_url->getValue();
    settings.api_key = ResolveOpenAICompatibleApiKey();
    settings.default_model = g_ai_openai_default_model->getValue();
    settings.connect_timeout_ms = g_ai_openai_connect_timeout_ms->getValue();
    settings.request_timeout_ms = g_ai_openai_request_timeout_ms->getValue();
    return settings;
}

ApiKeyPoolSettings AiAppConfig::GetApiKeyPoolSettings()
{
    ApiKeyPoolSettings settings;
    settings.enabled = g_ai_openai_key_pool_enabled->getValue();
    settings.hot_reload_enabled = g_ai_openai_key_pool_hot_reload_enabled->getValue();
    settings.reload_interval_ms = g_ai_openai_key_pool_reload_interval_ms->getValue();
    settings.max_retry_per_request = g_ai_openai_key_pool_max_retry_per_request->getValue();
    settings.cooldown_short_ms = g_ai_openai_key_pool_cooldown_short_ms->getValue();
    settings.cooldown_long_ms = g_ai_openai_key_pool_cooldown_long_ms->getValue();
    return settings;
}

ApiKeyPoolSettings AiAppConfig::GetOpenAIKeyPoolSettings()
{
    return GetApiKeyPoolSettings();
}

LlmSettings AiAppConfig::GetLlmSettings()
{
    LlmSettings settings;
    settings.routing.default_provider_id = g_ai_llm_routing_default_provider_id->getValue();
    settings.routing.model_to_provider = g_ai_llm_routing_model_map->getValue();

    const std::string providers_yaml = g_ai_llm_providers_yaml->getValue();
    if (providers_yaml.empty())
    {
        return settings;
    }

    try
    {
        YAML::Node providers = YAML::Load(providers_yaml);
        if (!providers || !providers.IsSequence())
        {
            return settings;
        }

        for (size_t i = 0; i < providers.size(); ++i)
        {
            settings.providers.push_back(ParseLlmProviderSettings(providers[i]));
        }
    }
    catch (...)
    {
        settings.providers.clear();
    }

    return settings;
}

AnthropicSettings AiAppConfig::GetAnthropicSettings()
{
    AnthropicSettings settings;
    settings.base_url = g_ai_anthropic_base_url->getValue();
    settings.api_key = ResolveAnthropicApiKey();
    settings.default_model = g_ai_anthropic_default_model->getValue();
    settings.api_version = g_ai_anthropic_api_version->getValue();
    settings.connect_timeout_ms = g_ai_anthropic_connect_timeout_ms->getValue();
    settings.request_timeout_ms = g_ai_anthropic_request_timeout_ms->getValue();
    return settings;
}

ChatSettings AiAppConfig::GetChatSettings()
{
    ChatSettings settings;
    settings.require_sid = g_ai_chat_require_sid->getValue();
    settings.max_context_messages = static_cast<size_t>(g_ai_chat_max_context_messages->getValue());
    settings.history_load_limit = static_cast<size_t>(g_ai_chat_history_load_limit->getValue());
    settings.history_query_limit_max = static_cast<size_t>(g_ai_chat_history_query_limit_max->getValue());
    settings.max_context_tokens = static_cast<size_t>(g_ai_chat_max_context_tokens->getValue());
    settings.recent_window_messages = static_cast<size_t>(g_ai_chat_recent_window_messages->getValue());
    settings.summary_trigger_tokens = static_cast<size_t>(g_ai_chat_summary_trigger_tokens->getValue());
    settings.summary_max_tokens = g_ai_chat_summary_max_tokens->getValue();
    settings.default_temperature = g_ai_chat_default_temperature->getValue();
    settings.default_max_tokens = g_ai_chat_default_max_tokens->getValue();
    settings.system_prompt = g_ai_chat_system_prompt->getValue();
    settings.summary_prompt = g_ai_chat_summary_prompt->getValue();
    return settings;
}

AuthSettings AiAppConfig::GetAuthSettings()
{
    AuthSettings settings;
    settings.token_ttl_seconds = g_ai_auth_token_ttl_seconds->getValue();
    settings.password_pbkdf2_iterations = g_ai_auth_password_pbkdf2_iterations->getValue();
    return settings;
}

MysqlSettings AiAppConfig::GetMysqlSettings()
{
    MysqlSettings settings;
    settings.host = g_ai_mysql_host->getValue();
    settings.port = static_cast<uint16_t>(g_ai_mysql_port->getValue());
    settings.user = g_ai_mysql_user->getValue();
    settings.password = g_ai_mysql_password->getValue();
    settings.database = g_ai_mysql_database->getValue();
    settings.charset = g_ai_mysql_charset->getValue();
    settings.connect_timeout_seconds = g_ai_mysql_connect_timeout_seconds->getValue();
    settings.pool_min_size = static_cast<size_t>(g_ai_mysql_pool_min_size->getValue());
    settings.pool_max_size = static_cast<size_t>(g_ai_mysql_pool_max_size->getValue());
    settings.pool_acquire_timeout_ms = g_ai_mysql_pool_acquire_timeout_ms->getValue();
    return settings;
}

PersistSettings AiAppConfig::GetPersistSettings()
{
    PersistSettings settings;
    settings.queue_capacity = static_cast<size_t>(g_ai_persist_queue_capacity->getValue());
    settings.flush_interval_ms = g_ai_persist_flush_interval_ms->getValue();
    settings.batch_size = static_cast<size_t>(g_ai_persist_batch_size->getValue());
    return settings;
}

RagSettings AiAppConfig::GetRagSettings()
{
    RagSettings settings;
    settings.enabled = g_ai_rag_enabled->getValue();
    settings.recall_trigger_mode = g_ai_rag_recall_trigger_mode->getValue();
    settings.recall_intent_min_chars = static_cast<size_t>(g_ai_rag_recall_intent_min_chars->getValue());
    settings.top_k = static_cast<size_t>(g_ai_rag_top_k->getValue());
    settings.score_threshold = g_ai_rag_score_threshold->getValue();
    settings.max_snippet_chars = static_cast<size_t>(g_ai_rag_max_snippet_chars->getValue());
    return settings;
}

EmbeddingSettings AiAppConfig::GetEmbeddingSettings()
{
    EmbeddingSettings settings;
    settings.provider = g_ai_embedding_provider->getValue();
    settings.base_url = g_ai_embedding_base_url->getValue();
    settings.model = g_ai_embedding_model->getValue();
    settings.connect_timeout_ms = g_ai_embedding_connect_timeout_ms->getValue();
    settings.request_timeout_ms = g_ai_embedding_request_timeout_ms->getValue();
    return settings;
}

QdrantSettings AiAppConfig::GetQdrantSettings()
{
    QdrantSettings settings;
    settings.base_url = g_ai_qdrant_base_url->getValue();
    settings.collection = g_ai_qdrant_collection->getValue();
    settings.request_timeout_ms = g_ai_qdrant_request_timeout_ms->getValue();
    return settings;
}

RagIndexerSettings AiAppConfig::GetRagIndexerSettings()
{
    RagIndexerSettings settings;
    settings.queue_capacity = static_cast<size_t>(g_ai_rag_indexer_queue_capacity->getValue());
    settings.batch_size = static_cast<size_t>(g_ai_rag_indexer_batch_size->getValue());
    settings.flush_interval_ms = g_ai_rag_indexer_flush_interval_ms->getValue();
    settings.assistant_index_mode = g_ai_rag_indexer_assistant_index_mode->getValue();
    settings.assistant_min_chars = static_cast<size_t>(g_ai_rag_indexer_assistant_min_chars->getValue());
    settings.dedup_ttl_ms = g_ai_rag_indexer_dedup_ttl_ms->getValue();
    settings.dedup_max_entries = static_cast<size_t>(g_ai_rag_indexer_dedup_max_entries->getValue());
    return settings;
}

std::string AiAppConfig::ResolveOpenAICompatibleApiKey()
{
    return ResolveOpenAICompatibleApiKeyFrom(g_ai_openai_api_key->getValue());
}

std::string AiAppConfig::ResolveAnthropicApiKey()
{
    return ResolveAnthropicApiKeyFrom(g_ai_anthropic_api_key->getValue());
}

bool AiAppConfig::Validate(std::string& error)
{
    const ServerSettings server = GetServerSettings();
    if (server.host.empty())
    {
        error = "ai.server.host can not be empty";
        return false;
    }
    if (server.port == 0)
    {
        error = "ai.server.port can not be 0";
        return false;
    }
    if (server.enable_ssl && (server.cert_file.empty() || server.key_file.empty()))
    {
        error = "ssl enabled but cert/key file is empty";
        return false;
    }

    const LlmSettings llm = GetLlmSettings();
    if (llm.providers.empty())
    {
        error = "ai.llm.providers can not be empty";
        return false;
    }

    std::set<std::string> provider_ids;
    std::set<std::string> enabled_provider_ids;
    for (size_t i = 0; i < llm.providers.size(); ++i)
    {
        const LlmProviderSettings& provider = llm.providers[i];
        if (provider.id.empty())
        {
            error = "ai.llm.providers[].id can not be empty";
            return false;
        }
        if (!provider_ids.insert(provider.id).second)
        {
            error = "duplicated ai.llm.providers[].id: " + provider.id;
            return false;
        }
        if (provider.type != "openai_compatible" && provider.type != "anthropic")
        {
            error = "ai.llm.providers[].type must be one of: openai_compatible, anthropic";
            return false;
        }
        if (!provider.enabled)
        {
            continue;
        }
        enabled_provider_ids.insert(provider.id);
        if (provider.type == "openai_compatible")
        {
            const OpenAICompatibleSettings& openai = provider.openai_compatible;
            const ApiKeyPoolSettings& key_pool = provider.key_pool;
            if (openai.base_url.empty())
            {
                error = "ai.llm.providers[].base_url can not be empty for openai_compatible provider: " + provider.id;
                return false;
            }
            if (openai.api_key.empty() && !key_pool.enabled)
            {
                error = "openai-compatible provider api key is empty when key_pool is disabled: " + provider.id;
                return false;
            }
            if (provider.default_model.empty())
            {
                error = "openai-compatible provider default_model can not be empty: " + provider.id;
                return false;
            }
            if (key_pool.enabled)
            {
                if (key_pool.hot_reload_enabled && key_pool.reload_interval_ms == 0)
                {
                    error = "openai key_pool.reload_interval_ms must be > 0 for provider: " + provider.id;
                    return false;
                }
                if (key_pool.max_retry_per_request > 8)
                {
                    error = "openai key_pool.max_retry_per_request must be <= 8 for provider: " + provider.id;
                    return false;
                }
                if (key_pool.cooldown_short_ms == 0 || key_pool.cooldown_long_ms == 0)
                {
                    error = "openai key_pool cooldown values must be > 0 for provider: " + provider.id;
                    return false;
                }
            }
        }
        else
        {
            const AnthropicSettings& anthropic = provider.anthropic;
            if (anthropic.base_url.empty())
            {
                error = "anthropic provider base_url can not be empty: " + provider.id;
                return false;
            }
            const ApiKeyPoolSettings& key_pool = provider.key_pool;
            if (anthropic.api_key.empty() && !key_pool.enabled)
            {
                error = "anthropic provider api key is empty when key_pool is disabled: " + provider.id;
                return false;
            }
            if (provider.default_model.empty())
            {
                error = "anthropic provider default_model can not be empty: " + provider.id;
                return false;
            }
            if (anthropic.api_version.empty())
            {
                error = "anthropic provider api_version can not be empty: " + provider.id;
                return false;
            }
            if (key_pool.enabled)
            {
                if (key_pool.hot_reload_enabled && key_pool.reload_interval_ms == 0)
                {
                    error = "anthropic key_pool.reload_interval_ms must be > 0 for provider: " + provider.id;
                    return false;
                }
                if (key_pool.max_retry_per_request > 8)
                {
                    error = "anthropic key_pool.max_retry_per_request must be <= 8 for provider: " + provider.id;
                    return false;
                }
                if (key_pool.cooldown_short_ms == 0 || key_pool.cooldown_long_ms == 0)
                {
                    error = "anthropic key_pool cooldown values must be > 0 for provider: " + provider.id;
                    return false;
                }
            }
        }
    }

    if (enabled_provider_ids.empty())
    {
        error = "ai.llm.providers has no enabled provider";
        return false;
    }

    if (llm.routing.default_provider_id.empty())
    {
        error = "ai.llm.routing.default_provider_id can not be empty";
        return false;
    }
    if (enabled_provider_ids.find(llm.routing.default_provider_id) == enabled_provider_ids.end())
    {
        error = "ai.llm.routing.default_provider_id is not enabled provider: " + llm.routing.default_provider_id;
        return false;
    }
    for (std::unordered_map<std::string, std::string>::const_iterator it = llm.routing.model_to_provider.begin();
         it != llm.routing.model_to_provider.end();
         ++it)
    {
        if (it->first.empty() || it->second.empty())
        {
            error = "ai.llm.routing.model_map key/value can not be empty";
            return false;
        }
        if (enabled_provider_ids.find(it->second) == enabled_provider_ids.end())
        {
            error = "ai.llm.routing.model_map points to unknown or disabled provider: " + it->second;
            return false;
        }
    }

    const ChatSettings chat = GetChatSettings();
    if (chat.max_context_messages == 0)
    {
        error = "ai.chat.max_context_messages must be > 0";
        return false;
    }
    if (chat.history_load_limit == 0)
    {
        error = "ai.chat.history_load_limit must be > 0";
        return false;
    }
    if (chat.max_context_tokens == 0)
    {
        error = "ai.chat.max_context_tokens must be > 0";
        return false;
    }
    if (chat.recent_window_messages == 0)
    {
        error = "ai.chat.recent_window_messages must be > 0";
        return false;
    }
    if (chat.summary_trigger_tokens == 0)
    {
        error = "ai.chat.summary_trigger_tokens must be > 0";
        return false;
    }
    if (chat.summary_max_tokens == 0)
    {
        error = "ai.chat.summary_max_tokens must be > 0";
        return false;
    }
    if (chat.summary_prompt.empty())
    {
        error = "ai.chat.summary_prompt can not be empty";
        return false;
    }

    const AuthSettings auth = GetAuthSettings();
    if (auth.token_ttl_seconds == 0)
    {
        error = "ai.auth.token_ttl_seconds must be > 0";
        return false;
    }
    if (auth.password_pbkdf2_iterations == 0)
    {
        error = "ai.auth.password_pbkdf2_iterations must be > 0";
        return false;
    }

    const MysqlSettings mysql = GetMysqlSettings();
    if (mysql.host.empty() || mysql.user.empty() || mysql.database.empty())
    {
        error = "mysql host/user/database can not be empty";
        return false;
    }
    if (mysql.pool_min_size == 0 || mysql.pool_max_size == 0 || mysql.pool_min_size > mysql.pool_max_size)
    {
        error = "ai.mysql.pool_min_size/pool_max_size invalid";
        return false;
    }
    if (mysql.pool_acquire_timeout_ms == 0)
    {
        error = "ai.mysql.pool_acquire_timeout_ms must be > 0";
        return false;
    }

    const PersistSettings persist = GetPersistSettings();
    if (persist.queue_capacity == 0 || persist.batch_size == 0 || persist.flush_interval_ms == 0)
    {
        error = "ai.persist settings queue_capacity/batch_size/flush_interval_ms must be > 0";
        return false;
    }

    const RagSettings rag = GetRagSettings();
    if (rag.enabled)
    {
        if (rag.recall_trigger_mode != "always" && rag.recall_trigger_mode != "intent")
        {
            error = "ai.rag.recall_trigger_mode must be one of: always, intent";
            return false;
        }
        if (rag.recall_trigger_mode == "intent" && rag.recall_intent_min_chars == 0)
        {
            error = "ai.rag.recall_intent_min_chars must be > 0 when recall_trigger_mode=intent";
            return false;
        }
        if (rag.top_k == 0)
        {
            error = "ai.rag.top_k must be > 0";
            return false;
        }
        if (rag.max_snippet_chars == 0)
        {
            error = "ai.rag.max_snippet_chars must be > 0";
            return false;
        }

        const EmbeddingSettings embedding = GetEmbeddingSettings();
        if (embedding.provider != "ollama")
        {
            error = "ai.embedding.provider currently only supports ollama";
            return false;
        }
        if (embedding.base_url.empty() || embedding.model.empty())
        {
            error = "ai.embedding.base_url/model can not be empty";
            return false;
        }
        if (embedding.connect_timeout_ms == 0 || embedding.request_timeout_ms == 0)
        {
            error = "ai.embedding.connect_timeout_ms/request_timeout_ms must be > 0";
            return false;
        }

        const QdrantSettings qdrant = GetQdrantSettings();
        if (qdrant.base_url.empty() || qdrant.collection.empty())
        {
            error = "ai.vector_store.qdrant.base_url/collection can not be empty";
            return false;
        }
        if (qdrant.request_timeout_ms == 0)
        {
            error = "ai.vector_store.qdrant.request_timeout_ms must be > 0";
            return false;
        }

        const RagIndexerSettings rag_indexer = GetRagIndexerSettings();
        if (rag_indexer.queue_capacity == 0 ||
            rag_indexer.batch_size == 0 ||
            rag_indexer.flush_interval_ms == 0)
        {
            error = "ai.rag_indexer queue_capacity/batch_size/flush_interval_ms must be > 0";
            return false;
        }
        if (rag_indexer.assistant_index_mode != "all" &&
            rag_indexer.assistant_index_mode != "fact_like" &&
            rag_indexer.assistant_index_mode != "none")
        {
            error = "ai.rag_indexer.assistant_index_mode must be one of: all, fact_like, none";
            return false;
        }
        if (rag_indexer.assistant_index_mode == "fact_like" && rag_indexer.assistant_min_chars == 0)
        {
            error = "ai.rag_indexer.assistant_min_chars must be > 0 when assistant_index_mode=fact_like";
            return false;
        }
        if ((rag_indexer.dedup_ttl_ms == 0) != (rag_indexer.dedup_max_entries == 0))
        {
            error = "ai.rag_indexer.dedup_ttl_ms and dedup_max_entries should both be 0 or both > 0";
            return false;
        }
    }

    return true;
}

} // namespace config
} // namespace ai
