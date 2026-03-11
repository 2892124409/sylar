#include "http/core/http_context.h"
#include "log/logger.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

namespace
{

    class FakeSocketStream : public sylar::SocketStream
    {
    public:
        FakeSocketStream(const std::vector<std::string> &chunks)
            : sylar::SocketStream(sylar::Socket::ptr(), false), m_chunks(chunks), m_index(0)
        {
        }

        virtual int read(void *buffer, size_t length) override
        {
            if (m_index >= m_chunks.size())
            {
                return 0;
            }
            const std::string &chunk = m_chunks[m_index++];
            size_t size = std::min(length, chunk.size());
            std::memcpy(buffer, chunk.data(), size);
            return static_cast<int>(size);
        }

        virtual int read(sylar::ByteArray::ptr, size_t) override
        {
            return -1;
        }

        virtual int write(const void *, size_t) override
        {
            return -1;
        }

        virtual int write(sylar::ByteArray::ptr, size_t) override
        {
            return -1;
        }

        virtual void close() override
        {
        }

    private:
        std::vector<std::string> m_chunks;
        size_t m_index;
    };

} // namespace

void test_half_packet_request()
{
    http::HttpContext context;
    std::vector<std::string> chunks;
    chunks.push_back("GET /hel");
    chunks.push_back("lo HTTP/1.1\r\nHost: localhost\r\n\r\n");
    FakeSocketStream stream(chunks);

    http::HttpRequest::ptr request = context.recvRequest(stream);
    assert(request);
    assert(request->getMethod() == http::HttpMethod::GET);
    assert(request->getPath() == "/hello");
    assert(!context.hasError());
}

void test_pipelined_requests()
{
    http::HttpContext context;
    std::vector<std::string> chunks;
    chunks.push_back(
        "GET /a HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /b HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    FakeSocketStream stream(chunks);

    http::HttpRequest::ptr request1 = context.recvRequest(stream);
    assert(request1);
    assert(request1->getPath() == "/a");
    assert(request1->isKeepAlive());

    http::HttpRequest::ptr request2 = context.recvRequest(stream);
    assert(request2);
    assert(request2->getPath() == "/b");
    assert(!request2->isKeepAlive());
}

void test_invalid_request_error()
{
    http::HttpContext context;
    std::vector<std::string> chunks;
    chunks.push_back("BAD / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    FakeSocketStream stream(chunks);

    http::HttpRequest::ptr request = context.recvRequest(stream);
    assert(!request);
    assert(context.hasError());
    assert(context.getError() == "unsupported http method");
}

void test_request_too_large_error()
{
    size_t old_header = http::HttpRequestParser::GetMaxHeaderSize();
    size_t old_body = http::HttpRequestParser::GetMaxBodySize();
    http::HttpRequestParser::SetMaxHeaderSize(32);
    http::HttpRequestParser::SetMaxBodySize(8);

    {
        http::HttpContext context;
        std::vector<std::string> chunks;
        chunks.push_back("GET /ping HTTP/1.1\r\nHost: localhost\r\nX-Long: 1234567890\r\n\r\n");
        FakeSocketStream stream(chunks);
        http::HttpRequest::ptr request = context.recvRequest(stream);
        assert(!request);
        assert(context.hasError());
        assert(context.isRequestTooLarge());
    }

    {
        http::HttpContext context;
        std::vector<std::string> chunks;
        chunks.push_back("POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 10\r\n\r\n0123456789");
        FakeSocketStream stream(chunks);
        http::HttpRequest::ptr request = context.recvRequest(stream);
        assert(!request);
        assert(context.hasError());
        assert(context.isRequestTooLarge());
    }

    http::HttpRequestParser::SetMaxHeaderSize(old_header);
    http::HttpRequestParser::SetMaxBodySize(old_body);
}

int main()
{
    test_half_packet_request();
    test_pipelined_requests();
    test_invalid_request_error();
    test_request_too_large_error();
    BASE_LOG_INFO(g_logger) << "test_http_context passed";
    return 0;
}
