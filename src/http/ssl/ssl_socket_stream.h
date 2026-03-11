#ifndef __SYLAR_HTTP_SSL_SSL_SOCKET_STREAM_H__
#define __SYLAR_HTTP_SSL_SSL_SOCKET_STREAM_H__

#include "http/ssl/ssl_socket.h"
#include "sylar/net/socket_stream.h"

namespace http
{
    namespace ssl
    {

        /**
         * @brief SSL Socket 的 Stream 适配器
         * @details
         * 复用 SocketStream 读写接口，使上层可按 Stream 抽象操作 TLS 连接。
         */
        class SslSocketStream : public sylar::SocketStream
        {
        public:
            typedef std::shared_ptr<SslSocketStream> ptr;

            /**
             * @brief 构造函数
             * @param sock  SSL 套接字
             * @param owner 是否由当前对象持有并管理 socket 生命周期
             */
            SslSocketStream(SslSocket::ptr sock, bool owner = true)
                // 复用 SocketStream 构造逻辑，把 SslSocket 作为底层 socket 注入。
                : SocketStream(sock, owner)
            {
                // 无额外初始化。
            }

            /** @brief 获取底层 SslSocket */
            SslSocket::ptr getSslSocket() const
            {
                // 从基类保存的通用 Socket::ptr 动态转换回 SslSocket::ptr。
                return std::dynamic_pointer_cast<SslSocket>(m_socket);
            }
        };

    } // namespace ssl
} // namespace http

#endif
