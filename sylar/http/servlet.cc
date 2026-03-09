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

            static bool MatchGlob(const std::string &pattern, const std::string &uri)
            {
                if (pattern == "*")
                {
                    return true;
                }
                if (pattern.size() >= 1 && pattern[pattern.size() - 1] == '*')
                {
                    return uri.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
                }
                return pattern == uri;
            }

            static std::vector<std::string> SplitPathSegments(const std::string &path)
            {
                std::vector<std::string> segments;
                std::string current;
                for (size_t i = 0; i < path.size(); ++i)
                {
                    if (path[i] == '/')
                    {
                        if (!current.empty())
                        {
                            segments.push_back(current);
                            current.clear();
                        }
                        continue;
                    }
                    current.push_back(path[i]);
                }
                if (!current.empty())
                {
                    segments.push_back(current);
                }
                return segments;
            }

            static bool MatchParamRoute(const std::vector<std::string> &pattern_segments,
                                        const std::string &uri,
                                        HttpRequest::ptr request)
            {
                std::vector<std::string> uri_segments = SplitPathSegments(uri);
                if (pattern_segments.size() != uri_segments.size())
                {
                    return false;
                }

                request->clearRouteParams();
                for (size_t i = 0; i < pattern_segments.size(); ++i)
                {
                    const std::string &pattern_segment = pattern_segments[i];
                    const std::string &uri_segment = uri_segments[i];
                    if (!pattern_segment.empty() && pattern_segment[0] == ':')
                    {
                        if (uri_segment.empty())
                        {
                            request->clearRouteParams();
                            return false;
                        }
                        request->setRouteParam(pattern_segment.substr(1), uri_segment);
                        continue;
                    }
                    if (pattern_segment != uri_segment)
                    {
                        request->clearRouteParams();
                        return false;
                    }
                }
                return true;
            }

        } // namespace

        Servlet::Servlet(const std::string &name)
            : m_name(name)
        {
        }

        FunctionServlet::FunctionServlet(Callback cb, const std::string &name)
            : Servlet(name), m_cb(cb)
        {
        }

        int32_t FunctionServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
        {
            return m_cb(request, response, session);
        }

        ServletDispatch::ServletDispatch()
            : Servlet("ServletDispatch"), m_default(new NotFoundServlet())
        {
        }

        void ServletDispatch::addServlet(const std::string &uri, Servlet::ptr servlet)
        {
            for (size_t i = 0; i < m_exact.size(); ++i)
            {
                if (m_exact[i].first == uri)
                {
                    m_exact[i].second = servlet;
                    return;
                }
            }
            m_exact.push_back(std::make_pair(uri, servlet));
        }

        void ServletDispatch::addServlet(const std::string &uri, FunctionServlet::Callback cb)
        {
            addServlet(uri, Servlet::ptr(new FunctionServlet(cb)));
        }

        void ServletDispatch::addGlobServlet(const std::string &pattern, Servlet::ptr servlet)
        {
            for (size_t i = 0; i < m_globs.size(); ++i)
            {
                if (m_globs[i].pattern == pattern)
                {
                    m_globs[i].servlet = servlet;
                    return;
                }
            }
            GlobItem item;
            item.pattern = pattern;
            item.servlet = servlet;
            m_globs.push_back(item);
        }

        void ServletDispatch::addGlobServlet(const std::string &pattern, FunctionServlet::Callback cb)
        {
            addGlobServlet(pattern, Servlet::ptr(new FunctionServlet(cb)));
        }

        void ServletDispatch::addParamServlet(const std::string &pattern, Servlet::ptr servlet)
        {
            std::vector<std::string> segments = SplitPathSegments(pattern);
            for (size_t i = 0; i < m_params.size(); ++i)
            {
                if (m_params[i].pattern == pattern)
                {
                    m_params[i].segments = segments;
                    m_params[i].servlet = servlet;
                    return;
                }
            }
            ParamItem item;
            item.pattern = pattern;
            item.segments = segments;
            item.servlet = servlet;
            m_params.push_back(item);
        }

        void ServletDispatch::addParamServlet(const std::string &pattern, FunctionServlet::Callback cb)
        {
            addParamServlet(pattern, Servlet::ptr(new FunctionServlet(cb)));
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
        }

        Servlet::ptr ServletDispatch::getMatched(const std::string &uri) const
        {
            for (size_t i = 0; i < m_exact.size(); ++i)
            {
                if (m_exact[i].first == uri)
                {
                    return m_exact[i].second;
                }
            }
            for (size_t i = 0; i < m_params.size(); ++i)
            {
                if (MatchParamRoute(m_params[i].segments, uri, HttpRequest::ptr(new HttpRequest())))
                {
                    return m_params[i].servlet;
                }
            }
            for (size_t i = 0; i < m_globs.size(); ++i)
            {
                if (MatchGlob(m_globs[i].pattern, uri))
                {
                    return m_globs[i].servlet;
                }
            }
            return m_default;
        }

        Servlet::ptr ServletDispatch::getMatched(HttpRequest::ptr request) const
        {
            const std::string &uri = request->getPath();
            request->clearRouteParams();
            for (size_t i = 0; i < m_exact.size(); ++i)
            {
                if (m_exact[i].first == uri)
                {
                    return m_exact[i].second;
                }
            }
            for (size_t i = 0; i < m_params.size(); ++i)
            {
                if (MatchParamRoute(m_params[i].segments, uri, request))
                {
                    return m_params[i].servlet;
                }
            }
            for (size_t i = 0; i < m_globs.size(); ++i)
            {
                if (MatchGlob(m_globs[i].pattern, uri))
                {
                    request->clearRouteParams();
                    return m_globs[i].servlet;
                }
            }
            request->clearRouteParams();
            return m_default;
        }

        int32_t ServletDispatch::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
        {
            for (size_t i = 0; i < m_preInterceptors.size(); ++i)
            {
                if (!m_preInterceptors[i](request, response, session))
                {
                    for (size_t j = 0; j < m_postInterceptors.size(); ++j)
                    {
                        m_postInterceptors[j](request, response, session);
                    }
                    return 0;
                }
            }

            Servlet::ptr servlet = getMatched(request);
            int32_t rt = servlet->handle(request, response, session);
            for (size_t i = 0; i < m_postInterceptors.size(); ++i)
            {
                m_postInterceptors[i](request, response, session);
            }
            return rt;
        }

    } // namespace http
} // namespace sylar
