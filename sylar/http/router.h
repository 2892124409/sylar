#ifndef __SYLAR_HTTP_ROUTER_H__
#define __SYLAR_HTTP_ROUTER_H__

#include "sylar/http/http_request.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sylar
{
    namespace http
    {

        class Servlet;

        class Router
        {
        public:
            typedef std::shared_ptr<Router> ptr;
            typedef std::map<std::string, std::string> ParamsMap;

            struct RouteMatch
            {
                enum Type
                {
                    NONE = 0,
                    EXACT = 1,
                    PARAM = 2,
                    GLOB = 3,
                    DEFAULT = 4
                };

                RouteMatch()
                    : type(NONE)
                {
                }

                bool matched() const { return servlet != nullptr; }

                Type type;
                std::shared_ptr<Servlet> servlet;
                ParamsMap route_params;
            };

            Router();

            void addServlet(const std::string &uri, std::shared_ptr<Servlet> servlet);
            void addServlet(HttpMethod method, const std::string &uri, std::shared_ptr<Servlet> servlet);

            void addGlobServlet(const std::string &pattern, std::shared_ptr<Servlet> servlet);
            void addGlobServlet(HttpMethod method, const std::string &pattern, std::shared_ptr<Servlet> servlet);

            void addParamServlet(const std::string &pattern, std::shared_ptr<Servlet> servlet);
            void addParamServlet(HttpMethod method, const std::string &pattern, std::shared_ptr<Servlet> servlet);

            void setDefault(std::shared_ptr<Servlet> servlet);

            std::shared_ptr<Servlet> getMatched(const std::string &uri) const;
            RouteMatch match(const std::string &uri, HttpMethod method) const;
            RouteMatch match(HttpRequest::ptr request) const;

        private:
            struct GlobItem
            {
                HttpMethod method;
                std::string pattern;
                std::shared_ptr<Servlet> servlet;
            };

            struct ParamItem
            {
                HttpMethod method;
                std::string pattern;
                std::vector<std::string> segments;
                std::shared_ptr<Servlet> servlet;
            };

            struct ExactItem
            {
                HttpMethod method;
                std::string uri;
                std::shared_ptr<Servlet> servlet;
            };

        private:
            std::vector<ExactItem> m_exact;
            std::vector<GlobItem> m_globs;
            std::vector<ParamItem> m_params;
            std::shared_ptr<Servlet> m_default;
        };

    } // namespace http
} // namespace sylar

#endif
