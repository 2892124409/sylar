#include "sylar/http/http_framework_config.h"

#include "sylar/base/config.h"

namespace sylar
{
    namespace http
    {

        namespace
        {
            // 重载形式：运行时动态读取型（每次解析请求时读取，后续请求立即生效）
            static ConfigVar<uint64_t>::ptr g_http_max_header_size =
                Config::Lookup<uint64_t>("http.max_header_size", 8 * 1024, "http request max header size");

            // 重载形式：运行时动态读取型（每次解析请求时读取，后续请求立即生效）
            static ConfigVar<uint64_t>::ptr g_http_max_body_size =
                Config::Lookup<uint64_t>("http.max_body_size", 10 * 1024 * 1024, "http request max body size");

            // 重载形式：启动参数型（当前在 HttpServer 启动时读取；运行中修改需重启或手动重建定时器）
            static ConfigVar<uint64_t>::ptr g_http_session_sweep_interval_ms =
                Config::Lookup<uint64_t>("http.session_sweep_interval_ms", 60 * 1000, "http session sweep interval ms");

            // 重载形式：新对象生效型（SSE 业务侧按需读取，已运行中的发送流程不会自动重配）
            static ConfigVar<uint64_t>::ptr g_http_sse_heartbeat_interval_ms =
                Config::Lookup<uint64_t>("http.sse_heartbeat_interval_ms", 15 * 1000, "http sse heartbeat interval ms");

            // 重载形式：运行时动态读取型（每次构造错误响应时读取，后续响应立即生效）
            // 取值：0=text, 1=json
            static ConfigVar<int>::ptr g_http_error_response_format =
                Config::Lookup<int>("http.error_response_format", static_cast<int>(HttpFrameworkConfig::ERROR_FORMAT_JSON),
                                    "http error response format: 0=text, 1=json");
        }

        size_t HttpFrameworkConfig::GetMaxHeaderSize()
        {
            return static_cast<size_t>(g_http_max_header_size->getValue());
        }

        void HttpFrameworkConfig::SetMaxHeaderSize(size_t value)
        {
            g_http_max_header_size->setValue(static_cast<uint64_t>(value));
        }

        size_t HttpFrameworkConfig::GetMaxBodySize()
        {
            return static_cast<size_t>(g_http_max_body_size->getValue());
        }

        void HttpFrameworkConfig::SetMaxBodySize(size_t value)
        {
            g_http_max_body_size->setValue(static_cast<uint64_t>(value));
        }

        uint64_t HttpFrameworkConfig::GetSessionSweepIntervalMs()
        {
            return g_http_session_sweep_interval_ms->getValue();
        }

        void HttpFrameworkConfig::SetSessionSweepIntervalMs(uint64_t value)
        {
            g_http_session_sweep_interval_ms->setValue(value);
        }

        uint64_t HttpFrameworkConfig::GetSSEHeartbeatIntervalMs()
        {
            return g_http_sse_heartbeat_interval_ms->getValue();
        }

        void HttpFrameworkConfig::SetSSEHeartbeatIntervalMs(uint64_t value)
        {
            g_http_sse_heartbeat_interval_ms->setValue(value);
        }

        HttpFrameworkConfig::ErrorResponseFormat HttpFrameworkConfig::GetErrorResponseFormat()
        {
            int value = g_http_error_response_format->getValue();
            if (value != static_cast<int>(ERROR_FORMAT_TEXT) &&
                value != static_cast<int>(ERROR_FORMAT_JSON))
            {
                return ERROR_FORMAT_JSON;
            }
            return static_cast<ErrorResponseFormat>(value);
        }

        void HttpFrameworkConfig::SetErrorResponseFormat(ErrorResponseFormat value)
        {
            g_http_error_response_format->setValue(static_cast<int>(value));
        }

    } // namespace http
} // namespace sylar
