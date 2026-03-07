#include "sylar/http/http_request.h"

namespace sylar {
namespace http {

HttpRequest::HttpRequest()
    : m_method(HttpMethod::GET)
    , m_versionMajor(1)
    , m_versionMinor(1)
    , m_keepalive(true)
    , m_path("/") {
}

void HttpRequest::setHeader(const std::string& key, const std::string& value) {
    m_headers[key] = value;
}

std::string HttpRequest::getHeader(const std::string& key, const std::string& def) const {
    MapType::const_iterator it = m_headers.find(key);
    return it == m_headers.end() ? def : it->second;
}

bool HttpRequest::hasHeader(const std::string& key) const {
    return m_headers.find(key) != m_headers.end();
}

void HttpRequest::setParam(const std::string& key, const std::string& value) {
    m_params[key] = value;
}

std::string HttpRequest::getParam(const std::string& key, const std::string& def) const {
    MapType::const_iterator it = m_params.find(key);
    return it == m_params.end() ? def : it->second;
}

void HttpRequest::setCookie(const std::string& key, const std::string& value) {
    m_cookies[key] = value;
}

std::string HttpRequest::getCookie(const std::string& key, const std::string& def) const {
    MapType::const_iterator it = m_cookies.find(key);
    return it == m_cookies.end() ? def : it->second;
}

std::string HttpRequest::getVersionString() const {
    return "HTTP/" + std::to_string(m_versionMajor) + "." + std::to_string(m_versionMinor);
}

std::string HttpRequest::getPathWithQuery() const {
    std::string target = m_path.empty() ? "/" : m_path;
    if (!m_query.empty()) {
        target.append("?").append(m_query);
    }
    if (!m_fragment.empty()) {
        target.append("#").append(m_fragment);
    }
    return target;
}

} // namespace http
} // namespace sylar
