#include "http/middleware/cors/cors_config.h"

namespace sylar
{
    namespace http
    {
        namespace cors
        {

            CorsConfig::CorsConfig()
                : m_allowedMethods("GET, POST, PUT, PATCH, DELETE, OPTIONS")
                , m_allowedHeaders("Content-Type, Authorization, X-Requested-With")
                , m_maxAge(600)
                , m_allowCredentials(false)
            {
                m_allowedOrigins.push_back("*");
            }

            bool CorsConfig::isOriginAllowed(const std::string &origin) const
            {
                for (size_t i = 0; i < m_allowedOrigins.size(); ++i)
                {
                    if (m_allowedOrigins[i] == "*" || m_allowedOrigins[i] == origin)
                    {
                        return true;
                    }
                }
                return false;
            }

        } // namespace cors
    } // namespace http
} // namespace sylar
