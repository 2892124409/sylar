#include "sylar/http/middleware.h"

namespace sylar
{
    namespace http
    {

        void MiddlewareChain::addMiddleware(Middleware::ptr middleware)
        {
            m_middlewares.push_back(middleware);
        }

        bool MiddlewareChain::processBefore(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) const
        {
            for (size_t i = 0; i < m_middlewares.size(); ++i)
            {
                if (!m_middlewares[i]->before(request, response, session))
                {
                    return false;
                }
            }
            return true;
        }

        void MiddlewareChain::processAfter(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) const
        {
            for (size_t i = 0; i < m_middlewares.size(); ++i)
            {
                m_middlewares[i]->after(request, response, session);
            }
        }

    } // namespace http
} // namespace sylar
