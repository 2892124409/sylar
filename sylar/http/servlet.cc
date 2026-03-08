#include "sylar/http/servlet.h"

namespace sylar
{
    namespace http
    {
        namespace
        {

            // 默认兜底 Servlet：当没有任何路由匹配时，返回 404。
            class NotFoundServlet : public Servlet
            {
            public:
                // 仅设置名称，便于日志定位和调试识别。
                NotFoundServlet()
                    : Servlet("NotFoundServlet")
                {
                }

                // 统一构造 404 响应。
                // 参数里未使用 request/session，所以省略变量名。
                virtual int32_t handle(HttpRequest::ptr, HttpResponse::ptr response, HttpSession::ptr) override
                {
                    response->setStatus(HttpStatus::NOT_FOUND);
                    response->setHeader("Content-Type", "text/plain; charset=utf-8");
                    response->setBody("404 Not Found");
                    return 0;
                }
            };

            // 简化版通配匹配规则：
            // 1) "*"         -> 匹配任意 URI
            // 2) "prefix*"   -> 前缀匹配（例如 /api/*）
            // 3) 其他         -> 精确匹配
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

        } // namespace

        Servlet::Servlet(const std::string &name)
            : m_name(name)
        {
        }

        // FunctionServlet 的核心思想：
        // 把“可调用对象（lambda / function / bind）”适配到 Servlet 接口。
        FunctionServlet::FunctionServlet(Callback cb, const std::string &name)
            : Servlet(name), m_cb(cb)
        {
        }

        // handle 不做额外逻辑，直接把请求处理委托给 m_cb。
        // 即：真正业务逻辑在注册时传入的回调函数中。
        int32_t FunctionServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
        {
            return m_cb(request, response, session);
        }

        // 分发器本身也是一个 Servlet，便于被上层统一调用。
        // 默认安装 NotFoundServlet，确保“未命中路由”也有确定行为。
        ServletDispatch::ServletDispatch()
            : Servlet("ServletDispatch"), m_default(new NotFoundServlet())
        {
        }

        // 注册精确路由。
        // 若 uri 已存在，则覆盖旧处理器；否则追加新条目。
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

        // 精确路由的回调版注册：先包装成 FunctionServlet，再复用主注册逻辑。
        void ServletDispatch::addServlet(const std::string &uri, FunctionServlet::Callback cb)
        {
            addServlet(uri, Servlet::ptr(new FunctionServlet(cb)));
        }

        // 注册通配路由。
        // 若 pattern 已存在，则覆盖旧处理器；否则追加新条目。
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

        // 通配路由的回调版注册：先包装成 FunctionServlet，再复用主注册逻辑。
        void ServletDispatch::addGlobServlet(const std::string &pattern, FunctionServlet::Callback cb)
        {
            addGlobServlet(pattern, Servlet::ptr(new FunctionServlet(cb)));
        }

        // 设置默认路由处理器（通常用于统一 404 或通用错误页）。
        void ServletDispatch::setDefault(Servlet::ptr servlet)
        {
            m_default = servlet;
        }

        // 路由查找顺序：
        // 1) 先查精确匹配
        // 2) 再查通配匹配
        // 3) 最后返回默认处理器
        // 这个顺序保证“更具体的规则”优先于“更宽泛的规则”。
        Servlet::ptr ServletDispatch::getMatched(const std::string &uri) const
        {
            for (size_t i = 0; i < m_exact.size(); ++i)
            {
                if (m_exact[i].first == uri)
                {
                    return m_exact[i].second;
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

        // 分发入口：
        // 1) 从 request 中取 path 作为路由键
        // 2) 找到匹配的 Servlet
        // 3) 调用目标 Servlet::handle 完成业务处理
        int32_t ServletDispatch::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
        {
            return getMatched(request->getPath())->handle(request, response, session);
        }

    } // namespace http
} // namespace sylar
