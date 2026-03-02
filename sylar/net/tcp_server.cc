/**
 * @file tcp_server.cc
 * @brief TCP 服务器实现
 */

#include "sylar/net/tcp_server.h"
#include "sylar/log/logger.h"
#include <sstream>

namespace sylar {
namespace net {

// 全局日志器
static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// ============================================================================
// 构造函数与析构函数
// ============================================================================

TcpServer::TcpServer(IOManager* io_worker,
                     IOManager* accept_worker)
    : m_ioWorker(io_worker)
    , m_acceptWorker(accept_worker)
    , m_recvTimeout(60 * 1000 * 2)  // 默认 2 分钟
    , m_name("sylar/1.0.0")
    , m_type("tcp")
    , m_isStop(true) {
}

TcpServer::~TcpServer() {
    // 析构时停止服务器
    for (auto& sock : m_socks) {
        sock->close();
    }
    m_socks.clear();
}

// ============================================================================
// 绑定方法
// ============================================================================

bool TcpServer::bind(Address::ptr addr) {
    std::vector<Address::ptr> addrs;
    std::vector<Address::ptr> fails;
    addrs.push_back(addr);
    return bind(addrs, fails);
}

bool TcpServer::bind(const std::vector<Address::ptr>& addrs,
                     std::vector<Address::ptr>& fails) {
    for (auto& addr : addrs) {
        // 创建 TCP Socket
        Socket::ptr sock = Socket::CreateTCP(addr);
        if (!sock) {
            SYLAR_LOG_ERROR(g_logger) << "bind create socket fail: "
                                      << addr->toString();
            fails.push_back(addr);
            continue;
        }

        // 绑定地址
        if (!sock->bind(addr)) {
            SYLAR_LOG_ERROR(g_logger) << "bind fail: " << addr->toString();
            fails.push_back(addr);
            continue;
        }

        // 开始监听
        if (!sock->listen()) {
            SYLAR_LOG_ERROR(g_logger) << "listen fail: " << addr->toString();
            fails.push_back(addr);
            continue;
        }

        m_socks.push_back(sock);
        SYLAR_LOG_INFO(g_logger) << "server bind success: " << addr->toString();
    }

    // 如果有失败的绑定，清空所有 Socket
    if (!fails.empty()) {
        m_socks.clear();
        return false;
    }

    return true;
}

// ============================================================================
// 启动与停止
// ============================================================================

bool TcpServer::start() {
    if (!m_isStop) {
        return true;  // 已经启动
    }
    m_isStop = false;

    // 为每个监听 Socket 启动 accept 协程
    for (auto& sock : m_socks) {
        m_acceptWorker->schedule(std::bind(&TcpServer::startAccept,
                                           shared_from_this(), sock));
    }
    return true;
}

void TcpServer::stop() {
    if (m_isStop) {
        return;  // 已经停止
    }
    m_isStop = true;

    auto self = shared_from_this();

    // 异步清理资源（避免死锁）
    m_acceptWorker->schedule([this, self]() {
        for (auto& sock : m_socks) {
            sock->cancelAll();  // 取消所有事件
            sock->close();
        }
        m_socks.clear();
        SYLAR_LOG_INFO(g_logger) << "TcpServer cleanup completed";
    });

    SYLAR_LOG_INFO(g_logger) << "TcpServer stop scheduled";
}

// ============================================================================
// 客户端处理
// ============================================================================

void TcpServer::handleClient(Socket::ptr client) {
    SYLAR_LOG_INFO(g_logger) << "handleClient: " << *client;
    // 默认实现：只打印日志
    // 子类可以重写此方法实现具体业务逻辑
}

void TcpServer::startAccept(Socket::ptr sock) {
    while (!m_isStop) {
        Socket::ptr client = sock->accept();
        if (client) {
            client->setRecvTimeout(m_recvTimeout);
            m_ioWorker->schedule(std::bind(&TcpServer::handleClient,
                                           shared_from_this(), client));
        } else {
            // accept 失败，检查是否应该退出
            if (m_isStop) {
                break;
            }
            SYLAR_LOG_ERROR(g_logger) << "accept errno=" << errno
                                      << " str=" << strerror(errno);
            // 如果是致命错误（socket已关闭），退出循环
            if (errno == EBADF || errno == EINVAL || errno == ENOTSOCK) {
                SYLAR_LOG_ERROR(g_logger) << "accept socket error, exit accept loop";
                break;
            }
        }
    }
    SYLAR_LOG_INFO(g_logger) << "startAccept exit";
}

// ============================================================================
// 辅助方法
// ============================================================================

std::string TcpServer::toString(const std::string& prefix) {
    std::stringstream ss;
    ss << prefix << "[TcpServer name=" << m_name
       << " type=" << m_type
       << " recvTimeout=" << m_recvTimeout
       << " isStop=" << m_isStop
       << "]" << std::endl;

    for (size_t i = 0; i < m_socks.size(); ++i) {
        ss << prefix << "  sock[" << i << "]: " << m_socks[i]->toString() << std::endl;
    }
    return ss.str();
}

} // namespace net
} // namespace sylar
