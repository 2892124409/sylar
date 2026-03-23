#include "http/core/http_error.h"
#include "http/core/http_framework_config.h"
#include "http/core/http_parser.h"
#include "http/server/http_server.h"
#include "log/logger.h"
#include "sylar/fiber/hook.h"
#include "sylar/net/address.h"
#include "sylar/net/socket.h"
#include "sylar/net/socket_stream.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

namespace
{
class ScopedHookState
{
  public:
    explicit ScopedHookState(bool enabled)
        : m_old(sylar::is_hook_enable())
    {
        sylar::set_hook_enable(enabled);
    }

    ~ScopedHookState()
    {
        sylar::set_hook_enable(m_old);
    }

  private:
    bool m_old;
};

size_t ParseContentLength(const std::string& response)
{
    size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
        return 0;
    }

    size_t pos = response.find("content-length:");
    if (pos == std::string::npos || pos > header_end)
    {
        return 0;
    }

    pos += std::string("content-length:").size();
    while (pos < header_end && response[pos] == ' ')
    {
        ++pos;
    }

    size_t line_end = response.find("\r\n", pos);
    std::string length_text = response.substr(pos, line_end == std::string::npos ? header_end - pos : line_end - pos);
    return static_cast<size_t>(std::strtoul(length_text.c_str(), nullptr, 10));
}

std::string ReadHttpResponse(sylar::SocketStream& stream)
{
    std::string response;
    char buf[1024] = {0};
    size_t header_end = std::string::npos;
    size_t content_length = 0;

    while (true)
    {
        if (header_end != std::string::npos &&
            response.size() >= header_end + 4 + content_length)
        {
            break;
        }

        int rt = stream.read(buf, sizeof(buf));
        if (rt <= 0)
        {
            break;
        }

        response.append(buf, rt);
        if (header_end == std::string::npos)
        {
            header_end = response.find("\r\n\r\n");
            if (header_end != std::string::npos)
            {
                content_length = ParseContentLength(response);
            }
        }
    }

    return response;
}

sylar::Socket::ptr ConnectTo(uint16_t port, int64_t recv_timeout_ms = 2000,
                             size_t attempts = 20, useconds_t retry_interval_us = 100 * 1000)
{
    sylar::Address::ptr addr = sylar::Address::LookupAny("127.0.0.1:" + std::to_string(port));
    assert(addr);

    for (size_t i = 0; i < attempts; ++i)
    {
        sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
        sock->setRecvTimeout(recv_timeout_ms);
        if (sock->connect(addr))
        {
            return sock;
        }
        sock->close();
        if (i + 1 < attempts)
        {
            usleep(retry_interval_us);
        }
    }

    assert(false && "ConnectTo exhausted retries");
    return sylar::Socket::ptr();
}

sylar::Socket::ptr ConnectToEventuallyNoHook(uint16_t port, int64_t recv_timeout_ms = 2000,
                                             size_t attempts = 20, useconds_t retry_interval_us = 100 * 1000)
{
    ScopedHookState hook_guard(false);
    sylar::Address::ptr addr = sylar::Address::LookupAny("127.0.0.1:" + std::to_string(port));
    assert(addr);

    for (size_t i = 0; i < attempts; ++i)
    {
        sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
        sock->setRecvTimeout(recv_timeout_ms);
        if (sock->connect(addr))
        {
            return sock;
        }
        sock->close();
        if (i + 1 < attempts)
        {
            ::usleep(retry_interval_us);
        }
    }

    assert(false && "ConnectToEventuallyNoHook exhausted retries");
    return sylar::Socket::ptr();
}

uint16_t AllocatePort()
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    assert(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    socklen_t len = sizeof(addr);
    assert(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);

    uint16_t port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

void SendRequest(sylar::SocketStream& stream, const std::string& request)
{
    assert(stream.writeFixSize(request.c_str(), request.size()) == static_cast<int>(request.size()));
}

} // namespace

int main()
{
    sylar::set_hook_enable(true);

    uint64_t old_connection_timeout = http::HttpFrameworkConfig::GetConnectionTimeoutMs();
    uint64_t old_keepalive_timeout = http::HttpFrameworkConfig::GetKeepAliveTimeoutMs();
    uint32_t old_keepalive_max_requests = http::HttpFrameworkConfig::GetKeepAliveMaxRequests();
    uint32_t old_max_connections = http::HttpFrameworkConfig::GetMaxConnections();
    uint32_t old_io_worker_threads = http::HttpFrameworkConfig::GetIOWorkerThreads();
    uint32_t old_accept_worker_threads = http::HttpFrameworkConfig::GetAcceptWorkerThreads();
    http::HttpFrameworkConfig::ErrorResponseFormat old_error_format = http::HttpFrameworkConfig::GetErrorResponseFormat();

    http::HttpFrameworkConfig::SetErrorResponseFormat(http::HttpFrameworkConfig::ERROR_FORMAT_JSON);
    uint16_t http_port = AllocatePort();
    uint16_t config_http_port = AllocatePort();

    {
        http::HttpFrameworkConfig::SetConnectionTimeoutMs(4321);
        sylar::IOManager io_iom(2, true, "http_test_io");
        sylar::IOManager accept_iom(1, false, "http_test_accept");
        http::HttpServer::ptr server(new http::HttpServer(&io_iom, &accept_iom));
        assert(server->getRecvTimeout() == 4321);
        http::HttpFrameworkConfig::SetConnectionTimeoutMs(old_connection_timeout);

        server->getServletDispatch()->addServlet("/ping", [](http::HttpRequest::ptr,
                                                             http::HttpResponse::ptr rsp,
                                                             http::HttpSession::ptr)
                                                 {
            rsp->setHeader("Content-Type", "text/plain");
            rsp->setBody("pong");
            return 0; });
        server->getServletDispatch()->addServlet("/user/me", [](http::HttpRequest::ptr,
                                                                http::HttpResponse::ptr rsp,
                                                                http::HttpSession::ptr)
                                                 {
            rsp->setBody("exact-user");
            return 0; });
        server->getServletDispatch()->addParamServlet("/user/:id", [](http::HttpRequest::ptr req,
                                                                      http::HttpResponse::ptr rsp,
                                                                      http::HttpSession::ptr)
                                                      {
            rsp->setHeader("Content-Type", "text/plain");
            rsp->setBody("user:" + req->getRouteParam("id"));
            return 0; });
        server->getServletDispatch()->addPreInterceptor([](http::HttpRequest::ptr req,
                                                           http::HttpResponse::ptr rsp,
                                                           http::HttpSession::ptr)
                                                        {
            rsp->setHeader("X-Pre", "1");
            if (req->getPath() == "/blocked")
            {
                http::ApplyErrorResponse(rsp, http::HttpStatus::BAD_REQUEST, "Blocked", "blocked by pre interceptor");
                return false;
            }
            return true; });
        server->getServletDispatch()->addPostInterceptor([](http::HttpRequest::ptr,
                                                            http::HttpResponse::ptr rsp,
                                                            http::HttpSession::ptr)
                                                         { rsp->setHeader("X-Post", "1"); });

        std::vector<sylar::Address::ptr> addrs;
        std::vector<sylar::Address::ptr> fails;
        addrs.push_back(sylar::Address::LookupAny("127.0.0.1:" + std::to_string(http_port)));
        assert(server->bind(addrs, fails));
        assert(server->start());

        io_iom.schedule([http_port]()
                     {
            sleep(1);
            sylar::Socket::ptr sock = ConnectTo(http_port);
            sylar::SocketStream stream(sock);
            SendRequest(stream, "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
            std::string rsp = ReadHttpResponse(stream);
            assert(rsp.find("200 OK") != std::string::npos);
            assert(rsp.find("pong") != std::string::npos);
            assert(rsp.find("x-pre: 1") != std::string::npos);
            assert(rsp.find("x-post: 1") != std::string::npos);
            stream.close(); });

        io_iom.schedule([http_port]()
                     {
            sleep(2);
            sylar::Socket::ptr sock = ConnectTo(http_port);
            sylar::SocketStream stream(sock);
            SendRequest(stream, "GET /user/42 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
            std::string rsp = ReadHttpResponse(stream);
            assert(rsp.find("user:42") != std::string::npos);
            stream.close(); });

        io_iom.schedule([http_port]()
                     {
            sleep(3);
            sylar::Socket::ptr sock = ConnectTo(http_port);
            sylar::SocketStream stream(sock);
            SendRequest(stream, "GET /user/me HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
            std::string rsp = ReadHttpResponse(stream);
            assert(rsp.find("exact-user") != std::string::npos);
            stream.close(); });

        io_iom.schedule([http_port]()
                     {
            sleep(4);
            sylar::Socket::ptr sock = ConnectTo(http_port);
            sylar::SocketStream stream(sock);
            SendRequest(stream, "GET /blocked HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
            std::string rsp = ReadHttpResponse(stream);
            assert(rsp.find("\"code\":400") != std::string::npos);
            assert(rsp.find("blocked by pre interceptor") != std::string::npos);
            assert(rsp.find("x-post: 1") != std::string::npos);
            stream.close(); });

        io_iom.schedule([http_port]()
                     {
            sleep(5);
            sylar::Socket::ptr sock = ConnectTo(http_port);
            sylar::SocketStream stream(sock);
            SendRequest(stream, "GET /missing HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
            std::string rsp = ReadHttpResponse(stream);
            assert(rsp.find("\"code\":404") != std::string::npos);
            stream.close(); });

        io_iom.schedule([http_port]()
                     {
            sleep(6);
            size_t old_header = http::HttpRequestParser::GetMaxHeaderSize();
            http::HttpRequestParser::SetMaxHeaderSize(32);

            sylar::Socket::ptr sock = ConnectTo(http_port);
            sylar::SocketStream stream(sock);
            SendRequest(stream, "GET /ping HTTP/1.1\r\nHost: localhost\r\nX-Long: 1234567890\r\n\r\n");
            std::string rsp = ReadHttpResponse(stream);
            assert(rsp.find("\"code\":413") != std::string::npos);
            stream.close();

            http::HttpRequestParser::SetMaxHeaderSize(old_header); });

        io_iom.schedule([server, http_port, old_keepalive_timeout, old_keepalive_max_requests, old_max_connections]()
                     {
            sleep(7);

            http::HttpFrameworkConfig::SetMaxConnections(1);
            http::HttpFrameworkConfig::SetKeepAliveTimeoutMs(5000);
            http::HttpFrameworkConfig::SetKeepAliveMaxRequests(0);

            sylar::Socket::ptr hold_sock = ConnectTo(http_port);
            sylar::SocketStream hold_stream(hold_sock);
            SendRequest(hold_stream, "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n");
            std::string hold_rsp = ReadHttpResponse(hold_stream);
            assert(hold_rsp.find("200 OK") != std::string::npos);
            assert(hold_rsp.find("connection: keep-alive") != std::string::npos);

            sylar::Socket::ptr reject_sock = ConnectTo(http_port);
            sylar::SocketStream reject_stream(reject_sock);
            SendRequest(reject_stream, "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
            std::string reject_rsp = ReadHttpResponse(reject_stream);
            assert(reject_rsp.find("503 Service Unavailable") != std::string::npos);
            assert(reject_rsp.find("too many active connections") != std::string::npos);
            reject_stream.close();
            hold_stream.close();

            http::HttpFrameworkConfig::SetMaxConnections(0);
            http::HttpFrameworkConfig::SetKeepAliveMaxRequests(2);
            http::HttpFrameworkConfig::SetKeepAliveTimeoutMs(5000);

            sylar::Socket::ptr keepalive_sock = ConnectTo(http_port);
            sylar::SocketStream keepalive_stream(keepalive_sock);
            const std::string keepalive_request = "GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n";

            SendRequest(keepalive_stream, keepalive_request);
            std::string keepalive_rsp1 = ReadHttpResponse(keepalive_stream);
            assert(keepalive_rsp1.find("200 OK") != std::string::npos);
            assert(keepalive_rsp1.find("connection: keep-alive") != std::string::npos);
            assert(keepalive_rsp1.find("keep-alive: timeout=5, max=2") != std::string::npos);

            SendRequest(keepalive_stream, keepalive_request);
            std::string keepalive_rsp2 = ReadHttpResponse(keepalive_stream);
            assert(keepalive_rsp2.find("200 OK") != std::string::npos);
            assert(keepalive_rsp2.find("connection: close") != std::string::npos);

            char eof_buf[16] = {0};
            int eof_rt = keepalive_stream.read(eof_buf, sizeof(eof_buf));
            assert(eof_rt == 0);
            keepalive_stream.close();

            http::HttpFrameworkConfig::SetKeepAliveMaxRequests(0);
            http::HttpFrameworkConfig::SetKeepAliveTimeoutMs(200);

            sylar::Socket::ptr timeout_sock = ConnectTo(http_port, 3000);
            sylar::SocketStream timeout_stream(timeout_sock);
            SendRequest(timeout_stream, keepalive_request);
            std::string timeout_rsp = ReadHttpResponse(timeout_stream);
            assert(timeout_rsp.find("200 OK") != std::string::npos);
            assert(timeout_rsp.find("connection: keep-alive") != std::string::npos);
            sleep(1);

            char timeout_buf[16] = {0};
            int timeout_rt = timeout_stream.read(timeout_buf, sizeof(timeout_buf));
            assert(timeout_rt == 0);
            timeout_stream.close();

            http::HttpFrameworkConfig::SetKeepAliveTimeoutMs(old_keepalive_timeout);
            http::HttpFrameworkConfig::SetKeepAliveMaxRequests(old_keepalive_max_requests);
            http::HttpFrameworkConfig::SetMaxConnections(old_max_connections);
            server->stop(); });
    }

    http::HttpFrameworkConfig::SetIOWorkerThreads(1);
    http::HttpFrameworkConfig::SetAcceptWorkerThreads(1);
    {
        http::HttpServer::ptr config_server = http::HttpServer::CreateWithConfig();
        config_server->getServletDispatch()->addServlet("/config", [](http::HttpRequest::ptr,
                                                                      http::HttpResponse::ptr rsp,
                                                                      http::HttpSession::ptr)
                                                        {
            rsp->setHeader("Content-Type", "text/plain");
            rsp->setBody("config-workers");
            return 0; });

        std::vector<sylar::Address::ptr> addrs;
        std::vector<sylar::Address::ptr> fails;
        addrs.push_back(sylar::Address::LookupAny("127.0.0.1:" + std::to_string(config_http_port)));
        assert(config_server->bind(addrs, fails));
        assert(config_server->start());

        sleep(1);
        {
            // 这段验证运行在主线程，当前线程没有 IOManager；hook 又是线程局部开关。
            // 因此这里只在主线程局部关闭 hook，使用同步阻塞 socket 做探活；
            // 服务端工作线程仍在 Scheduler::run() 内开启 hook，不受这里影响。
            // 这样测试不会耦合到“hook 已开启但当前线程没有 IOManager”的退化语义。
            ScopedHookState hook_guard(false);
            sylar::Socket::ptr sock = ConnectToEventuallyNoHook(config_http_port);
            sylar::SocketStream stream(sock);
            SendRequest(stream, "GET /config HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
            std::string rsp = ReadHttpResponse(stream);
            assert(rsp.find("200 OK") != std::string::npos);
            assert(rsp.find("config-workers") != std::string::npos);
            stream.close();
        }

        config_server->stop();
        sleep(1);
    }

    http::HttpFrameworkConfig::SetConnectionTimeoutMs(old_connection_timeout);
    http::HttpFrameworkConfig::SetKeepAliveTimeoutMs(old_keepalive_timeout);
    http::HttpFrameworkConfig::SetKeepAliveMaxRequests(old_keepalive_max_requests);
    http::HttpFrameworkConfig::SetMaxConnections(old_max_connections);
    http::HttpFrameworkConfig::SetIOWorkerThreads(old_io_worker_threads);
    http::HttpFrameworkConfig::SetAcceptWorkerThreads(old_accept_worker_threads);
    http::HttpFrameworkConfig::SetErrorResponseFormat(old_error_format);

    BASE_LOG_INFO(g_logger) << "test_http_server passed";
    return 0;
}
