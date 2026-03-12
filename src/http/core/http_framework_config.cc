#include "http/core/http_framework_config.h"

#include "config/config.h"

#include <atomic>
#include <thread>

namespace http
{

    namespace
    {
        static const uint64_t kDefaultHttpConnectionTimeoutMs = 120 * 1000;
        static const uint64_t kDefaultHttpSessionInactivityTimeoutMs = 30 * 60 * 1000;
        static const uint64_t kDefaultHttpSocketReadBufferSize = 4096;
        static const uint64_t kDefaultHttpKeepAliveTimeoutMs = 60 * 1000;
        static const uint32_t kDefaultHttpKeepAliveMaxRequests = 100;
        static const uint32_t kDefaultHttpMaxConnections = 10000;

        static uint32_t GetDefaultHttpIoWorkerThreads()
        {
            uint32_t concurrency = static_cast<uint32_t>(std::thread::hardware_concurrency());
            return concurrency == 0 ? 1 : concurrency;
        }

        static uint64_t NormalizeUint64OrDefault(uint64_t value, uint64_t default_value)
        {
            return value == 0 ? default_value : value;
        }

        static uint32_t NormalizeUint32OrDefault(uint32_t value, uint32_t default_value)
        {
            return value == 0 ? default_value : value;
        }

        // 重载形式：运行时动态读取型（每次解析请求时读取，后续请求立即生效）
        static ConfigVar<uint64_t>::ptr g_http_max_header_size =
            Config::Lookup<uint64_t>("http.max_header_size", 8 * 1024, "http request max header size");

        // 重载形式：运行时动态读取型（每次解析请求时读取，后续请求立即生效）
        static ConfigVar<uint64_t>::ptr g_http_max_body_size =
            Config::Lookup<uint64_t>("http.max_body_size", 10 * 1024 * 1024, "http request max body size");

        // 重载形式：启动参数型（HttpServer 构造时读取；运行中修改不影响已启动服务）
        static ConfigVar<uint64_t>::ptr g_http_connection_timeout_ms =
            Config::Lookup<uint64_t>("http.connection_timeout_ms", kDefaultHttpConnectionTimeoutMs, "http connection timeout ms");

        // 重载形式：运行时动态读取型（后续请求是否自动创建 Session 立即生效）
        static ConfigVar<bool>::ptr g_http_session_enabled =
            Config::Lookup<bool>("http.session_enabled", true, "http auto session enabled");

        // 重载形式：新对象生效型（新建 SessionManager 时读取）
        static ConfigVar<uint64_t>::ptr g_http_session_inactivity_timeout_ms =
            Config::Lookup<uint64_t>("http.session_inactivity_timeout_ms", kDefaultHttpSessionInactivityTimeoutMs,
                                     "http session inactivity timeout ms");

        // 重载形式：启动参数型（当前在 HttpServer 启动时读取；运行中修改需重启或手动重建定时器）
        static ConfigVar<uint64_t>::ptr g_http_session_sweep_interval_ms =
            Config::Lookup<uint64_t>("http.session_sweep_interval_ms", 60 * 1000, "http session sweep interval ms");

        // 重载形式：新对象生效型（SSE 业务侧按需读取，已运行中的发送流程不会自动重配）
        static ConfigVar<uint64_t>::ptr g_http_sse_heartbeat_interval_ms =
            Config::Lookup<uint64_t>("http.sse_heartbeat_interval_ms", 15 * 1000, "http sse heartbeat interval ms");

        // 重载形式：运行时动态读取型（每次读取请求体时读取，后续读取立即生效）
        static ConfigVar<uint64_t>::ptr g_http_socket_read_buffer_size =
            Config::Lookup<uint64_t>("http.socket_read_buffer_size", kDefaultHttpSocketReadBufferSize,
                                     "http socket read buffer size");

        // 重载形式：监听器驱动缓存同步型（配置变化后同步到 HttpServer 运行时缓存）
        // 取值：0=unlimited
        static ConfigVar<uint32_t>::ptr g_http_max_connections =
            Config::Lookup<uint32_t>("http.max_connections", kDefaultHttpMaxConnections, "http max concurrent connections");

        // 重载形式：监听器驱动缓存同步型（配置变化后同步到 HttpServer keep-alive 超时缓存）
        static ConfigVar<uint64_t>::ptr g_http_keepalive_timeout_ms =
            Config::Lookup<uint64_t>("http.keepalive_timeout_ms", kDefaultHttpKeepAliveTimeoutMs, "http keepalive timeout ms");

        // 重载形式：监听器驱动缓存同步型（配置变化后同步到 HttpServer keep-alive 请求上限缓存）
        // 取值：0=unlimited
        static ConfigVar<uint32_t>::ptr g_http_keepalive_max_requests =
            Config::Lookup<uint32_t>("http.keepalive_max_requests", kDefaultHttpKeepAliveMaxRequests,
                                     "http keepalive max requests per connection");

        // 重载形式：启动参数型（通过 HttpServer::CreateWithConfig 创建 worker 时读取）
        static ConfigVar<uint32_t>::ptr g_http_io_worker_threads =
            Config::Lookup<uint32_t>("http.io_worker_threads", GetDefaultHttpIoWorkerThreads(), "http io worker threads");

        // 重载形式：启动参数型（通过 HttpServer::CreateWithConfig 创建 accept worker 时读取）
        static ConfigVar<uint32_t>::ptr g_http_accept_worker_threads =
            Config::Lookup<uint32_t>("http.accept_worker_threads", 1, "http accept worker threads");

        // 重载形式：运行时动态读取型（每次构造错误响应时读取，后续响应立即生效）
        // 取值：0=text, 1=json
        static ConfigVar<int>::ptr g_http_error_response_format =
            Config::Lookup<int>("http.error_response_format", static_cast<int>(HttpFrameworkConfig::ERROR_FORMAT_JSON),
                                "http error response format: 0=text, 1=json");

        static std::atomic<uint32_t> g_http_max_connections_cache(g_http_max_connections->getValue());
        static std::atomic<uint64_t> g_http_keepalive_timeout_ms_cache(
            NormalizeUint64OrDefault(g_http_keepalive_timeout_ms->getValue(), kDefaultHttpKeepAliveTimeoutMs));
        static std::atomic<uint32_t> g_http_keepalive_max_requests_cache(g_http_keepalive_max_requests->getValue());

        struct HttpFrameworkConfigCacheInitializer
        {
            HttpFrameworkConfigCacheInitializer()
            {
                g_http_max_connections->addListener([](const uint32_t &, const uint32_t &new_value) {
                    g_http_max_connections_cache.store(new_value, std::memory_order_release);
                });
                g_http_keepalive_timeout_ms->addListener([](const uint64_t &, const uint64_t &new_value) {
                    g_http_keepalive_timeout_ms_cache.store(
                        NormalizeUint64OrDefault(new_value, kDefaultHttpKeepAliveTimeoutMs), std::memory_order_release);
                });
                g_http_keepalive_max_requests->addListener([](const uint32_t &, const uint32_t &new_value) {
                    g_http_keepalive_max_requests_cache.store(new_value, std::memory_order_release);
                });
            }
        };

        static HttpFrameworkConfigCacheInitializer g_http_framework_config_cache_initializer;
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

    uint64_t HttpFrameworkConfig::GetConnectionTimeoutMs()
    {
        return NormalizeUint64OrDefault(g_http_connection_timeout_ms->getValue(), kDefaultHttpConnectionTimeoutMs);
    }

    void HttpFrameworkConfig::SetConnectionTimeoutMs(uint64_t value)
    {
        g_http_connection_timeout_ms->setValue(NormalizeUint64OrDefault(value, kDefaultHttpConnectionTimeoutMs));
    }

    bool HttpFrameworkConfig::GetSessionEnabled()
    {
        return g_http_session_enabled->getValue();
    }

    void HttpFrameworkConfig::SetSessionEnabled(bool value)
    {
        g_http_session_enabled->setValue(value);
    }

    uint64_t HttpFrameworkConfig::GetSessionInactivityTimeoutMs()
    {
        return NormalizeUint64OrDefault(g_http_session_inactivity_timeout_ms->getValue(), kDefaultHttpSessionInactivityTimeoutMs);
    }

    void HttpFrameworkConfig::SetSessionInactivityTimeoutMs(uint64_t value)
    {
        g_http_session_inactivity_timeout_ms->setValue(
            NormalizeUint64OrDefault(value, kDefaultHttpSessionInactivityTimeoutMs));
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

    size_t HttpFrameworkConfig::GetSocketReadBufferSize()
    {
        return static_cast<size_t>(
            NormalizeUint64OrDefault(g_http_socket_read_buffer_size->getValue(), kDefaultHttpSocketReadBufferSize));
    }

    void HttpFrameworkConfig::SetSocketReadBufferSize(size_t value)
    {
        g_http_socket_read_buffer_size->setValue(
            NormalizeUint64OrDefault(static_cast<uint64_t>(value), kDefaultHttpSocketReadBufferSize));
    }

    uint32_t HttpFrameworkConfig::GetMaxConnections()
    {
        return g_http_max_connections_cache.load(std::memory_order_acquire);
    }

    void HttpFrameworkConfig::SetMaxConnections(uint32_t value)
    {
        g_http_max_connections->setValue(value);
    }

    uint64_t HttpFrameworkConfig::GetKeepAliveTimeoutMs()
    {
        return g_http_keepalive_timeout_ms_cache.load(std::memory_order_acquire);
    }

    void HttpFrameworkConfig::SetKeepAliveTimeoutMs(uint64_t value)
    {
        g_http_keepalive_timeout_ms->setValue(NormalizeUint64OrDefault(value, kDefaultHttpKeepAliveTimeoutMs));
    }

    uint32_t HttpFrameworkConfig::GetKeepAliveMaxRequests()
    {
        return g_http_keepalive_max_requests_cache.load(std::memory_order_acquire);
    }

    void HttpFrameworkConfig::SetKeepAliveMaxRequests(uint32_t value)
    {
        g_http_keepalive_max_requests->setValue(value);
    }

    uint32_t HttpFrameworkConfig::GetIOWorkerThreads()
    {
        return NormalizeUint32OrDefault(g_http_io_worker_threads->getValue(), GetDefaultHttpIoWorkerThreads());
    }

    void HttpFrameworkConfig::SetIOWorkerThreads(uint32_t value)
    {
        g_http_io_worker_threads->setValue(NormalizeUint32OrDefault(value, GetDefaultHttpIoWorkerThreads()));
    }

    uint32_t HttpFrameworkConfig::GetAcceptWorkerThreads()
    {
        return NormalizeUint32OrDefault(g_http_accept_worker_threads->getValue(), 1);
    }

    void HttpFrameworkConfig::SetAcceptWorkerThreads(uint32_t value)
    {
        g_http_accept_worker_threads->setValue(NormalizeUint32OrDefault(value, 1));
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
