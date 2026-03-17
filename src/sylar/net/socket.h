#ifndef SYLAR_NET_SOCKET_H
#define SYLAR_NET_SOCKET_H

#include "address.h"
#include "sylar/base/noncopyable.h"
#include <iostream>
#include <memory>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

namespace sylar
{

/**
 * @brief Socket 封装类
 * @details 对 Linux socket 的 C++ 封装，支持 TCP/UDP、IPv4/IPv6/Unix
 */
class Socket : public std::enable_shared_from_this<Socket>, Noncopyable
{
  public:
    typedef std::shared_ptr<Socket> ptr;
    typedef std::weak_ptr<Socket> weak_ptr;

    /**
     * @brief Socket 类型
     */
    enum Type
    {
        /// TCP 类型
        TCP = SOCK_STREAM,
        /// UDP 类型
        UDP = SOCK_DGRAM
    };

    /**
     * @brief Socket 协议簇
     */
    enum Family
    {
        /// IPv4 socket
        IPv4 = AF_INET,
        /// IPv6 socket
        IPv6 = AF_INET6,
        /// Unix socket
        UNIX = AF_UNIX
    };

    /**
     * @brief 创建 TCP Socket (根据地址类型自动选择)
     * @param[in] address 地址
     */
    static Socket::ptr CreateTCP(sylar::Address::ptr address);

    /**
     * @brief 创建 UDP Socket (根据地址类型自动选择)
     * @param[in] address 地址
     */
    static Socket::ptr CreateUDP(sylar::Address::ptr address);

    /**
     * @brief 创建 IPv4 的 TCP Socket
     */
    static Socket::ptr CreateTCPSocket();

    /**
     * @brief 创建 IPv4 的 UDP Socket
     */
    static Socket::ptr CreateUDPSocket();

    /**
     * @brief 创建 IPv6 的 TCP Socket
     */
    static Socket::ptr CreateTCPSocket6();

    /**
     * @brief 创建 IPv6 的 UDP Socket
     */
    static Socket::ptr CreateUDPSocket6();

    /**
     * @brief 创建 Unix 的 TCP Socket
     */
    static Socket::ptr CreateUnixTCPSocket();

    /**
     * @brief 创建 Unix 的 UDP Socket
     */
    static Socket::ptr CreateUnixUDPSocket();

    /**
     * @brief Socket 构造函数
     * @param[in] family 协议簇 (AF_INET, AF_INET6, AF_UNIX)
     * @param[in] type 类型 (SOCK_STREAM, SOCK_DGRAM)
     * @param[in] protocol 协议 (默认为 0)
     */
    Socket(int family, int type, int protocol = 0);

    /**
     * @brief 析构函数
     */
    virtual ~Socket();

    /**
     * @brief 获取发送超时时间 (毫秒)
     */
    int64_t getSendTimeout();

    /**
     * @brief 设置发送超时时间 (毫秒)
     * @param[in] v 超时时间
     */
    void setSendTimeout(int64_t v);

    /**
     * @brief 获取接收超时时间 (毫秒)
     */
    int64_t getRecvTimeout();

    /**
     * @brief 设置接收超时时间 (毫秒)
     * @param[in] v 超时时间
     */
    void setRecvTimeout(int64_t v);

    /**
     * @brief 获取 socket 选项
     * @param[in] level 选项级别 (SOL_SOCKET, IPPROTO_TCP 等)
     * @param[in] option 选项名称
     * @param[out] result 选项值
     * @param[in,out] len 选项值长度
     * @return 是否获取成功
     */
    bool getOption(int level, int option, void* result, socklen_t* len);

    /**
     * @brief 获取 socket 选项 (模板版本)
     * @param[in] level 选项级别
     * @param[in] option 选项名称
     * @param[out] result 选项值
     * @return 是否获取成功
     */
    template <class T>
    bool getOption(int level, int option, T& result)
    {
        socklen_t length = sizeof(T);
        return getOption(level, option, &result, &length);
    }

    /**
     * @brief 设置 socket 选项
     * @param[in] level 选项级别
     * @param[in] option 选项名称
     * @param[in] result 选项值
     * @param[in] len 选项值长度
     * @return 是否设置成功
     */
    bool setOption(int level, int option, const void* result, socklen_t len);

    /**
     * @brief 设置 socket 选项 (模板版本)
     * @param[in] level 选项级别
     * @param[in] option 选项名称
     * @param[in] value 选项值
     * @return 是否设置成功
     */
    template <class T>
    bool setOption(int level, int option, const T& value)
    {
        return setOption(level, option, &value, sizeof(T));
    }

    /**
     * @brief 接受客户端连接
     * @return 成功返回新连接的 socket, 失败返回 nullptr
     * @pre Socket 必须 bind、listen 成功
     */
    virtual Socket::ptr accept();

    /**
     * @brief 绑定地址
     * @param[in] addr 地址
     * @return 是否绑定成功
     */
    virtual bool bind(const Address::ptr addr);

    /**
     * @brief 连接服务器
     * @param[in] addr 目标地址
     * @param[in] timeout_ms 超时时间 (毫秒), -1 表示永不超时
     * @return 是否连接成功
     */
    virtual bool connect(const Address::ptr addr, uint64_t timeout_ms = -1);

    /**
     * @brief 重连
     * @param[in] timeout_ms 超时时间 (毫秒), -1 表示永不超时
     * @return 是否连接成功
     */
    virtual bool reconnect(uint64_t timeout_ms = -1);

    /**
     * @brief 监听 socket
     * @param[in] backlog 未完成连接队列的最大长度
     * @return 监听是否成功
     * @pre 必须先 bind 成功
     */
    virtual bool listen(int backlog = SOMAXCONN);

    /**
     * @brief 关闭 socket
     * @return 是否关闭成功
     */
    virtual bool close();

    /**
     * @brief 发送数据
     * @param[in] buffer 待发送数据的内存
     * @param[in] length 待发送数据的长度
     * @param[in] flags 标志字
     * @return
     *      @retval >0 发送成功对应大小的数据
     *      @retval =0 socket 被关闭
     *      @retval <0 socket 出错
     */
    virtual int send(const void* buffer, size_t length, int flags = 0);

    /**
     * @brief 发送数据 (iovec 分散发送)
     * @param[in] buffers 待发送数据的内存 (iovec 数组)
     * @param[in] length 待发送数据的长度 (iovec 长度)
     * @param[in] flags 标志字
     * @return
     *      @retval >0 发送成功对应大小的数据
     *      @retval =0 socket 被关闭
     *      @retval <0 socket 出错
     */
    virtual int send(const iovec* buffers, size_t length, int flags = 0);

    /**
     * @brief 发送数据 (UDP)
     * @param[in] buffer 待发送数据的内存
     * @param[in] length 待发送数据的长度
     * @param[in] to 发送的目标地址
     * @param[in] flags 标志字
     * @return
     *      @retval >0 发送成功对应大小的数据
     *      @retval =0 socket 被关闭
     *      @retval <0 socket 出错
     */
    virtual int sendTo(const void* buffer, size_t length, const Address::ptr to, int flags = 0);

    /**
     * @brief 发送数据 (UDP, iovec 分散发送)
     * @param[in] buffers 待发送数据的内存 (iovec 数组)
     * @param[in] length 待发送数据的长度 (iovec 数组长度)
     * @param[in] to 发送的目标地址
     * @param[in] flags 标志字
     * @return
     *      @retval >0 发送成功对应大小的数据
     *      @retval =0 socket 被关闭
     *      @retval <0 socket 出错
     */
    virtual int sendTo(const iovec* buffers, size_t length, const Address::ptr to, int flags = 0);

    /**
     * @brief 接收数据
     * @param[out] buffer 接收数据的内存
     * @param[in] length 接收数据的内存大小
     * @param[in] flags 标志字
     * @return
     *      @retval >0 接收到对应大小的数据
     *      @retval =0 socket 被关闭
     *      @retval <0 socket 出错
     */
    virtual int recv(void* buffer, size_t length, int flags = 0);

    /**
     * @brief 接收数据 (iovec 分散接收)
     * @param[out] buffers 接收数据的内存 (iovec 数组)
     * @param[in] length 接收数据的内存大小 (iovec 数组长度/元素个数)
     * @param[in] flags 标志字
     * @return
     *      @retval >0 接收到对应大小的数据
     *      @retval =0 socket 被关闭
     *      @retval <0 socket 出错
     */
    virtual int recv(iovec* buffers, size_t length, int flags = 0);

    /**
     * @brief 接收数据 (UDP)
     * @param[out] buffer 接收数据的内存
     * @param[in] length 接收数据的内存大小
     * @param[out] from 发送端地址（引用传递）
     * @param[in] flags 标志字
     * @return
     *      @retval >0 接收到对应大小的数据
     *      @retval =0 socket 被关闭
     *      @retval <0 socket 出错
     */
    virtual int recvFrom(void* buffer, size_t length, Address::ptr& from, int flags = 0);

    /**
     * @brief 接收数据 (UDP, iovec 分散接收)
     * @param[out] buffers 接收数据的内存 (iovec 数组)
     * @param[in] length 接收数据的内存大小 (iovec 数组长度)
     * @param[out] from 发送端地址（引用传递）
     * @param[in] flags 标志字
     * @return
     *      @retval >0 接收到对应大小的数据
     *      @retval =0 socket 被关闭
     *      @retval <0 socket 出错
     */
    virtual int recvFrom(iovec* buffers, size_t length, Address::ptr& from, int flags = 0);

    /**
     * @brief 获取远端地址
     * @return 远端地址
     */
    Address::ptr getRemoteAddress();

    /**
     * @brief 获取本地地址
     * @return 本地地址
     */
    Address::ptr getLocalAddress();

    /**
     * @brief 获取协议簇
     * @return 地址族 (AF_INET, AF_INET6, AF_UNIX)
     */
    int getFamily() const
    {
        return m_family;
    }

    /**
     * @brief 获取类型
     * @return 类型 (SOCK_STREAM, SOCK_DGRAM)
     */
    int getType() const
    {
        return m_type;
    }

    /**
     * @brief 获取协议
     * @return 协议
     */
    int getProtocol() const
    {
        return m_protocol;
    }

    /**
     * @brief 是否已连接
     * @return 是否已连接
     */
    bool isConnected() const
    {
        return m_isConnected;
    }

    /**
     * @brief 是否有效 (m_sock != -1)
     * @return 是否有效
     */
    bool isValid() const;

    /**
     * @brief 获取 Socket 错误
     * @return 错误码
     */
    int getError();

    /**
     * @brief 输出信息到流中
     * @param[in] os 输出流
     * @return 输出流
     */
    virtual std::ostream& dump(std::ostream& os) const;

    /**
     * @brief 转换为字符串
     * @return 字符串
     */
    virtual std::string toString() const;

    /**
     * @brief 获取 socket 句柄
     * @return socket 文件描述符
     */
    int getSocket() const
    {
        return m_sock;
    }

    /**
     * @brief 取消读事件
     * @return 是否取消成功
     */
    bool cancelRead();

    /**
     * @brief 取消写事件
     * @return 是否取消成功
     */
    bool cancelWrite();

    /**
     * @brief 取消 accept 事件
     * @return 是否取消成功
     */
    bool cancelAccept();

    /**
     * @brief 取消所有事件
     * @return 是否取消成功
     */
    bool cancelAll();

  protected:
    /**
     * @brief 初始化 socket，设置Socket套接字选项
     */
    void initSock();

    /**
     * @brief 创建新的 socket，创建fd文件描述符
     */
    void newSock();

    /**
     * @brief 初始化 socket (接受已存在的 socket)，一般是在accept返回客户端fd时使用，所有不需要设置套接字选项
     * @param[in] sock socket 文件描述符
     * @return 是否初始化成功
     */
    virtual bool init(int sock);

  protected:
    /// socket 句柄
    int m_sock;
    /// 协议簇 (AF_INET, AF_INET6, AF_UNIX)
    int m_family;
    /// 类型 (SOCK_STREAM, SOCK_DGRAM)(TCP/UDP)
    int m_type;
    /// 协议 (IPPROTO_TCP, IPPROTO_UDP)
    int m_protocol;
    /// 是否已连接
    bool m_isConnected;
    /// 本地地址
    Address::ptr m_localAddress;
    /// 远端地址
    Address::ptr m_remoteAddress;
};

/**
 * @brief 流式输出 socket
 * @param[in, out] os 输出流
 * @param[in] sock Socket 类
 * @return 输出流
 */
std::ostream& operator<<(std::ostream& os, const Socket& sock);

} // namespace sylar

#endif // SYLAR_NET_SOCKET_H
