#ifndef __SYLAR_HTTP_MIDDLEWARE_CORS_CONFIG_H__
#define __SYLAR_HTTP_MIDDLEWARE_CORS_CONFIG_H__

#include <string>
#include <vector>

namespace sylar
{
    namespace http
    {
        namespace cors
        {

            class CorsConfig
            {
            public:
                CorsConfig();

                const std::vector<std::string> &getAllowedOrigins() const { return m_allowedOrigins; }
                void setAllowedOrigins(const std::vector<std::string> &value) { m_allowedOrigins = value; }

                const std::string &getAllowedMethods() const { return m_allowedMethods; }
                void setAllowedMethods(const std::string &value) { m_allowedMethods = value; }

                const std::string &getAllowedHeaders() const { return m_allowedHeaders; }
                void setAllowedHeaders(const std::string &value) { m_allowedHeaders = value; }

                const std::string &getExposeHeaders() const { return m_exposeHeaders; }
                void setExposeHeaders(const std::string &value) { m_exposeHeaders = value; }

                int getMaxAge() const { return m_maxAge; }
                void setMaxAge(int value) { m_maxAge = value; }

                bool getAllowCredentials() const { return m_allowCredentials; }
                void setAllowCredentials(bool value) { m_allowCredentials = value; }

                bool isOriginAllowed(const std::string &origin) const;

            private:
                std::vector<std::string> m_allowedOrigins;
                std::string m_allowedMethods;
                std::string m_allowedHeaders;
                std::string m_exposeHeaders;
                int m_maxAge;
                bool m_allowCredentials;
            };

        } // namespace cors
    } // namespace http
} // namespace sylar

#endif
