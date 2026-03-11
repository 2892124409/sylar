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
    uint64_t old_sweep = http::HttpFrameworkConfig::GetSessionSweepIntervalMs();
    uint64_t old_heartbeat = http::HttpFrameworkConfig::GetSSEHeartbeatIntervalMs();
    http::HttpFrameworkConfig::ErrorResponseFormat old_format = http::HttpFrameworkConfig::GetErrorResponseFormat();

    http::HttpFrameworkConfig::SetMaxHeaderSize(4096);
    http::HttpFrameworkConfig::SetMaxBodySize(2048);
    http::HttpFrameworkConfig::SetSessionSweepIntervalMs(5000);
    http::HttpFrameworkConfig::SetSSEHeartbeatIntervalMs(3000);
    http::HttpFrameworkConfig::SetErrorResponseFormat(http::HttpFrameworkConfig::ERROR_FORMAT_JSON);

    assert(http::HttpRequestParser::GetMaxHeaderSize() == 4096);
    assert(http::HttpRequestParser::GetMaxBodySize() == 2048);
    assert(http::HttpFrameworkConfig::GetSessionSweepIntervalMs() == 5000);
    assert(http::HttpFrameworkConfig::GetSSEHeartbeatIntervalMs() == 3000);

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
    http::HttpFrameworkConfig::SetSessionSweepIntervalMs(old_sweep);
    http::HttpFrameworkConfig::SetSSEHeartbeatIntervalMs(old_heartbeat);
    http::HttpFrameworkConfig::SetErrorResponseFormat(old_format);

    BASE_LOG_INFO(g_logger) << "test_http_framework_config passed";
    return 0;
}
