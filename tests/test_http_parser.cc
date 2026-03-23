#include "http/core/http_parser.h"

#include <assert.h>
#include <iostream>
#include <string>

namespace
{

void test_parse_pipeline_requests()
{
    http::HttpRequestParser parser;

    std::string buffer =
        "POST /echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "hello"
        "GET /ping HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";

    size_t consumed = 0;
    http::HttpRequest::ptr first = parser.parse(buffer, consumed);
    assert(first);
    assert(consumed > 0);
    assert(first->getMethod() == http::HttpMethod::POST);
    assert(first->getPath() == "/echo");
    assert(first->getBody() == "hello");
    assert(first->isKeepAlive());

    http::HttpRequest::ptr second = parser.parse(buffer.substr(consumed), consumed);
    assert(second);
    assert(second->getMethod() == http::HttpMethod::GET);
    assert(second->getPath() == "/ping");
    assert(!second->isKeepAlive());
}

void test_reject_invalid_http_version()
{
    http::HttpRequestParser parser;
    size_t consumed = 0;
    http::HttpRequest::ptr request = parser.parse(
        "GET /ping HTTP/a.b\r\n"
        "Host: localhost\r\n"
        "\r\n",
        consumed);

    assert(!request);
    assert(consumed == 0);
    assert(parser.hasError());
    assert(parser.getErrorCode() == http::HttpRequestParser::ERROR_INVALID_REQUEST);
    assert(parser.getError() == "invalid http version");
}

void test_parse_chunked_request()
{
    http::HttpRequestParser parser;
    size_t consumed = 0;
    std::string buffer =
        "POST /echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6;foo=bar\r\n world\r\n"
        "0\r\n"
        "X-Debug: ok\r\n"
        "\r\n"
        "GET /ping HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";

    http::HttpRequest::ptr request = parser.parse(buffer, consumed);

    assert(request);
    assert(request->getMethod() == http::HttpMethod::POST);
    assert(request->getBody() == "hello world");
    assert(request->isKeepAlive());

    http::HttpRequest::ptr next = parser.parse(buffer.substr(consumed), consumed);
    assert(next);
    assert(next->getMethod() == http::HttpMethod::GET);
    assert(next->getPath() == "/ping");
    assert(!next->isKeepAlive());
}

void test_reject_unsupported_transfer_encoding()
{
    http::HttpRequestParser parser;
    size_t consumed = 0;
    http::HttpRequest::ptr request = parser.parse(
        "POST /echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: gzip\r\n"
        "\r\n",
        consumed);

    assert(!request);
    assert(consumed == 0);
    assert(parser.hasError());
    assert(parser.getErrorCode() == http::HttpRequestParser::ERROR_NOT_IMPLEMENTED);
    assert(parser.getError() == "unsupported transfer-encoding");
}

} // namespace

int main()
{
    test_parse_pipeline_requests();
    test_reject_invalid_http_version();
    test_parse_chunked_request();
    test_reject_unsupported_transfer_encoding();

    std::cout << "test_http_parser passed" << std::endl;
    return 0;
}
