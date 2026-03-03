/**
 * @file tcp_server.h
 * @brief TCP 服务器封装
 */

#ifndef __SYLAR_NET_TCP_SERVER_H__
#define __SYLAR_NET_TCP_SERVER_H__

#include "sylar/net/socket.h"
#include "sylar/fiber/iomanager.h"
#include "sylar/base/noncopyable.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace sylar {
namespace net {
/**
 * @brief TCP 服务器类
 *
 * 功能：
 * 1. 支持绑定多个地址并监听
 * 2. 异步接受客户端连接
 * 3. 使用协程调度器处理客户端请求
 * 4. 支持优雅启动和停止
 */
class TcpServer : public std::enable_shared_from_this<TcpServer>
                , public ::sylar::Noncopyable {
public:
    typedef std::shared_ptr<TcpServer> ptr;

    /**
     * @brief 构造函数
     * @param io_worker 处理客户端连接的协程调度器
     * @param accept_worker 执行 accept 的协程调度器
     */
    TcpServer(IOManager* io_worker = IOManager::GetThis(),
              IOManager* accept_worker = IOManager::GetThis());

    /**
     * @brief 析构函数
     */
    virtual ~TcpServer();

    /**
     * @brief 绑定单个地址
     * @param addr 需要绑定的地址
     * @return 是否绑定成功
     */
    virtual bool bind(Address::ptr addr);

    /**
     * @brief 绑定多个地址
     * @param addrs 需要绑定的地址数组
     * @param fails 绑定失败的地址（输出参数）
     * @return 是否全部绑定成功
     */
    virtual bool bind(const std::vector<Address::ptr>& addrs,
                      std::vector<Address::ptr>& fails);

    /**
     * @brief 启动服务器
     * @return 是否启动成功
     */
    virtual bool start();

    /**
     * @brief 停止服务器
     */
    virtual void stop();

    // Getter/Setter
    uint64_t getRecvTimeout() const { return m_recvTimeout; }
    std::string getName() const { return m_name; }
    void setRecvTimeout(uint64_t v) { m_recvTimeout = v; }
    virtual void setName(const std::string& v) { m_name = v; }
    bool isStop() const { return m_isStop.load(std::memory_order_acquire); }

    /**
     * @brief 转换为字符串（用于调试）
     */
    virtual std::string toString(const std::string& prefix = "");

protected:
    /**
     * @brief 处理客户端连接（虚函数，子类可重写）
     * @param client 客户端 Socket
     */
    virtual void handleClient(Socket::ptr client);

    /**
     * @brief 开始接受连接（循环 accept）
     * @param sock 监听 Socket
     */
    virtual void startAccept(Socket::ptr sock);

protected:
    std::vector<Socket::ptr> m_socks;      // 监听 Socket 数组
    IOManager* m_ioWorker;                 // 处理客户端的调度器
    IOManager* m_acceptWorker;             // 执行 accept 的调度器
    uint64_t m_recvTimeout;                // 接收超时时间（毫秒）
    std::string m_name;                    // 服务器名称
    std::string m_type;                    // 服务器类型
    std::atomic<bool> m_isStop;            // 是否停止
    mutable std::mutex m_mutex;            // 保护 m_socks
};

} // namespace net
} // namespace sylar

#endif
