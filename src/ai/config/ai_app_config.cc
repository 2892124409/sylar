#include "ai/config/ai_app_config.h"

#include "config/config.h"

#include <stdlib.h>

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

http::ConfigVar<bool>::ptr g_ai_chat_require_sid =
    http::Config::Lookup<bool>("ai.chat.require_sid", true, "whether sid is required");

http::ConfigVar<uint64_t>::ptr g_ai_chat_max_context_messages =
    http::Config::Lookup<uint64_t>("ai.chat.max_context_messages", 20, "max messages kept in memory context");

http::ConfigVar<uint64_t>::ptr g_ai_chat_history_load_limit =
    http::Config::Lookup<uint64_t>("ai.chat.history_load_limit", 20, "history count loaded for context hydration");

http::ConfigVar<uint64_t>::ptr g_ai_chat_history_query_limit_max =
    http::Config::Lookup<uint64_t>("ai.chat.history_query_limit_max", 200, "max history query limit");

http::ConfigVar<double>::ptr g_ai_chat_default_temperature =
    http::Config::Lookup<double>("ai.chat.default_temperature", 0.7, "default model temperature");

http::ConfigVar<uint32_t>::ptr g_ai_chat_default_max_tokens =
    http::Config::Lookup<uint32_t>("ai.chat.default_max_tokens", 1024, "default max tokens");

http::ConfigVar<std::string>::ptr g_ai_chat_system_prompt =
    http::Config::Lookup<std::string>("ai.chat.system_prompt", "", "default system prompt");

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

http::ConfigVar<uint64_t>::ptr g_ai_persist_queue_capacity =
    http::Config::Lookup<uint64_t>("ai.persist.queue_capacity", 10000, "persist queue capacity");

http::ConfigVar<uint64_t>::ptr g_ai_persist_flush_interval_ms =
    http::Config::Lookup<uint64_t>("ai.persist.flush_interval_ms", 200, "persist flush interval ms");

http::ConfigVar<uint64_t>::ptr g_ai_persist_batch_size =
    http::Config::Lookup<uint64_t>("ai.persist.batch_size", 128, "persist batch size");

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

ChatSettings AiAppConfig::GetChatSettings()
{
    ChatSettings settings;
    settings.require_sid = g_ai_chat_require_sid->getValue();
    settings.max_context_messages = static_cast<size_t>(g_ai_chat_max_context_messages->getValue());
    settings.history_load_limit = static_cast<size_t>(g_ai_chat_history_load_limit->getValue());
    settings.history_query_limit_max = static_cast<size_t>(g_ai_chat_history_query_limit_max->getValue());
    settings.default_temperature = g_ai_chat_default_temperature->getValue();
    settings.default_max_tokens = g_ai_chat_default_max_tokens->getValue();
    settings.system_prompt = g_ai_chat_system_prompt->getValue();
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

std::string AiAppConfig::ResolveOpenAICompatibleApiKey()
{
    std::string key = g_ai_openai_api_key->getValue();
    if (!key.empty())
    {
        return key;
    }

    const char* env_key = std::getenv("OPENAI_COMPATIBLE_API_KEY");
    if (env_key && env_key[0] != '\0')
    {
        return std::string(env_key);
    }

    env_key = std::getenv("OPENAI_API_KEY");
    return env_key ? std::string(env_key) : std::string();
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

    const OpenAICompatibleSettings openai = GetOpenAICompatibleSettings();
    if (openai.base_url.empty())
    {
        error = "ai.openai_compatible.base_url can not be empty";
        return false;
    }
    if (openai.api_key.empty())
    {
        error = "openai-compatible api key is empty, configure ai.openai_compatible.api_key"
                " or OPENAI_COMPATIBLE_API_KEY / OPENAI_API_KEY";
        return false;
    }
    if (openai.default_model.empty())
    {
        error = "ai.openai_compatible.default_model can not be empty";
        return false;
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

    const MysqlSettings mysql = GetMysqlSettings();
    if (mysql.host.empty() || mysql.user.empty() || mysql.database.empty())
    {
        error = "mysql host/user/database can not be empty";
        return false;
    }

    const PersistSettings persist = GetPersistSettings();
    if (persist.queue_capacity == 0 || persist.batch_size == 0 || persist.flush_interval_ms == 0)
    {
        error = "ai.persist settings queue_capacity/batch_size/flush_interval_ms must be > 0";
        return false;
    }

    return true;
}

} // namespace config
} // namespace ai
