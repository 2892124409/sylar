#include "sylar/http/servlet.h"

namespace sylar {
namespace http {
namespace {

class NotFoundServlet : public Servlet {
public:
    NotFoundServlet()
        : Servlet("NotFoundServlet") {
    }

    virtual int32_t handle(HttpRequest::ptr, HttpResponse::ptr response, HttpSession::ptr) override {
        response->setStatus(HttpStatus::NOT_FOUND);
        response->setHeader("Content-Type", "text/plain; charset=utf-8");
        response->setBody("404 Not Found");
        return 0;
    }
};

static bool MatchGlob(const std::string& pattern, const std::string& uri) {
    if (pattern == "*") {
        return true;
    }
    if (pattern.size() >= 1 && pattern[pattern.size() - 1] == '*') {
        return uri.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
    }
    return pattern == uri;
}

} // namespace

Servlet::Servlet(const std::string& name)
    : m_name(name) {
}

FunctionServlet::FunctionServlet(Callback cb, const std::string& name)
    : Servlet(name)
    , m_cb(cb) {
}

int32_t FunctionServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) {
    return m_cb(request, response, session);
}

ServletDispatch::ServletDispatch()
    : Servlet("ServletDispatch")
    , m_default(new NotFoundServlet()) {
}

void ServletDispatch::addServlet(const std::string& uri, Servlet::ptr servlet) {
    for (size_t i = 0; i < m_exact.size(); ++i) {
        if (m_exact[i].first == uri) {
            m_exact[i].second = servlet;
            return;
        }
    }
    m_exact.push_back(std::make_pair(uri, servlet));
}

void ServletDispatch::addServlet(const std::string& uri, FunctionServlet::Callback cb) {
    addServlet(uri, Servlet::ptr(new FunctionServlet(cb)));
}

void ServletDispatch::addGlobServlet(const std::string& pattern, Servlet::ptr servlet) {
    for (size_t i = 0; i < m_globs.size(); ++i) {
        if (m_globs[i].pattern == pattern) {
            m_globs[i].servlet = servlet;
            return;
        }
    }
    GlobItem item;
    item.pattern = pattern;
    item.servlet = servlet;
    m_globs.push_back(item);
}

void ServletDispatch::addGlobServlet(const std::string& pattern, FunctionServlet::Callback cb) {
    addGlobServlet(pattern, Servlet::ptr(new FunctionServlet(cb)));
}

void ServletDispatch::setDefault(Servlet::ptr servlet) {
    m_default = servlet;
}

Servlet::ptr ServletDispatch::getMatched(const std::string& uri) const {
    for (size_t i = 0; i < m_exact.size(); ++i) {
        if (m_exact[i].first == uri) {
            return m_exact[i].second;
        }
    }
    for (size_t i = 0; i < m_globs.size(); ++i) {
        if (MatchGlob(m_globs[i].pattern, uri)) {
            return m_globs[i].servlet;
        }
    }
    return m_default;
}

int32_t ServletDispatch::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session) {
    return getMatched(request->getPath())->handle(request, response, session);
}

} // namespace http
} // namespace sylar
