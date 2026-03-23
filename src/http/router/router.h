// 头文件保护宏：防止 router.h 被重复包含。
#ifndef __SYLAR_HTTP_ROUTER_H__
// 定义头文件保护宏。
#define __SYLAR_HTTP_ROUTER_H__

// 引入 HttpRequest/HttpMethod 等 HTTP 基础类型定义。
#include "http/core/http_request.h"

// 引入 std::unordered_map，用于保存路由参数 key-value。
#include <unordered_map>
// 引入智能指针能力。
#include <memory>
// 引入 std::string 字符串类型。
#include <string>
// 引入 std::vector 容器，保存路由表与路径分段。
#include <vector>

// sylar 顶层命名空间。
namespace http
{

// 前置声明 Servlet，避免在头文件中直接包含 servlet.h 造成循环依赖。
class Servlet;

// Router：负责 HTTP 路由注册与匹配的独立模块。
class Router
{
  public:
    // Router 智能指针别名。
    typedef std::shared_ptr<Router> ptr;
    // 路由参数映射类型，例如 :id -> 42。
    typedef std::unordered_map<std::string, std::string> ParamsMap;

    // RouteMatch：一次匹配的结构化结果。
    struct RouteMatch
    {
        // 命中类型枚举，便于后续统计/调试/扩展。
        enum Type
        {
            // 未命中。
            NONE = 0,
            // 命中精确路由。
            EXACT = 1,
            // 命中参数路由。
            PARAM = 2,
            // 命中通配路由。
            GLOB = 3,
            // 命中默认路由（兜底）。
            DEFAULT = 4
        };

        // 默认构造：初始状态设为 NONE。
        RouteMatch()
            // 初始化 type 字段。
            : type(NONE)
        {
            // 无额外初始化逻辑。
        }

        // 便捷判断：是否存在命中 servlet。
        bool matched() const
        {
            return servlet != nullptr;
        }

        // 命中类型。
        Type type;
        // 命中的处理器。
        std::shared_ptr<Servlet> servlet;
        // 命中参数路由时提取到的参数集合。
        ParamsMap route_params;
    };

    // 默认构造函数。
    Router();

    // 注册精确路由（不限制方法）。
    void addServlet(const std::string& uri, std::shared_ptr<Servlet> servlet);
    // 注册精确路由（限制方法）。
    void addServlet(HttpMethod method, const std::string& uri, std::shared_ptr<Servlet> servlet);

    // 注册通配路由（不限制方法），例如 /api/*。
    void addGlobServlet(const std::string& pattern, std::shared_ptr<Servlet> servlet);
    // 注册通配路由（限制方法）。
    void addGlobServlet(HttpMethod method, const std::string& pattern, std::shared_ptr<Servlet> servlet);

    // 注册参数路由（不限制方法），例如 /user/:id。
    void addParamServlet(const std::string& pattern, std::shared_ptr<Servlet> servlet);
    // 注册参数路由（限制方法）。
    void addParamServlet(HttpMethod method, const std::string& pattern, std::shared_ptr<Servlet> servlet);

    // 设置默认兜底路由。
    void setDefault(std::shared_ptr<Servlet> servlet);

    // 兼容接口：仅按 URI 匹配，忽略方法。
    std::shared_ptr<Servlet> getMatched(const std::string& uri) const;
    // 核心匹配接口：按 URI + 方法匹配并返回结构化结果。
    RouteMatch match(const std::string& uri, HttpMethod method) const;
    // 便捷接口：从 HttpRequest 读取 path/method 后匹配。
    RouteMatch match(HttpRequest::ptr request) const;

  private:
    // 通配路由项。
    struct GlobItem
    {
        // 方法约束；INVALID_METHOD 表示不限制方法。
        HttpMethod method;
        // 通配模式字符串。
        std::string pattern;
        // 对应处理器。
        std::shared_ptr<Servlet> servlet;
    };

    // 参数路由项。
    struct ParamItem
    {
        // 方法约束；INVALID_METHOD 表示不限制方法。
        HttpMethod method;
        // 参数路由原始模式。
        std::string pattern;
        // 预切分段，减少匹配时重复 split 开销。
        std::vector<std::string> segments;
        // 对应处理器。
        std::shared_ptr<Servlet> servlet;
    };

    // 精确路由项。
    struct ExactItem
    {
        // 方法约束；INVALID_METHOD 表示不限制方法。
        HttpMethod method;
        // 精确路径。
        std::string uri;
        // 对应处理器。
        std::shared_ptr<Servlet> servlet;
    };

  private:
    // 精确路由表。
    std::vector<ExactItem> m_exact;
    // 通配路由表。
    std::vector<GlobItem> m_globs;
    // 参数路由表。
    std::vector<ParamItem> m_params;
    // 默认兜底处理器。
    std::shared_ptr<Servlet> m_default;
};

} // namespace http

// 结束头文件保护宏。
#endif
