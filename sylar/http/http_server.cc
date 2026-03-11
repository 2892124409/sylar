#include "sylar/http/http_server.h"

#include "sylar/http/http_error.h"
#include "sylar/http/http_framework_config.h"
#include "sylar/http/http_parser.h"
#include "sylar/http/ssl/ssl_socket.h"
#include "sylar/log/logger.h"

namespace sylar
{
    namespace http
    {

        static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

        HttpServer::HttpServer(IOManager *io_worker, IOManager *accept_worker)
            : sylar::net::TcpServer(io_worker, accept_worker), m_dispatch(new ServletDispatch()), m_sessionManager(new SessionManager())
        {
            HttpRequestParser::SetMaxHeaderSize(HttpFrameworkConfig::GetMaxHeaderSize());
            HttpRequestParser::SetMaxBodySize(HttpFrameworkConfig::GetMaxBodySize());
            if (io_worker)
            {
                m_sessionManager->startSweepTimer(static_cast<TimerManager *>(io_worker), HttpFrameworkConfig::GetSessionSweepIntervalMs());
            }
            setName("sylar-http-server");
        }

        void HttpServer::stop()
        {
            m_sessionManager->stopSweepTimer();
            sylar::net::TcpServer::stop();
        }

        bool HttpServer::setSslConfig(const ssl::SslConfig &config)
        {
            ssl::SslContext::ptr context(new ssl::SslContext(config, ssl::SslMode::SERVER));
            if (!context->initialize())
            {
                return false;
            }
            m_sslContext = context;
            return true;
        }

        void HttpServer::handleClient(Socket::ptr client)
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
                    SYLAR_LOG_ERROR(g_logger) << "HttpServer SSL handshake failed";
                    return;
                }
                client = ssl_client;
            }

            HttpSession::ptr session(new HttpSession(client));

            // keep-alive 场景下，一个连接可承载多次请求，因此循环处理直到需要断开。
            while (!isStop())
            {
                // 从连接读取并解析一条 HTTP 请求。
                // 成功返回 request，失败/断连返回空。
                HttpRequest::ptr request = session->recvRequest();
                if (!request)
                {
                    // 若为空且存在解析错误，返回 400 并附带错误信息。
                    if (session->hasParserError())
                    {
                        HttpResponse::ptr response(new HttpResponse());
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
                HttpResponse::ptr response(new HttpResponse());
                // 响应版本跟随请求版本。
                response->setVersion(request->getVersionMajor(), request->getVersionMinor());
                // 默认 keep-alive 语义跟随请求。
                response->setKeepAlive(request->isKeepAlive());

                // 基于请求中的 SID 获取或创建服务端会话；必要时写回 Set-Cookie。
                Session::ptr http_session = m_sessionManager->getOrCreate(request, response);
                // 当前函数暂未直接使用该变量，保留会话创建/续期副作用。
                (void)http_session;

                try
                {
                    m_dispatch->handle(request, response, session);
                }
                catch (const std::exception &ex)
                {
                    ApplyErrorResponse(response, HttpStatus::INTERNAL_SERVER_ERROR, "Internal Server Error", ex.what());
                }
                catch (...)
                {
                    ApplyErrorResponse(response, HttpStatus::INTERNAL_SERVER_ERROR, "Internal Server Error", "unknown exception");
                }

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
            SYLAR_LOG_INFO(g_logger) << "HttpServer client closed";
        }

    } // namespace http
} // namespace sylar
