#include "http/router/servlet.h"

#include "http/core/http_error.h"

namespace sylar
{
    namespace http
    {
        namespace
        {

            class NotFoundServlet : public Servlet
            {
            public:
                NotFoundServlet()
                    : Servlet("NotFoundServlet")
                {
                }

                virtual int32_t handle(HttpRequest::ptr, HttpResponse::ptr response, HttpSession::ptr) override
                {
                    ApplyErrorResponse(response, HttpStatus::NOT_FOUND, "Not Found", "route not found");
                    return 0;
                }
            };

            static void ApplyRouteMatchToRequest(HttpRequest::ptr request, const Router::RouteMatch &match)
            {
                request->clearRouteParams();
                for (Router::ParamsMap::const_iterator it = match.route_params.begin(); it != match.route_params.end(); ++it)
                {
                    request->setRouteParam(it->first, it->second);
                }
            }

        } // namespace

        Servlet::Servlet(const std::string &name)
            : m_name(name)
        {
        }

        FunctionServlet::FunctionServlet(Callback cb, const std::string &name)
            : Servlet(name), m_cb(cb)
        {
        }

        int32_t FunctionServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
        {
            return m_cb(request, response, session);
        }

        ServletDispatch::ServletDispatch()
            : Servlet("ServletDispatch"), m_default(new NotFoundServlet())
        {
            m_router.setDefault(m_default);
        }

        void ServletDispatch::addServlet(const std::string &uri, Servlet::ptr servlet)
        {
            m_router.addServlet(uri, servlet);
        }

        void ServletDispatch::addServlet(HttpMethod method, const std::string &uri, Servlet::ptr servlet)
        {
            m_router.addServlet(method, uri, servlet);
        }

        void ServletDispatch::addServlet(const std::string &uri, FunctionServlet::Callback cb)
        {
            addServlet(uri, Servlet::ptr(new FunctionServlet(cb)));
        }

        void ServletDispatch::addServlet(HttpMethod method, const std::string &uri, FunctionServlet::Callback cb)
        {
            addServlet(method, uri, Servlet::ptr(new FunctionServlet(cb)));
        }

        void ServletDispatch::addGlobServlet(const std::string &pattern, Servlet::ptr servlet)
        {
            m_router.addGlobServlet(pattern, servlet);
        }

        void ServletDispatch::addGlobServlet(HttpMethod method, const std::string &pattern, Servlet::ptr servlet)
        {
            m_router.addGlobServlet(method, pattern, servlet);
        }

        void ServletDispatch::addGlobServlet(const std::string &pattern, FunctionServlet::Callback cb)
        {
            addGlobServlet(pattern, Servlet::ptr(new FunctionServlet(cb)));
        }

        void ServletDispatch::addGlobServlet(HttpMethod method, const std::string &pattern, FunctionServlet::Callback cb)
        {
            addGlobServlet(method, pattern, Servlet::ptr(new FunctionServlet(cb)));
        }

        void ServletDispatch::addParamServlet(const std::string &pattern, Servlet::ptr servlet)
        {
            m_router.addParamServlet(pattern, servlet);
        }

        void ServletDispatch::addParamServlet(HttpMethod method, const std::string &pattern, Servlet::ptr servlet)
        {
            m_router.addParamServlet(method, pattern, servlet);
        }

        void ServletDispatch::addParamServlet(const std::string &pattern, FunctionServlet::Callback cb)
        {
            addParamServlet(pattern, Servlet::ptr(new FunctionServlet(cb)));
        }

        void ServletDispatch::addParamServlet(HttpMethod method, const std::string &pattern, FunctionServlet::Callback cb)
        {
            addParamServlet(method, pattern, Servlet::ptr(new FunctionServlet(cb)));
        }

        void ServletDispatch::addMiddleware(Middleware::ptr middleware)
        {
            m_middlewareChain.addMiddleware(middleware);
        }

        void ServletDispatch::addPreInterceptor(PreInterceptor cb)
        {
            m_preInterceptors.push_back(cb);
        }

        void ServletDispatch::addPostInterceptor(PostInterceptor cb)
        {
            m_postInterceptors.push_back(cb);
        }

        void ServletDispatch::setDefault(Servlet::ptr servlet)
        {
            m_default = servlet;
            m_router.setDefault(servlet);
        }

        Servlet::ptr ServletDispatch::getMatched(const std::string &uri) const
        {
            return m_router.getMatched(uri);
        }

        Router::RouteMatch ServletDispatch::getRouteMatch(HttpRequest::ptr request) const
        {
            Router::RouteMatch match = m_router.match(request);
            ApplyRouteMatchToRequest(request, match);
            return match;
        }

        Servlet::ptr ServletDispatch::getMatched(HttpRequest::ptr request) const
        {
            return getRouteMatch(request).servlet;
        }

        int32_t ServletDispatch::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
        {
            // 记录本次请求在 middleware before 阶段“成功进入”的中间件集合，供 after 逆序收尾使用。
            MiddlewareChain::ExecutionState middleware_state;
            // 标记是否已经进入过 middleware 流程（用于决定是否执行 processAfter）。
            bool middleware_entered = false;
            // 标记是否应执行 post interceptor（只有进入 pre/handler 主链路后才需要）。
            bool post_should_run = false;
            // 主流程开关：false 表示已被 middleware 或 pre interceptor 短路，不再进入 handler。
            bool run_handler = true;
            // handler 返回值，默认 0；若未执行 handler 则保持 0。
            int32_t rt = 0;

            // 用 try/catch 包裹主链路，确保异常路径也能执行 post/after 收尾。
            try
            {
                // 执行 middleware before 链；任一返回 false 将短路主业务处理。
                if (!m_middlewareChain.processBefore(request, response, session, middleware_state))
                {
                    // 即便 before 短路，也视为已进入 middleware 流程，后续需要执行 after 收尾。
                    middleware_entered = true;
                    // 关闭 handler 执行开关，后续不进入 pre/route/handler 主链路。
                    run_handler = false;
                }

                // 仅在未被 middleware 短路时继续执行 pre interceptor。
                if (run_handler)
                {
                    // 已进入 middleware 主路径，后续应执行 after。
                    middleware_entered = true;

                    // 按注册顺序执行 pre interceptor。
                    for (size_t i = 0; i < m_preInterceptors.size(); ++i)
                    {
                        // 任一 pre interceptor 返回 false，表示拦截请求并短路 handler。
                        if (!m_preInterceptors[i](request, response, session))
                        {
                            // pre 已执行过，因此 post interceptor 需要运行（保持前后对称语义）。
                            post_should_run = true;
                            // 关闭 handler 执行开关，停止进入路由与业务处理。
                            run_handler = false;
                            // 结束 pre interceptor 循环。
                            break;
                        }
                    }
                }

                // 仅在未被前置链路短路时，执行路由匹配与业务 handler。
                if (run_handler)
                {
                    // 将 post interceptor 标记为需要执行（正常主链路或异常路径都要收尾）。
                    post_should_run = true;
                    // 执行路由匹配，并把参数路由结果写回 request。
                    Router::RouteMatch match = getRouteMatch(request);
                    // 调用命中的业务处理器并记录其返回值。
                    rt = match.servlet->handle(request, response, session);
                }
            }
            // 捕获所有异常：保证执行 post/after 收尾后再继续向上抛。
            catch (...)
            {
                // 若前置链路已进入 post 语义，则先执行 post interceptor。
                if (post_should_run)
                {
                    // 按注册顺序执行 post interceptor。
                    for (size_t i = 0; i < m_postInterceptors.size(); ++i)
                    {
                        // 执行第 i 个 post interceptor。
                        m_postInterceptors[i](request, response, session);
                    }
                }
                // 若 middleware 流程已进入，则执行 after 逆序收尾。
                if (middleware_entered)
                {
                    // 根据 before 实际进入记录做 after 执行（逆序）。
                    m_middlewareChain.processAfter(request, response, session, middleware_state);
                }
                // 收尾完成后继续抛出异常，交由上层（如 HttpServer）统一处理。
                throw;
            }

            // 正常路径：若需要则执行 post interceptor。
            if (post_should_run)
            {
                // 按注册顺序执行所有 post interceptor。
                for (size_t i = 0; i < m_postInterceptors.size(); ++i)
                {
                    // 执行第 i 个 post interceptor。
                    m_postInterceptors[i](request, response, session);
                }
            }
            // 正常路径：若 middleware 已进入，则执行 after 收尾。
            if (middleware_entered)
            {
                // 仅对 before 成功进入的中间件执行 after，且按逆序。
                m_middlewareChain.processAfter(request, response, session, middleware_state);
            }
            // 返回业务处理结果（若被短路则保持默认值 0）。
            return rt;
        }

    } // namespace http
} // namespace sylar
