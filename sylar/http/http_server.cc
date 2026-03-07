#include "sylar/http/http_server.h"

#include "sylar/log/logger.h"

namespace sylar {
namespace http {

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

HttpServer::HttpServer(IOManager* io_worker, IOManager* accept_worker)
    : sylar::net::TcpServer(io_worker, accept_worker)
    , m_dispatch(new ServletDispatch())
    , m_sessionManager(new SessionManager()) {
    setName("sylar-http-server");
}

void HttpServer::handleClient(Socket::ptr client) {
    HttpSession::ptr session(new HttpSession(client));
    while (!isStop()) {
        HttpRequest::ptr request = session->recvRequest();
        if (!request) {
            if (session->hasParserError()) {
                HttpResponse::ptr response(new HttpResponse());
                response->setStatus(HttpStatus::BAD_REQUEST);
                response->setKeepAlive(false);
                response->setHeader("Content-Type", "text/plain; charset=utf-8");
                response->setBody("400 Bad Request\n" + session->getParserError());
                session->sendResponse(response);
            }
            break;
        }

        HttpResponse::ptr response(new HttpResponse());
        response->setVersion(request->getVersionMajor(), request->getVersionMinor());
        response->setKeepAlive(request->isKeepAlive());
        Session::ptr http_session = m_sessionManager->getOrCreate(request, response);
        (void)http_session;
        m_dispatch->handle(request, response, session);
        if (session->sendResponse(response) <= 0) {
            break;
        }
        if (!request->isKeepAlive() || !response->isKeepAlive()) {
            break;
        }
    }
    session->close();
    SYLAR_LOG_INFO(g_logger) << "HttpServer client closed";
}

} // namespace http
} // namespace sylar
