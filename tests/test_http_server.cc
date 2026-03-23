#include "http/server/http_server.h"
#include "http/router/servlet.h"
#include "http/core/http_request.h"
#include "http/core/http_response.h"
#include "http/server/http_session.h"
#include "log/logger.h"
#include "sylar/net/address.h"

static base::Logger::ptr g_logger = BASE_LOG_ROOT();

int main()
{
    // 创建 IOManager
    auto io_worker   = std::make_shared<sylar::IOManager>(2, false, "io");
    auto accept_worker = std::make_shared<sylar::IOManager>(1, false, "accept");

    auto server = std::make_shared<http::HttpServer>(io_worker.get(), accept_worker.get());

    // 注册 GET /ping -> 返回 pong
    server->getServletDispatch()->addServlet(
        "/ping",
        [](http::HttpRequest::ptr req, http::HttpResponse::ptr resp, http::HttpSession::ptr) -> int32_t {
            resp->setStatus(http::HttpStatus::OK);
            resp->setHeader("Content-Type", "text/plain");
            resp->setBody("pong");
            return 0;
        });

    // 注册 POST /echo -> 返回请求 body
    server->getServletDispatch()->addServlet(
        "/echo",
        [](http::HttpRequest::ptr req, http::HttpResponse::ptr resp, http::HttpSession::ptr) -> int32_t {
            resp->setStatus(http::HttpStatus::OK);
            resp->setHeader("Content-Type", "text/plain");
            resp->setBody(req->getBody());
            return 0;
        });

    auto addr = sylar::IPv4Address::Create("0.0.0.0", 8080);
    if (!addr) {
        BASE_LOG_ERROR(g_logger) << "create address failed";
        return 1;
    }

    if (!server->bind(addr)) {
        BASE_LOG_ERROR(g_logger) << "bind 0.0.0.0:8080 failed";
        return 1;
    }

    BASE_LOG_INFO(g_logger) << "HttpServer listening on 0.0.0.0:8080";
    BASE_LOG_INFO(g_logger) << "  GET  /ping  -> pong";
    BASE_LOG_INFO(g_logger) << "  POST /echo  -> echo body";

    server->start();

    // 保持主线程存活
    io_worker->stop();
    accept_worker->stop();
    return 0;
}
