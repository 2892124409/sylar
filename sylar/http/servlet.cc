// 引入 Servlet 相关声明（Servlet/FunctionServlet/ServletDispatch）。
#include "sylar/http/servlet.h"

// 引入统一错误响应工具（用于默认 404 处理）。
#include "sylar/http/http_error.h"

// sylar 顶层命名空间开始。
namespace sylar
{
    // http 子命名空间开始。
    namespace http
    {
        // 匿名命名空间：仅本编译单元可见的内部实现细节。
        namespace
        {

            // 默认兜底 Servlet：当没有任何路由匹配时返回 404。
            class NotFoundServlet : public Servlet
            {
            public:
                // 构造函数：给这个默认处理器命名，便于日志定位。
                NotFoundServlet()
                    : Servlet("NotFoundServlet")
                {
                    // 无额外初始化逻辑。
                }

                // 处理未匹配路由请求：统一输出 404。
                virtual int32_t handle(HttpRequest::ptr, HttpResponse::ptr response, HttpSession::ptr) override
                {
                    // 使用统一错误输出工具，保持错误响应风格一致。
                    ApplyErrorResponse(response, HttpStatus::NOT_FOUND, "Not Found", "route not found");
                    // 返回 0 表示处理流程正常结束（语义成功，业务结果是 404）。
                    return 0;
                }
            };

            // 通配匹配函数（简化版）：支持 "*" 和 "prefix*"。
            static bool MatchGlob(const std::string &pattern, const std::string &uri)
            {
                // pattern 为 "*" 时，匹配任意 URI。
                if (pattern == "*")
                {
                    // 直接命中。
                    return true;
                }
                // 若 pattern 以 '*' 结尾，采用前缀匹配（如 /api/*）。
                if (pattern.size() >= 1 && pattern[pattern.size() - 1] == '*')
                {
                    // 比较 pattern 去掉 '*' 后的前缀是否与 uri 开头一致。
                    return uri.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
                }
                // 其他情况按精确字符串比较。
                return pattern == uri;
            }

            // 将路径按 '/' 拆分为段（忽略空段）。
            static std::vector<std::string> SplitPathSegments(const std::string &path)
            {
                // 存放最终分段结果。
                std::vector<std::string> segments;
                // 存放当前正在累积的段内容。
                std::string current;
                // 逐字符扫描路径字符串。
                for (size_t i = 0; i < path.size(); ++i)
                {
                    // 遇到分隔符 '/'。
                    if (path[i] == '/')
                    {
                        // 若当前段非空，推入结果。
                        if (!current.empty())
                        {
                            // 保存当前段。
                            segments.push_back(current);
                            // 清空 current，准备下一段。
                            current.clear();
                        }
                        // 无论是否有段，都继续处理下一个字符。
                        continue;
                    }
                    // 普通字符：追加到当前段。
                    current.push_back(path[i]);
                }
                // 循环结束后，若还有尾段，补入结果。
                if (!current.empty())
                {
                    // 保存最后一个段。
                    segments.push_back(current);
                }
                // 返回分段结果。
                return segments;
            }

            // 参数路由匹配：pattern 段中以 ':' 开头的段视为参数名。
            static bool MatchParamRoute(const std::vector<std::string> &pattern_segments,
                                        const std::string &uri,
                                        HttpRequest::ptr request)
            {
                // 先把请求 URI 拆分为路径段。
                std::vector<std::string> uri_segments = SplitPathSegments(uri);
                // 段数不一致直接不匹配。
                if (pattern_segments.size() != uri_segments.size())
                {
                    // 返回失败。
                    return false;
                }

                // 每次匹配前清空 request 上旧的路由参数，避免污染。
                request->clearRouteParams();
                // 逐段比对。
                for (size_t i = 0; i < pattern_segments.size(); ++i)
                {
                    // 当前 pattern 段。
                    const std::string &pattern_segment = pattern_segments[i];
                    // 当前 URI 段。
                    const std::string &uri_segment = uri_segments[i];
                    // 若 pattern 段以 ':' 开头，则表示参数段（如 :id）。
                    if (!pattern_segment.empty() && pattern_segment[0] == ':')
                    {
                        // 参数值为空则视为不匹配（防御性校验）。
                        if (uri_segment.empty())
                        {
                            // 清空已写入的参数，避免部分匹配残留。
                            request->clearRouteParams();
                            // 返回失败。
                            return false;
                        }
                        // 去掉前导 ':' 得到参数名，并写入 request。
                        request->setRouteParam(pattern_segment.substr(1), uri_segment);
                        // 参数段匹配完成，继续下一段。
                        continue;
                    }
                    // 非参数段必须严格相等。
                    if (pattern_segment != uri_segment)
                    {
                        // 不相等则匹配失败，清空参数。
                        request->clearRouteParams();
                        // 返回失败。
                        return false;
                    }
                }
                // 所有段都匹配成功。
                return true;
            }

        } // 匿名命名空间结束。

        // Servlet 基类构造：保存名称。
        Servlet::Servlet(const std::string &name)
            : m_name(name)
        {
            // 无额外初始化。
        }

        // FunctionServlet 构造：把回调保存到 m_cb。
        FunctionServlet::FunctionServlet(Callback cb, const std::string &name)
            : Servlet(name), m_cb(cb)
        {
            // 无额外初始化。
        }

        // FunctionServlet 的 handle 直接委托给回调函数执行。
        int32_t FunctionServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
        {
            // 将三元组原样传给 m_cb。
            return m_cb(request, response, session);
        }

        // 分发器构造：默认路由设置为 NotFoundServlet。
        ServletDispatch::ServletDispatch()
            : Servlet("ServletDispatch"), m_default(new NotFoundServlet())
        {
            // 无额外初始化。
        }

        // 注册精确路由：uri -> servlet。
        void ServletDispatch::addServlet(const std::string &uri, Servlet::ptr servlet)
        {
            // 先查是否已有同名 URI。
            for (size_t i = 0; i < m_exact.size(); ++i)
            {
                // 命中同 URI 时执行覆盖更新。
                if (m_exact[i].first == uri)
                {
                    // 替换目标 servlet。
                    m_exact[i].second = servlet;
                    // 更新后直接返回。
                    return;
                }
            }
            // 未命中则追加新路由项。
            m_exact.push_back(std::make_pair(uri, servlet));
        }

        // 精确路由回调版：把回调包装成 FunctionServlet 后复用主逻辑。
        void ServletDispatch::addServlet(const std::string &uri, FunctionServlet::Callback cb)
        {
            // 创建 FunctionServlet 并注册。
            addServlet(uri, Servlet::ptr(new FunctionServlet(cb)));
        }

        // 注册通配路由：pattern -> servlet。
        void ServletDispatch::addGlobServlet(const std::string &pattern, Servlet::ptr servlet)
        {
            // 先查是否已有同 pattern。
            for (size_t i = 0; i < m_globs.size(); ++i)
            {
                // 命中同 pattern 时执行覆盖更新。
                if (m_globs[i].pattern == pattern)
                {
                    // 替换目标 servlet。
                    m_globs[i].servlet = servlet;
                    // 更新后返回。
                    return;
                }
            }
            // 构造新的通配路由项。
            GlobItem item;
            // 设置 pattern。
            item.pattern = pattern;
            // 绑定 servlet。
            item.servlet = servlet;
            // 追加到通配路由表。
            m_globs.push_back(item);
        }

        // 通配路由回调版：包装后复用主逻辑。
        void ServletDispatch::addGlobServlet(const std::string &pattern, FunctionServlet::Callback cb)
        {
            // 创建 FunctionServlet 并注册。
            addGlobServlet(pattern, Servlet::ptr(new FunctionServlet(cb)));
        }

        // 注册参数路由：pattern(如 /user/:id) -> servlet。
        void ServletDispatch::addParamServlet(const std::string &pattern, Servlet::ptr servlet)
        {
            // 先把 pattern 预切分，减少匹配时重复开销。
            std::vector<std::string> segments = SplitPathSegments(pattern);
            // 查找是否已存在同 pattern。
            for (size_t i = 0; i < m_params.size(); ++i)
            {
                // 命中同 pattern 时覆盖更新。
                if (m_params[i].pattern == pattern)
                {
                    // 更新预切分段。
                    m_params[i].segments = segments;
                    // 更新 servlet。
                    m_params[i].servlet = servlet;
                    // 更新完成返回。
                    return;
                }
            }
            // 构造新参数路由项。
            ParamItem item;
            // 设置 pattern 原文。
            item.pattern = pattern;
            // 保存预切分段。
            item.segments = segments;
            // 绑定 servlet。
            item.servlet = servlet;
            // 追加到参数路由表。
            m_params.push_back(item);
        }

        // 参数路由回调版：包装后复用主逻辑。
        void ServletDispatch::addParamServlet(const std::string &pattern, FunctionServlet::Callback cb)
        {
            // 创建 FunctionServlet 并注册。
            addParamServlet(pattern, Servlet::ptr(new FunctionServlet(cb)));
        }

        // 注册前置拦截器。
        void ServletDispatch::addPreInterceptor(PreInterceptor cb)
        {
            // 追加到前置拦截器链。
            m_preInterceptors.push_back(cb);
        }

        // 注册后置拦截器。
        void ServletDispatch::addPostInterceptor(PostInterceptor cb)
        {
            // 追加到后置拦截器链。
            m_postInterceptors.push_back(cb);
        }

        // 设置默认路由处理器（兜底处理器）。
        void ServletDispatch::setDefault(Servlet::ptr servlet)
        {
            // 直接替换默认处理器。
            m_default = servlet;
        }

        // 仅按 URI 查匹配（不保留参数），用于旧接口兼容。
        Servlet::ptr ServletDispatch::getMatched(const std::string &uri) const
        {
            // 1) 精确匹配优先。
            for (size_t i = 0; i < m_exact.size(); ++i)
            {
                // 命中精确路由直接返回。
                if (m_exact[i].first == uri)
                {
                    return m_exact[i].second;
                }
            }
            // 2) 参数路由匹配（这里用临时 request，只判断是否命中，不保留参数）。
            for (size_t i = 0; i < m_params.size(); ++i)
            {
                // 命中参数路由返回对应 servlet。
                if (MatchParamRoute(m_params[i].segments, uri, HttpRequest::ptr(new HttpRequest())))
                {
                    return m_params[i].servlet;
                }
            }
            // 3) 通配匹配。
            for (size_t i = 0; i < m_globs.size(); ++i)
            {
                // 命中通配返回对应 servlet。
                if (MatchGlob(m_globs[i].pattern, uri))
                {
                    return m_globs[i].servlet;
                }
            }
            // 4) 全部未命中，返回默认 servlet。
            return m_default;
        }

        // 按完整 request 匹配（支持写入 route params）。
        Servlet::ptr ServletDispatch::getMatched(HttpRequest::ptr request) const
        {
            // 读取请求路径。
            const std::string &uri = request->getPath();
            // 每次匹配前清空旧参数。
            request->clearRouteParams();
            // 1) 精确匹配优先。
            for (size_t i = 0; i < m_exact.size(); ++i)
            {
                // 命中精确路由直接返回。
                if (m_exact[i].first == uri)
                {
                    return m_exact[i].second;
                }
            }
            // 2) 参数路由匹配。
            for (size_t i = 0; i < m_params.size(); ++i)
            {
                // 命中后会把参数写入 request->routeParams。
                if (MatchParamRoute(m_params[i].segments, uri, request))
                {
                    return m_params[i].servlet;
                }
            }
            // 3) 通配匹配。
            for (size_t i = 0; i < m_globs.size(); ++i)
            {
                // 命中通配时清空 route params（通配不产生参数）。
                if (MatchGlob(m_globs[i].pattern, uri))
                {
                    request->clearRouteParams();
                    return m_globs[i].servlet;
                }
            }
            // 4) 默认路由前也清空 route params，保持语义干净。
            request->clearRouteParams();
            // 返回默认处理器。
            return m_default;
        }

        // 分发入口：执行 pre 拦截器 -> 路由处理 -> post 拦截器。
        int32_t ServletDispatch::handle(HttpRequest::ptr request, HttpResponse::ptr response, HttpSession::ptr session)
        {
            // 先执行前置拦截器链。
            for (size_t i = 0; i < m_preInterceptors.size(); ++i)
            {
                // 任一 pre 返回 false：中断业务 handler。
                if (!m_preInterceptors[i](request, response, session))
                {
                    // 即便被拦截，也执行后置拦截器（便于统一收尾）。
                    for (size_t j = 0; j < m_postInterceptors.size(); ++j)
                    {
                        // 执行后置拦截器。
                        m_postInterceptors[j](request, response, session);
                    }
                    // 返回 0，表示分发流程正常结束（虽未进入业务 handler）。
                    return 0;
                }
            }

            // 计算最终命中的 servlet（含参数路由写参逻辑）。
            Servlet::ptr servlet = getMatched(request);
            // 调用目标 servlet 的 handle，得到业务返回码。
            int32_t rt = servlet->handle(request, response, session);
            // 执行业务后置拦截器链。
            for (size_t i = 0; i < m_postInterceptors.size(); ++i)
            {
                // 执行后置拦截器。
                m_postInterceptors[i](request, response, session);
            }
            // 返回业务 handler 的返回码。
            return rt;
        }

    } // http 命名空间结束。
} // sylar 命名空间结束。
