#ifndef __SYLAR_HTTP_SSL_SSL_SOCKET_STREAM_H__
#define __SYLAR_HTTP_SSL_SSL_SOCKET_STREAM_H__

#include "sylar/http/ssl/ssl_socket.h"
#include "sylar/net/socket_stream.h"

namespace sylar
{
    namespace http
    {
        namespace ssl
        {

            class SslSocketStream : public sylar::SocketStream
            {
            public:
                typedef std::shared_ptr<SslSocketStream> ptr;

                SslSocketStream(SslSocket::ptr sock, bool owner = true)
                    : SocketStream(sock, owner)
                {
                }

                SslSocket::ptr getSslSocket() const
                {
                    return std::dynamic_pointer_cast<SslSocket>(m_socket);
                }
            };

        } // namespace ssl
    } // namespace http
} // namespace sylar

#endif
