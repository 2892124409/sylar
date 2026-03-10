#include "sylar/http/servlet.h"

#include "sylar/http/http_error.h"

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
            : Servlet(name)
            , m_cb(cb)
        {
        }

        int32_t FunctionServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
        {
            return m_cb(request, response, session);
        }

        ServletDispatch::ServletDispatch()
            : Servlet("ServletDispatch")
            , m_default(new NotFoundServlet())
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
            MiddlewareChain::ExecutionState middleware_state;
            bool middleware_entered = false;
            bool post_should_run = false;
            bool run_handler = true;
            int32_t rt = 0;

            try
            {
                if (!m_middlewareChain.processBefore(request, response, session, middleware_state))
                {
                    middleware_entered = true;
                    run_handler = false;
                }

                if (run_handler)
                {
                    middleware_entered = true;

                    for (size_t i = 0; i < m_preInterceptors.size(); ++i)
                    {
                        if (!m_preInterceptors[i](request, response, session))
                        {
                            post_should_run = true;
                            run_handler = false;
                            break;
                        }
                    }
                }

                if (run_handler)
                {
                    post_should_run = true;
                    Router::RouteMatch match = getRouteMatch(request);
                    rt = match.servlet->handle(request, response, session);
                }
            }
            catch (...)
            {
                if (post_should_run)
                {
                    for (size_t i = 0; i < m_postInterceptors.size(); ++i)
                    {
                        m_postInterceptors[i](request, response, session);
                    }
                }
                if (middleware_entered)
                {
                    m_middlewareChain.processAfter(request, response, session, middleware_state);
                }
                throw;
            }

            if (post_should_run)
            {
                for (size_t i = 0; i < m_postInterceptors.size(); ++i)
                {
                    m_postInterceptors[i](request, response, session);
                }
            }
            if (middleware_entered)
            {
                m_middlewareChain.processAfter(request, response, session, middleware_state);
            }
            return rt;
        }

    } // namespace http
} // namespace sylar
