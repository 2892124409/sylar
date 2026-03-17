#include "socket.h"
#include "log/logger.h"
#include "sylar/fiber/fd_manager.h"
#include "sylar/fiber/hook.h"
#include "sylar/fiber/iomanager.h"
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace sylar
{

static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

// ============================================================================
// Socket 静态工厂方法实现
// ============================================================================

Socket::ptr Socket::CreateTCP(sylar::Address::ptr address)
{
    if (!address)
    {
        return nullptr;
    }
    Socket::ptr sock(new Socket(address->getFamily(), TCP, 0));
    sock->newSock();
    return sock;
}

Socket::ptr Socket::CreateUDP(sylar::Address::ptr address)
{
    if (!address)
    {
        return nullptr;
    }
    Socket::ptr sock(new Socket(address->getFamily(), UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Socket::ptr Socket::CreateTCPSocket()
{
    Socket::ptr sock(new Socket(IPv4, TCP, 0));
    sock->newSock();
    return sock;
}

Socket::ptr Socket::CreateUDPSocket()
{
    Socket::ptr sock(new Socket(IPv4, UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Socket::ptr Socket::CreateTCPSocket6()
{
    Socket::ptr sock(new Socket(IPv6, TCP, 0));
    sock->newSock();
    return sock;
}

Socket::ptr Socket::CreateUDPSocket6()
{
    Socket::ptr sock(new Socket(IPv6, UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Socket::ptr Socket::CreateUnixTCPSocket()
{
    Socket::ptr sock(new Socket(UNIX, TCP, 0));
    sock->newSock();
    return sock;
}

Socket::ptr Socket::CreateUnixUDPSocket()
{
    Socket::ptr sock(new Socket(UNIX, UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

// ============================================================================
// Socket 构造和析构
// ============================================================================

Socket::Socket(int family, int type, int protocol)
    : m_sock(-1), m_family(family), m_type(type), m_protocol(protocol), m_isConnected(false)
{
}

Socket::~Socket()
{
    close();
}

// ============================================================================
// Socket 初始化方法
// ============================================================================

void Socket::newSock()
{
    m_sock = socket(m_family, m_type, m_protocol);
    if (m_sock < 0)
    {
        BASE_LOG_ERROR(g_logger) << "socket(" << m_family << ", "
                                 << m_type << ", " << m_protocol << ") fail "
                                 << " errno=" << errno
                                 << " errstr=" << strerror(errno);
        return;
    }
    initSock();
}

void Socket::initSock()
{
    int optval = 1;
    setOption(SOL_SOCKET, SO_REUSEADDR, optval);
    if (m_type == TCP)
    {
        setOption(IPPROTO_TCP, TCP_NODELAY, optval);
    }
}

bool Socket::init(int sock)
{
    // 将 socket fd 注册到 FdManager，auto_create=true 会创建 FdCtx 并初始化
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(sock, true);
    if (ctx && ctx->isSocket() && !ctx->isClose())
    {
        m_sock = sock;
        m_isConnected = true;
        initSock(); // 设置 SO_REUSEADDR 和 TCP_NODELAY
        getLocalAddress();
        getRemoteAddress();
        return true;
    }
    return false;
}

// ============================================================================
// 超时控制
// ============================================================================

int64_t Socket::getSendTimeout()
{
    timeval tv = {0, 0};
    if (!getOption(SOL_SOCKET, SO_SNDTIMEO, tv))
    {
        return -1;
    }
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void Socket::setSendTimeout(int64_t v)
{
    timeval tv;
    tv.tv_sec = v / 1000;
    tv.tv_usec = (v % 1000) * 1000;
    setOption(SOL_SOCKET, SO_SNDTIMEO, tv);
}

int64_t Socket::getRecvTimeout()
{
    timeval tv = {0, 0};
    if (!getOption(SOL_SOCKET, SO_RCVTIMEO, tv))
    {
        return -1;
    }
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void Socket::setRecvTimeout(int64_t v)
{
    timeval tv;
    tv.tv_sec = v / 1000;
    tv.tv_usec = (v % 1000) * 1000;
    setOption(SOL_SOCKET, SO_RCVTIMEO, tv);
}

// ============================================================================
// Socket 选项操作
// ============================================================================

bool Socket::getOption(int level, int option, void* result, socklen_t* len)
{
    if (::getsockopt(m_sock, level, option, result, len))
    {
        BASE_LOG_DEBUG(g_logger) << "getsockopt sock=" << m_sock
                                 << " level=" << level
                                 << " option=" << option
                                 << " errno=" << errno
                                 << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

bool Socket::setOption(int level, int option, const void* result, socklen_t len)
{
    if (::setsockopt(m_sock, level, option, result, len))
    {
        BASE_LOG_DEBUG(g_logger) << "setsockopt sock=" << m_sock
                                 << " level=" << level
                                 << " option=" << option
                                 << " errno=" << errno
                                 << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

// ============================================================================
// 连接相关操作
// ============================================================================

Socket::ptr Socket::accept()
{
    sockaddr_in6 addr;
    socklen_t len = sizeof(addr);
    // 调用全局的 accept 函数（会被 hook）
    int client_sock = ::accept(m_sock, (sockaddr*)&addr, &len);
    if (client_sock < 0)
    {
        if (errno == EBADF || errno == EINVAL || errno == ENOTSOCK)
        {
            // 停服关闭监听 fd 时，阻塞中的 accept 会以这些 errno 唤醒。
            // 这种情况由上层 accept loop 正常收尾，不需要错误级别噪声。
            BASE_LOG_DEBUG(g_logger) << "accept sock=" << m_sock
                                     << " errno=" << errno
                                     << " errstr=" << strerror(errno);
        }
        else
        {
            BASE_LOG_ERROR(g_logger) << "accept sock=" << m_sock
                                     << " errno=" << errno
                                     << " errstr=" << strerror(errno);
        }
        return nullptr;
    }
    Socket::ptr client(new Socket(m_family, m_type, m_protocol));
    if (!client->init(client_sock))
    {
        ::close(client_sock);
        return nullptr;
    }
    client->m_isConnected = true;
    client->m_remoteAddress = Address::Create((sockaddr*)&addr, len);
    return client;
}

bool Socket::bind(const Address::ptr addr)
{
    if (!isValid())
    {
        newSock();
        if (!isValid())
        {
            return false;
        }
    }
    if (::bind(m_sock, addr->getAddr(), addr->getAddrLen()))
    {
        BASE_LOG_ERROR(g_logger) << "bind sock=" << m_sock
                                 << " addr=" << addr->toString()
                                 << " errno=" << errno
                                 << " errstr=" << strerror(errno);
        return false;
    }
    m_localAddress = addr;
    return true;
}

bool Socket::connect(const Address::ptr addr, uint64_t timeout_ms)
{
    if (!isValid())
    {
        newSock();
        if (!isValid())
        {
            return false;
        }
    }
    if (timeout_ms != (uint64_t)-1)
    {
        setSendTimeout(timeout_ms);
    }
    if (::connect(m_sock, addr->getAddr(), addr->getAddrLen()))
    {
        BASE_LOG_ERROR(g_logger) << "connect sock=" << m_sock
                                 << " addr=" << addr->toString()
                                 << " errno=" << errno
                                 << " errstr=" << strerror(errno);
        return false;
    }
    m_isConnected = true;
    m_remoteAddress = addr;
    return true;
}

bool Socket::reconnect(uint64_t timeout_ms)
{
    if (!m_remoteAddress)
    {
        return false;
    }
    m_isConnected = false;
    return connect(m_remoteAddress, timeout_ms);
}

bool Socket::listen(int backlog)
{
    if (!isValid())
    {
        return false;
    }
    if (::listen(m_sock, backlog))
    {
        BASE_LOG_ERROR(g_logger) << "listen sock=" << m_sock
                                 << " backlog=" << backlog
                                 << " errno=" << errno
                                 << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

bool Socket::close()
{
    if (!isValid())
    {
        return true; // 已经关闭
    }

    m_isConnected = false;

    if (::close(m_sock) != 0)
    {
        BASE_LOG_ERROR(g_logger) << "close sock=" << m_sock
                                 << " errno=" << errno
                                 << " errstr=" << strerror(errno);
        return false;
    }

    m_sock = -1;
    return true;
}

// ============================================================================
// 数据收发 - send
// ============================================================================

int Socket::send(const void* buffer, size_t length, int flags)
{
    if (isConnected())
    {
        return ::send(m_sock, buffer, length, flags);
    }
    return -1;
}

int Socket::send(const iovec* buffers, size_t length, int flags)
{
    if (isConnected())
    {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_iovlen = length;
        return ::sendmsg(m_sock, &msg, flags);
    }
    return -1;
}

int Socket::sendTo(const void* buffer, size_t length, const Address::ptr to, int flags)
{
    if (!isValid())
    {
        return -1;
    }
    return ::sendto(m_sock, buffer, length, flags, to->getAddr(), to->getAddrLen());
}

int Socket::sendTo(const iovec* buffers, size_t length, const Address::ptr to, int flags)
{
    if (!isValid())
    {
        return -1;
    }
    msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = (iovec*)buffers;
    msg.msg_iovlen = length;
    msg.msg_name = (char*)to->getAddr();
    msg.msg_namelen = to->getAddrLen();
    return ::sendmsg(m_sock, &msg, flags);
}

// ============================================================================
// 数据收发 - recv
// ============================================================================

int Socket::recv(void* buffer, size_t length, int flags)
{
    if (isConnected())
    {
        return ::recv(m_sock, buffer, length, flags);
    }
    return -1;
}

int Socket::recv(iovec* buffers, size_t length, int flags)
{
    if (isConnected())
    {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = buffers;
        msg.msg_iovlen = length;
        return ::recvmsg(m_sock, &msg, flags);
    }
    return -1;
}

int Socket::recvFrom(void* buffer, size_t length, Address::ptr& from, int flags)
{
    if (!isValid())
    {
        return -1;
    }
    sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    int n = ::recvfrom(m_sock, buffer, length, flags, (sockaddr*)&addr, &len);
    if (n > 0)
    {
        from = Address::Create((sockaddr*)&addr, len);
    }
    return n;
}

int Socket::recvFrom(iovec* buffers, size_t length, Address::ptr& from, int flags)
{
    if (!isValid())
    {
        return -1;
    }
    msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = buffers;
    msg.msg_iovlen = length;
    sockaddr_storage addr;
    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);
    int n = ::recvmsg(m_sock, &msg, flags);
    if (n > 0)
    {
        from = Address::Create((sockaddr*)msg.msg_name, msg.msg_namelen);
    }
    return n;
}

// ============================================================================
// 地址信息获取
// ============================================================================

Address::ptr Socket::getRemoteAddress()
{
    if (m_remoteAddress)
    {
        return m_remoteAddress;
    }
    return Address::ptr();
}

Address::ptr Socket::getLocalAddress()
{
    if (m_localAddress)
    {
        return m_localAddress;
    }
    if (!isValid())
    {
        return Address::ptr();
    }
    sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getsockname(m_sock, (sockaddr*)&addr, &len))
    {
        BASE_LOG_ERROR(g_logger) << "getsockname sock=" << m_sock
                                 << " errno=" << errno
                                 << " errstr=" << strerror(errno);
        return Address::ptr();
    }
    m_localAddress = Address::Create((sockaddr*)&addr, len);
    return m_localAddress;
}

// ============================================================================
// 状态查询
// ============================================================================

bool Socket::isValid() const
{
    return m_sock != -1;
}

int Socket::getError()
{
    int error = 0;
    if (!getOption(SOL_SOCKET, SO_ERROR, error))
    {
        return errno;
    }
    return error;
}

// ============================================================================
// 输出和调试
// ============================================================================

std::ostream& Socket::dump(std::ostream& os) const
{
    os << "[Socket sock=" << m_sock
       << " isConnected=" << m_isConnected
       << " family=" << m_family
       << " type=" << m_type
       << " protocol=" << m_protocol
       << " localAddress=" << (m_localAddress ? m_localAddress->toString() : "null")
       << " remoteAddress=" << (m_remoteAddress ? m_remoteAddress->toString() : "null")
       << "]";
    return os;
}

std::string Socket::toString() const
{
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

// ============================================================================
// IO 取消操作
// ============================================================================

bool Socket::cancelRead()
{
    return IOManager::GetThis()->cancelEvent(m_sock, sylar::IOManager::READ);
}

bool Socket::cancelWrite()
{
    return IOManager::GetThis()->cancelEvent(m_sock, sylar::IOManager::WRITE);
}

bool Socket::cancelAccept()
{
    return IOManager::GetThis()->cancelEvent(m_sock, sylar::IOManager::READ);
}

bool Socket::cancelAll()
{
    return IOManager::GetThis()->cancelAll(m_sock);
}

// ============================================================================
// 全局函数
// ============================================================================

std::ostream& operator<<(std::ostream& os, const Socket& sock)
{
    return sock.dump(os);
}

} // namespace sylar
