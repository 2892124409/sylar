// 引入 Router 声明。
#include "http/router/router.h"

// 引入 Servlet 完整定义（Router 内部需要持有/返回 Servlet 指针）。
#include "http/router/servlet.h"

// sylar 顶层命名空间。
namespace sylar
{
    // http 子命名空间。
    namespace http
    {
        // 匿名命名空间：仅当前编译单元可见的内部工具函数。
        namespace
        {

            // 通配匹配函数：支持 "*" 与 "prefix*" 两种形式。
            static bool MatchGlob(const std::string &pattern, const std::string &uri)
            {
                // "*" 直接匹配任意 URI。
                if (pattern == "*")
                {
                    // 命中。
                    return true;
                }
                // 若 pattern 以 '*' 结尾，按前缀匹配处理。
                if (!pattern.empty() && pattern[pattern.size() - 1] == '*')
                {
                    // 比较去掉 '*' 后的前缀是否相等。
                    return uri.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
                }
                // 其余情况按精确字符串比较。
                return pattern == uri;
            }

            // 方法匹配函数：INVALID_METHOD 代表不限制方法。
            static bool MatchMethod(HttpMethod route_method, HttpMethod request_method)
            {
                // 不限制方法或方法相等即视为匹配。
                return route_method == HttpMethod::INVALID_METHOD || route_method == request_method;
            }

            // 把路径按 '/' 切分为段，并忽略空段。
            static std::vector<std::string> SplitPathSegments(const std::string &path)
            {
                // 保存最终分段结果。
                std::vector<std::string> segments;
                // 保存当前正在累积的段。
                std::string current;
                // 逐字符扫描 path。
                for (size_t i = 0; i < path.size(); ++i)
                {
                    // 遇到分隔符 '/'。
                    if (path[i] == '/')
                    {
                        // 当前段非空时，先入结果。
                        if (!current.empty())
                        {
                            // 保存当前段。
                            segments.push_back(current);
                            // 清空 current，准备下一段。
                            current.clear();
                        }
                        // 跳过本次循环，继续下一个字符。
                        continue;
                    }
                    // 非分隔符字符，追加到当前段。
                    current.push_back(path[i]);
                }
                // 循环结束后，若仍有尾段，补入结果。
                if (!current.empty())
                {
                    // 保存最后一段。
                    segments.push_back(current);
                }
                // 返回切分结果。
                return segments;
            }

            // 参数路由匹配函数：pattern 段中以 ':' 开头视为参数名。
            static bool MatchParamRoute(const std::vector<std::string> &pattern_segments,
                                        const std::string &uri,
                                        Router::ParamsMap &route_params)
            {
                // 先把目标 URI 切分成路径段。
                std::vector<std::string> uri_segments = SplitPathSegments(uri);
                // 段数不一致直接不匹配。
                if (pattern_segments.size() != uri_segments.size())
                {
                    // 返回失败。
                    return false;
                }

                // 匹配开始前先清空旧参数。
                route_params.clear();
                // 逐段比较 pattern 与 uri。
                for (size_t i = 0; i < pattern_segments.size(); ++i)
                {
                    // 当前 pattern 段。
                    const std::string &pattern_segment = pattern_segments[i];
                    // 当前 URI 段。
                    const std::string &uri_segment = uri_segments[i];
                    // 参数段（例如 :id）。
                    if (!pattern_segment.empty() && pattern_segment[0] == ':')
                    {
                        // 参数值为空，视为不匹配。
                        if (uri_segment.empty())
                        {
                            // 清理已写入参数，保持失败语义干净。
                            route_params.clear();
                            // 返回失败。
                            return false;
                        }
                        // 写入参数名与参数值（去掉前导 ':'）。
                        route_params[pattern_segment.substr(1)] = uri_segment;
                        // 当前段匹配结束，继续下一段。
                        continue;
                    }
                    // 非参数段必须严格相等。
                    if (pattern_segment != uri_segment)
                    {
                        // 不匹配时清理参数，避免半匹配残留。
                        route_params.clear();
                        // 返回失败。
                        return false;
                    }
                }
                // 全部段匹配成功。
                return true;
            }

        } // namespace

        // Router 默认构造函数。
        Router::Router()
        {
            // 无额外初始化逻辑。
        }

        // 注册精确路由（不限制方法）。
        void Router::addServlet(const std::string &uri, std::shared_ptr<Servlet> servlet)
        {
            // 转调到方法版本，使用 INVALID_METHOD 表示不限方法。
            addServlet(HttpMethod::INVALID_METHOD, uri, servlet);
        }

        // 注册精确路由（可限制方法）。
        void Router::addServlet(HttpMethod method, const std::string &uri, std::shared_ptr<Servlet> servlet)
        {
            // 若已存在同 method+uri，直接覆盖更新。
            for (size_t i = 0; i < m_exact.size(); ++i)
            {
                // 命中已有项。
                if (m_exact[i].method == method && m_exact[i].uri == uri)
                {
                    // 覆盖处理器。
                    m_exact[i].servlet = servlet;
                    // 覆盖后返回。
                    return;
                }
            }
            // 未命中时构造新路由项。
            ExactItem item;
            // 写入方法约束。
            item.method = method;
            // 写入路径。
            item.uri = uri;
            // 写入处理器。
            item.servlet = servlet;
            // 追加到精确路由表。
            m_exact.push_back(item);
        }

        // 注册通配路由（不限制方法）。
        void Router::addGlobServlet(const std::string &pattern, std::shared_ptr<Servlet> servlet)
        {
            // 转调到方法版本，默认不限方法。
            addGlobServlet(HttpMethod::INVALID_METHOD, pattern, servlet);
        }

        // 注册通配路由（可限制方法）。
        void Router::addGlobServlet(HttpMethod method, const std::string &pattern, std::shared_ptr<Servlet> servlet)
        {
            // 若已存在同 method+pattern，执行覆盖更新。
            for (size_t i = 0; i < m_globs.size(); ++i)
            {
                // 命中已有项。
                if (m_globs[i].method == method && m_globs[i].pattern == pattern)
                {
                    // 覆盖处理器。
                    m_globs[i].servlet = servlet;
                    // 覆盖后返回。
                    return;
                }
            }
            // 未命中时创建新通配路由项。
            GlobItem item;
            // 写入方法约束。
            item.method = method;
            // 写入通配模式。
            item.pattern = pattern;
            // 写入处理器。
            item.servlet = servlet;
            // 追加到通配路由表。
            m_globs.push_back(item);
        }

        // 注册参数路由（不限制方法）。
        void Router::addParamServlet(const std::string &pattern, std::shared_ptr<Servlet> servlet)
        {
            // 转调到方法版本，默认不限方法。
            addParamServlet(HttpMethod::INVALID_METHOD, pattern, servlet);
        }

        // 注册参数路由（可限制方法）。
        void Router::addParamServlet(HttpMethod method, const std::string &pattern, std::shared_ptr<Servlet> servlet)
        {
            // 预切分 pattern，减少匹配时重复 split 开销。
            std::vector<std::string> segments = SplitPathSegments(pattern);
            // 若已存在同 method+pattern，覆盖更新。
            for (size_t i = 0; i < m_params.size(); ++i)
            {
                // 命中已有项。
                if (m_params[i].method == method && m_params[i].pattern == pattern)
                {
                    // 更新缓存分段。
                    m_params[i].segments = segments;
                    // 更新处理器。
                    m_params[i].servlet = servlet;
                    // 覆盖后返回。
                    return;
                }
            }
            // 未命中时创建新参数路由项。
            ParamItem item;
            // 写入方法约束。
            item.method = method;
            // 写入原始 pattern。
            item.pattern = pattern;
            // 写入预切分段。
            item.segments = segments;
            // 写入处理器。
            item.servlet = servlet;
            // 追加到参数路由表。
            m_params.push_back(item);
        }

        // 设置默认兜底路由处理器。
        void Router::setDefault(std::shared_ptr<Servlet> servlet)
        {
            // 直接保存默认处理器。
            m_default = servlet;
        }

        // 兼容接口：只按 URI 匹配（忽略方法）。
        std::shared_ptr<Servlet> Router::getMatched(const std::string &uri) const
        {
            // 使用 INVALID_METHOD 触发“不限制方法”匹配。
            return match(uri, HttpMethod::INVALID_METHOD).servlet;
        }

        // 核心匹配函数：按 exact -> param -> glob -> default 顺序匹配。
        Router::RouteMatch Router::match(const std::string &uri, HttpMethod method) const
        {
            // 构造默认空结果。
            RouteMatch result;
            // 1) 精确匹配优先。
            for (size_t i = 0; i < m_exact.size(); ++i)
            {
                // 方法匹配且路径精确相等即命中。
                if (MatchMethod(m_exact[i].method, method) && m_exact[i].uri == uri)
                {
                    // 记录命中类型。
                    result.type = RouteMatch::EXACT;
                    // 写入命中处理器。
                    result.servlet = m_exact[i].servlet;
                    // 返回匹配结果。
                    return result;
                }
            }
            // 2) 参数路由匹配。
            for (size_t i = 0; i < m_params.size(); ++i)
            {
                // 临时参数表：仅在命中时再写入结果。
                ParamsMap route_params;
                // 方法匹配且参数模式匹配即命中。
                if (MatchMethod(m_params[i].method, method) && MatchParamRoute(m_params[i].segments, uri, route_params))
                {
                    // 记录命中类型。
                    result.type = RouteMatch::PARAM;
                    // 写入命中处理器。
                    result.servlet = m_params[i].servlet;
                    // 搬运参数结果，避免额外拷贝开销。
                    result.route_params.swap(route_params);
                    // 返回匹配结果。
                    return result;
                }
            }
            // 3) 通配路由匹配。
            for (size_t i = 0; i < m_globs.size(); ++i)
            {
                // 方法匹配且通配规则命中即返回。
                if (MatchMethod(m_globs[i].method, method) && MatchGlob(m_globs[i].pattern, uri))
                {
                    // 记录命中类型。
                    result.type = RouteMatch::GLOB;
                    // 写入命中处理器。
                    result.servlet = m_globs[i].servlet;
                    // 返回匹配结果。
                    return result;
                }
            }
            // 4) 未命中任何路由，回落默认处理器。
            result.type = RouteMatch::DEFAULT;
            // 写入默认处理器（可能为空，取决于调用方是否设置默认路由）。
            result.servlet = m_default;
            // 返回最终结果。
            return result;
        }

        // 便捷匹配：从 request 读取 path 与 method 后复用核心匹配逻辑。
        Router::RouteMatch Router::match(HttpRequest::ptr request) const
        {
            // 委托到 path+method 版本。
            return match(request->getPath(), request->getMethod());
        }

    } // namespace http
} // namespace sylar
