#include "http/ssl/ssl_socket.h"

#include "log/logger.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <unistd.h>

namespace sylar
{
    namespace http
    {
        namespace ssl
        {

            static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

            namespace
            {

                static int HandleSslIoResult(SSL *ssl, int rt)
                {
                    if (rt > 0)
                    {
                        return rt;
                    }
                    int err = SSL_get_error(ssl, rt);
                    if (err == SSL_ERROR_ZERO_RETURN)
                    {
                        return 0;
                    }
                    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                    {
                        return -2;
                    }
                    return -1;
                }

            } // namespace

            SslSocket::SslSocket(SslContext::ptr ctx, SslMode mode, int family)
                : Socket(family, Socket::TCP, 0)
                , m_ctx(ctx)
                , m_mode(mode)
                , m_ssl(nullptr)
                , m_handshakeDone(false)
            {
            }

            SslSocket::~SslSocket()
            {
                close();
            }

            SslSocket::ptr SslSocket::CreateTCPSocket(SslContext::ptr ctx, SslMode mode, int family)
            {
                SslSocket::ptr sock(new SslSocket(ctx, mode, family));
                sock->newSock();
                return sock;
            }

            SslSocket::ptr SslSocket::FromSocket(Socket::ptr socket, SslContext::ptr ctx, SslMode mode)
            {
                if (!socket || !ctx || !ctx->getNativeHandle())
                {
                    return nullptr;
                }

                int fd = ::dup(socket->getSocket());
                if (fd < 0)
                {
                    SYLAR_LOG_ERROR(g_logger) << "dup socket for ssl failed errno=" << errno;
                    return nullptr;
                }

                SslSocket::ptr ssl_socket(new SslSocket(ctx, mode, socket->getFamily()));
                if (!ssl_socket->init(fd))
                {
                    ::close(fd);
                    return nullptr;
                }
                if (!ssl_socket->initializeSsl())
                {
                    ssl_socket->close();
                    return nullptr;
                }
                return ssl_socket;
            }

            bool SslSocket::connect(const Address::ptr addr, uint64_t timeout_ms)
            {
                if (!Socket::connect(addr, timeout_ms))
                {
                    return false;
                }
                if (!initializeSsl())
                {
                    return false;
                }
                return handshake();
            }

            bool SslSocket::close()
            {
                m_handshakeDone = false;
                if (m_ssl)
                {
                    SSL_shutdown(m_ssl);
                    SSL_free(m_ssl);
                    m_ssl = nullptr;
                }
                return Socket::close();
            }

            int SslSocket::send(const void *buffer, size_t length, int)
            {
                if (!m_ssl || !m_handshakeDone)
                {
                    return -1;
                }
                while (true)
                {
                    int rt = SSL_write(m_ssl, buffer, static_cast<int>(length));
                    int result = HandleSslIoResult(m_ssl, rt);
                    if (result == -2)
                    {
                        continue;
                    }
                    return result;
                }
            }

            int SslSocket::send(const iovec *buffers, size_t length, int flags)
            {
                std::string data;
                for (size_t i = 0; i < length; ++i)
                {
                    data.append(static_cast<const char *>(buffers[i].iov_base), buffers[i].iov_len);
                }
                return send(data.data(), data.size(), flags);
            }

            int SslSocket::recv(void *buffer, size_t length, int)
            {
                if (!m_ssl || !m_handshakeDone)
                {
                    return -1;
                }
                while (true)
                {
                    int rt = SSL_read(m_ssl, buffer, static_cast<int>(length));
                    int result = HandleSslIoResult(m_ssl, rt);
                    if (result == -2)
                    {
                        continue;
                    }
                    return result;
                }
            }

            int SslSocket::recv(iovec *buffers, size_t length, int flags)
            {
                size_t total = 0;
                for (size_t i = 0; i < length; ++i)
                {
                    total += buffers[i].iov_len;
                }
                std::string data(total, '\0');
                int rt = recv(&data[0], total, flags);
                if (rt <= 0)
                {
                    return rt;
                }

                size_t copied = 0;
                for (size_t i = 0; i < length && copied < static_cast<size_t>(rt); ++i)
                {
                    size_t chunk = std::min(buffers[i].iov_len, static_cast<size_t>(rt) - copied);
                    memcpy(buffers[i].iov_base, data.data() + copied, chunk);
                    copied += chunk;
                }
                return rt;
            }

            bool SslSocket::handshake()
            {
                if (!m_ssl && !initializeSsl())
                {
                    return false;
                }
                while (true)
                {
                    int rt = (m_mode == SslMode::SERVER) ? SSL_accept(m_ssl) : SSL_connect(m_ssl);
                    if (rt == 1)
                    {
                        m_handshakeDone = true;
                        return true;
                    }
                    int err = SSL_get_error(m_ssl, rt);
                    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                    {
                        continue;
                    }
                    SYLAR_LOG_ERROR(g_logger) << "SSL handshake failed err=" << err;
                    return false;
                }
            }

            bool SslSocket::initializeSsl()
            {
                if (m_ssl)
                {
                    return true;
                }
                if (!m_ctx || !m_ctx->getNativeHandle() || !isValid())
                {
                    return false;
                }
                m_ssl = SSL_new(m_ctx->getNativeHandle());
                if (!m_ssl)
                {
                    SYLAR_LOG_ERROR(g_logger) << "SSL_new failed";
                    return false;
                }
                if (SSL_set_fd(m_ssl, getSocket()) != 1)
                {
                    SYLAR_LOG_ERROR(g_logger) << "SSL_set_fd failed";
                    SSL_free(m_ssl);
                    m_ssl = nullptr;
                    return false;
                }
                return true;
            }

        } // namespace ssl
    } // namespace http
} // namespace sylar
