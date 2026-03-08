#include "sylar/http/http_server.h"

#include "sylar/log/logger.h"

namespace sylar
{
    namespace http
    {

        static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

        HttpServer::HttpServer(IOManager *io_worker, IOManager *accept_worker)
            : sylar::net::TcpServer(io_worker, accept_worker), m_dispatch(new ServletDispatch()), m_sessionManager(new SessionManager())
        {
            setName("sylar-http-server");
        }

        void HttpServer::handleClient(Socket::ptr client)
        {
            // 把底层 TCP 连接包装成 HttpSession，后续 HTTP 收发都经由它完成。
            HttpSession::ptr session(new HttpSession(client));

            // keep-alive 场景下，一个连接可承载多次请求，因此循环处理直到需要断开。
            while (!isStop())
            {
                // 从连接读取并解析一条 HTTP 请求。
                // 成功返回 request，失败/断连返回空。
                HttpRequest::ptr request = session->recvRequest();
                if (!request)
                {
                    // 若为空且存在解析错误，返回 400 并附带错误信息。
                    if (session->hasParserError())
                    {
                        HttpResponse::ptr response(new HttpResponse());
                        // 非法请求：400
                        response->setStatus(HttpStatus::BAD_REQUEST);
                        // 出错后不再保持连接
                        response->setKeepAlive(false);
                        response->setHeader("Content-Type", "text/plain; charset=utf-8");
                        response->setBody("400 Bad Request\n" + session->getParserError());
                        // 尝试把 400 发回客户端
                        session->sendResponse(response);
                    }
                    // 读取失败、对端关闭或解析失败都结束当前连接处理。
                    break;
                }

                // 构造本次请求对应的响应对象。
                HttpResponse::ptr response(new HttpResponse());
                // 响应版本跟随请求版本。
                response->setVersion(request->getVersionMajor(), request->getVersionMinor());
                // 默认 keep-alive 语义跟随请求。
                response->setKeepAlive(request->isKeepAlive());

                // 基于请求中的 SID 获取或创建服务端会话；必要时写回 Set-Cookie。
                Session::ptr http_session = m_sessionManager->getOrCreate(request, response);
                // 当前函数暂未直接使用该变量，保留会话创建/续期副作用。
                (void)http_session;

                // 路由分发：按 path 找到目标 Servlet，并执行业务 handle。
                m_dispatch->handle(request, response, session);

                // 发送响应；发送失败通常意味着连接不可用，结束循环。
                if (session->sendResponse(response) <= 0)
                {
                    break;
                }

                // 只有请求和响应都允许 keep-alive 才继续处理下一条请求。
                if (!request->isKeepAlive() || !response->isKeepAlive())
                {
                    break;
                }
            }

            // 跳出循环后关闭连接并记录日志。
            session->close();
            SYLAR_LOG_INFO(g_logger) << "HttpServer client closed";
        }

    } // namespace http
} // namespace sylar
