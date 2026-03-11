#include "sylar/http/middleware/cors/cors_middleware.h"

namespace sylar
{
    namespace http
    {
        namespace cors
        {

            CorsMiddleware::CorsMiddleware(const CorsConfig &config)
                : m_config(config)
            {
            }

            bool CorsMiddleware::before(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr)
            {
                applyCommonHeaders(request, response);
                if (request->getMethod() != HttpMethod::OPTIONS)
                {
                    return true;
                }

                response->setStatus(static_cast<HttpStatus>(204));
                response->setReason("No Content");
                response->setHeader("Access-Control-Allow-Methods", m_config.getAllowedMethods());
                response->setHeader("Access-Control-Allow-Headers", m_config.getAllowedHeaders());
                response->setHeader("Access-Control-Max-Age", std::to_string(m_config.getMaxAge()));
                response->setBody("");
                return false;
            }

            void CorsMiddleware::after(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr)
            {
                applyCommonHeaders(request, response);
                if (!m_config.getExposeHeaders().empty())
                {
                    response->setHeader("Access-Control-Expose-Headers", m_config.getExposeHeaders());
                }
            }

            void CorsMiddleware::applyCommonHeaders(HttpRequest::ptr request, HttpResponse::ptr response) const
            {
                std::string origin = request->getHeader("Origin");
                if (origin.empty() || !m_config.isOriginAllowed(origin))
                {
                    return;
                }

                const std::vector<std::string> &allowed_origins = m_config.getAllowedOrigins();
                if (!allowed_origins.empty() && allowed_origins[0] == "*" && !m_config.getAllowCredentials())
                {
                    response->setHeader("Access-Control-Allow-Origin", "*");
                }
                else
                {
                    response->setHeader("Access-Control-Allow-Origin", origin);
                    response->setHeader("Vary", "Origin");
                }

                if (m_config.getAllowCredentials())
                {
                    response->setHeader("Access-Control-Allow-Credentials", "true");
                }
            }

        } // namespace cors
    } // namespace http
} // namespace sylar
