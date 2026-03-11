#ifndef __SYLAR_HTTP_MIDDLEWARE_CORS_MIDDLEWARE_H__
#define __SYLAR_HTTP_MIDDLEWARE_CORS_MIDDLEWARE_H__

#include "http/middleware/middleware.h"
#include "http/middleware/cors/cors_config.h"

namespace sylar
{
    namespace http
    {
        namespace cors
        {

            class CorsMiddleware : public Middleware
            {
            public:
                typedef std::shared_ptr<CorsMiddleware> ptr;

                CorsMiddleware(const CorsConfig &config = CorsConfig());

                virtual bool before(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;
                virtual void after(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) override;

            private:
                void applyCommonHeaders(HttpRequest::ptr request, HttpResponse::ptr response) const;

            private:
                CorsConfig m_config;
            };

        } // namespace cors
    } // namespace http
} // namespace sylar

#endif
