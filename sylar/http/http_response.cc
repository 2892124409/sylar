#include "sylar/http/http_response.h"

#include <sstream>

namespace sylar
{
    namespace http
    {

        HttpResponse::HttpResponse()
            : m_status(HttpStatus::OK), m_versionMajor(1), m_versionMinor(1), m_keepalive(true), m_stream(false)
        {
        }

        void HttpResponse::setHeader(const std::string &key, const std::string &value)
        {
            m_headers[key] = value;
        }

        std::string HttpResponse::getHeader(const std::string &key, const std::string &def) const
        {
            MapType::const_iterator it = m_headers.find(key);
            return it == m_headers.end() ? def : it->second;
        }

        void HttpResponse::addSetCookie(const std::string &cookie)
        {
            m_setCookies.push_back(cookie);
        }

        std::string HttpResponse::getVersionString() const
        {
            return "HTTP/" + std::to_string(m_versionMajor) + "." + std::to_string(m_versionMinor);
        }

        std::string HttpResponse::toHeaderString() const
        {
            std::ostringstream ss;
            std::string reason = m_reason.empty() ? HttpStatusToString(m_status) : m_reason;
            ss << getVersionString() << " " << static_cast<int>(m_status) << " " << reason << "\r\n";

            bool has_connection = false;
            for (MapType::const_iterator it = m_headers.begin(); it != m_headers.end(); ++it)
            {
                if (it->first == "Connection")
                {
                    has_connection = true;
                }
                ss << it->first << ": " << it->second << "\r\n";
            }

            if (!has_connection)
            {
                ss << "Connection: " << (m_keepalive ? "keep-alive" : "close") << "\r\n";
            }
            for (size_t i = 0; i < m_setCookies.size(); ++i)
            {
                ss << "Set-Cookie: " << m_setCookies[i] << "\r\n";
            }
            ss << "\r\n";
            return ss.str();
        }

        std::string HttpResponse::toString() const
        {
            std::ostringstream ss;
            std::string reason = m_reason.empty() ? HttpStatusToString(m_status) : m_reason;
            ss << getVersionString() << " " << static_cast<int>(m_status) << " " << reason << "\r\n";

            bool has_content_length = false;
            bool has_connection = false;
            for (MapType::const_iterator it = m_headers.begin(); it != m_headers.end(); ++it)
            {
                if (it->first == "Content-Length")
                {
                    has_content_length = true;
                }
                else if (it->first == "Connection")
                {
                    has_connection = true;
                }
                ss << it->first << ": " << it->second << "\r\n";
            }

            if (!has_connection)
            {
                ss << "Connection: " << (m_keepalive ? "keep-alive" : "close") << "\r\n";
            }
            if (!has_content_length)
            {
                ss << "Content-Length: " << m_body.size() << "\r\n";
            }
            for (size_t i = 0; i < m_setCookies.size(); ++i)
            {
                ss << "Set-Cookie: " << m_setCookies[i] << "\r\n";
            }
            ss << "\r\n";
            ss << m_body;
            return ss.str();
        }

    } // namespace http
} // namespace sylar
