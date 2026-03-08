#ifndef __SYLAR_HTTP_HTTP_RESPONSE_H__
#define __SYLAR_HTTP_HTTP_RESPONSE_H__

#include "sylar/http/http.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sylar
{
    namespace http
    {

        /**
         * @brief HTTP 响应对象
         * @details
         * 它表示一条“准备发回客户端”的 HTTP 响应。
         * Servlet 主要做的事情，就是填充这个对象。
         *
         * 当前阶段它负责：
         * - 保存状态码
         * - 保存响应头
         * - 保存响应体
         * - 保存 keep-alive 语义
         * - 序列化为完整 HTTP/1.1 文本响应
         */
        class HttpResponse
        {
        public:
            typedef std::shared_ptr<HttpResponse> ptr;
            typedef std::map<std::string, std::string> MapType;

            /**
             * @brief 构造默认响应
             * @details
             * 默认是 200 OK + HTTP/1.1 + keep-alive。
             */
            HttpResponse();

            /// 响应状态码，例如 200、404、500
            HttpStatus getStatus() const { return m_status; }
            void setStatus(HttpStatus value) { m_status = value; }

            /// HTTP 版本号
            uint8_t getVersionMajor() const { return m_versionMajor; }
            uint8_t getVersionMinor() const { return m_versionMinor; }
            void setVersion(uint8_t major, uint8_t minor)
            {
                m_versionMajor = major;
                m_versionMinor = minor;
            }

            /// 是否保持连接
            bool isKeepAlive() const { return m_keepalive; }
            void setKeepAlive(bool value) { m_keepalive = value; }

            /// 是否为流式响应（响应头与响应体由业务代码自行写出）
            bool isStream() const { return m_stream; }
            void setStream(bool value) { m_stream = value; }

            /// 响应体
            const std::string &getBody() const { return m_body; }
            void setBody(const std::string &value) { m_body = value; }

            /// 自定义 reason phrase；为空时使用默认 reason
            const std::string &getReason() const { return m_reason; }
            void setReason(const std::string &value) { m_reason = value; }

            /// 设置普通响应头
            void setHeader(const std::string &key, const std::string &value);

            /// 获取响应头
            std::string getHeader(const std::string &key, const std::string &def = "") const;
            const MapType &getHeaders() const { return m_headers; }

            /**
             * @brief 追加一个 Set-Cookie 头
             * @details
             * 因为一个响应可能要发多个 Set-Cookie，
             * 所以单独维护一个数组，而不是塞进普通 map。
             */
            void addSetCookie(const std::string &cookie);
            const std::vector<std::string> &getSetCookies() const { return m_setCookies; }

            /// 返回 HTTP/1.1 这样的版本字符串
            std::string getVersionString() const;

            /**
             * @brief 序列化成完整 HTTP 响应报文
             * @details
             * 包含：状态行 + headers + 空行 + body。
             */
            std::string toString() const;

            /// 仅序列化响应头（不包含 Content-Length 和 body），常用于流式响应
            std::string toHeaderString() const;

private:
    /// HTTP 响应状态码（200/404/500...）
    HttpStatus m_status;

    /// HTTP 主版本号，例如 HTTP/1.1 中的 1
    uint8_t m_versionMajor;

    /// HTTP 次版本号，例如 HTTP/1.1 中的 1
    uint8_t m_versionMinor;

    /// 是否保持连接（影响 Connection: keep-alive/close）
    bool m_keepalive;

    /// 是否为流式响应；为 true 时通常由业务代码自行写 header/body
    bool m_stream;

    /// 响应体正文内容
    std::string m_body;

    /// 自定义 reason phrase；为空时使用状态码默认文本
    std::string m_reason;

    /// 普通响应头键值对（不包含 Set-Cookie）
    MapType m_headers;

    /// Set-Cookie 头列表（一个响应可能包含多个 Set-Cookie）
    std::vector<std::string> m_setCookies;
};

    } // namespace http
} // namespace sylar

#endif
