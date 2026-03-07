#ifndef __SYLAR_HTTP_SERVLET_H__
#define __SYLAR_HTTP_SERVLET_H__

#include "sylar/http/http_request.h"
#include "sylar/http/http_response.h"
#include "sylar/http/http_session.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sylar {
namespace http {

/**
 * @brief Servlet 抽象基类
 * @details
 * 可以把它理解成 HTTP 框架里的“业务处理器接口”。
 *
 * 当一条请求被路由匹配后，最终一定会落到某个 Servlet 的 `handle()` 上。
 * 以后无论是 `/ping`、`/chat/send` 还是 `/events`，都可以用 Servlet 来承载。
 */
class Servlet {
public:
    typedef std::shared_ptr<Servlet> ptr;

    /// 给 Servlet 起一个名字，方便调试和日志定位
    Servlet(const std::string& name);
    virtual ~Servlet() {}

    const std::string& getName() const { return m_name; }

    /**
     * @brief 处理一条 HTTP 请求
     * @param request 已解析好的 HTTP 请求
     * @param response 需要填写的 HTTP 响应
     * @param session 当前连接对应的 HttpSession
     * @return 约定返回值，当前阶段主要用于保留扩展点
     */
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) = 0;

private:
    std::string m_name;
};

/**
 * @brief 函数式 Servlet
 * @details
 * 这个类的作用，是把普通 lambda / std::function 包装成一个 Servlet。
 *
 * 这样我们在测试或业务注册路由时，不必每次都手写一个继承类，
 * 直接用 lambda 就能快速注册接口。
 */
class FunctionServlet : public Servlet {
public:
    typedef std::function<int32_t(HttpRequest::ptr, HttpResponse::ptr, HttpSession::ptr)> Callback;

    /// 使用一个回调函数构造 Servlet
    FunctionServlet(Callback cb, const std::string& name = "FunctionServlet");
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

private:
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
class ServletDispatch : public Servlet {
public:
    typedef std::shared_ptr<ServletDispatch> ptr;

    /// 构造默认分发器，并自动带上默认 404 Servlet
    ServletDispatch();

    /// 注册精确匹配 Servlet
    void addServlet(const std::string& uri, Servlet::ptr servlet);

    /// 注册精确匹配回调版 Servlet
    void addServlet(const std::string& uri, FunctionServlet::Callback cb);

    /// 注册通配匹配 Servlet，例如 /api/*
    void addGlobServlet(const std::string& pattern, Servlet::ptr servlet);

    /// 注册通配匹配回调版 Servlet
    void addGlobServlet(const std::string& pattern, FunctionServlet::Callback cb);

    /// 设置默认 Servlet，一般用来统一处理 404 或兜底错误
    void setDefault(Servlet::ptr servlet);

    /// 根据 URI 找到最终匹配的 Servlet
    Servlet::ptr getMatched(const std::string& uri) const;

    /// 分发请求到匹配的 Servlet
    virtual int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

private:
    /**
     * @brief 通配路由项
     * @details 保存 pattern 与对应的 Servlet。
     */
    struct GlobItem {
        std::string pattern;
        Servlet::ptr servlet;
    };

private:
    std::vector<std::pair<std::string, Servlet::ptr> > m_exact;
    std::vector<GlobItem> m_globs;
    Servlet::ptr m_default;
};

} // namespace http
} // namespace sylar

#endif
