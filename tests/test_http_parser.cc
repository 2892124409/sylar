#include "sylar/http/http_parser.h"
#include "sylar/log/logger.h"

#include <cassert>

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

void test_simple_request() {
    sylar::http::HttpRequestParser parser;
    std::string raw = "GET /ping?name=sylar HTTP/1.1\r\nHost: localhost\r\nCookie: SID=abc; theme=dark\r\n\r\n";
    size_t consumed = 0;
    sylar::http::HttpRequest::ptr request = parser.parse(raw, consumed);
    assert(request);
    assert(consumed == raw.size());
    assert(request->getMethod() == sylar::http::HttpMethod::GET);
    assert(request->getPath() == "/ping");
    assert(request->getParam("name") == "sylar");
    assert(request->getCookie("SID") == "abc");
    assert(request->isKeepAlive());
}

void test_half_body() {
    sylar::http::HttpRequestParser parser;
    std::string raw = "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nhel";
    size_t consumed = 0;
    sylar::http::HttpRequest::ptr request = parser.parse(raw, consumed);
    assert(!request);
    assert(consumed == 0);

    raw += "lo";
    request = parser.parse(raw, consumed);
    assert(request);
    assert(request->getBody() == "hello");
}

void test_pipelined_requests() {
    sylar::http::HttpRequestParser parser;
    std::string raw =
        "GET /a HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /b HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    size_t consumed = 0;
    sylar::http::HttpRequest::ptr request = parser.parse(raw, consumed);
    assert(request);
    assert(request->getPath() == "/a");
    std::string remain = raw.substr(consumed);
    request = parser.parse(remain, consumed);
    assert(request);
    assert(request->getPath() == "/b");
    assert(!request->isKeepAlive());
}

int main() {
    test_simple_request();
    test_half_body();
    test_pipelined_requests();
    SYLAR_LOG_INFO(g_logger) << "test_http_parser passed";
    return 0;
}
