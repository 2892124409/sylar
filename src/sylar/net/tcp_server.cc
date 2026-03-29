/**
 * @file tcp_server.cc
 * @brief TCP 服务器实现
 */

#include "sylar/net/tcp_server.h"
#include "sylar/concurrency/thread.h"
#include "sylar/fiber/fd_manager.h"
#include "sylar/log/logger.h"
#include <climits>
#include <cstring>
#include <sstream>

namespace sylar
{
    namespace net
    {

        // 全局日志器
        static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

        // ============================================================================
        // 构造函数与析构函数
        // ============================================================================

        TcpServer::TcpServer(IOManager *io_worker,
                             IOManager *accept_worker)
            : m_ioWorker(io_worker), m_acceptWorker(accept_worker), m_recvTimeout(60 * 1000 * 2) // 默认 2 分钟
              ,
              m_name("sylar/1.0.0"), m_type("tcp"), m_isStop(true)
        {
        }

        TcpServer::~TcpServer()
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

        bool TcpServer::bind(Address::ptr addr)
        {
            std::vector<Address::ptr> addrs;
            std::vector<Address::ptr> fails;
            addrs.push_back(addr);
            return bind(addrs, fails);
        }

        bool TcpServer::bind(const std::vector<Address::ptr> &addrs,
                             std::vector<Address::ptr> &fails)
        {
            std::vector<Socket::ptr> new_socks;
            for (auto &addr : addrs)
            {
                // 创建 TCP Socket
                Socket::ptr sock = Socket::CreateTCP(addr);
                if (!sock)
                {
                    SYLAR_LOG_ERROR(g_logger) << "bind create socket fail: "
                                              << addr->toString();
                    fails.push_back(addr);
                    continue;
                }

                // 绑定地址
                if (!sock->bind(addr))
                {
                    SYLAR_LOG_ERROR(g_logger) << "bind fail: " << addr->toString();
                    fails.push_back(addr);
                    continue;
                }

                // 开始监听
                if (!sock->listen())
                {
                    SYLAR_LOG_ERROR(g_logger) << "listen fail: " << addr->toString();
                    fails.push_back(addr);
                    continue;
                }

                new_socks.push_back(sock);
                SYLAR_LOG_INFO(g_logger) << "server bind success: " << addr->toString();
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

        bool TcpServer::start()
        {
            if (!m_ioWorker || !m_acceptWorker)
            {
                SYLAR_LOG_ERROR(g_logger) << "TcpServer start failed: io_worker or accept_worker is null";
                return false;
            }
            if (m_ioWorker == m_acceptWorker)
            {
                SYLAR_LOG_ERROR(g_logger) << "TcpServer start failed: accept_worker and io_worker must be different IOManager instances";
                return false;
            }

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

            // 为每个监听 Socket 启动 accept 协程
            for (auto &sock : socks)
            {
                // std::bind 将成员换函数和参数绑定好，生成一个无参可调用对象（函数）
                m_acceptWorker->schedule(std::bind(&TcpServer::startAccept,
                                                   shared_from_this(), sock));
            }
            return true;
        }

        void TcpServer::stop()
        {
            bool expected = false;
            if (!m_isStop.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return; // 已经停止
            }

            auto self = shared_from_this();

            // 异步清理资源（避免死锁）
            m_acceptWorker->schedule([this, self]()
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
        SYLAR_LOG_INFO(g_logger) << "TcpServer cleanup completed"; });

            SYLAR_LOG_INFO(g_logger) << "TcpServer stop scheduled";
        }

        // ============================================================================
        // 客户端处理
        // ============================================================================

        void TcpServer::handleClient(Socket::ptr client)
        {
            SYLAR_LOG_INFO(g_logger) << "handleClient: " << *client;
            // 默认实现：只打印日志
            // 子类可以重写此方法实现具体业务逻辑
        }

        int TcpServer::selectIoThreadForNewClient()
        {
            if (!m_ioWorker)
            {
                return -1;
            }

            std::vector<sylar::Scheduler::WorkerStats> stats = m_ioWorker->getWorkerStatsSnapshot();
            if (stats.empty())
            {
                return -1;
            }

            std::lock_guard<std::mutex> lock(m_ioBalanceMutex);
            uint64_t best_score = ULLONG_MAX;
            std::vector<int> candidates;
            candidates.reserve(stats.size());

            for (size_t i = 0; i < stats.size(); ++i)
            {
                const sylar::Scheduler::WorkerStats& stat = stats[i];
                if (stat.threadId <= 0)
                {
                    continue;
                }
                uint64_t inflight = 0;
                auto it = m_inflightClientsByThread.find(stat.threadId);
                if (it != m_inflightClientsByThread.end())
                {
                    inflight = it->second;
                }

                // 优先平衡连接负载，队列长度用于细粒度打散。
                uint64_t score = inflight * 8ull + static_cast<uint64_t>(stat.queuedTasks);
                if (score < best_score)
                {
                    best_score = score;
                    candidates.clear();
                    candidates.push_back(stat.threadId);
                }
                else if (score == best_score)
                {
                    candidates.push_back(stat.threadId);
                }
            }

            if (candidates.empty())
            {
                return -1;
            }
            int picked = candidates[m_ioSelectSeq % candidates.size()];
            ++m_ioSelectSeq;
            return picked;
        }

        void TcpServer::onClientScheduled(int thread_id)
        {
            if (thread_id <= 0)
            {
                return;
            }
            std::lock_guard<std::mutex> lock(m_ioBalanceMutex);
            ++m_inflightClientsByThread[thread_id];
        }

        void TcpServer::onClientFinished(int thread_id)
        {
            if (thread_id <= 0)
            {
                return;
            }
            std::lock_guard<std::mutex> lock(m_ioBalanceMutex);
            auto it = m_inflightClientsByThread.find(thread_id);
            if (it == m_inflightClientsByThread.end())
            {
                return;
            }
            if (it->second > 0)
            {
                --it->second;
            }
            if (it->second == 0)
            {
                m_inflightClientsByThread.erase(it);
            }
        }

        void TcpServer::startAccept(Socket::ptr sock)
        {
            while (!m_isStop.load(std::memory_order_acquire))
            {
                Socket::ptr client = sock->accept();
                if (client)
                {
                    client->setRecvTimeout(m_recvTimeout);
                    int target_thread = selectIoThreadForNewClient();
                    onClientScheduled(target_thread);
                    auto self = shared_from_this();
                    m_ioWorker->schedule([self, client, target_thread]()
                                         {
                                             std::shared_ptr<void> load_guard(nullptr, [self, target_thread](void *) {
                                                 self->onClientFinished(target_thread);
                                             });
                                             int fd = client->getSocket();
                                             sylar::FdCtx::ptr fd_ctx = sylar::FdMgr::GetInstance()->get(fd, true);
                                             if (!fd_ctx || fd_ctx->isClose())
                                             {
                                                 SYLAR_LOG_ERROR(g_logger) << "bind client affinity failed: invalid fd_ctx fd=" << fd;
                                                 client->close();
                                                 return;
                                             }
                                             if (!fd_ctx->bindAffinityIfUnset(sylar::GetThreadId()))
                                             {
                                                 SYLAR_LOG_ERROR(g_logger) << "bind client affinity failed: mismatch"
                                                                           << " fd=" << fd
                                                                           << " current_tid=" << sylar::GetThreadId()
                                                                           << " affinity_tid=" << fd_ctx->getAffinityThread();
                                                 client->close();
                                                 return;
                                             }
                                             self->handleClient(client);
                                         },
                                         target_thread);
                }
                else
                {
                    // accept 失败，检查是否应该退出
                    if (m_isStop.load(std::memory_order_acquire))
                    {
                        break;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        continue;
                    }
                    SYLAR_LOG_ERROR(g_logger) << "accept errno=" << errno
                                              << " str=" << strerror(errno);
                    // 如果是致命错误（socket已关闭），退出循环
                    if (errno == EBADF || errno == EINVAL || errno == ENOTSOCK)
                    {
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

        std::string TcpServer::toString(const std::string &prefix)
        {
            std::vector<Socket::ptr> socks;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                socks = m_socks;
            }

            std::stringstream ss;
            ss << prefix << "[TcpServer name=" << m_name
               << " type=" << m_type
               << " recvTimeout=" << m_recvTimeout
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
