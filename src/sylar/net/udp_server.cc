/**
 * @file udp_server.cc
 * @brief UDP 服务器实现
 */

#include "sylar/net/udp_server.h"
#include "log/logger.h"
#include <cstring>
#include <sstream>

namespace sylar
{
    namespace net
    {

        // 全局日志器
        static Logger::ptr g_logger = BASE_LOG_NAME("system");

        // ============================================================================
        // 构造函数与析构函数
        // ============================================================================

        UdpServer::UdpServer(IOManager *io_worker,
                             IOManager *recv_worker)
            : m_ioWorker(io_worker), m_recvWorker(recv_worker), m_recvTimeout(60 * 1000 * 2) // 默认 2 分钟
              ,
              m_name("sylar/1.0.0"), m_type("udp"), m_isStop(true), m_bufferSize(65536)
        { // UDP 最大 64KB
        }

        UdpServer::~UdpServer()
        {
            std::vector<Socket::ptr> socks;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_isStop.store(true, std::memory_order_release);
                socks.swap(m_socks);
            }
            for (auto &sock : socks)
            {
                sock->close();
            }
        }

        // ============================================================================
        // 绑定方法
        // ============================================================================

        bool UdpServer::bind(Address::ptr addr)
        {
            std::vector<Address::ptr> addrs;
            std::vector<Address::ptr> fails;
            addrs.push_back(addr);
            return bind(addrs, fails);
        }

        bool UdpServer::bind(const std::vector<Address::ptr> &addrs,
                             std::vector<Address::ptr> &fails)
        {
            std::vector<Socket::ptr> new_socks;
            for (auto &addr : addrs)
            {
                // 创建 UDP Socket
                Socket::ptr sock = Socket::CreateUDP(addr);
                if (!sock)
                {
                    BASE_LOG_ERROR(g_logger) << "bind create socket fail: "
                                              << addr->toString();
                    fails.push_back(addr);
                    continue;
                }

                // 绑定地址（UDP 不需要 listen）
                if (!sock->bind(addr))
                {
                    BASE_LOG_ERROR(g_logger) << "bind fail: " << addr->toString();
                    fails.push_back(addr);
                    continue;
                }

                new_socks.push_back(sock);
                BASE_LOG_INFO(g_logger) << "udp server bind success: " << addr->toString();
            }

            // 如果有失败的绑定，清空所有 Socket
            if (!fails.empty())
            {
                for (auto &sock : new_socks)
                {
                    sock->close();
                }
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto &sock : m_socks)
                {
                    sock->close();
                }
                m_socks.clear();
                return false;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            m_socks.insert(m_socks.end(), new_socks.begin(), new_socks.end());
            return true;
        }

        // ============================================================================
        // 启动与停止
        // ============================================================================

        bool UdpServer::start()
        {
            bool expected = true;
            if (!m_isStop.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
            {
                return true; // 已经启动
            }

            std::vector<Socket::ptr> socks;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                socks = m_socks;
            }

            // 为每个绑定 Socket 启动 recvfrom 协程
            for (auto &sock : socks)
            {
                m_recvWorker->schedule(std::bind(&UdpServer::startReceive,
                                                 shared_from_this(), sock));
            }
            return true;
        }

        void UdpServer::stop()
        {
            bool expected = false;
            if (!m_isStop.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return; // 已经停止
            }

            auto self = shared_from_this();

            // 异步清理资源（避免死锁）
            m_recvWorker->schedule([this, self]()
                                   {
        std::vector<Socket::ptr> socks;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            socks.swap(m_socks);
        }
        for (auto& sock : socks) {
            sock->cancelAll();  // 取消所有事件
            sock->close();
        }
        BASE_LOG_INFO(g_logger) << "UdpServer cleanup completed"; });

            BASE_LOG_INFO(g_logger) << "UdpServer stop scheduled";
        }

        // ============================================================================
        // 数据发送
        // ============================================================================

        int UdpServer::sendTo(const void *buffer, size_t length, Address::ptr to, size_t sockIndex)
        {
            Socket::ptr sock;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (sockIndex >= m_socks.size())
                {
                    BASE_LOG_ERROR(g_logger) << "sendTo invalid sockIndex=" << sockIndex
                                              << " socks.size()=" << m_socks.size();
                    return -1;
                }
                sock = m_socks[sockIndex];
            }
            if (!sock)
            {
                BASE_LOG_ERROR(g_logger) << "sendTo invalid sockIndex=" << sockIndex
                                          << " sock is null";
                return -1;
            }
            return sock->sendTo(buffer, length, to);
        }

        // ============================================================================
        // 数据报处理
        // ============================================================================

        void UdpServer::handleDatagram(const void *data, size_t len,
                                       Address::ptr from, Socket::ptr sock)
        {
            BASE_LOG_INFO(g_logger) << "handleDatagram from " << from->toString()
                                     << " len=" << len
                                     << " data=" << std::string((const char *)data, len);
            // 默认实现：只打印日志
            // 子类可以重写此方法实现具体业务逻辑
        }

        void UdpServer::startReceive(Socket::ptr sock)
        {
            sock->setRecvTimeout(m_recvTimeout);

            // 分配接收缓冲区
            char *buffer = new char[m_bufferSize];

            while (!m_isStop.load(std::memory_order_acquire))
            {
                Address::ptr from;
                int n = sock->recvFrom(buffer, m_bufferSize, from);
                if (n > 0)
                {
                    // 收到数据，调度到 io_worker 处理
                    // 复制数据以避免缓冲区竞争
                    std::string data(buffer, n);
                    m_ioWorker->schedule([this, data, from, sock]()
                                         { handleDatagram(data.data(), data.size(), from, sock); });
                }
                else if (n < 0)
                {
                    // 出错，检查是否应该退出
                    if (m_isStop.load(std::memory_order_acquire))
                    {
                        break;
                    }
                    BASE_LOG_ERROR(g_logger) << "recvfrom errno=" << errno
                                              << " str=" << strerror(errno);
                    // 如果是致命错误（socket已关闭），退出循环
                    if (errno == EBADF || errno == EINVAL || errno == ENOTSOCK)
                    {
                        BASE_LOG_ERROR(g_logger) << "recvfrom socket error, exit receive loop";
                        break;
                    }
                }
                // n == 0 表示收到空数据报（合法的 UDP 数据报）
            }

            delete[] buffer;
            BASE_LOG_INFO(g_logger) << "startReceive exit";
        }

        // ============================================================================
        // 辅助方法
        // ============================================================================

        std::string UdpServer::toString(const std::string &prefix)
        {
            std::vector<Socket::ptr> socks;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                socks = m_socks;
            }

            std::stringstream ss;
            ss << prefix << "[UdpServer name=" << m_name
               << " type=" << m_type
               << " recvTimeout=" << m_recvTimeout
               << " bufferSize=" << m_bufferSize
               << " isStop=" << m_isStop.load(std::memory_order_acquire)
               << "]" << std::endl;

            for (size_t i = 0; i < socks.size(); ++i)
            {
                ss << prefix << "  sock[" << i << "]: " << socks[i]->toString() << std::endl;
            }
            return ss.str();
        }

    } // namespace net
} // namespace sylar
