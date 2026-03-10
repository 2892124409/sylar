// 引入中间件相关声明（Middleware/CallbackMiddleware/MiddlewareChain）。
#include "sylar/http/middleware.h"

// sylar 顶层命名空间。
namespace sylar
{
    // http 子命名空间。
    namespace http
    {

        // 注册中间件：追加到链尾，保持调用顺序与注册顺序一致。
        void MiddlewareChain::addMiddleware(Middleware::ptr middleware)
        {
            // 把 middleware 放入容器尾部。
            m_middlewares.push_back(middleware);
        }

        // 执行 before 链：任一中间件返回 false 则立刻短路。
        bool MiddlewareChain::processBefore(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) const
        {
            // 按注册顺序依次调用每个中间件的 before。
            for (size_t i = 0; i < m_middlewares.size(); ++i)
            {
                // 若某个 before 返回 false，则中断后续 before 执行。
                if (!m_middlewares[i]->before(request, response, session))
                {
                    // 返回 false 告知上层主流程应被短路。
                    return false;
                }
            }
            // 全部 before 都通过，返回 true 继续主流程。
            return true;
        }

        // 执行 after 链：按注册顺序调用每个中间件的 after。
        void MiddlewareChain::processAfter(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) const
        {
            // 按顺序执行收尾逻辑。
            for (size_t i = 0; i < m_middlewares.size(); ++i)
            {
                // 调用当前中间件的 after。
                m_middlewares[i]->after(request, response, session);
            }
        }

    } // namespace http
} // namespace sylar
