#include "http/core/http_response.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace sylar
{
    namespace http
    {
        namespace
        {
            // 将字符串转为小写，用于 header key 归一化。
            static std::string HeaderKeyToLower(const std::string &key)
            {
                std::string lower = key;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c)
                               { return static_cast<char>(std::tolower(c)); });
                return lower;
            }
        } // namespace

        HttpResponse::HttpResponse()
            : m_status(HttpStatus::OK), m_versionMajor(1), m_versionMinor(1), m_keepalive(true), m_stream(false)
        {
        }

        void HttpResponse::setHeader(const std::string &key, const std::string &value)
        {
            m_headers[HeaderKeyToLower(key)] = value;
        }

        std::string HttpResponse::getHeader(const std::string &key, const std::string &def) const
        {
            MapType::const_iterator it = m_headers.find(HeaderKeyToLower(key));
            return it == m_headers.end() ? def : it->second;
        }

        void HttpResponse::addSetCookie(const std::string &cookie)
        {
            // Set-Cookie 可重复，因此用 vector 累积而非 map 覆盖
            m_setCookies.push_back(cookie);
        }

        std::string HttpResponse::getVersionString() const
        {
            // 组合 HTTP 版本字符串，如 "HTTP/1.1"
            return "HTTP/" + std::to_string(m_versionMajor) + "." + std::to_string(m_versionMinor);
        }

        std::string HttpResponse::toHeaderString() const
        {
            // 字符串流用于高可读性拼接 HTTP 报文文本
            std::ostringstream ss;
            // reason phrase：
            // - 用户没自定义时，用状态码默认文本（如 200 -> OK）
            // - 有自定义时优先使用自定义文本
            std::string reason = m_reason.empty() ? HttpStatusToString(m_status) : m_reason;
            // 写入状态行：HTTP/1.1 200 OK\r\n
            ss << getVersionString() << " " << static_cast<int>(m_status) << " " << reason << "\r\n";

            // 标记是否已有 Connection 头，避免重复写
            bool has_connection = false;
            // 写出所有普通响应头（m_headers）
            for (MapType::const_iterator it = m_headers.begin(); it != m_headers.end(); ++it)
            {
                // 记录是否已有 Connection
                if (it->first == "connection")
                {
                    has_connection = true;
                }
                // 逐行写出：Key: Value\r\n
                ss << it->first << ": " << it->second << "\r\n";
            }

            // 若用户未显式给 Connection，则按 keepalive 语义自动补
            if (!has_connection)
            {
                ss << "connection: " << (m_keepalive ? "keep-alive" : "close") << "\r\n";
            }
            // 逐条写 Set-Cookie（一个响应可能有多个）
            for (size_t i = 0; i < m_setCookies.size(); ++i)
            {
                ss << "set-cookie: " << m_setCookies[i] << "\r\n";
            }
            // 头部结束空行（\r\n）
            ss << "\r\n";
            // 仅返回"头部文本"，不带 body / Content-Length（供流式场景使用）
            return ss.str();
        }

        std::string HttpResponse::toString() const
        {
            // 字符串流用于组装完整 HTTP 响应（头 + 体）
            std::ostringstream ss;
            // reason phrase 选择逻辑同 toHeaderString()
            std::string reason = m_reason.empty() ? HttpStatusToString(m_status) : m_reason;
            // 写状态行
            ss << getVersionString() << " " << static_cast<int>(m_status) << " " << reason << "\r\n";

            // 记录是否已有 Content-Length / Connection，避免重复写
            bool has_content_length = false;
            bool has_connection = false;
            // 写普通响应头
            for (MapType::const_iterator it = m_headers.begin(); it != m_headers.end(); ++it)
            {
                // 用户已设置 Content-Length
                if (it->first == "content-length")
                {
                    has_content_length = true;
                }
                // 用户已设置 Connection
                else if (it->first == "connection")
                {
                    has_connection = true;
                }
                // 写头部行
                ss << it->first << ": " << it->second << "\r\n";
            }

            // 若未提供 Connection，按 keepalive 自动补齐
            if (!has_connection)
            {
                ss << "connection: " << (m_keepalive ? "keep-alive" : "close") << "\r\n";
            }
            // 若未提供 Content-Length，按当前 body 实际字节数自动补齐
            if (!has_content_length)
            {
                ss << "content-length: " << m_body.size() << "\r\n";
            }
            // 写所有 Set-Cookie 头
            for (size_t i = 0; i < m_setCookies.size(); ++i)
            {
                ss << "set-cookie: " << m_setCookies[i] << "\r\n";
            }
            // 头部结束空行
            ss << "\r\n";
            // 追加响应体正文
            ss << m_body;
            // 返回完整 HTTP 响应文本
            return ss.str();
        }

    } // namespace http
} // namespace sylar
