#include "sylar/http/http_framework_config.h"

namespace sylar
{
    namespace http
    {

        namespace
        {
            size_t s_http_max_header_size = 8 * 1024;
            size_t s_http_max_body_size = 10 * 1024 * 1024;
            uint64_t s_http_session_sweep_interval_ms = 60 * 1000;
            uint64_t s_http_sse_heartbeat_interval_ms = 15 * 1000;
            HttpFrameworkConfig::ErrorResponseFormat s_http_error_response_format = HttpFrameworkConfig::ERROR_FORMAT_JSON;
        }

        size_t HttpFrameworkConfig::GetMaxHeaderSize()
        {
            return s_http_max_header_size;
        }

        void HttpFrameworkConfig::SetMaxHeaderSize(size_t value)
        {
            s_http_max_header_size = value;
        }

        size_t HttpFrameworkConfig::GetMaxBodySize()
        {
            return s_http_max_body_size;
        }

        void HttpFrameworkConfig::SetMaxBodySize(size_t value)
        {
            s_http_max_body_size = value;
        }

        uint64_t HttpFrameworkConfig::GetSessionSweepIntervalMs()
        {
            return s_http_session_sweep_interval_ms;
        }

        void HttpFrameworkConfig::SetSessionSweepIntervalMs(uint64_t value)
        {
            s_http_session_sweep_interval_ms = value;
        }

        uint64_t HttpFrameworkConfig::GetSSEHeartbeatIntervalMs()
        {
            return s_http_sse_heartbeat_interval_ms;
        }

        void HttpFrameworkConfig::SetSSEHeartbeatIntervalMs(uint64_t value)
        {
            s_http_sse_heartbeat_interval_ms = value;
        }

        HttpFrameworkConfig::ErrorResponseFormat HttpFrameworkConfig::GetErrorResponseFormat()
        {
            return s_http_error_response_format;
        }

        void HttpFrameworkConfig::SetErrorResponseFormat(ErrorResponseFormat value)
        {
            s_http_error_response_format = value;
        }

    } // namespace http
} // namespace sylar
