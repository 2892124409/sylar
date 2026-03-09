#include "sylar/http/http_framework_config.h"
#include "sylar/http/http_error.h"
#include "sylar/http/http_parser.h"
#include "sylar/log/logger.h"

#include <cassert>
#include <cstddef>
#include <stdint.h>

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

int main() {
    size_t old_header = sylar::http::HttpFrameworkConfig::GetMaxHeaderSize();
    size_t old_body = sylar::http::HttpFrameworkConfig::GetMaxBodySize();
    uint64_t old_sweep = sylar::http::HttpFrameworkConfig::GetSessionSweepIntervalMs();
    uint64_t old_heartbeat = sylar::http::HttpFrameworkConfig::GetSSEHeartbeatIntervalMs();
    sylar::http::HttpFrameworkConfig::ErrorResponseFormat old_format = sylar::http::HttpFrameworkConfig::GetErrorResponseFormat();

    sylar::http::HttpFrameworkConfig::SetMaxHeaderSize(4096);
    sylar::http::HttpFrameworkConfig::SetMaxBodySize(2048);
    sylar::http::HttpFrameworkConfig::SetSessionSweepIntervalMs(5000);
    sylar::http::HttpFrameworkConfig::SetSSEHeartbeatIntervalMs(3000);
    sylar::http::HttpFrameworkConfig::SetErrorResponseFormat(sylar::http::HttpFrameworkConfig::ERROR_FORMAT_JSON);

    assert(sylar::http::HttpRequestParser::GetMaxHeaderSize() == 4096);
    assert(sylar::http::HttpRequestParser::GetMaxBodySize() == 2048);
    assert(sylar::http::HttpFrameworkConfig::GetSessionSweepIntervalMs() == 5000);
    assert(sylar::http::HttpFrameworkConfig::GetSSEHeartbeatIntervalMs() == 3000);

    {
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        sylar::http::ApplyErrorResponse(rsp, sylar::http::HttpStatus::BAD_REQUEST, "Bad Request", "config json");
        assert(rsp->getHeader("Content-Type") == "application/json; charset=utf-8");
        assert(rsp->getBody().find("\"code\":400") != std::string::npos);
        assert(rsp->getBody().find("\"message\":\"Bad Request\"") != std::string::npos);
    }

    sylar::http::HttpFrameworkConfig::SetErrorResponseFormat(sylar::http::HttpFrameworkConfig::ERROR_FORMAT_TEXT);
    {
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        sylar::http::ApplyErrorResponse(rsp, sylar::http::HttpStatus::NOT_FOUND, "Not Found", "plain text");
        assert(rsp->getHeader("Content-Type") == "text/plain; charset=utf-8");
        assert(rsp->getBody().find("Not Found") != std::string::npos);
    }

    sylar::http::HttpFrameworkConfig::SetMaxHeaderSize(old_header);
    sylar::http::HttpFrameworkConfig::SetMaxBodySize(old_body);
    sylar::http::HttpFrameworkConfig::SetSessionSweepIntervalMs(old_sweep);
    sylar::http::HttpFrameworkConfig::SetSSEHeartbeatIntervalMs(old_heartbeat);
    sylar::http::HttpFrameworkConfig::SetErrorResponseFormat(old_format);

    SYLAR_LOG_INFO(g_logger) << "test_http_framework_config passed";
    return 0;
}
