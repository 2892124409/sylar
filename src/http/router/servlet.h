#ifndef __SYLAR_HTTP_SERVLET_H__
#define __SYLAR_HTTP_SERVLET_H__

#include "http/core/http_request.h"
#include "http/core/http_response.h"
#include "http/middleware/middleware.h"
#include "http/router/router.h"
#include "http/server/http_session.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace http
{

/**
 * @brief Servlet 抽象基类
 * @details
 * 可以把它理解成 HTTP 框架里的“业务处理器接口”，负责拿到请求后怎么处理。
 *
 * 当一条请求被路由匹配后，最终一定会落到某个 Servlet 的 `handle()` 上。
 * 以后无论是 `/ping`、`/chat/send` 还是 `/events`，都可以用 Servlet 来承载。
 */
class Servlet
{
  public:
    typedef std::shared_ptr<Servlet> ptr;

    /// 给 Servlet 起一个名字，方便调试和日志定位
    Servlet(const std::string& name);
    virtual ~Servlet() {}

    const std::string& getName() const
    {
        return m_name;
    }

    /**
     * @brief 处理一条 HTTP 请求
     * @param request 已解析好的 HTTP 请求
     * @param response 需要填写的 HTTP 响应
     * @param session 当前连接对应的 HttpSession
     * @return 约定返回值，当前阶段主要用于保留扩展点
     */
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) = 0;

  private:
    /// Servlet 名称，用于日志和调试定位
    std::string m_name;
};

/**
 * @brief 函数式 Servlet
 * @details
 * 这个类的作用，是把普通 lambda / std::function 包装成一个 Servlet。
 *
 * 这样我们在测试或业务注册路由时，不必每次都手写一个继承类，
 * 直接用 lambda 就能快速注册接口。
 *
 * 使用示例：
 * @code
 * ServletDispatch::ptr dispatch(new ServletDispatch());
 * dispatch->addServlet("/ping", [](HttpRequest::ptr req,
 *                                   HttpResponse::ptr rsp,
 *                                   HttpSession::ptr session) -> int32_t {
 *     (void)req;
 *     (void)session;
 *     rsp->setStatus(HttpStatus::OK);
 *     rsp->setHeader("Content-Type", "text/plain");
 *     rsp->setBody("pong");
 *     return 0;
 * });
 * @endcode
 */
class FunctionServlet : public Servlet
{
  public:
    // 包装一个返回值:int32_t
    // 参数：(HttpRequest::ptr, HttpResponse::ptr, HttpSession::ptr)的可调用对象
    // 包装的对象包含：普通函数、Lambda表达式、std::bind结果
    typedef std::function<int32_t(HttpRequest::ptr, HttpResponse::ptr, HttpSession::ptr)> Callback;

    /// 使用一个回调函数构造 Servlet
    FunctionServlet(Callback cb, const std::string& name = "FunctionServlet");
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

  private:
    /// 业务处理回调，FunctionServlet 的核心执行体，就是处理请求时真正要执行的函数
    Callback m_cb;
};

/**
 * @brief Servlet 分发器 / 路由器
 * @details
 * 它本身也是一个 Servlet，但内部不直接处理业务，
 * 而是根据 URI 再把请求分发给真正的目标 Servlet。
 *
 * 当前支持两种路由：
 * - 精确匹配，例如 `/ping`
 * - 通配匹配，例如“前缀路由 `/api/` 可以匹配 `/api/xxx`”
 *
 * 如果都匹配不到，则走默认 Servlet（当前是 404）。
 */
class ServletDispatch : public Servlet
{
  public:
    typedef std::shared_ptr<ServletDispatch> ptr;
    typedef std::function<bool(HttpRequest::ptr, HttpResponse::ptr, HttpSession::ptr)> PreInterceptor;
    typedef std::function<void(HttpRequest::ptr, HttpResponse::ptr, HttpSession::ptr)> PostInterceptor;

    /// 构造默认分发器，并自动带上默认 404 Servlet
    ServletDispatch();

    /// 注册精确匹配 Servlet
    void addServlet(const std::string& uri, Servlet::ptr servlet);

    /// 注册带方法约束的精确匹配 Servlet
    void addServlet(HttpMethod method, const std::string& uri, Servlet::ptr servlet);

    /// 注册精确匹配回调版 Servlet
    void addServlet(const std::string& uri, FunctionServlet::Callback cb);

    /// 注册带方法约束的精确匹配回调版 Servlet
    void addServlet(HttpMethod method, const std::string& uri, FunctionServlet::Callback cb);

    /// 注册通配匹配 Servlet，例如 /api/*
    void addGlobServlet(const std::string& pattern, Servlet::ptr servlet);

    /// 注册带方法约束的通配匹配 Servlet
    void addGlobServlet(HttpMethod method, const std::string& pattern, Servlet::ptr servlet);

    /// 注册通配匹配回调版 Servlet
    void addGlobServlet(const std::string& pattern, FunctionServlet::Callback cb);

    /// 注册带方法约束的通配匹配回调版 Servlet
    void addGlobServlet(HttpMethod method, const std::string& pattern, FunctionServlet::Callback cb);

    /// 注册参数路由 Servlet，例如 /user/:id
    void addParamServlet(const std::string& pattern, Servlet::ptr servlet);

    /// 注册带方法约束的参数路由 Servlet
    void addParamServlet(HttpMethod method, const std::string& pattern, Servlet::ptr servlet);

    /// 注册参数路由回调版 Servlet
    void addParamServlet(const std::string& pattern, FunctionServlet::Callback cb);

    /// 注册带方法约束的参数路由回调版 Servlet
    void addParamServlet(HttpMethod method, const std::string& pattern, FunctionServlet::Callback cb);

    /// 注册中间件
    void addMiddleware(Middleware::ptr middleware);

    /// 注册前置拦截器，返回 false 表示中断后续业务处理
    void addPreInterceptor(PreInterceptor cb);

    /// 注册后置拦截器，在业务处理后、响应发送前执行
    void addPostInterceptor(PostInterceptor cb);

    /// 设置默认 Servlet，一般用来统一处理 404 或兜底错误
    void setDefault(Servlet::ptr servlet);

    /// 根据 URI 找到最终匹配的 Servlet
    Servlet::ptr getMatched(const std::string& uri) const;

    /// 根据请求匹配最终 Servlet，并在命中参数路由时写入 route params
    Servlet::ptr getMatched(HttpRequest::ptr request) const;

    /// 返回完整路由匹配结果（供分发流程使用）
    Router::RouteMatch getRouteMatch(HttpRequest::ptr request) const;

    /// 分发请求到匹配的 Servlet
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

  private:
    /// 兜底 Servlet（未命中任何路由时使用）
    Servlet::ptr m_default;
    /// Router：统一管理路由注册与匹配。
    Router m_router;
    /// 前置拦截器链
    std::vector<PreInterceptor> m_preInterceptors;
    /// 后置拦截器链
    std::vector<PostInterceptor> m_postInterceptors;
    /// 中间件链（第四阶段在拦截器之上进一步抽象）
    MiddlewareChain m_middlewareChain;
};

} // namespace http

#endif
