#ifndef __SYLAR_HTTP_HTTP_REQUEST_H__
#define __SYLAR_HTTP_HTTP_REQUEST_H__

#include "http/core/http.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace http
{

    /**
     * @brief HTTP 请求对象
     * @details
     * 这个类表示“已经被解析完成的一条 HTTP 请求”。
     * 它不负责从 socket 读取数据，也不负责解析字节流，
     * 只负责承载解析结果，供 Servlet/业务层读取。
     *
     * 当前阶段它保存了：
     * - 请求方法（GET/POST/...）
     * - HTTP 版本
     * - path/query/fragment
     * - headers
     * - body
     * - cookies
     * - keep-alive 语义
     */
    class HttpRequest
    {
    public:
        typedef std::shared_ptr<HttpRequest> ptr;
        typedef std::unordered_map<std::string, std::string> MapType;

        /**
         * @brief 构造一个默认请求对象
         * @details
         * 默认值采用最常见的 HTTP/1.1 GET / 语义，
         * 方便解析器在填充字段时逐步覆盖。
         */
        HttpRequest();

        /// 请求方法，例如 GET/POST
        HttpMethod getMethod() const { return m_method; }
        void setMethod(HttpMethod value) { m_method = value; }

        /// HTTP 主版本号和次版本号，例如 HTTP/1.1 -> 1,1
        uint8_t getVersionMajor() const { return m_versionMajor; }
        uint8_t getVersionMinor() const { return m_versionMinor; }
        void setVersion(uint8_t major, uint8_t minor)
        {
            m_versionMajor = major;
            m_versionMinor = minor;
        }

        /// URL 中 ? 之前的路径部分，例如 /chat/send
        const std::string &getPath() const { return m_path; }
        void setPath(const std::string &value) { m_path = value; }

        /// URL 中 ? 后面的查询串，例如 a=1&b=2
        const std::string &getQuery() const { return m_query; }
        void setQuery(const std::string &value) { m_query = value; }

        /// URL 中 # 后面的片段，目前服务端一般不依赖它
        const std::string &getFragment() const { return m_fragment; }
        void setFragment(const std::string &value) { m_fragment = value; }

        /// 请求体，常用于 POST/PUT
        const std::string &getBody() const { return m_body; }
        void setBody(const std::string &value) { m_body = value; }

        /// 当前请求是否希望保持连接
        bool isKeepAlive() const { return m_keepalive; }
        void setKeepAlive(bool value) { m_keepalive = value; }

        /// 原始头字段集合
        const MapType &getHeaders() const { return m_headers; }
        /// 由 query string 解析出的参数集合
        const MapType &getParams() const { return m_params; }
        /// 由路由匹配解析出的参数集合，例如 /user/:id -> id=42
        const MapType &getRouteParams() const { return m_routeParams; }
        /// 由 Cookie 头解析出的 cookie 集合
        const MapType &getCookies() const { return m_cookies; }

        /**
         * @brief 设置/覆盖一个 header
         */
        void setHeader(const std::string &key, const std::string &value);

        /**
         * @brief 获取指定 header，不存在则返回默认值
         */
        std::string getHeader(const std::string &key, const std::string &def = "") const;

        /**
         * @brief 判断 header 是否存在
         */
        bool hasHeader(const std::string &key) const;

        /// 保存 query 参数
        void setParam(const std::string &key, const std::string &value);

        /// 获取 query 参数
        std::string getParam(const std::string &key, const std::string &def = "") const;

        /// 保存 cookie
        void setCookie(const std::string &key, const std::string &value);

        /// 获取 cookie
        std::string getCookie(const std::string &key, const std::string &def = "") const;

        /// 保存路由参数
        void setRouteParam(const std::string &key, const std::string &value);

        /// 获取路由参数
        std::string getRouteParam(const std::string &key, const std::string &def = "") const;

        /// 判断路由参数是否存在
        bool hasRouteParam(const std::string &key) const;

        /// 清空当前请求上的所有路由参数（每次路由匹配前重置）
        void clearRouteParams();

        /**
         * @brief 返回 HTTP 版本字符串，例如 HTTP/1.1
         */
        std::string getVersionString() const;

        /**
         * @brief 返回 path + query + fragment 拼装后的请求目标
         * @details
         * 主要用于调试打印或后续反向代理等场景。
         */
        std::string getPathWithQuery() const;

    private:
        /// HTTP 请求方法（GET/POST/PUT...）
        HttpMethod m_method;

        /// HTTP 主版本号，例如 HTTP/1.1 中的 1
        uint8_t m_versionMajor;

        /// HTTP 次版本号，例如 HTTP/1.1 中的 1
        uint8_t m_versionMinor;

        /// 连接是否保持（keep-alive 语义结果）
        bool m_keepalive;

        /// URL 路径部分（不含 query 和 fragment），例如 /api/chat
        std::string m_path;

        /// URL 查询串（? 后部分），例如 a=1&b=2
        std::string m_query;

        /// URL 片段（# 后部分），服务端一般很少使用
        std::string m_fragment;

        /// 请求体内容（POST/PUT/PATCH 常用）
        std::string m_body;

        /// 原始请求头键值对
        MapType m_headers;

        /// 从 query string 解析出的参数键值对
        MapType m_params;

        /// 从参数路由解析出的键值对，例如 :id -> 42
        MapType m_routeParams;

        /// 从 Cookie 请求头解析出的 cookie 键值对
        MapType m_cookies;
    };

} // namespace http

#endif
