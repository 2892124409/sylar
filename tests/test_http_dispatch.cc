#include "sylar/http/servlet.h"
#include "sylar/http/http_error.h"
#include "sylar/log/logger.h"

#include <cassert>

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

void test_param_route_and_priority() {
    sylar::http::ServletDispatch::ptr dispatch(new sylar::http::ServletDispatch());
    dispatch->addServlet("/user/me", [](sylar::http::HttpRequest::ptr req,
                                        sylar::http::HttpResponse::ptr rsp,
                                        sylar::http::HttpSession::ptr) {
        assert(!req->hasRouteParam("id"));
        rsp->setBody("exact");
        return 0;
    });
    dispatch->addParamServlet("/user/:id", [](sylar::http::HttpRequest::ptr req,
                                              sylar::http::HttpResponse::ptr rsp,
                                              sylar::http::HttpSession::ptr) {
        rsp->setBody(req->getRouteParam("id"));
        return 0;
    });
    dispatch->addGlobServlet("/user/*", [](sylar::http::HttpRequest::ptr,
                                           sylar::http::HttpResponse::ptr rsp,
                                           sylar::http::HttpSession::ptr) {
        rsp->setBody("glob");
        return 0;
    });

    {
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        req->setPath("/user/42");
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        assert(rsp->getBody() == "42");
        assert(req->getRouteParam("id") == "42");
    }

    {
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        req->setPath("/user/me");
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        assert(rsp->getBody() == "exact");
    }
}

void test_method_aware_routes() {
    sylar::http::ServletDispatch::ptr dispatch(new sylar::http::ServletDispatch());
    dispatch->addServlet(sylar::http::HttpMethod::GET, "/items", [](sylar::http::HttpRequest::ptr,
                                                                     sylar::http::HttpResponse::ptr rsp,
                                                                     sylar::http::HttpSession::ptr) {
        rsp->setBody("get-items");
        return 0;
    });
    dispatch->addServlet(sylar::http::HttpMethod::POST, "/items", [](sylar::http::HttpRequest::ptr,
                                                                      sylar::http::HttpResponse::ptr rsp,
                                                                      sylar::http::HttpSession::ptr) {
        rsp->setBody("post-items");
        return 0;
    });
    dispatch->addParamServlet(sylar::http::HttpMethod::GET, "/items/:id", [](sylar::http::HttpRequest::ptr req,
                                                                              sylar::http::HttpResponse::ptr rsp,
                                                                              sylar::http::HttpSession::ptr) {
        rsp->setBody("get-item:" + req->getRouteParam("id"));
        return 0;
    });

    {
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        req->setMethod(sylar::http::HttpMethod::GET);
        req->setPath("/items");
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        assert(rsp->getBody() == "get-items");
    }

    {
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        req->setMethod(sylar::http::HttpMethod::POST);
        req->setPath("/items");
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        assert(rsp->getBody() == "post-items");
    }

    {
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        req->setMethod(sylar::http::HttpMethod::GET);
        req->setPath("/items/7");
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        assert(rsp->getBody() == "get-item:7");
        assert(req->getRouteParam("id") == "7");
    }
}

void test_interceptors() {
    sylar::http::ServletDispatch::ptr dispatch(new sylar::http::ServletDispatch());
    int pre_count = 0;
    int post_count = 0;

    dispatch->addPreInterceptor([&](sylar::http::HttpRequest::ptr req,
                                    sylar::http::HttpResponse::ptr rsp,
                                    sylar::http::HttpSession::ptr) {
        ++pre_count;
        rsp->setHeader("X-Pre", "1");
        if (req->getPath() == "/blocked") {
            sylar::http::ApplyErrorResponse(rsp, sylar::http::HttpStatus::BAD_REQUEST, "Blocked", "pre interceptor blocked");
            return false;
        }
        return true;
    });

    dispatch->addPostInterceptor([&](sylar::http::HttpRequest::ptr,
                                     sylar::http::HttpResponse::ptr rsp,
                                     sylar::http::HttpSession::ptr) {
        ++post_count;
        rsp->setHeader("X-Post", "1");
    });

    dispatch->addServlet("/ok", [](sylar::http::HttpRequest::ptr,
                                   sylar::http::HttpResponse::ptr rsp,
                                   sylar::http::HttpSession::ptr) {
        rsp->setBody("ok");
        return 0;
    });

    {
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        req->setPath("/ok");
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        assert(rsp->getHeader("X-Pre") == "1");
        assert(rsp->getHeader("X-Post") == "1");
        assert(rsp->getBody() == "ok");
    }

    {
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        req->setPath("/blocked");
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        assert(rsp->getStatus() == sylar::http::HttpStatus::BAD_REQUEST);
        assert(rsp->getHeader("X-Post") == "1");
    }

    assert(pre_count == 2);
    assert(post_count == 2);
}

void test_middlewares() {
    sylar::http::ServletDispatch::ptr dispatch(new sylar::http::ServletDispatch());
    int before_count = 0;
    int after_count = 0;

    dispatch->addMiddleware(sylar::http::Middleware::ptr(new sylar::http::CallbackMiddleware(
        [&](sylar::http::HttpRequest::ptr req, sylar::http::HttpResponse::ptr rsp, sylar::http::HttpSession::ptr) {
            ++before_count;
            rsp->setHeader("X-MW-Before", "1");
            if (req->getPath() == "/mw-blocked") {
                sylar::http::ApplyErrorResponse(rsp, sylar::http::HttpStatus::BAD_REQUEST, "Blocked", "blocked by middleware");
                return false;
            }
            return true;
        },
        [&](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr rsp, sylar::http::HttpSession::ptr) {
            ++after_count;
            rsp->setHeader("X-MW-After", "1");
        })));

    dispatch->addServlet("/mw-ok", [](sylar::http::HttpRequest::ptr,
                                       sylar::http::HttpResponse::ptr rsp,
                                       sylar::http::HttpSession::ptr) {
        rsp->setBody("mw-ok");
        return 0;
    });

    {
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        req->setPath("/mw-ok");
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        assert(rsp->getBody() == "mw-ok");
        assert(rsp->getHeader("X-MW-Before") == "1");
        assert(rsp->getHeader("X-MW-After") == "1");
    }

    {
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        req->setPath("/mw-blocked");
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        assert(rsp->getStatus() == sylar::http::HttpStatus::BAD_REQUEST);
        assert(rsp->getHeader("X-MW-After") == "1");
    }

    assert(before_count == 2);
    assert(after_count == 2);
}

int main() {
    test_param_route_and_priority();
    test_method_aware_routes();
    test_interceptors();
    test_middlewares();
    SYLAR_LOG_INFO(g_logger) << "test_http_dispatch passed";
    return 0;
}
