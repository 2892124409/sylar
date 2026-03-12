#include "http/core/http_framework_config.h"
#include "http/core/http_error.h"
#include "http/core/http_parser.h"
#include "log/logger.h"

#include <cassert>
#include <cstddef>
#include <stdint.h>

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

int main() {
    size_t old_header = http::HttpFrameworkConfig::GetMaxHeaderSize();
    size_t old_body = http::HttpFrameworkConfig::GetMaxBodySize();
    uint64_t old_connection_timeout = http::HttpFrameworkConfig::GetConnectionTimeoutMs();
    bool old_session_enabled = http::HttpFrameworkConfig::GetSessionEnabled();
    uint64_t old_session_inactivity = http::HttpFrameworkConfig::GetSessionInactivityTimeoutMs();
    uint64_t old_sweep = http::HttpFrameworkConfig::GetSessionSweepIntervalMs();
    uint64_t old_heartbeat = http::HttpFrameworkConfig::GetSSEHeartbeatIntervalMs();
    size_t old_socket_read_buffer = http::HttpFrameworkConfig::GetSocketReadBufferSize();
    uint32_t old_max_connections = http::HttpFrameworkConfig::GetMaxConnections();
    uint64_t old_keepalive_timeout = http::HttpFrameworkConfig::GetKeepAliveTimeoutMs();
    uint32_t old_keepalive_max_requests = http::HttpFrameworkConfig::GetKeepAliveMaxRequests();
    uint32_t old_io_worker_threads = http::HttpFrameworkConfig::GetIOWorkerThreads();
    uint32_t old_accept_worker_threads = http::HttpFrameworkConfig::GetAcceptWorkerThreads();
    http::HttpFrameworkConfig::ErrorResponseFormat old_format = http::HttpFrameworkConfig::GetErrorResponseFormat();

    http::HttpFrameworkConfig::SetMaxHeaderSize(4096);
    http::HttpFrameworkConfig::SetMaxBodySize(2048);
    http::HttpFrameworkConfig::SetConnectionTimeoutMs(4321);
    http::HttpFrameworkConfig::SetSessionEnabled(false);
    http::HttpFrameworkConfig::SetSessionInactivityTimeoutMs(6789);
    http::HttpFrameworkConfig::SetSessionSweepIntervalMs(5000);
    http::HttpFrameworkConfig::SetSSEHeartbeatIntervalMs(3000);
    http::HttpFrameworkConfig::SetSocketReadBufferSize(256);
    http::HttpFrameworkConfig::SetMaxConnections(1234);
    http::HttpFrameworkConfig::SetKeepAliveTimeoutMs(2345);
    http::HttpFrameworkConfig::SetKeepAliveMaxRequests(12);
    http::HttpFrameworkConfig::SetIOWorkerThreads(3);
    http::HttpFrameworkConfig::SetAcceptWorkerThreads(2);
    http::HttpFrameworkConfig::SetErrorResponseFormat(http::HttpFrameworkConfig::ERROR_FORMAT_JSON);

    assert(http::HttpRequestParser::GetMaxHeaderSize() == 4096);
    assert(http::HttpRequestParser::GetMaxBodySize() == 2048);
    assert(http::HttpFrameworkConfig::GetConnectionTimeoutMs() == 4321);
    assert(!http::HttpFrameworkConfig::GetSessionEnabled());
    assert(http::HttpFrameworkConfig::GetSessionInactivityTimeoutMs() == 6789);
    assert(http::HttpFrameworkConfig::GetSessionSweepIntervalMs() == 5000);
    assert(http::HttpFrameworkConfig::GetSSEHeartbeatIntervalMs() == 3000);
    assert(http::HttpFrameworkConfig::GetSocketReadBufferSize() == 256);
    assert(http::HttpFrameworkConfig::GetMaxConnections() == 1234);
    assert(http::HttpFrameworkConfig::GetKeepAliveTimeoutMs() == 2345);
    assert(http::HttpFrameworkConfig::GetKeepAliveMaxRequests() == 12);
    assert(http::HttpFrameworkConfig::GetIOWorkerThreads() == 3);
    assert(http::HttpFrameworkConfig::GetAcceptWorkerThreads() == 2);

    http::HttpFrameworkConfig::SetConnectionTimeoutMs(0);
    http::HttpFrameworkConfig::SetSessionInactivityTimeoutMs(0);
    http::HttpFrameworkConfig::SetSocketReadBufferSize(0);
    http::HttpFrameworkConfig::SetKeepAliveTimeoutMs(0);
    http::HttpFrameworkConfig::SetIOWorkerThreads(0);
    http::HttpFrameworkConfig::SetAcceptWorkerThreads(0);
    assert(http::HttpFrameworkConfig::GetConnectionTimeoutMs() == 120000);
    assert(http::HttpFrameworkConfig::GetSessionInactivityTimeoutMs() == 1800000);
    assert(http::HttpFrameworkConfig::GetSocketReadBufferSize() == 4096);
    assert(http::HttpFrameworkConfig::GetKeepAliveTimeoutMs() == 60000);
    assert(http::HttpFrameworkConfig::GetIOWorkerThreads() >= 1);
    assert(http::HttpFrameworkConfig::GetAcceptWorkerThreads() == 1);

    http::HttpFrameworkConfig::SetMaxConnections(0);
    http::HttpFrameworkConfig::SetKeepAliveMaxRequests(0);
    assert(http::HttpFrameworkConfig::GetMaxConnections() == 0);
    assert(http::HttpFrameworkConfig::GetKeepAliveMaxRequests() == 0);

    {
        http::HttpResponse::ptr rsp(new http::HttpResponse());
        http::ApplyErrorResponse(rsp, http::HttpStatus::BAD_REQUEST, "Bad Request", "config json");
        assert(rsp->getHeader("Content-Type") == "application/json; charset=utf-8");
        assert(rsp->getBody().find("\"code\":400") != std::string::npos);
        assert(rsp->getBody().find("\"message\":\"Bad Request\"") != std::string::npos);
    }

    http::HttpFrameworkConfig::SetErrorResponseFormat(http::HttpFrameworkConfig::ERROR_FORMAT_TEXT);
    {
        http::HttpResponse::ptr rsp(new http::HttpResponse());
        http::ApplyErrorResponse(rsp, http::HttpStatus::NOT_FOUND, "Not Found", "plain text");
        assert(rsp->getHeader("Content-Type") == "text/plain; charset=utf-8");
        assert(rsp->getBody().find("Not Found") != std::string::npos);
    }

    http::HttpFrameworkConfig::SetMaxHeaderSize(old_header);
    http::HttpFrameworkConfig::SetMaxBodySize(old_body);
    http::HttpFrameworkConfig::SetConnectionTimeoutMs(old_connection_timeout);
    http::HttpFrameworkConfig::SetSessionEnabled(old_session_enabled);
    http::HttpFrameworkConfig::SetSessionInactivityTimeoutMs(old_session_inactivity);
    http::HttpFrameworkConfig::SetSessionSweepIntervalMs(old_sweep);
    http::HttpFrameworkConfig::SetSSEHeartbeatIntervalMs(old_heartbeat);
    http::HttpFrameworkConfig::SetSocketReadBufferSize(old_socket_read_buffer);
    http::HttpFrameworkConfig::SetMaxConnections(old_max_connections);
    http::HttpFrameworkConfig::SetKeepAliveTimeoutMs(old_keepalive_timeout);
    http::HttpFrameworkConfig::SetKeepAliveMaxRequests(old_keepalive_max_requests);
    http::HttpFrameworkConfig::SetIOWorkerThreads(old_io_worker_threads);
    http::HttpFrameworkConfig::SetAcceptWorkerThreads(old_accept_worker_threads);
    http::HttpFrameworkConfig::SetErrorResponseFormat(old_format);

    BASE_LOG_INFO(g_logger) << "test_http_framework_config passed";
    return 0;
}
