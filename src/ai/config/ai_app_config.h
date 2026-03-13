#ifndef __SYLAR_AI_CONFIG_AI_APP_CONFIG_H__
#define __SYLAR_AI_CONFIG_AI_APP_CONFIG_H__

#include <stddef.h>
#include <stdint.h>

#include <string>

namespace ai
{
namespace config
{

struct ServerSettings
{
    std::string host;
    uint16_t port;
    bool enable_ssl;
    std::string cert_file;
    std::string key_file;
};

struct DeepSeekSettings
{
    std::string base_url;
    std::string api_key;
    std::string default_model;
    uint64_t connect_timeout_ms;
    uint64_t request_timeout_ms;
};

struct ChatSettings
{
    bool require_sid;
    size_t max_context_messages;
    size_t history_load_limit;
    size_t history_query_limit_max;
    double default_temperature;
    uint32_t default_max_tokens;
    std::string system_prompt;
};

struct MysqlSettings
{
    std::string host;
    uint16_t port;
    std::string user;
    std::string password;
    std::string database;
    std::string charset;
    uint32_t connect_timeout_seconds;
};

struct PersistSettings
{
    size_t queue_capacity;
    uint64_t flush_interval_ms;
    size_t batch_size;
};

class AiAppConfig
{
public:
    static ServerSettings GetServerSettings();
    static DeepSeekSettings GetDeepSeekSettings();
    static ChatSettings GetChatSettings();
    static MysqlSettings GetMysqlSettings();
    static PersistSettings GetPersistSettings();

    static std::string ResolveDeepSeekApiKey();

    static bool Validate(std::string &error);
};

} // namespace config
} // namespace ai

#endif
