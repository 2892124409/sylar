#include "http/core/http_framework_config.h"
#include "http/core/http_request.h"
#include "http/core/http_response.h"
#include "http/router/servlet.h"
#include "http/server/http_server.h"
#include "http/server/http_session.h"
#include "sylar/fiber/iomanager.h"
#include "sylar/net/address.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

uint16_t PickAvailablePort()
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    int reuse = 1;
    int rt = ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    assert(rt == 0);

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    rt = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    assert(rt == 0);

    socklen_t len = sizeof(addr);
    rt = ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    assert(rt == 0);

    uint16_t port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

int ConnectWithRetry(uint16_t port)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(fd >= 0);

        timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        int rt = ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        assert(rt == 0);
        rt = ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        assert(rt == 0);

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
        {
            return fd;
        }

        int err = errno;
        ::close(fd);
        if (err != ECONNREFUSED)
        {
            std::cerr << "connect failed, errno=" << err << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    assert(false && "connect retry timeout");
    return -1;
}

void SendAll(int fd, const std::string& data)
{
    size_t offset = 0;
    while (offset < data.size())
    {
        ssize_t rt = ::send(fd, data.data() + offset, data.size() - offset, 0);
        assert(rt > 0);
        offset += static_cast<size_t>(rt);
    }
}

std::string ReadAll(int fd)
{
    std::string out;
    char buffer[4096];
    while (true)
    {
        ssize_t rt = ::recv(fd, buffer, sizeof(buffer), 0);
        if (rt == 0)
        {
            break;
        }
        if (rt < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            assert(false && "recv failed");
        }
        out.append(buffer, static_cast<size_t>(rt));
    }
    return out;
}

std::string SendHttpAndReadAll(uint16_t port, const std::string& request)
{
    int fd = ConnectWithRetry(port);
    SendAll(fd, request);
    std::string response = ReadAll(fd);
    ::close(fd);
    return response;
}

void AssertContains(const std::string& haystack, const std::string& needle)
{
    if (haystack.find(needle) == std::string::npos)
    {
        std::cerr << "missing expected fragment: " << needle << "\nresponse:\n"
                  << haystack << std::endl;
        assert(false);
    }
}

bool ConnectOnce(uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bool ok = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return ok;
}

void AssertShortConnectionBurst(uint16_t port, size_t total_requests, size_t concurrency)
{
    const std::string request =
        "GET /ping HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";

    size_t next_index = 0;
    size_t ok_count = 0;
    size_t bad_count = 0;
    std::vector<std::string> bad_examples;
    std::mutex mutex;

    auto worker = [&]()
    {
        while (true)
        {
            size_t current = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (next_index >= total_requests)
                {
                    return;
                }
                current = next_index++;
                (void)current;
            }

            std::string response = SendHttpAndReadAll(port, request);
            bool ok = response.find("HTTP/1.1 200 OK") != std::string::npos &&
                      response.find("\r\n\r\npong") != std::string::npos;

            std::lock_guard<std::mutex> lock(mutex);
            if (ok)
            {
                ++ok_count;
            }
            else
            {
                ++bad_count;
                if (bad_examples.size() < 5)
                {
                    bad_examples.push_back(response);
                }
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(concurrency);
    for (size_t i = 0; i < concurrency; ++i)
    {
        threads.emplace_back(worker);
    }
    for (size_t i = 0; i < threads.size(); ++i)
    {
        threads[i].join();
    }

    if (bad_count != 0)
    {
        std::cerr << "short connection burst failed: ok=" << ok_count
                  << " bad=" << bad_count << std::endl;
        for (size_t i = 0; i < bad_examples.size(); ++i)
        {
            std::cerr << "bad response[" << i << "]:\n"
                      << bad_examples[i] << std::endl;
        }
    }
    assert(bad_count == 0);
    assert(ok_count == total_requests);
}

} // namespace

int main()
{
    http::HttpFrameworkConfig::SetSessionEnabled(false);

    const uint16_t port = PickAvailablePort();
    auto io_worker = std::make_shared<sylar::IOManager>(2, false, "http-test-io");
    auto accept_worker = std::make_shared<sylar::IOManager>(1, false, "http-test-accept");
    auto server = std::make_shared<http::HttpServer>(io_worker.get(), accept_worker.get());

    server->getServletDispatch()->addServlet(
        "/ping",
        [](http::HttpRequest::ptr, http::HttpResponse::ptr resp, http::HttpSession::ptr) -> int32_t
        {
            resp->setStatus(http::HttpStatus::OK);
            resp->setHeader("Content-Type", "text/plain");
            resp->setBody("pong");
            return 0;
        });

    server->getServletDispatch()->addServlet(
        http::HttpMethod::POST,
        "/echo",
        [](http::HttpRequest::ptr req, http::HttpResponse::ptr resp, http::HttpSession::ptr) -> int32_t
        {
            resp->setStatus(http::HttpStatus::OK);
            resp->setHeader("Content-Type", "text/plain");
            resp->setBody(req->getBody());
            return 0;
        });

    sylar::IPv4Address::ptr addr = sylar::IPv4Address::Create("127.0.0.1", port);
    assert(addr);
    assert(server->bind(addr));
    assert(server->start());

    std::string ping_response = SendHttpAndReadAll(
        port,
        "GET /ping HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n");
    AssertContains(ping_response, "HTTP/1.1 200 OK");
    AssertContains(ping_response, "\r\n\r\npong");

    std::string pipeline_response = SendHttpAndReadAll(
        port,
        "POST /echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "hello"
        "GET /ping HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n");
    AssertContains(pipeline_response, "HTTP/1.1 200 OK");
    AssertContains(pipeline_response, "\r\n\r\nhello");
    AssertContains(pipeline_response, "\r\n\r\npong");

    std::string invalid_version_response = SendHttpAndReadAll(
        port,
        "GET /ping HTTP/a.b\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n");
    AssertContains(invalid_version_response, "HTTP/1.1 400 Bad Request");
    AssertContains(invalid_version_response, "invalid http version");

    std::string chunked_response = SendHttpAndReadAll(
        port,
        "POST /echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "0\r\n"
        "\r\n");
    AssertContains(chunked_response, "HTTP/1.1 200 OK");
    AssertContains(chunked_response, "\r\n\r\nhello");

    std::string chunked_pipeline_response = SendHttpAndReadAll(
        port,
        "POST /echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6\r\n world\r\n"
        "0\r\n"
        "X-Trailer: ok\r\n"
        "\r\n"
        "GET /ping HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n");
    AssertContains(chunked_pipeline_response, "HTTP/1.1 200 OK");
    AssertContains(chunked_pipeline_response, "\r\n\r\nhello world");
    AssertContains(chunked_pipeline_response, "\r\n\r\npong");

    std::string unsupported_te_response = SendHttpAndReadAll(
        port,
        "POST /echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: gzip\r\n"
        "Connection: close\r\n"
        "\r\n");
    AssertContains(unsupported_te_response, "HTTP/1.1 501 Not Implemented");
    AssertContains(unsupported_te_response, "unsupported transfer-encoding");

    AssertShortConnectionBurst(port, 8000, 128);

    server->stop();
    accept_worker->stop();
    io_worker->stop();

    assert(!ConnectOnce(port));

    std::cout << "test_http_server passed" << std::endl;
    return 0;
}
