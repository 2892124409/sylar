#ifndef __SYLAR_HTTP_MIDDLEWARE_H__
#define __SYLAR_HTTP_MIDDLEWARE_H__

#include "sylar/http/http_request.h"
#include "sylar/http/http_response.h"
#include "sylar/http/http_session.h"

#include <functional>
#include <memory>
#include <vector>

namespace sylar
{
    namespace http
    {

        class Middleware
        {
        public:
            typedef std::shared_ptr<Middleware> ptr;
            virtual ~Middleware() {}

            virtual bool before(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
            {
                (void)request;
                (void)response;
                (void)session;
                return true;
            }

            virtual void after(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
            {
                (void)request;
                (void)response;
                (void)session;
            }
        };

        class CallbackMiddleware : public Middleware
        {
        public:
            typedef std::function<bool(HttpRequest::ptr, HttpResponse::ptr, HttpSession::ptr)> BeforeCallback;
            typedef std::function<void(HttpRequest::ptr, HttpResponse::ptr, HttpSession::ptr)> AfterCallback;

            CallbackMiddleware(BeforeCallback before, AfterCallback after)
                : m_before(before), m_after(after)
            {
            }

            virtual bool before(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override
            {
                return m_before ? m_before(request, response, session) : true;
            }

            virtual void after(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override
            {
                if (m_after)
                {
                    m_after(request, response, session);
                }
            }

        private:
            BeforeCallback m_before;
            AfterCallback m_after;
        };

        class MiddlewareChain
        {
        public:
            void addMiddleware(Middleware::ptr middleware);
            bool processBefore(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) const;
            void processAfter(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) const;

        private:
            std::vector<Middleware::ptr> m_middlewares;
        };

    } // namespace http
} // namespace sylar

#endif
