#ifndef __SYLAR_HTTP_HTTP_SERVER_H__
#define __SYLAR_HTTP_HTTP_SERVER_H__

#include "sylar/http/servlet.h"
#include "sylar/http/session_manager.h"
#include "sylar/net/tcp_server.h"

namespace sylar {
namespace http {

/**
 * @brief HTTP 服务器
 * @details
 * 这个类是整个 HTTP 框架的核心入口。
 *
 * 它复用了现有的 `sylar::net::TcpServer`：
 * - `TcpServer` 负责 accept、连接管理、协程调度
 * - `HttpServer` 负责把连接解释成 HTTP 语义
 *
 * 换句话说：
 * - `TcpServer` 只知道“这是一个 TCP 连接”
 * - `HttpServer` 知道“这是一个 HTTP 客户端连接”
 */
class HttpServer : public sylar::net::TcpServer {
public:
    typedef std::shared_ptr<HttpServer> ptr;

    /**
     * @brief 构造 HTTP 服务器
     * @param io_worker 处理 HTTP 业务的调度器
     * @param accept_worker 负责 accept 的调度器
     */
    HttpServer(IOManager* io_worker = IOManager::GetThis(),
               IOManager* accept_worker = IOManager::GetThis());

    /// 返回路由分发器，业务层通过它注册接口
    ServletDispatch::ptr getServletDispatch() const { return m_dispatch; }

    /// 返回 Session 管理器，便于后续业务或框架扩展使用
    SessionManager::ptr getSessionManager() const { return m_sessionManager; }

protected:
    /**
     * @brief 处理一个客户端连接
     * @details
     * 主流程是：
     * 1. 包装成 `HttpSession`
     * 2. 循环读取请求
     * 3. 交给路由分发器
     * 4. 写回响应
     * 5. 根据 keep-alive 决定是否继续读下一条请求
     */
    virtual void handleClient(Socket::ptr client) override;

private:
    ServletDispatch::ptr m_dispatch;
    SessionManager::ptr m_sessionManager;
};

} // namespace http
} // namespace sylar

#endif
