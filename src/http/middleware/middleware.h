// 头文件保护宏：防止该头文件被重复包含。
#ifndef __SYLAR_HTTP_MIDDLEWARE_H__
// 定义头文件保护宏。
#define __SYLAR_HTTP_MIDDLEWARE_H__

// 引入 HttpRequest 声明，用于中间件读取请求信息。
#include "http/core/http_request.h"
// 引入 HttpResponse 声明，用于中间件写响应信息。
#include "http/core/http_response.h"
// 引入 HttpSession 声明，用于访问连接级上下文。
#include "http/server/http_session.h"

// 引入 std::function，支持函数式中间件回调。
#include <functional>
// 引入智能指针类型。
#include <memory>
// 引入动态数组容器，保存中间件链。
#include <vector>

// sylar 顶层命名空间。
namespace http
{

// 中间件基类：定义前置/后置处理统一接口。
class Middleware
{
  public:
    // Middleware 智能指针别名，便于统一书写。
    typedef std::shared_ptr<Middleware> ptr;
    // 虚析构：确保通过基类指针释放派生类对象时行为正确。
    virtual ~Middleware() {}

    // 前置处理钩子：返回 false 表示中断后续业务主流程。
    virtual bool before(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
    {
        // 默认实现不使用 request 参数，没有任何实际作用，只是防止编译器警告参数未使用。
        (void)request;
        // 默认实现不使用 response 参数。
        (void)response;
        // 默认实现不使用 session 参数。
        (void)session;
        // 默认允许继续后续流程。
        return true;
    }

    // 后置处理钩子：用于收尾逻辑（日志、补头、打点等）。
    virtual void after(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
    {
        // 默认实现不使用 request 参数。
        (void)request;
        // 默认实现不使用 response 参数。
        (void)response;
        // 默认实现不使用 session 参数。
        (void)session;
    }
};

// 函数式中间件：把 before/after 回调包装为 Middleware 实例。
class CallbackMiddleware : public Middleware
{
  public:
    // 前置回调类型：返回 false 可中断后续主流程。
    typedef std::function<bool(HttpRequest::ptr, HttpResponse::ptr, HttpSession::ptr)> BeforeCallback;
    // 后置回调类型：无返回值，用于收尾。
    typedef std::function<void(HttpRequest::ptr, HttpResponse::ptr, HttpSession::ptr)> AfterCallback;

    // 构造函数：注入 before/after 两个回调。
    CallbackMiddleware(BeforeCallback before, AfterCallback after)
        // 初始化前置回调成员。
        : m_before(before), m_after(after)
    {
        // 无额外初始化逻辑。
    }

    // 覆盖前置钩子：若设置了回调则委托给回调执行。
    virtual bool before(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override
    {
        // 有回调则调用回调，否则默认放行。
        return m_before ? m_before(request, response, session) : true;
    }

    // 覆盖后置钩子：若设置了回调则执行回调。
    virtual void after(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override
    {
        // 只有在 m_after 非空时才执行。
        if (m_after)
        {
            // 调用后置回调。
            m_after(request, response, session);
        }
    }

  private:
    // 保存前置回调。
    BeforeCallback m_before;
    // 保存后置回调。
    AfterCallback m_after;
};

// 中间件链：管理多个中间件并按顺序执行。
class MiddlewareChain
{
  public:
    struct ExecutionState
    {
        // 记录 before 阶段成功进入的中间件数量（替代 vector 避免每请求堆分配）。
        size_t enteredCount = 0;
    };

    // 注册一个中间件到链尾。
    void addMiddleware(Middleware::ptr middleware);
    // 顺序执行 before 链：任一返回 false 则整体返回 false。
    bool processBefore(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session, ExecutionState& state) const;
    // 按实际进入链路的中间件逆序执行 after。
    void processAfter(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session, const ExecutionState& state) const;

  private:
    // 中间件容器：按注册顺序保存。
    std::vector<Middleware::ptr> m_middlewares;
};

} // namespace http

// 结束头文件保护宏。
#endif
