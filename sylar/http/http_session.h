#ifndef __SYLAR_HTTP_HTTP_SESSION_H__
#define __SYLAR_HTTP_HTTP_SESSION_H__

#include "sylar/http/http_parser.h"
#include "sylar/http/http_response.h"
#include "sylar/net/socket_stream.h"

namespace sylar {
namespace http {

/**
 * @brief HTTP 会话封装
 * @details
 * 它继承自 `SocketStream`，相当于在 TCP 连接之上包了一层 HTTP 语义。
 *
 * `SocketStream` 只知道“读写字节流”；
 * `HttpSession` 则进一步知道：
 * - 如何从字节流里读出一条完整 HTTP 请求
 * - 如何把 `HttpResponse` 写回客户端
 *
 * 它内部维护一个连接级缓冲区 `m_buffer`，用于支持：
 * - 半包
 * - 粘包
 * - keep-alive 下同一连接多个请求
 */
class HttpSession : public sylar::SocketStream {
public:
    typedef std::shared_ptr<HttpSession> ptr;

    /**
     * @brief 构造一个 HTTP 会话
     * @param sock 已建立的 TCP 连接
     * @param owner 是否由当前对象负责关闭 socket
     */
    HttpSession(Socket::ptr sock, bool owner = true);

    /**
     * @brief 接收并解析一条 HTTP 请求
     * @details
     * 内部会循环读 socket，把数据追加到 `m_buffer`，
     * 然后交给 `HttpRequestParser` 尝试解析。
     */
    HttpRequest::ptr recvRequest();

    /**
     * @brief 发送 HTTP 响应
     */
    int sendResponse(HttpResponse::ptr response);

    /// 解析器是否出错
    bool hasParserError() const { return m_parser.hasError(); }

    /// 返回最近一次解析错误原因
    const std::string& getParserError() const { return m_parser.getError(); }

private:
    HttpRequestParser m_parser;
    std::string m_buffer;
};

} // namespace http
} // namespace sylar

#endif
