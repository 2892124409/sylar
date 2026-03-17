#include "http/core/http_parser.h"
#include "log/logger.h"

#include <cassert>

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

void test_simple_request()
{
    http::HttpRequestParser parser;
    std::string raw = "GET /ping?name=sylar HTTP/1.1\r\nHost: localhost\r\nCookie: SID=abc; theme=dark\r\n\r\n";
    size_t consumed = 0;
    http::HttpRequest::ptr request = parser.parse(raw, consumed);
    assert(request);
    assert(consumed == raw.size());
    assert(request->getMethod() == http::HttpMethod::GET);
    assert(request->getPath() == "/ping");
    assert(request->getParam("name") == "sylar");
    assert(request->getCookie("SID") == "abc");
    assert(request->isKeepAlive());
}

void test_half_body()
{
    http::HttpRequestParser parser;
    std::string raw = "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nhel";
    size_t consumed = 0;
    http::HttpRequest::ptr request = parser.parse(raw, consumed);
    assert(!request);
    assert(consumed == 0);

    raw += "lo";
    request = parser.parse(raw, consumed);
    assert(request);
    assert(request->getBody() == "hello");
}

void test_pipelined_requests()
{
    http::HttpRequestParser parser;
    std::string raw =
        "GET /a HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /b HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    size_t consumed = 0;
    http::HttpRequest::ptr request = parser.parse(raw, consumed);
    assert(request);
    assert(request->getPath() == "/a");
    std::string remain = raw.substr(consumed);
    request = parser.parse(remain, consumed);
    assert(request);
    assert(request->getPath() == "/b");
    assert(!request->isKeepAlive());
}

void test_request_too_large()
{
    size_t old_header = http::HttpRequestParser::GetMaxHeaderSize();
    size_t old_body = http::HttpRequestParser::GetMaxBodySize();

    http::HttpRequestParser::SetMaxHeaderSize(32);
    http::HttpRequestParser::SetMaxBodySize(8);

    {
        http::HttpRequestParser parser;
        std::string raw = "GET /ping HTTP/1.1\r\nHost: localhost\r\nX-Long: 1234567890\r\n\r\n";
        size_t consumed = 0;
        http::HttpRequest::ptr request = parser.parse(raw, consumed);
        assert(!request);
        assert(parser.hasError());
        assert(parser.isRequestTooLarge());
    }

    {
        http::HttpRequestParser parser;
        std::string raw = "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 10\r\n\r\n0123456789";
        size_t consumed = 0;
        http::HttpRequest::ptr request = parser.parse(raw, consumed);
        assert(!request);
        assert(parser.hasError());
        assert(parser.isRequestTooLarge());
    }

    http::HttpRequestParser::SetMaxHeaderSize(old_header);
    http::HttpRequestParser::SetMaxBodySize(old_body);
}

int main()
{
    test_simple_request();
    test_half_body();
    test_pipelined_requests();
    test_request_too_large();
    BASE_LOG_INFO(g_logger) << "test_http_parser passed";
    return 0;
}
