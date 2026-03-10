#include "sylar/http/servlet.h"
#include "sylar/http/router.h"
#include "sylar/log/logger.h"

#include <cassert>

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

void test_router_match_priority()
{
    sylar::http::Router router;
    router.addGlobServlet("/user/*", sylar::http::Servlet::ptr(new sylar::http::FunctionServlet(
                                        [](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr, sylar::http::HttpSession::ptr) { return 0; },
                                        "glob")));
    router.addParamServlet("/user/:id", sylar::http::Servlet::ptr(new sylar::http::FunctionServlet(
                                         [](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr, sylar::http::HttpSession::ptr) { return 0; },
                                         "param")));
    router.addServlet("/user/me", sylar::http::Servlet::ptr(new sylar::http::FunctionServlet(
                                  [](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr, sylar::http::HttpSession::ptr) { return 0; },
                                  "exact")));

    sylar::http::Router::RouteMatch exact = router.match("/user/me", sylar::http::HttpMethod::GET);
    assert(exact.matched());
    assert(exact.type == sylar::http::Router::RouteMatch::EXACT);

    sylar::http::Router::RouteMatch param = router.match("/user/42", sylar::http::HttpMethod::GET);
    assert(param.matched());
    assert(param.type == sylar::http::Router::RouteMatch::PARAM);
    assert(param.route_params["id"] == "42");
}

void test_router_method_match()
{
    sylar::http::Router router;
    router.addServlet(sylar::http::HttpMethod::GET, "/items", sylar::http::Servlet::ptr(new sylar::http::FunctionServlet(
                                                                    [](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr, sylar::http::HttpSession::ptr) { return 0; },
                                                                    "get-items")));
    router.addServlet(sylar::http::HttpMethod::POST, "/items", sylar::http::Servlet::ptr(new sylar::http::FunctionServlet(
                                                                     [](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr, sylar::http::HttpSession::ptr) { return 0; },
                                                                     "post-items")));

    sylar::http::Router::RouteMatch get_match = router.match("/items", sylar::http::HttpMethod::GET);
    sylar::http::Router::RouteMatch post_match = router.match("/items", sylar::http::HttpMethod::POST);
    assert(get_match.matched());
    assert(post_match.matched());
    assert(get_match.servlet != post_match.servlet);
}

int main()
{
    test_router_match_priority();
    test_router_method_match();
    SYLAR_LOG_INFO(g_logger) << "test_http_router passed";
    return 0;
}
