#include "http/middleware/cors/cors_middleware.h"
#include "http/router/servlet.h"
#include "log/logger.h"

#include <cassert>
#include <string>
#include <vector>

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

void test_simple_cors_request()
{
    http::ServletDispatch::ptr dispatch(new http::ServletDispatch());
    http::cors::CorsConfig config;
    config.setAllowedOrigins(std::vector<std::string>(1, "https://example.com"));
    config.setAllowCredentials(true);
    config.setExposeHeaders("X-Trace-Id");
    dispatch->addMiddleware(http::Middleware::ptr(new http::cors::CorsMiddleware(config)));
    dispatch->addServlet("/ping", [](http::HttpRequest::ptr,
                                     http::HttpResponse::ptr rsp,
                                     http::HttpSession::ptr)
                         {
        rsp->setBody("pong");
        return 0; });

    http::HttpRequest::ptr req(new http::HttpRequest());
    http::HttpResponse::ptr rsp(new http::HttpResponse());
    req->setMethod(http::HttpMethod::GET);
    req->setPath("/ping");
    req->setHeader("Origin", "https://example.com");

    dispatch->handle(req, rsp, http::HttpSession::ptr());
    assert(rsp->getBody() == "pong");
    assert(rsp->getHeader("Access-Control-Allow-Origin") == "https://example.com");
    assert(rsp->getHeader("Access-Control-Allow-Credentials") == "true");
    assert(rsp->getHeader("Access-Control-Expose-Headers") == "X-Trace-Id");
}

void test_cors_preflight_short_circuit()
{
    http::ServletDispatch::ptr dispatch(new http::ServletDispatch());
    http::cors::CorsConfig config;
    dispatch->addMiddleware(http::Middleware::ptr(new http::cors::CorsMiddleware(config)));
    dispatch->addServlet("/ping", [](http::HttpRequest::ptr,
                                     http::HttpResponse::ptr rsp,
                                     http::HttpSession::ptr)
                         {
        rsp->setBody("should-not-run");
        return 0; });

    http::HttpRequest::ptr req(new http::HttpRequest());
    http::HttpResponse::ptr rsp(new http::HttpResponse());
    req->setMethod(http::HttpMethod::OPTIONS);
    req->setPath("/ping");
    req->setHeader("Origin", "https://frontend.local");

    dispatch->handle(req, rsp, http::HttpSession::ptr());
    assert(rsp->getStatus() == static_cast<http::HttpStatus>(204));
    assert(rsp->getBody().empty());
    assert(rsp->getHeader("Access-Control-Allow-Origin") == "*");
    assert(rsp->getHeader("Access-Control-Allow-Methods").find("OPTIONS") != std::string::npos);
    assert(rsp->getHeader("Access-Control-Allow-Headers").find("Content-Type") != std::string::npos);
}

int main()
{
    test_simple_cors_request();
    test_cors_preflight_short_circuit();
    BASE_LOG_INFO(g_logger) << "test_cors_middleware passed";
    return 0;
}
