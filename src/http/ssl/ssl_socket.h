#ifndef __SYLAR_HTTP_SSL_SSL_SOCKET_H__
#define __SYLAR_HTTP_SSL_SSL_SOCKET_H__

#include "http/ssl/ssl_context.h"
#include "sylar/net/socket.h"

typedef struct ssl_st SSL;

namespace sylar
{
    namespace http
    {
        namespace ssl
        {

            /**
             * @brief TLS 连接套接字
             * @details
             * 继承自 Socket，在 TCP 连接之上增加 TLS 会话能力：
             * - initializeSsl(): 创建并绑定 SSL* 会话
             * - handshake(): 执行 TLS 握手
             * - send/recv(): 通过 SSL_write/SSL_read 进行加密收发
             */
            class SslSocket : public sylar::Socket
            {
            public:
                typedef std::shared_ptr<SslSocket> ptr;

                /**
                 * @brief 构造 SSL 套接字
                 * @param ctx SSL 上下文
                 * @param mode 客户端或服务端模式
                 * @param family 地址族
                 */
                SslSocket(SslContext::ptr ctx, SslMode mode, int family = Socket::IPv4);

                /** @brief 析构并释放 SSL 会话资源 */
                virtual ~SslSocket();

                /**
                 * @brief 创建尚未连接的 TCP SslSocket
                 */
                static SslSocket::ptr CreateTCPSocket(SslContext::ptr ctx, SslMode mode, int family = Socket::IPv4);

                /**
                 * @brief 基于已有 Socket 包装出 SslSocket
                 * @details 常用于服务端 accept 后升级 TLS。
                 */
                static SslSocket::ptr FromSocket(Socket::ptr socket, SslContext::ptr ctx, SslMode mode);

                /** @brief TCP 连接后执行 TLS 握手 */
                virtual bool connect(const Address::ptr addr, uint64_t timeout_ms = -1) override;

                /** @brief 关闭 TLS 会话并关闭底层 socket */
                virtual bool close() override;

                /** @brief TLS 连续缓冲区发送 */
                virtual int send(const void *buffer, size_t length, int flags = 0) override;

                /** @brief TLS iovec 发送 */
                virtual int send(const iovec *buffers, size_t length, int flags = 0) override;

                /** @brief TLS 连续缓冲区接收 */
                virtual int recv(void *buffer, size_t length, int flags = 0) override;

                /** @brief TLS iovec 接收 */
                virtual int recv(iovec *buffers, size_t length, int flags = 0) override;

                /** @brief 执行 TLS 握手 */
                bool handshake();

                /** @brief 返回握手是否完成 */
                bool isHandshakeDone() const { return m_handshakeDone; }

            private:
                /** @brief 创建 SSL* 并绑定当前 socket fd */
                bool initializeSsl();

            private:
                SslContext::ptr m_ctx;  ///< 共享 SSL 上下文
                SslMode m_mode;         ///< 当前模式（CLIENT/SERVER）
                SSL *m_ssl;             ///< 单连接 SSL 会话对象
                bool m_handshakeDone;   ///< 握手是否完成
            };

        } // namespace ssl
    } // namespace http
} // namespace sylar

#endif
