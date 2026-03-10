#include "sylar/http/router.h"

#include "sylar/http/servlet.h"

namespace sylar
{
    namespace http
    {
        namespace
        {

            static bool MatchGlob(const std::string &pattern, const std::string &uri)
            {
                if (pattern == "*")
                {
                    return true;
                }
                if (!pattern.empty() && pattern[pattern.size() - 1] == '*')
                {
                    return uri.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
                }
                return pattern == uri;
            }

            static bool MatchMethod(HttpMethod route_method, HttpMethod request_method)
            {
                return route_method == HttpMethod::INVALID_METHOD || route_method == request_method;
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
                                        Router::ParamsMap &route_params)
            {
                std::vector<std::string> uri_segments = SplitPathSegments(uri);
                if (pattern_segments.size() != uri_segments.size())
                {
                    return false;
                }

                route_params.clear();
                for (size_t i = 0; i < pattern_segments.size(); ++i)
                {
                    const std::string &pattern_segment = pattern_segments[i];
                    const std::string &uri_segment = uri_segments[i];
                    if (!pattern_segment.empty() && pattern_segment[0] == ':')
                    {
                        if (uri_segment.empty())
                        {
                            route_params.clear();
                            return false;
                        }
                        route_params[pattern_segment.substr(1)] = uri_segment;
                        continue;
                    }
                    if (pattern_segment != uri_segment)
                    {
                        route_params.clear();
                        return false;
                    }
                }
                return true;
            }

        } // namespace

        Router::Router()
        {
        }

        void Router::addServlet(const std::string &uri, std::shared_ptr<Servlet> servlet)
        {
            addServlet(HttpMethod::INVALID_METHOD, uri, servlet);
        }

        void Router::addServlet(HttpMethod method, const std::string &uri, std::shared_ptr<Servlet> servlet)
        {
            for (size_t i = 0; i < m_exact.size(); ++i)
            {
                if (m_exact[i].method == method && m_exact[i].uri == uri)
                {
                    m_exact[i].servlet = servlet;
                    return;
                }
            }
            ExactItem item;
            item.method = method;
            item.uri = uri;
            item.servlet = servlet;
            m_exact.push_back(item);
        }

        void Router::addGlobServlet(const std::string &pattern, std::shared_ptr<Servlet> servlet)
        {
            addGlobServlet(HttpMethod::INVALID_METHOD, pattern, servlet);
        }

        void Router::addGlobServlet(HttpMethod method, const std::string &pattern, std::shared_ptr<Servlet> servlet)
        {
            for (size_t i = 0; i < m_globs.size(); ++i)
            {
                if (m_globs[i].method == method && m_globs[i].pattern == pattern)
                {
                    m_globs[i].servlet = servlet;
                    return;
                }
            }
            GlobItem item;
            item.method = method;
            item.pattern = pattern;
            item.servlet = servlet;
            m_globs.push_back(item);
        }

        void Router::addParamServlet(const std::string &pattern, std::shared_ptr<Servlet> servlet)
        {
            addParamServlet(HttpMethod::INVALID_METHOD, pattern, servlet);
        }

        void Router::addParamServlet(HttpMethod method, const std::string &pattern, std::shared_ptr<Servlet> servlet)
        {
            std::vector<std::string> segments = SplitPathSegments(pattern);
            for (size_t i = 0; i < m_params.size(); ++i)
            {
                if (m_params[i].method == method && m_params[i].pattern == pattern)
                {
                    m_params[i].segments = segments;
                    m_params[i].servlet = servlet;
                    return;
                }
            }
            ParamItem item;
            item.method = method;
            item.pattern = pattern;
            item.segments = segments;
            item.servlet = servlet;
            m_params.push_back(item);
        }

        void Router::setDefault(std::shared_ptr<Servlet> servlet)
        {
            m_default = servlet;
        }

        std::shared_ptr<Servlet> Router::getMatched(const std::string &uri) const
        {
            return match(uri, HttpMethod::INVALID_METHOD).servlet;
        }

        Router::RouteMatch Router::match(const std::string &uri, HttpMethod method) const
        {
            RouteMatch result;
            for (size_t i = 0; i < m_exact.size(); ++i)
            {
                if (MatchMethod(m_exact[i].method, method) && m_exact[i].uri == uri)
                {
                    result.type = RouteMatch::EXACT;
                    result.servlet = m_exact[i].servlet;
                    return result;
                }
            }
            for (size_t i = 0; i < m_params.size(); ++i)
            {
                ParamsMap route_params;
                if (MatchMethod(m_params[i].method, method) && MatchParamRoute(m_params[i].segments, uri, route_params))
                {
                    result.type = RouteMatch::PARAM;
                    result.servlet = m_params[i].servlet;
                    result.route_params.swap(route_params);
                    return result;
                }
            }
            for (size_t i = 0; i < m_globs.size(); ++i)
            {
                if (MatchMethod(m_globs[i].method, method) && MatchGlob(m_globs[i].pattern, uri))
                {
                    result.type = RouteMatch::GLOB;
                    result.servlet = m_globs[i].servlet;
                    return result;
                }
            }
            result.type = RouteMatch::DEFAULT;
            result.servlet = m_default;
            return result;
        }

        Router::RouteMatch Router::match(HttpRequest::ptr request) const
        {
            return match(request->getPath(), request->getMethod());
        }

    } // namespace http
} // namespace sylar
