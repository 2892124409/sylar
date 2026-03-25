#include "http/ssl/ssl_socket.h"

#include "log/logger.h"
#include "sylar/concurrency/thread.h"
#include "sylar/fiber/fd_manager.h"

#include <errno.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <unistd.h>

namespace http
{
namespace ssl
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

namespace
{

/**
 * @brief 归一化 SSL I/O 返回值，并给出需要等待的事件
 */
static int HandleSslIoResult(SSL* ssl, int rt)
{
    // rt > 0 代表本次读/写成功处理的字节数。
    if (rt > 0)
    {
        return rt;
    }
    // 将 OpenSSL 返回值转换为错误类型。
    int err = SSL_get_error(ssl, rt);
    // 对端正常关闭 TLS 会话，按 EOF 语义返回 0。
    if (err == SSL_ERROR_ZERO_RETURN)
    {
        return 0;
    }
    // 非阻塞/协程场景下需要等待读写条件后重试。
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
    {
        return -2;
    }
    // 某些 nonblocking + hook 组合下会走 SYSCALL 并带 EAGAIN/EWOULDBLOCK，
    // 这类场景与 WANT_READ/WANT_WRITE 等价，应该重试而非直接失败。
    if (err == SSL_ERROR_SYSCALL &&
        (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    {
        return -2;
    }
    // 其他错误统一映射为失败。
    return -1;
}

} // namespace

/** @brief 构造 SSL Socket */
SslSocket::SslSocket(SslContext::ptr ctx, SslMode mode, int family)
    // 仍然基于 TCP 套接字工作（TLS 依赖连接语义）。
    : Socket(family, Socket::TCP, 0)
      // 保存共享 SSL 上下文。
      ,
      m_ctx(ctx)
      // 保存当前工作模式。
      ,
      m_mode(mode)
      // 构造时尚未创建 SSL*。
      ,
      m_ssl(nullptr)
      // 构造时握手状态为未完成。
      ,
      m_handshakeDone(false)
{
}

/** @brief 析构并释放资源 */
SslSocket::~SslSocket()
{
    close();
}

/** @brief 创建未连接 SSL Socket */
SslSocket::ptr SslSocket::CreateTCPSocket(SslContext::ptr ctx, SslMode mode, int family)
{
    // 分配对象实例。
    SslSocket::ptr sock(new SslSocket(ctx, mode, family));
    // 创建底层 fd。
    sock->newSock();
    // 返回可继续 connect/handshake 的对象。
    return sock;
}

/** @brief 从已有 Socket 包装 SSL Socket */
SslSocket::ptr SslSocket::FromSocket(sylar::Socket::ptr socket, SslContext::ptr ctx, SslMode mode)
{
    if (!socket || !ctx || !ctx->getNativeHandle())
    {
        // 入参不完整时直接失败。
        return nullptr;
    }

    int fd = ::dup(socket->getSocket());
    if (fd < 0)
    {
        // dup 失败说明无法安全接管已有 fd。
        BASE_LOG_ERROR(g_logger) << "dup socket for ssl failed errno=" << errno;
        return nullptr;
    }

    SslSocket::ptr ssl_socket(new SslSocket(ctx, mode, socket->getFamily()));
    // dup 出来的 fd 不一定已经在 FdManager 中注册（尤其是 upstream_ref 变体），
    // 这里显式创建上下文，确保后续 Socket::init(fd) 能成功接管。
#ifdef SYLAR_NET_VARIANT_UPSTREAM_REF
    if (!sylar::FdMgr::GetInstance()->get(fd, true))
    {
        BASE_LOG_ERROR(g_logger) << "create fd_ctx for ssl socket failed fd=" << fd;
        ::close(fd);
        return nullptr;
    }
#else
    sylar::FdCtx::ptr fd_ctx = sylar::FdMgr::GetInstance()->get(fd, true);
    if (!fd_ctx || fd_ctx->isClose())
    {
        BASE_LOG_ERROR(g_logger) << "create fd_ctx for ssl socket failed fd=" << fd;
        ::close(fd);
        return nullptr;
    }
    if (!fd_ctx->bindAffinityIfUnset(sylar::GetThreadId()))
    {
        BASE_LOG_ERROR(g_logger) << "bind ssl socket affinity failed"
                                 << " fd=" << fd
                                 << " current_tid=" << sylar::GetThreadId()
                                 << " affinity_tid=" << fd_ctx->getAffinityThread();
        ::close(fd);
        return nullptr;
    }
#endif
    if (!ssl_socket->init(fd))
    {
        BASE_LOG_ERROR(g_logger) << "init ssl socket from fd failed fd=" << fd;
        // init 失败需手动关闭 dup 出来的 fd。
        ::close(fd);
        return nullptr;
    }
    if (!ssl_socket->initializeSsl())
    {
        // SSL 初始化失败统一走 close 做资源清理。
        ssl_socket->close();
        return nullptr;
    }
    // 返回已完成 SSL* 绑定的对象（握手可后续执行）。
    return ssl_socket;
}

/** @brief 客户端连接并握手 */
bool SslSocket::connect(const sylar::Address::ptr addr, uint64_t timeout_ms)
{
    if (!Socket::connect(addr, timeout_ms))
    {
        // TCP 连接失败。
        return false;
    }
    if (!initializeSsl())
    {
        // 创建/绑定 SSL* 失败。
        return false;
    }
    // TCP 已连通，继续完成 TLS 握手。
    return handshake();
}

/** @brief 关闭 TLS 会话及底层 fd */
bool SslSocket::close()
{
    m_handshakeDone = false;
    if (m_ssl)
    {
        // 主动发送 TLS close_notify（最佳努力）。
        SSL_shutdown(m_ssl);
        // 释放连接会话对象。
        SSL_free(m_ssl);
        // 置空避免重复释放。
        m_ssl = nullptr;
    }
    // 关闭底层 socket。
    return Socket::close();
}

/** @brief TLS 连续缓冲区发送 */
int SslSocket::send(const void* buffer, size_t length, int)
{
    if (!m_ssl || !m_handshakeDone)
    {
        // 未握手成功前禁止数据面读写。
        return -1;
    }
    while (true)
    {
        // 通过 TLS 会话写入密文数据。
        int rt = SSL_write(m_ssl, buffer, static_cast<int>(length));
        // 统一错误语义。
        int result = HandleSslIoResult(m_ssl, rt);
        if (result == -2)
        {
            // WANT_READ/WANT_WRITE 时重试。
            continue;
        }
        // 返回成功字节数/EOF/失败。
        return result;
    }
}

/** @brief TLS iovec 发送 */
int SslSocket::send(const iovec* buffers, size_t length, int flags)
{
    std::string data;
    for (size_t i = 0; i < length; ++i)
    {
        // 把分散缓冲拼成连续块，复用连续发送逻辑。
        data.append(static_cast<const char*>(buffers[i].iov_base), buffers[i].iov_len);
    }
    return send(data.data(), data.size(), flags);
}

/** @brief TLS 连续缓冲区接收 */
int SslSocket::recv(void* buffer, size_t length, int)
{
    if (!m_ssl || !m_handshakeDone)
    {
        // 未握手成功前禁止数据面读写。
        return -1;
    }
    while (true)
    {
        // 通过 TLS 会话读取并解密数据。
        int rt = SSL_read(m_ssl, buffer, static_cast<int>(length));
        // 统一错误语义。
        int result = HandleSslIoResult(m_ssl, rt);
        if (result == -2)
        {
            // WANT_READ/WANT_WRITE 时重试。
            continue;
        }
        // 返回成功字节数/EOF/失败。
        return result;
    }
}

/** @brief TLS iovec 接收 */
int SslSocket::recv(iovec* buffers, size_t length, int flags)
{
    size_t total = 0;
    for (size_t i = 0; i < length; ++i)
    {
        // 统计目标 iovec 的总容量。
        total += buffers[i].iov_len;
    }
    // 先用连续缓冲承接读取结果。
    std::string data(total, '\0');
    int rt = recv(&data[0], total, flags);
    if (rt <= 0)
    {
        // 失败或 EOF 直接透传。
        return rt;
    }

    size_t copied = 0;
    for (size_t i = 0; i < length && copied < static_cast<size_t>(rt); ++i)
    {
        // 当前段最多可拷贝字节数。
        size_t chunk = std::min(buffers[i].iov_len, static_cast<size_t>(rt) - copied);
        // 执行数据回填。
        memcpy(buffers[i].iov_base, data.data() + copied, chunk);
        // 更新累计偏移。
        copied += chunk;
    }
    // 返回实际接收字节数。
    return rt;
}

/** @brief 执行 TLS 握手 */
bool SslSocket::handshake()
{
    if (!m_ssl && !initializeSsl())
    {
        // 无法初始化 SSL* 时握手失败。
        return false;
    }
    while (true)
    {
        // SERVER 走 SSL_accept，CLIENT 走 SSL_connect。
        int rt = (m_mode == SslMode::SERVER) ? SSL_accept(m_ssl) : SSL_connect(m_ssl);
        if (rt == 1)
        {
            // 握手完成后才能进入应用数据阶段。
            m_handshakeDone = true;
            return true;
        }
        // 查询具体错误原因。
        int err = SSL_get_error(m_ssl, rt);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
        {
            // 需要等待 I/O 条件后继续握手。
            continue;
        }
        if (err == SSL_ERROR_SYSCALL &&
            (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        {
            continue;
        }
        // 其他错误直接判定握手失败。
        BASE_LOG_ERROR(g_logger) << "SSL handshake failed err=" << err
                                 << " errno=" << errno;
        return false;
    }
}

/** @brief 初始化 SSL* 并绑定 fd */
bool SslSocket::initializeSsl()
{
    if (m_ssl)
    {
        // 已初始化则直接复用。
        return true;
    }
    if (!m_ctx || !m_ctx->getNativeHandle() || !isValid())
    {
        // 上下文或 socket 无效。
        return false;
    }
    // 由共享 SSL_CTX 创建单连接 SSL*。
    m_ssl = SSL_new(m_ctx->getNativeHandle());
    if (!m_ssl)
    {
        BASE_LOG_ERROR(g_logger) << "SSL_new failed";
        return false;
    }
    // 把 SSL* 与当前 fd 绑定，后续 SSL_read/write 才能走底层 I/O。
    if (SSL_set_fd(m_ssl, getSocket()) != 1)
    {
        BASE_LOG_ERROR(g_logger) << "SSL_set_fd failed";
        // 绑定失败时立即释放避免泄漏。
        SSL_free(m_ssl);
        m_ssl = nullptr;
        return false;
    }
    // SSL 会话创建并绑定成功。
    return true;
}

} // namespace ssl
} // namespace http
