#ifndef __SYLAR_HTTP_HTTP_SESSION_H__
#define __SYLAR_HTTP_HTTP_SESSION_H__

#include "http/core/http_context.h"
#include "http/core/http_response.h"
#include "sylar/net/socket_stream.h"

namespace http
{

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
     * 在第五阶段以后，连接级解析状态被下沉到 `HttpContext`：
     * - `HttpContext` 管理 parser + buffer
     * - `HttpSession` 负责连接封装与对外 HTTP 收发接口
     */
    class HttpSession : public sylar::SocketStream
    {
    public:
        typedef std::shared_ptr<HttpSession> ptr;

        /**
         * @brief 构造一个 HTTP 会话
         * @param sock 已建立的 TCP 连接
         * @param owner 是否由当前对象负责关闭 socket
         */
        HttpSession(sylar::Socket::ptr sock, bool owner = true);

        /**
         * @brief 接收并解析一条 HTTP 请求
         * @details
         * 具体解析细节委托给 `HttpContext`，
         * 当前类只保留 HTTP 连接封装职责。
         */
        HttpRequest::ptr recvRequest();

        /**
         * @brief 发送 HTTP 响应
         */
        int sendResponse(HttpResponse::ptr response);

        /// 解析器是否出错
        bool hasParserError() const { return m_context.hasError(); }

        /// 返回最近一次解析错误原因
        const std::string &getParserError() const { return m_context.getError(); }

        /// 最近一次解析错误是否属于请求过大
        bool isRequestTooLarge() const { return m_context.isRequestTooLarge(); }

    private:
        /// HTTP 请求解析上下文：负责连接级 parser/buffer 状态。
        HttpContext m_context;
    };

} // namespace http

#endif
