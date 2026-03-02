/**
 * @file udp_server.h
 * @brief UDP 服务器封装
 */

#ifndef __SYLAR_NET_UDP_SERVER_H__
#define __SYLAR_NET_UDP_SERVER_H__

#include "sylar/net/socket.h"
#include "sylar/fiber/iomanager.h"
#include "sylar/base/noncopyable.h"
#include <memory>
#include <vector>

namespace sylar {
namespace net {

/**
 * @brief UDP 服务器类
 *
 * 功能：
 * 1. 支持绑定多个地址
 * 2. 异步接收 UDP 数据报
 * 3. 使用协程调度器处理数据报
 * 4. 支持优雅启动和停止
 */
class UdpServer : public std::enable_shared_from_this<UdpServer>
                , public ::sylar::Noncopyable {
public:
    typedef std::shared_ptr<UdpServer> ptr;

    /**
     * @brief 构造函数
     * @param io_worker 处理数据报的协程调度器
     * @param recv_worker 执行 recvfrom 的协程调度器
     */
    UdpServer(IOManager* io_worker = IOManager::GetThis(),
              IOManager* recv_worker = IOManager::GetThis());

    /**
     * @brief 析构函数
     */
    virtual ~UdpServer();

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

    /**
     * @brief 发送数据到指定地址
     * @param buffer 待发送的数据
     * @param length 数据长度
     * @param to 目标地址
     * @param sockIndex 使用哪个 socket 发送（默认 0）
     * @return 发送的字节数，失败返回 -1
     */
    int sendTo(const void* buffer, size_t length, Address::ptr to, size_t sockIndex = 0);

    // Getter/Setter
    uint64_t getRecvTimeout() const { return m_recvTimeout; }
    std::string getName() const { return m_name; }
    void setRecvTimeout(uint64_t v) { m_recvTimeout = v; }
    virtual void setName(const std::string& v) { m_name = v; }
    bool isStop() const { return m_isStop; }
    size_t getBufferSize() const { return m_bufferSize; }
    void setBufferSize(size_t size) { m_bufferSize = size; }

    /**
     * @brief 转换为字符串（用于调试）
     */
    virtual std::string toString(const std::string& prefix = "");

protected:
    /**
     * @brief 处理数据报（虚函数，子类可重写）
     * @param data 数据内容
     * @param len 数据长度
     * @param from 发送端地址
     * @param sock 接收数据的 Socket
     */
    virtual void handleDatagram(const void* data, size_t len,
                                Address::ptr from, Socket::ptr sock);

    /**
     * @brief 开始接收数据报（循环 recvfrom）
     * @param sock 绑定的 Socket
     */
    virtual void startReceive(Socket::ptr sock);

protected:
    std::vector<Socket::ptr> m_socks;      // 绑定的 Socket 数组
    IOManager* m_ioWorker;                 // 处理数据报的调度器
    IOManager* m_recvWorker;               // 执行 recvfrom 的调度器
    uint64_t m_recvTimeout;                // 接收超时时间（毫秒）
    std::string m_name;                    // 服务器名称
    std::string m_type;                    // 服务器类型
    bool m_isStop;                         // 是否停止
    size_t m_bufferSize;                   // 接收缓冲区大小（UDP 最大 64KB）
};

} // namespace net
} // namespace sylar

#endif
