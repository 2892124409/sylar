#include "http/core/http_response.h"

#include <algorithm>
#include <cctype>

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

        /**
         * @brief 估算 header 序列化后的总字节数
         * @details
         * 用于 reserve() 预分配，避免拼接过程中多次扩容。
         * 估算公式：每个 header = key.size() + ": ".size(2) + value.size() + "\r\n".size(2)
         */
        static size_t EstimateHeadersSize(const HttpResponse::MapType &headers,
                                          const std::vector<std::string> &setCookies)
        {
            // 状态行 "HTTP/1.1 200 OK\r\n" 约 20~30 字节
            // 加上 Connection / Content-Length 自动补齐行约 40 字节
            // 尾部空行 "\r\n" 2 字节
            size_t size = 72;
            for (HttpResponse::MapType::const_iterator it = headers.begin();
                 it != headers.end(); ++it)
            {
                // "key: value\r\n"
                size += it->first.size() + 2 + it->second.size() + 2;
            }
            for (size_t i = 0; i < setCookies.size(); ++i)
            {
                // "set-cookie: value\r\n"
                size += 12 + setCookies[i].size() + 2;
            }
            return size;
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
        std::string reason = m_reason.empty() ? HttpStatusToString(m_status) : m_reason;

        // 预估总大小，一次 reserve 到位，避免多次扩容
        size_t estimated = EstimateHeadersSize(m_headers, m_setCookies);
        std::string result;
        result.reserve(estimated);

        // 状态行：HTTP/1.1 200 OK\r\n
        result.append(getVersionString());
        result.append(" ");
        result.append(std::to_string(static_cast<int>(m_status)));
        result.append(" ");
        result.append(reason);
        result.append("\r\n");

        // 写出所有普通响应头
        bool has_connection = false;
        for (MapType::const_iterator it = m_headers.begin(); it != m_headers.end(); ++it)
        {
            if (it->first == "connection")
            {
                has_connection = true;
            }
            result.append(it->first);
            result.append(": ");
            result.append(it->second);
            result.append("\r\n");
        }

        // 若用户未显式给 Connection，则按 keepalive 语义自动补
        if (!has_connection)
        {
            result.append("connection: ");
            result.append(m_keepalive ? "keep-alive" : "close");
            result.append("\r\n");
        }
        // 逐条写 Set-Cookie
        for (size_t i = 0; i < m_setCookies.size(); ++i)
        {
            result.append("set-cookie: ");
            result.append(m_setCookies[i]);
            result.append("\r\n");
        }
        // 头部结束空行
        result.append("\r\n");
        return result;
    }

    std::string HttpResponse::toString() const
    {
        std::string reason = m_reason.empty() ? HttpStatusToString(m_status) : m_reason;

        // 预估总大小 = header 部分 + body，一次 reserve 到位
        size_t estimated = EstimateHeadersSize(m_headers, m_setCookies) + m_body.size();
        std::string result;
        result.reserve(estimated);

        // 状态行
        result.append(getVersionString());
        result.append(" ");
        result.append(std::to_string(static_cast<int>(m_status)));
        result.append(" ");
        result.append(reason);
        result.append("\r\n");

        // 写普通响应头，同时检测是否已有 Content-Length / Connection
        bool has_content_length = false;
        bool has_connection = false;
        for (MapType::const_iterator it = m_headers.begin(); it != m_headers.end(); ++it)
        {
            if (it->first == "content-length")
            {
                has_content_length = true;
            }
            else if (it->first == "connection")
            {
                has_connection = true;
            }
            result.append(it->first);
            result.append(": ");
            result.append(it->second);
            result.append("\r\n");
        }

        // 自动补齐 Connection
        if (!has_connection)
        {
            result.append("connection: ");
            result.append(m_keepalive ? "keep-alive" : "close");
            result.append("\r\n");
        }
        // 自动补齐 Content-Length
        if (!has_content_length)
        {
            result.append("content-length: ");
            result.append(std::to_string(m_body.size()));
            result.append("\r\n");
        }
        // 写所有 Set-Cookie 头
        for (size_t i = 0; i < m_setCookies.size(); ++i)
        {
            result.append("set-cookie: ");
            result.append(m_setCookies[i]);
            result.append("\r\n");
        }
        // 头部结束空行
        result.append("\r\n");
        // 追加响应体正文
        result.append(m_body);
        return result;
    }

} // namespace http
