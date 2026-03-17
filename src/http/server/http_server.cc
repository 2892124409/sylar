#include "http/server/http_server.h"

#include "http/core/http_error.h"
#include "http/core/http_framework_config.h"
#include "http/core/http_memory_pool.h"
#include "http/core/http_parser.h"
#include "http/ssl/ssl_socket.h"
#include "log/logger.h"

#include <sstream>

namespace http
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

namespace
{
class ScopedActiveConnection
{
  public:
    explicit ScopedActiveConnection(std::atomic<size_t>& counter)
        : m_counter(counter), m_current(m_counter.fetch_add(1, std::memory_order_acq_rel) + 1)
    {
    }

    ~ScopedActiveConnection()
    {
        m_counter.fetch_sub(1, std::memory_order_acq_rel);
    }

    size_t current() const
    {
        return m_current;
    }

  private:
    std::atomic<size_t>& m_counter;
    size_t m_current;
};

static void ApplyKeepAliveHeaderIfNeeded(const HttpResponse::ptr& response)
{
    if (!response->isKeepAlive() || !response->getHeader("keep-alive").empty())
    {
        return;
    }

    std::ostringstream header;
    uint64_t timeout_ms = HttpFrameworkConfig::GetKeepAliveTimeoutMs();
    if (timeout_ms > 0)
    {
        uint64_t timeout_seconds = (timeout_ms + 999) / 1000;
        header << "timeout=" << timeout_seconds;
    }

    uint32_t max_requests = HttpFrameworkConfig::GetKeepAliveMaxRequests();
    if (max_requests > 0)
    {
        if (header.tellp() > 0)
        {
            header << ", ";
        }
        header << "max=" << max_requests;
    }

    if (header.tellp() > 0)
    {
        response->setHeader("Keep-Alive", header.str());
    }
}

class ScopedCleanup
{
  public:
    explicit ScopedCleanup(const std::function<void()>& fn)
        : m_fn(fn)
    {
    }

    ~ScopedCleanup()
    {
        if (m_fn)
        {
            m_fn();
        }
    }

  private:
    std::function<void()> m_fn;
};
} // namespace

HttpServer::HttpServer(sylar::IOManager* io_worker, sylar::IOManager* accept_worker)
    : sylar::net::TcpServer(io_worker, accept_worker), m_dispatch(new ServletDispatch()), m_sessionManager(new SessionManager()), m_activeConnections(0)
{
    setRecvTimeout(HttpFrameworkConfig::GetConnectionTimeoutMs());
    HttpRequestParser::SetMaxHeaderSize(HttpFrameworkConfig::GetMaxHeaderSize());
    HttpRequestParser::SetMaxBodySize(HttpFrameworkConfig::GetMaxBodySize());
    if (HttpFrameworkConfig::GetSessionEnabled() && io_worker)
    {
        m_sessionManager->startSweepTimer(static_cast<sylar::TimerManager*>(io_worker), HttpFrameworkConfig::GetSessionSweepIntervalMs());
    }
    setName("sylar-http-server");
}

HttpServer::ptr HttpServer::CreateWithConfig()
{
    sylar::IOManager::ptr io_worker(new sylar::IOManager(
        HttpFrameworkConfig::GetIOWorkerThreads(), false, "sylar-http-io-worker"));
    sylar::IOManager::ptr accept_worker(new sylar::IOManager(
        HttpFrameworkConfig::GetAcceptWorkerThreads(), false, "sylar-http-accept-worker"));

    HttpServer::ptr server(new HttpServer(io_worker.get(), accept_worker.get()));
    server->m_ownedIoWorker = io_worker;
    server->m_ownedAcceptWorker = accept_worker;
    return server;
}

void HttpServer::stop()
{
    m_sessionManager->stopSweepTimer();
    sylar::net::TcpServer::stop();
}

bool HttpServer::setSslConfig(const ssl::SslConfig& config)
{
    ssl::SslContext::ptr context(new ssl::SslContext(config, ssl::SslMode::SERVER));
    if (!context->initialize())
    {
        return false;
    }
    m_sslContext = context;
    return true;
}

void HttpServer::handleClient(sylar::Socket::ptr client)
{
    // 把底层 TCP 连接包装成 HttpSession，后续 HTTP 收发都经由它完成。
    if (m_sslContext)
    {
        ssl::SslSocket::ptr ssl_client = ssl::SslSocket::FromSocket(client, m_sslContext, ssl::SslMode::SERVER);
        if (!ssl_client || !ssl_client->handshake())
        {
            if (ssl_client)
            {
                ssl_client->close();
            }
            else if (client)
            {
                client->close();
            }
            BASE_LOG_ERROR(g_logger) << "HttpServer SSL handshake failed";
            return;
        }
        client = ssl_client;
    }

    if (isStop())
    {
        if (client)
        {
            client->close();
        }
        return;
    }

    ScopedActiveConnection active_guard(m_activeConnections);
    uint32_t max_connections = HttpFrameworkConfig::GetMaxConnections();
    if (max_connections > 0 && active_guard.current() > static_cast<size_t>(max_connections))
    {
        HttpSession::ptr rejected_session = MakeHttpPooledShared<HttpSession>(client);
        HttpRequest::ptr rejected_request;
        sylar::Socket::ptr rejected_socket = rejected_session->getSocket();
        if (rejected_socket)
        {
            rejected_socket->setRecvTimeout(100);
            rejected_request = rejected_session->recvRequest();
        }
        HttpResponse::ptr response = MakeHttpPooledShared<HttpResponse>();
        if (rejected_request)
        {
            response->setVersion(rejected_request->getVersionMajor(), rejected_request->getVersionMinor());
        }
        response->setKeepAlive(false);
        ApplyErrorResponse(response, HttpStatus::SERVICE_UNAVAILABLE, "Service Unavailable", "too many active connections");
        rejected_session->sendResponse(response);
        rejected_session->close();
        BASE_LOG_WARN(g_logger) << "HttpServer reject connection: active=" << active_guard.current()
                                << " limit=" << max_connections;
        return;
    }

    HttpSession::ptr session = MakeHttpPooledShared<HttpSession>(client);
    size_t request_count = 0;

    // keep-alive 场景下，一个连接可承载多次请求，因此循环处理直到需要断开。
    while (!isStop())
    {
        sylar::Socket::ptr socket = session->getSocket();
        if (socket)
        {
            uint64_t timeout_ms = request_count == 0 ? getRecvTimeout() : HttpFrameworkConfig::GetKeepAliveTimeoutMs();
            socket->setRecvTimeout(static_cast<int64_t>(timeout_ms));
        }

        // 从连接读取并解析一条 HTTP 请求。
        // 成功返回 request，失败/断连返回空。
        HttpRequest::ptr request = session->recvRequest();
        if (!request)
        {
            // 若为空且存在解析错误，返回 400 并附带错误信息。
            if (session->hasParserError())
            {
                HttpResponse::ptr response = MakeHttpPooledShared<HttpResponse>();
                if (session->isRequestTooLarge())
                {
                    response->setStatus(static_cast<HttpStatus>(413));
                    response->setReason("Payload Too Large");
                    ApplyErrorResponse(response, static_cast<HttpStatus>(413), "Payload Too Large", session->getParserError());
                }
                else
                {
                    ApplyErrorResponse(response, HttpStatus::BAD_REQUEST, "Bad Request", session->getParserError());
                }
                session->sendResponse(response);
            }
            // 读取失败、对端关闭或解析失败都结束当前连接处理。
            break;
        }

        // 构造本次请求对应的响应对象。
        HttpResponse::ptr response = MakeHttpPooledShared<HttpResponse>();
        // 响应版本跟随请求版本。
        response->setVersion(request->getVersionMajor(), request->getVersionMinor());
        // 默认 keep-alive 语义跟随请求。
        response->setKeepAlive(request->isKeepAlive());
        ++request_count;

        // 基于请求中的 SID 获取或创建服务端会话；必要时写回 Set-Cookie。
        if (HttpFrameworkConfig::GetSessionEnabled())
        {
            Session::ptr http_session = m_sessionManager->getOrCreate(request, response);
            // 当前函数暂未直接使用该变量，保留会话创建/续期副作用。
            (void)http_session;
        }

        try
        {
            m_dispatch->handle(request, response, session);
        }
        catch (const std::exception& ex)
        {
            ApplyErrorResponse(response, HttpStatus::INTERNAL_SERVER_ERROR, "Internal Server Error", ex.what());
        }
        catch (...)
        {
            ApplyErrorResponse(response, HttpStatus::INTERNAL_SERVER_ERROR, "Internal Server Error", "unknown exception");
        }

        uint32_t keepalive_max_requests = HttpFrameworkConfig::GetKeepAliveMaxRequests();
        if (keepalive_max_requests > 0 && request_count >= keepalive_max_requests)
        {
            response->setKeepAlive(false);
        }
        ApplyKeepAliveHeaderIfNeeded(response);

        // 流式响应（如 SSE）由 Servlet 自己写 header/body。
        // 这里跳过默认 sendResponse，但仍遵循 keep-alive 决策。
        if (response->isStream())
        {
            if (!request->isKeepAlive() || !response->isKeepAlive())
            {
                break;
            }
            continue;
        }

        // 发送响应；发送失败通常意味着连接不可用，结束循环。
        if (session->sendResponse(response) <= 0)
        {
            break;
        }

        // 只有请求和响应都允许 keep-alive 才继续处理下一条请求。
        if (!request->isKeepAlive() || !response->isKeepAlive())
        {
            break;
        }
    }

    // 跳出循环后关闭连接并记录日志。
    session->close();
    BASE_LOG_INFO(g_logger) << "HttpServer client closed";
}

} // namespace http
