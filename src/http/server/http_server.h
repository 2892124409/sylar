#ifndef __SYLAR_HTTP_HTTP_SERVER_H__
#define __SYLAR_HTTP_HTTP_SERVER_H__

#include "http/router/servlet.h"
#include "http/session/session_manager.h"
#include "http/ssl/ssl_config.h"
#include "http/ssl/ssl_context.h"
#include "sylar/fiber/iomanager.h"
#include "sylar/net/tcp_server.h"

#include <atomic>

namespace http
{

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
class HttpServer : public sylar::net::TcpServer
{
  public:
    typedef std::shared_ptr<HttpServer> ptr;

    /**
     * @brief 构造 HTTP 服务器
     * @param io_worker 处理 HTTP 业务的调度器
     * @param accept_worker 负责 accept 的调度器
     */
    HttpServer(sylar::IOManager* io_worker = sylar::IOManager::GetThis(),
               sylar::IOManager* accept_worker = sylar::IOManager::GetThis());

    /**
     * @brief 按 HttpFrameworkConfig 创建带内部 worker 的 HTTP 服务器
     * @details
     * 该入口会读取 `http.io_worker_threads` 和 `http.accept_worker_threads`
     * 并为当前 HttpServer 托管两个 IOManager 的生命周期。
     * 已显式传入 worker 的构造路径不受这些配置项影响。
     */
    static ptr CreateWithConfig();

    /// 返回路由分发器，业务层通过它注册接口
    ServletDispatch::ptr getServletDispatch() const
    {
        return m_dispatch;
    }

    /// 返回 Session 管理器，便于后续业务或框架扩展使用
    SessionManager::ptr getSessionManager() const
    {
        return m_sessionManager;
    }

    /// 返回当前活跃连接数，便于调试和压测观测
    size_t getActiveConnections() const
    {
        return m_activeConnections.load(std::memory_order_acquire);
    }

    /// 直接向分发器注册 middleware 的便捷入口。
    void addMiddleware(Middleware::ptr middleware)
    {
        m_dispatch->addMiddleware(middleware);
    }

    /// 配置 HTTPS/SSL，成功后当前 HttpServer 将按 HTTPS 方式处理连接。
    bool setSslConfig(const ssl::SslConfig& config);

    /// 当前服务器是否已启用 SSL。
    bool isSSLEnabled() const
    {
        return m_sslContext != nullptr;
    }

    /// 停止 HTTP 服务并关闭 SessionManager 的后台清理定时器
    virtual void stop() override;

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
    virtual void handleClient(sylar::Socket::ptr client) override;

  private:
    /// 路由分发器：负责将请求路径分发到目标 Servlet
    ServletDispatch::ptr m_dispatch;
    /// Session 管理器：负责基于 SID 的会话创建、查找和续期
    SessionManager::ptr m_sessionManager;
    /// HTTPS SSL 上下文；为空表示当前服务走普通 HTTP。
    ssl::SslContext::ptr m_sslContext;
    /// 当前活跃连接数，用于执行 max_connections 限制。
    std::atomic<size_t> m_activeConnections;
    /// 通过 CreateWithConfig 托管的业务 IO worker。
    sylar::IOManager::ptr m_ownedIoWorker;
    /// 通过 CreateWithConfig 托管的 accept IO worker。
    sylar::IOManager::ptr m_ownedAcceptWorker;
};

} // namespace http

#endif
