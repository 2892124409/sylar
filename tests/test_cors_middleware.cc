#include "http/middleware/cors/cors_middleware.h"
#include "http/router/servlet.h"
#include "log/logger.h"

#include <cassert>
#include <string>
#include <vector>

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

void test_simple_cors_request()
{
    sylar::http::ServletDispatch::ptr dispatch(new sylar::http::ServletDispatch());
    sylar::http::cors::CorsConfig config;
    config.setAllowedOrigins(std::vector<std::string>(1, "https://example.com"));
    config.setAllowCredentials(true);
    config.setExposeHeaders("X-Trace-Id");
    dispatch->addMiddleware(sylar::http::Middleware::ptr(new sylar::http::cors::CorsMiddleware(config)));
    dispatch->addServlet("/ping", [](sylar::http::HttpRequest::ptr,
                                      sylar::http::HttpResponse::ptr rsp,
                                      sylar::http::HttpSession::ptr) {
        rsp->setBody("pong");
        return 0;
    });

    sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
    sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
    req->setMethod(sylar::http::HttpMethod::GET);
    req->setPath("/ping");
    req->setHeader("Origin", "https://example.com");

    dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
    assert(rsp->getBody() == "pong");
    assert(rsp->getHeader("Access-Control-Allow-Origin") == "https://example.com");
    assert(rsp->getHeader("Access-Control-Allow-Credentials") == "true");
    assert(rsp->getHeader("Access-Control-Expose-Headers") == "X-Trace-Id");
}

void test_cors_preflight_short_circuit()
{
    sylar::http::ServletDispatch::ptr dispatch(new sylar::http::ServletDispatch());
    sylar::http::cors::CorsConfig config;
    dispatch->addMiddleware(sylar::http::Middleware::ptr(new sylar::http::cors::CorsMiddleware(config)));
    dispatch->addServlet("/ping", [](sylar::http::HttpRequest::ptr,
                                      sylar::http::HttpResponse::ptr rsp,
                                      sylar::http::HttpSession::ptr) {
        rsp->setBody("should-not-run");
        return 0;
    });

    sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
    sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
    req->setMethod(sylar::http::HttpMethod::OPTIONS);
    req->setPath("/ping");
    req->setHeader("Origin", "https://frontend.local");

    dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
    assert(rsp->getStatus() == static_cast<sylar::http::HttpStatus>(204));
    assert(rsp->getBody().empty());
    assert(rsp->getHeader("Access-Control-Allow-Origin") == "*");
    assert(rsp->getHeader("Access-Control-Allow-Methods").find("OPTIONS") != std::string::npos);
    assert(rsp->getHeader("Access-Control-Allow-Headers").find("Content-Type") != std::string::npos);
}

int main()
{
    test_simple_cors_request();
    test_cors_preflight_short_circuit();
    SYLAR_LOG_INFO(g_logger) << "test_cors_middleware passed";
    return 0;
}
