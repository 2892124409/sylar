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

            class SslSocket : public sylar::Socket
            {
            public:
                typedef std::shared_ptr<SslSocket> ptr;

                SslSocket(SslContext::ptr ctx, SslMode mode, int family = Socket::IPv4);
                virtual ~SslSocket();

                static SslSocket::ptr CreateTCPSocket(SslContext::ptr ctx, SslMode mode, int family = Socket::IPv4);
                static SslSocket::ptr FromSocket(Socket::ptr socket, SslContext::ptr ctx, SslMode mode);

                virtual bool connect(const Address::ptr addr, uint64_t timeout_ms = -1) override;
                virtual bool close() override;

                virtual int send(const void *buffer, size_t length, int flags = 0) override;
                virtual int send(const iovec *buffers, size_t length, int flags = 0) override;
                virtual int recv(void *buffer, size_t length, int flags = 0) override;
                virtual int recv(iovec *buffers, size_t length, int flags = 0) override;

                bool handshake();
                bool isHandshakeDone() const { return m_handshakeDone; }

            private:
                bool initializeSsl();

            private:
                SslContext::ptr m_ctx;
                SslMode m_mode;
                SSL *m_ssl;
                bool m_handshakeDone;
            };

        } // namespace ssl
    } // namespace http
} // namespace sylar

#endif
