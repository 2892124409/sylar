#include "http/router/router.h"
#include "http/router/servlet.h"
#include "log/logger.h"

#include <cassert>

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

void test_router_match_priority()
{
    http::Router router;
    router.addGlobServlet("/user/*", http::Servlet::ptr(new http::FunctionServlet(
                                         [](http::HttpRequest::ptr, http::HttpResponse::ptr, http::HttpSession::ptr)
                                         { return 0; },
                                         "glob")));
    router.addParamServlet("/user/:id", http::Servlet::ptr(new http::FunctionServlet(
                                            [](http::HttpRequest::ptr, http::HttpResponse::ptr, http::HttpSession::ptr)
                                            { return 0; },
                                            "param")));
    router.addServlet("/user/me", http::Servlet::ptr(new http::FunctionServlet(
                                      [](http::HttpRequest::ptr, http::HttpResponse::ptr, http::HttpSession::ptr)
                                      { return 0; },
                                      "exact")));

    http::Router::RouteMatch exact = router.match("/user/me", http::HttpMethod::GET);
    assert(exact.matched());
    assert(exact.type == http::Router::RouteMatch::EXACT);

    http::Router::RouteMatch param = router.match("/user/42", http::HttpMethod::GET);
    assert(param.matched());
    assert(param.type == http::Router::RouteMatch::PARAM);
    assert(param.route_params["id"] == "42");
}

void test_router_method_match()
{
    http::Router router;
    router.addServlet(http::HttpMethod::GET, "/items", http::Servlet::ptr(new http::FunctionServlet([](http::HttpRequest::ptr, http::HttpResponse::ptr, http::HttpSession::ptr)
                                                                                                    { return 0; }, "get-items")));
    router.addServlet(http::HttpMethod::POST, "/items", http::Servlet::ptr(new http::FunctionServlet([](http::HttpRequest::ptr, http::HttpResponse::ptr, http::HttpSession::ptr)
                                                                                                     { return 0; }, "post-items")));

    http::Router::RouteMatch get_match = router.match("/items", http::HttpMethod::GET);
    http::Router::RouteMatch post_match = router.match("/items", http::HttpMethod::POST);
    assert(get_match.matched());
    assert(post_match.matched());
    assert(get_match.servlet != post_match.servlet);
}

int main()
{
    test_router_match_priority();
    test_router_method_match();
    BASE_LOG_INFO(g_logger) << "test_http_router passed";
    return 0;
}
