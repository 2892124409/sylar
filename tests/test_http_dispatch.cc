#include "sylar/http/servlet.h"
#include "sylar/http/http_error.h"
#include "sylar/log/logger.h"

#include <cassert>
#include <stdexcept>

// 测试日志器：沿用 system logger，便于与其他测试输出统一。
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 用例1：验证参数路由/精确路由/通配路由的优先级关系是否正确。
void test_param_route_and_priority()
{
    // 创建分发器（路由器）。
    sylar::http::ServletDispatch::ptr dispatch(new sylar::http::ServletDispatch());
    // 注册精确路由：/user/me。
    dispatch->addServlet("/user/me", [](sylar::http::HttpRequest::ptr req,
                                        sylar::http::HttpResponse::ptr rsp,
                                        sylar::http::HttpSession::ptr)
                         {
        // 命中精确路由时，不应带有参数路由产生的 id 参数。
        assert(!req->hasRouteParam("id"));
        // 返回 body = exact，便于断言到底命中了哪个处理器。
        rsp->setBody("exact");
        // 返回 0 表示处理完成。
        return 0; });
    // 注册参数路由：/user/:id。
    dispatch->addParamServlet("/user/:id", [](sylar::http::HttpRequest::ptr req,
                                              sylar::http::HttpResponse::ptr rsp,
                                              sylar::http::HttpSession::ptr)
                              {
        // 把提取到的 id 回写到响应体，方便验证参数提取成功。
        rsp->setBody(req->getRouteParam("id"));
        // 返回 0 表示处理完成。
        return 0; });
    // 注册通配路由：/user/*。
    dispatch->addGlobServlet("/user/*", [](sylar::http::HttpRequest::ptr,
                                           sylar::http::HttpResponse::ptr rsp,
                                           sylar::http::HttpSession::ptr)
                             {
        // 若命中通配，响应体写 glob。
        rsp->setBody("glob");
        // 返回 0 表示处理完成。
        return 0; });

    // 子场景A：/user/42 应命中参数路由。
    {
        // 构造请求对象。
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        // 构造响应对象。
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        // 设置请求路径为 /user/42。
        req->setPath("/user/42");
        // 触发分发处理。
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        // 断言响应体为参数值 42，说明命中了参数路由。
        assert(rsp->getBody() == "42");
        // 断言请求上的 route param 也被正确写入。
        assert(req->getRouteParam("id") == "42");
    }

    // 子场景B：/user/me 应优先命中精确路由，而不是参数路由。
    {
        // 构造请求对象。
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        // 构造响应对象。
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        // 设置请求路径为 /user/me。
        req->setPath("/user/me");
        // 触发分发处理。
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        // 断言命中精确路由返回的 exact。
        assert(rsp->getBody() == "exact");
    }
}

// 用例2：验证 method-aware 路由是否能区分同一路径不同方法。
void test_method_aware_routes()
{
    // 创建分发器。
    sylar::http::ServletDispatch::ptr dispatch(new sylar::http::ServletDispatch());
    // 注册 GET /items。
    dispatch->addServlet(sylar::http::HttpMethod::GET, "/items", [](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr rsp, sylar::http::HttpSession::ptr)
                         {
        // GET 命中时返回 get-items。
        rsp->setBody("get-items");
        // 返回 0。
        return 0; });
    // 注册 POST /items。
    dispatch->addServlet(sylar::http::HttpMethod::POST, "/items", [](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr rsp, sylar::http::HttpSession::ptr)
                         {
        // POST 命中时返回 post-items。
        rsp->setBody("post-items");
        // 返回 0。
        return 0; });
    // 注册 GET 参数路由 /items/:id。
    dispatch->addParamServlet(sylar::http::HttpMethod::GET, "/items/:id", [](sylar::http::HttpRequest::ptr req, sylar::http::HttpResponse::ptr rsp, sylar::http::HttpSession::ptr)
                              {
        // 响应体写入提取出的 id，便于断言。
        rsp->setBody("get-item:" + req->getRouteParam("id"));
        // 返回 0。
        return 0; });

    // 子场景A：GET /items。
    {
        // 构造请求对象。
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        // 构造响应对象。
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        // 设置请求方法为 GET。
        req->setMethod(sylar::http::HttpMethod::GET);
        // 设置请求路径为 /items。
        req->setPath("/items");
        // 分发处理。
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        // 断言命中 GET handler。
        assert(rsp->getBody() == "get-items");
    }

    // 子场景B：POST /items。
    {
        // 构造请求对象。
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        // 构造响应对象。
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        // 设置请求方法为 POST。
        req->setMethod(sylar::http::HttpMethod::POST);
        // 设置请求路径为 /items。
        req->setPath("/items");
        // 分发处理。
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        // 断言命中 POST handler。
        assert(rsp->getBody() == "post-items");
    }

    // 子场景C：GET /items/7。
    {
        // 构造请求对象。
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        // 构造响应对象。
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        // 设置请求方法为 GET。
        req->setMethod(sylar::http::HttpMethod::GET);
        // 设置请求路径为参数路由地址。
        req->setPath("/items/7");
        // 分发处理。
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        // 断言参数路由命中并携带 id=7。
        assert(rsp->getBody() == "get-item:7");
        // 再次断言 route param 已写入请求。
        assert(req->getRouteParam("id") == "7");
    }
}

// 用例3：验证第三阶段 interceptor 与第四阶段改动兼容。
void test_interceptors()
{
    // 创建分发器。
    sylar::http::ServletDispatch::ptr dispatch(new sylar::http::ServletDispatch());
    // 统计 pre 执行次数。
    int pre_count = 0;
    // 统计 post 执行次数。
    int post_count = 0;

    // 注册前置拦截器。
    dispatch->addPreInterceptor([&](sylar::http::HttpRequest::ptr req,
                                    sylar::http::HttpResponse::ptr rsp,
                                    sylar::http::HttpSession::ptr)
                                {
        // 记录 pre 执行次数。
        ++pre_count;
        // 给响应打上 pre 标记头。
        rsp->setHeader("X-Pre", "1");
        // 当路径为 /blocked 时拦截请求。
        if (req->getPath() == "/blocked") {
            // 构造统一 400 错误响应。
            sylar::http::ApplyErrorResponse(rsp, sylar::http::HttpStatus::BAD_REQUEST, "Blocked", "pre interceptor blocked");
            // 返回 false，阻止进入业务 servlet。
            return false;
        }
        // 其他路径放行。
        return true; });

    // 注册后置拦截器。
    dispatch->addPostInterceptor([&](sylar::http::HttpRequest::ptr,
                                     sylar::http::HttpResponse::ptr rsp,
                                     sylar::http::HttpSession::ptr)
                                 {
        // 记录 post 执行次数。
        ++post_count;
        // 给响应打上 post 标记头。
        rsp->setHeader("X-Post", "1"); });

    // 注册正常业务路由 /ok。
    dispatch->addServlet("/ok", [](sylar::http::HttpRequest::ptr,
                                   sylar::http::HttpResponse::ptr rsp,
                                   sylar::http::HttpSession::ptr)
                         {
        // 业务返回 ok。
        rsp->setBody("ok");
        // 返回 0。
        return 0; });

    // 子场景A：未被拦截路径 /ok。
    {
        // 构造请求对象。
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        // 构造响应对象。
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        // 设置请求路径。
        req->setPath("/ok");
        // 分发处理。
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        // 断言前置拦截器已执行。
        assert(rsp->getHeader("X-Pre") == "1");
        // 断言后置拦截器已执行。
        assert(rsp->getHeader("X-Post") == "1");
        // 断言业务 handler 已执行。
        assert(rsp->getBody() == "ok");
    }

    // 子场景B：被 pre 拦截的路径 /blocked。
    {
        // 构造请求对象。
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        // 构造响应对象。
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        // 设置请求路径。
        req->setPath("/blocked");
        // 分发处理。
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        // 断言状态码为 BAD_REQUEST，说明 pre 拦截生效。
        assert(rsp->getStatus() == sylar::http::HttpStatus::BAD_REQUEST);
        // 断言 post 仍执行（短路也收尾）。
        assert(rsp->getHeader("X-Post") == "1");
    }

    // 断言 pre 总共执行了两次（/ok 与 /blocked 各一次）。
    assert(pre_count == 2);
    // 断言 post 也执行了两次。
    assert(post_count == 2);
}

// 用例4：验证第四阶段新增 Middleware 链行为。
void test_middlewares()
{
    // 创建分发器。
    sylar::http::ServletDispatch::ptr dispatch(new sylar::http::ServletDispatch());
    // 统计 before 执行次数。
    int before_count = 0;
    // 统计 after 执行次数。
    int after_count = 0;
    // 记录执行顺序。
    std::string order;

    // 注册一个回调式中间件。
    dispatch->addMiddleware(sylar::http::Middleware::ptr(new sylar::http::CallbackMiddleware(
        // before：在业务处理前执行。
        [&](sylar::http::HttpRequest::ptr req, sylar::http::HttpResponse::ptr rsp, sylar::http::HttpSession::ptr)
        {
            // 记录 before 次数。
            ++before_count;
            // 记录第一个中间件 before。
            order += "A+";
            // 写入 before 标记头。
            rsp->setHeader("X-MW-Before", "1");
            // 其他请求放行。
            return true;
        },
        // after：在流程尾部执行。
        [&](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr rsp, sylar::http::HttpSession::ptr)
        {
            // 记录 after 次数。
            ++after_count;
            // 记录第一个中间件 after。
            order += "A-";
            // 写入 after 标记头。
            rsp->setHeader("X-MW-After", "1");
        })));

    // 注册第二个中间件，用于验证短路与 after 逆序。
    dispatch->addMiddleware(sylar::http::Middleware::ptr(new sylar::http::CallbackMiddleware(
        [&](sylar::http::HttpRequest::ptr req, sylar::http::HttpResponse::ptr rsp, sylar::http::HttpSession::ptr)
        {
            order += "B+";
            if (req->getPath() == "/mw-blocked")
            {
                sylar::http::ApplyErrorResponse(rsp, sylar::http::HttpStatus::BAD_REQUEST, "Blocked", "blocked by middleware");
                return false;
            }
            return true;
        },
        [&](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr, sylar::http::HttpSession::ptr)
        {
            order += "B-";
        })));

    // 注册第三个中间件，短路时不应该进入也不应该执行 after。
    dispatch->addMiddleware(sylar::http::Middleware::ptr(new sylar::http::CallbackMiddleware(
        [&](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr, sylar::http::HttpSession::ptr)
        {
            order += "C+";
            return true;
        },
        [&](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr, sylar::http::HttpSession::ptr)
        {
            order += "C-";
        })));

    // 注册业务路由 /mw-ok。
    dispatch->addServlet("/mw-ok", [](sylar::http::HttpRequest::ptr,
                                      sylar::http::HttpResponse::ptr rsp,
                                      sylar::http::HttpSession::ptr)
                         {
        // 业务返回 mw-ok。
        rsp->setBody("mw-ok");
        // 返回 0。
        return 0; });

    // 子场景A：正常路径 /mw-ok。
    {
        // 构造请求对象。
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        // 构造响应对象。
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        // 设置请求路径。
        req->setPath("/mw-ok");
        // 清空顺序记录。
        order.clear();
        // 分发处理。
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        // 断言业务结果正确。
        assert(rsp->getBody() == "mw-ok");
        // 断言 before 已执行。
        assert(rsp->getHeader("X-MW-Before") == "1");
        // 断言 after 已执行。
        assert(rsp->getHeader("X-MW-After") == "1");
        // 断言 before 正序、after 逆序。
        assert(order == "A+B+C+C-B-A-");
    }

    // 子场景B：被 middleware before 短路的路径 /mw-blocked。
    {
        // 构造请求对象。
        sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
        // 构造响应对象。
        sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
        // 设置请求路径。
        req->setPath("/mw-blocked");
        // 清空顺序记录。
        order.clear();
        // 分发处理。
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
        // 断言已被短路为 BAD_REQUEST。
        assert(rsp->getStatus() == sylar::http::HttpStatus::BAD_REQUEST);
        // 断言短路后 after 仍执行。
        assert(rsp->getHeader("X-MW-After") == "1");
        // 断言只有已成功进入的中间件执行 after，且逆序退出。
        assert(order == "A+B+A-");
    }

    // 断言 before 总共执行两次。
    assert(before_count == 2);
    // 断言 after 也执行两次（包括短路请求）。
    assert(after_count == 2);
}

void test_exception_cleanup()
{
    sylar::http::ServletDispatch::ptr dispatch(new sylar::http::ServletDispatch());
    bool post_called = false;
    std::string order;

    dispatch->addMiddleware(sylar::http::Middleware::ptr(new sylar::http::CallbackMiddleware(
        [&](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr, sylar::http::HttpSession::ptr)
        {
            order += "M+";
            return true;
        },
        [&](sylar::http::HttpRequest::ptr, sylar::http::HttpResponse::ptr, sylar::http::HttpSession::ptr)
        {
            order += "M-";
        })));

    dispatch->addPostInterceptor([&](sylar::http::HttpRequest::ptr,
                                     sylar::http::HttpResponse::ptr rsp,
                                     sylar::http::HttpSession::ptr)
                                 {
                                     post_called = true;
                                     rsp->setHeader("X-Post", "1");
                                 });

    dispatch->addServlet("/throw", [](sylar::http::HttpRequest::ptr,
                                       sylar::http::HttpResponse::ptr,
                                       sylar::http::HttpSession::ptr) -> int32_t
                         {
                             throw std::runtime_error("dispatch exception");
                         });

    sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest());
    sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse());
    req->setPath("/throw");

    bool caught = false;
    try
    {
        dispatch->handle(req, rsp, sylar::http::HttpSession::ptr());
    }
    catch (const std::exception &)
    {
        caught = true;
    }

    assert(caught);
    assert(post_called);
    assert(rsp->getHeader("X-Post") == "1");
    assert(order == "M+M-");
}

// 测试主函数：依次执行各子用例。
int main()
{
    // 运行参数路由与优先级测试。
    test_param_route_and_priority();
    // 运行 method-aware 路由测试。
    test_method_aware_routes();
    // 运行 interceptor 兼容测试。
    test_interceptors();
    // 运行 middleware 链测试。
    test_middlewares();
    // 运行异常收尾测试。
    test_exception_cleanup();
    // 打印通过日志。
    SYLAR_LOG_INFO(g_logger) << "test_http_dispatch passed";
    // 返回 0 表示程序正常结束。
    return 0;
}
