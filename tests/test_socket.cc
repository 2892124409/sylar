/**
 * @file test_socket.cc
 * @brief Socket 模块测试程序
 *
 * 测试内容：
 * 1. Socket 创建（TCP/UDP、IPv4/IPv6）
 * 2. Socket 选项设置和获取
 * 3. 超时设置
 * 4. TCP 服务器流程（bind、listen、accept）
 * 5. TCP 客户端流程（connect、send、recv）
 * 6. UDP 数据收发
 */

#include "sylar/net/socket.h"
#include "sylar/net/address.h"
#include "log/logger.h"
#include "sylar/fiber/iomanager.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

// ============================================================================
// 全局日志器
// ============================================================================

/// 文件级别的静态日志器，供本文件所有函数使用
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// ============================================================================
// 测试1：Socket 创建
// ============================================================================

/**
 * @brief 测试各种 Socket 创建方式
 *
 * Socket 类提供了多种工厂方法来创建不同类型的 Socket：
 * - CreateTCPSocket() / CreateUDPSocket()：IPv4
 * - CreateTCPSocket6() / CreateUDPSocket6()：IPv6
 * - CreateUnixTCPSocket() / CreateUnixUDPSocket()：Unix域套接字
 * - CreateTCP(addr) / CreateUDP(addr)：根据地址类型自动选择
 */
void test_socket_create()
{
    std::cout << "\n========== Socket 创建测试 ==========\n";

    // -------------------------------------------------------------------------
    // 1. 创建 IPv4 TCP Socket
    // -------------------------------------------------------------------------
    // 等价于 socket(AF_INET, SOCK_STREAM, 0)
    sylar::Socket::ptr tcp_sock = sylar::Socket::CreateTCPSocket();
    if (tcp_sock)
    {
        std::cout << "[1] IPv4 TCP Socket 创建成功\n";
        std::cout << "    " << tcp_sock->toString() << "\n";
        std::cout << "    family=" << tcp_sock->getFamily()
                  << " (AF_INET=" << AF_INET << ")\n";
        std::cout << "    type=" << tcp_sock->getType()
                  << " (SOCK_STREAM=" << SOCK_STREAM << ")\n";
    }

    // -------------------------------------------------------------------------
    // 2. 创建 IPv4 UDP Socket
    // -------------------------------------------------------------------------
    // 等价于 socket(AF_INET, SOCK_DGRAM, 0)
    sylar::Socket::ptr udp_sock = sylar::Socket::CreateUDPSocket();
    if (udp_sock)
    {
        std::cout << "\n[2] IPv4 UDP Socket 创建成功\n";
        std::cout << "    " << udp_sock->toString() << "\n";
        std::cout << "    type=" << udp_sock->getType()
                  << " (SOCK_DGRAM=" << SOCK_DGRAM << ")\n";
    }

    // -------------------------------------------------------------------------
    // 3. 创建 IPv6 TCP Socket
    // -------------------------------------------------------------------------
    // 等价于 socket(AF_INET6, SOCK_STREAM, 0)
    sylar::Socket::ptr tcp6_sock = sylar::Socket::CreateTCPSocket6();
    if (tcp6_sock)
    {
        std::cout << "\n[3] IPv6 TCP Socket 创建成功\n";
        std::cout << "    " << tcp6_sock->toString() << "\n";
        std::cout << "    family=" << tcp6_sock->getFamily()
                  << " (AF_INET6=" << AF_INET6 << ")\n";
    }

    // -------------------------------------------------------------------------
    // 4. 根据地址类型自动创建 Socket
    // -------------------------------------------------------------------------
    // 传入一个 IPv4 地址，自动创建 IPv4 Socket
    sylar::IPv4Address::ptr addr = sylar::IPv4Address::Create("127.0.0.1", 8080);
    sylar::Socket::ptr auto_sock = sylar::Socket::CreateTCP(addr);
    if (auto_sock)
    {
        std::cout << "\n[4] 根据 Address 自动创建 Socket 成功\n";
        std::cout << "    地址: " << addr->toString() << "\n";
        std::cout << "    Socket: " << auto_sock->toString() << "\n";
    }

    std::cout << "\n";
}

// ============================================================================
// 测试2：Socket 选项
// ============================================================================

/**
 * @brief 测试 Socket 选项的设置和获取
 *
 * Socket 选项控制 Socket 的行为，常用的包括：
 * - SO_REUSEADDR：地址复用，允许绑定处于 TIME_WAIT 状态的端口
 * - SO_KEEPALIVE：TCP 保活，自动检测对端是否存活
 * - SO_RCVBUF / SO_SNDBUF：收发缓冲区大小
 * - TCP_NODELAY：禁用 Nagle 算法，减少小包延迟
 */
void test_socket_options()
{
    std::cout << "\n========== Socket 选项测试 ==========\n";

    sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
    if (!sock)
    {
        std::cout << "Socket 创建失败\n";
        return;
    }

    // -------------------------------------------------------------------------
    // 1. 设置 SO_REUSEADDR（地址复用）
    // -------------------------------------------------------------------------
    // 服务器重启时可以立即绑定刚释放的端口
    // 默认情况下，端口关闭后会进入 TIME_WAIT 状态（约60秒）
    // 设置 SO_REUSEADDR 后可以忽略 TIME_WAIT 直接绑定
    int reuse = 1;
    if (sock->setOption(SOL_SOCKET, SO_REUSEADDR, reuse))
    {
        std::cout << "[1] SO_REUSEADDR 设置成功\n";
    }

    // 验证：读取刚设置的值
    int reuse_val = 0;
    if (sock->getOption(SOL_SOCKET, SO_REUSEADDR, reuse_val))
    {
        std::cout << "    验证: SO_REUSEADDR = " << reuse_val << "\n";
    }

    // -------------------------------------------------------------------------
    // 2. 设置 SO_KEEPALIVE（TCP 保活）
    // -------------------------------------------------------------------------
    // 启用后，如果连接长时间无数据传输，TCP 会自动发送探测包
    // 如果对端没有响应，连接会被关闭
    // 这对于检测"半开连接"很有用（对端崩溃但未发送 FIN）
    int keepalive = 1;
    if (sock->setOption(SOL_SOCKET, SO_KEEPALIVE, keepalive))
    {
        std::cout << "\n[2] SO_KEEPALIVE 设置成功\n";
    }

    // -------------------------------------------------------------------------
    // 3. 查看缓冲区大小
    // -------------------------------------------------------------------------
    // 接收缓冲区：存放从网络收到但应用还没读取的数据
    // 发送缓冲区：存放应用已写入但还没发送的数据
    int rcvbuf = 0, sndbuf = 0;
    sock->getOption(SOL_SOCKET, SO_RCVBUF, rcvbuf);
    sock->getOption(SOL_SOCKET, SO_SNDBUF, sndbuf);
    std::cout << "\n[3] 缓冲区大小\n";
    std::cout << "    接收缓冲区: " << rcvbuf << " 字节\n";
    std::cout << "    发送缓冲区: " << sndbuf << " 字节\n";
    // 注意：Linux 内核实际分配的通常是设置值的 2 倍

    // -------------------------------------------------------------------------
    // 4. 设置 TCP_NODELAY（禁用 Nagle 算法）
    // -------------------------------------------------------------------------
    // Nagle 算法：将多个小包合并发送，减少网络负载
    // 但会增加延迟，对于实时性要求高的应用（游戏、SSH）需要禁用
    int nodelay = 1;
    if (sock->setOption(IPPROTO_TCP, TCP_NODELAY, nodelay))
    {
        std::cout << "\n[4] TCP_NODELAY 设置成功（禁用 Nagle 算法）\n";
    }

    // -------------------------------------------------------------------------
    // 5. 获取 Socket 错误状态
    // -------------------------------------------------------------------------
    // SO_ERROR 用于获取并清除 Socket 的错误状态
    // 正常情况下应该返回 0
    int error = sock->getError();
    std::cout << "\n[5] Socket 错误状态: " << error << "\n";

    std::cout << "\n";
}

// ============================================================================
// 测试3：超时设置
// ============================================================================

/**
 * @brief 测试 Socket 超时设置
 *
 * 超时设置用于防止操作无限阻塞：
 * - SO_SNDTIMEO：发送超时
 * - SO_RCVTIMEO：接收超时
 *
 * 时间单位是毫秒，内部会转换为 timeval 结构（秒 + 微秒）
 */
void test_socket_timeout()
{
    std::cout << "\n========== Socket 超时测试 ==========\n";

    sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
    if (!sock)
    {
        std::cout << "Socket 创建失败\n";
        return;
    }

    // -------------------------------------------------------------------------
    // 1. 设置发送超时
    // -------------------------------------------------------------------------
    // 如果发送缓冲区满，send() 最多等待这么长时间
    // 超时后 send() 会返回 -1，errno 设置为 EAGAIN 或 EWOULDBLOCK
    int64_t send_timeout_ms = 3000; // 3 秒
    sock->setSendTimeout(send_timeout_ms);
    std::cout << "[1] 设置发送超时: " << send_timeout_ms << " ms\n";

    // 验证：读取超时值
    int64_t actual_send = sock->getSendTimeout();
    std::cout << "    验证: 实际发送超时 = " << actual_send << " ms\n";

    // -------------------------------------------------------------------------
    // 2. 设置接收超时
    // -------------------------------------------------------------------------
    // 如果接收缓冲区空，recv() 最多等待这么长时间
    // 超时后 recv() 会返回 -1，errno 设置为 EAGAIN 或 EWOULDBLOCK
    int64_t recv_timeout_ms = 5000; // 5 秒
    sock->setRecvTimeout(recv_timeout_ms);
    std::cout << "\n[2] 设置接收超时: " << recv_timeout_ms << " ms\n";

    // 验证：读取超时值
    int64_t actual_recv = sock->getRecvTimeout();
    std::cout << "    验证: 实际接收超时 = " << actual_recv << " ms\n";

    // -------------------------------------------------------------------------
    // 3. 超时的内部实现原理
    // -------------------------------------------------------------------------
    // setSendTimeout 内部调用：
    //   struct timeval tv;
    //   tv.tv_sec = v / 1000;           // 毫秒 -> 秒
    //   tv.tv_usec = (v % 1000) * 1000; // 剩余毫秒 -> 微秒
    //   setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::cout << "\n";
}

// ============================================================================
// 测试4：TCP 服务器（bind + listen）
// ============================================================================

/**
 * @brief 测试 TCP 服务器的基本流程
 *
 * TCP 服务器的基本步骤：
 * 1. socket() - 创建 Socket
 * 2. bind() - 绑定地址和端口
 * 3. listen() - 开始监听
 * 4. accept() - 接受客户端连接（在另一个测试中演示）
 */
void test_tcp_server_bind_listen()
{
    std::cout << "\n========== TCP 服务器 bind/listen 测试 ==========\n";

    // -------------------------------------------------------------------------
    // 1. 创建 TCP Socket
    // -------------------------------------------------------------------------
    sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
    if (!sock)
    {
        std::cout << "Socket 创建失败\n";
        return;
    }
    std::cout << "[1] 创建 TCP Socket 成功\n";

    // -------------------------------------------------------------------------
    // 2. 绑定地址
    // -------------------------------------------------------------------------
    // 创建地址对象：监听 127.0.0.1:18080
    // 127.0.0.1 是回环地址，只能本机访问
    // 如果想允许外部访问，可以使用 0.0.0.0
    sylar::IPv4Address::ptr addr = sylar::IPv4Address::Create("127.0.0.1", 18080);
    if (!addr)
    {
        std::cout << "地址创建失败\n";
        return;
    }

    // bind() 将 Socket 绑定到指定地址和端口
    // 只有 bind 成功后，才能开始监听
    if (!sock->bind(addr))
    {
        std::cout << "[2] bind 失败（可能端口被占用）\n";
        return;
    }
    std::cout << "[2] bind 成功: " << addr->toString() << "\n";

    // 获取实际绑定的地址（当端口设为 0 时，系统会自动分配）
    sylar::Address::ptr local = sock->getLocalAddress();
    if (local)
    {
        std::cout << "    本地地址: " << local->toString() << "\n";
    }

    // -------------------------------------------------------------------------
    // 3. 开始监听
    // -------------------------------------------------------------------------
    // listen() 将 Socket 标记为被动 Socket，准备接受连接
    // 参数 backlog 指定未完成连接队列的最大长度
    // SOMAXCONN 是系统定义的最大值（通常为 128）
    if (!sock->listen())
    {
        std::cout << "[3] listen 失败\n";
        return;
    }
    std::cout << "[3] listen 成功，等待连接...\n";

    // -------------------------------------------------------------------------
    // 4. 显示 Socket 状态
    // -------------------------------------------------------------------------
    std::cout << "\n[4] Socket 状态:\n";
    std::cout << "    " << sock->toString() << "\n";
    std::cout << "    是否有效: " << (sock->isValid() ? "是" : "否") << "\n";
    std::cout << "    是否已连接: " << (sock->isConnected() ? "是" : "否") << "\n";
    // 注意：监听 Socket 的 isConnected 是 false，因为它还没有与客户端建立连接

    std::cout << "\n    (服务器已就绪，可以接受连接)\n";

    // 这里不 accept，让 Socket 在函数结束时自动关闭
    std::cout << "\n";
}

// ============================================================================
// 测试5：TCP 客户端/服务器通信
// ============================================================================

/**
 * @brief 测试 TCP 客户端和服务器之间的通信
 *
 * 完整流程：
 * 服务器：socket -> bind -> listen -> accept -> recv -> send -> close
 * 客户端：socket -> connect -> send -> recv -> close
 *
 * 这个测试在一个线程中模拟，使用短连接
 */
void test_tcp_client_server()
{
    std::cout << "\n========== TCP 客户端/服务器通信测试 ==========\n";

    // -------------------------------------------------------------------------
    // 第一步：启动服务器
    // -------------------------------------------------------------------------
    sylar::Socket::ptr server = sylar::Socket::CreateTCPSocket();
    sylar::IPv4Address::ptr server_addr = sylar::IPv4Address::Create("127.0.0.1", 18081);

    if (!server->bind(server_addr))
    {
        std::cout << "服务器 bind 失败\n";
        return;
    }
    if (!server->listen())
    {
        std::cout << "服务器 listen 失败\n";
        return;
    }
    std::cout << "[1] 服务器启动: " << server_addr->toString() << "\n";

    // -------------------------------------------------------------------------
    // 第二步：在另一个线程中运行客户端
    // -------------------------------------------------------------------------
    // 为什么要用线程？
    // 因为 accept() 是阻塞的，需要有人在等待连接，同时有人去连接
    std::thread client_thread([server_addr]()
                              {
        // 等待服务器准备好
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::cout << "\n[客户端] 开始连接...\n";

        // 创建客户端 Socket
        sylar::Socket::ptr client = sylar::Socket::CreateTCPSocket();
        if (!client)
        {
            std::cout << "[客户端] Socket 创建失败\n";
            return;
        }

        // connect() 建立与服务器的连接
        // 三次握手在这个过程中完成：SYN -> SYN-ACK -> ACK
        if (!client->connect(server_addr))
        {
            std::cout << "[客户端] connect 失败\n";
            return;
        }
        std::cout << "[客户端] 连接成功\n";
        std::cout << "[客户端] 本地地址: " << client->getLocalAddress()->toString() << "\n";
        std::cout << "[客户端] 远端地址: " << client->getRemoteAddress()->toString() << "\n";

        // 发送数据
        const char* msg = "Hello from client!";
        int sent = client->send(msg, strlen(msg));
        std::cout << "[客户端] 发送 " << sent << " 字节: \"" << msg << "\"\n";

        // 接收响应
        char buffer[1024] = {0};
        int received = client->recv(buffer, sizeof(buffer) - 1);
        if (received > 0)
        {
            buffer[received] = '\0';
            std::cout << "[客户端] 收到响应: \"" << buffer << "\"\n";
        }

        // 关闭连接
        client->close();
        std::cout << "[客户端] 连接关闭\n"; });

    // -------------------------------------------------------------------------
    // 第三步：服务器接受连接并处理
    // -------------------------------------------------------------------------
    std::cout << "[服务器] 等待客户端连接...\n";

    // accept() 阻塞等待客户端连接
    // 返回一个新的 Socket，专门用于与这个客户端通信
    // 原来的 server Socket 继续监听其他连接
    sylar::Socket::ptr client_conn = server->accept();
    if (!client_conn)
    {
        std::cout << "[服务器] accept 失败\n";
        client_thread.join();
        return;
    }
    std::cout << "[服务器] 接受新连接\n";
    std::cout << "[服务器] 客户端地址: " << client_conn->getRemoteAddress()->toString() << "\n";

    // 接收客户端数据
    char buffer[1024] = {0};
    int received = client_conn->recv(buffer, sizeof(buffer) - 1);
    if (received > 0)
    {
        buffer[received] = '\0';
        std::cout << "[服务器] 收到 " << received << " 字节: \"" << buffer << "\"\n";
    }

    // 发送响应
    const char *response = "Hello from server!";
    client_conn->send(response, strlen(response));
    std::cout << "[服务器] 发送响应: \"" << response << "\"\n";

    // 等待客户端线程结束
    client_thread.join();

    std::cout << "\n[测试完成] TCP 通信成功\n";
    std::cout << "\n";
}

// ============================================================================
// 测试6：UDP 通信
// ============================================================================

/**
 * @brief 测试 UDP 数据报通信
 *
 * UDP 与 TCP 的区别：
 * - 无连接：不需要 connect、listen、accept
 * - 不可靠：数据可能丢失、乱序
 * - 面向消息：保留消息边界（send 一次 = recv 一次）
 *
 * UDP 使用 sendto 和 recvfrom，每次都要指定/获取对方地址
 */
void test_udp_communication()
{
    std::cout << "\n========== UDP 通信测试 ==========\n";

    // -------------------------------------------------------------------------
    // 1. 创建两个 UDP Socket
    // -------------------------------------------------------------------------
    // UDP 不需要建立连接，双方都只需要 bind 自己的地址
    sylar::Socket::ptr sock1 = sylar::Socket::CreateUDPSocket();
    sylar::Socket::ptr sock2 = sylar::Socket::CreateUDPSocket();

    sylar::IPv4Address::ptr addr1 = sylar::IPv4Address::Create("127.0.0.1", 18082);
    sylar::IPv4Address::ptr addr2 = sylar::IPv4Address::Create("127.0.0.1", 18083);

    sock1->bind(addr1);
    sock2->bind(addr2);

    std::cout << "[1] UDP Socket 创建成功\n";
    std::cout << "    sock1: " << addr1->toString() << "\n";
    std::cout << "    sock2: " << addr2->toString() << "\n";

    // -------------------------------------------------------------------------
    // 2. sock1 向 sock2 发送数据
    // -------------------------------------------------------------------------
    // UDP 使用 sendTo，需要指定目标地址
    const char *msg = "UDP Hello!";
    int sent = sock1->sendTo(msg, strlen(msg), addr2);
    std::cout << "\n[2] sock1 发送 " << sent << " 字节到 " << addr2->toString() << "\n";

    // -------------------------------------------------------------------------
    // 3. sock2 接收数据
    // -------------------------------------------------------------------------
    // UDP 使用 recvFrom，可以获取发送者地址
    char buffer[1024] = {0};
    sylar::Address::ptr from_addr;

    // 注意：recvFrom 的 from 参数是用来返回发送者地址的
    // 这里用 getLocalAddress 获取地址作为临时方案
    int received = sock2->recv(buffer, sizeof(buffer) - 1);
    if (received > 0)
    {
        buffer[received] = '\0';
        std::cout << "[3] sock2 收到 " << received << " 字节: \"" << buffer << "\"\n";
    }

    // -------------------------------------------------------------------------
    // 4. sock2 回复
    // -------------------------------------------------------------------------
    const char *reply = "UDP Reply!";
    sock2->sendTo(reply, strlen(reply), addr1);
    std::cout << "\n[4] sock2 发送回复\n";

    // sock1 接收回复
    memset(buffer, 0, sizeof(buffer));
    received = sock1->recv(buffer, sizeof(buffer) - 1);
    if (received > 0)
    {
        buffer[received] = '\0';
        std::cout << "[5] sock1 收到回复: \"" << buffer << "\"\n";
    }

    std::cout << "\n[测试完成] UDP 通信成功\n";
    std::cout << "\n";
}

// ============================================================================
// 测试7：iovec 分散/聚集 I/O
// ============================================================================

/**
 * @brief 测试 iovec 方式的数据收发
 *
 * iovec（分散/聚集 I/O）允许一次系统调用读写多个缓冲区：
 * - send/recv with iovec：一次发送/接收多个不连续的内存块
 *
 * 应用场景：
 * - 发送协议头 + 消息体（两块不同的内存）
 * - 零拷贝优化
 */
void test_iovec_send_recv()
{
    std::cout << "\n========== iovec 分散/聚集 I/O 测试 ==========\n";

    // 设置服务器
    sylar::Socket::ptr server = sylar::Socket::CreateTCPSocket();
    sylar::IPv4Address::ptr addr = sylar::IPv4Address::Create("127.0.0.1", 18084);
    server->bind(addr);
    server->listen();
    std::cout << "[1] 服务器启动: " << addr->toString() << "\n";

    // 客户端线程
    std::thread client_thread([addr]()
                              {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        sylar::Socket::ptr client = sylar::Socket::CreateTCPSocket();
        client->connect(addr);

        // -------------------------------------------------------------------------
        // 使用 iovec 一次发送多块数据
        // -------------------------------------------------------------------------
        // 假设我们要发送：头部(8字节) + 内容(12字节)
        // 这两块数据在内存中不连续，但可以用一次 send 发出
        char header[] = "HEADER:";
        char body[] = "Hello World!";

        struct iovec iov[2];
        iov[0].iov_base = header;           // 第一块：头部
        iov[0].iov_len = strlen(header);
        iov[1].iov_base = body;             // 第二块：内容
        iov[1].iov_len = strlen(body);

        // send(iovec) 一次发送所有数据
        int sent = client->send(iov, 2);
        std::cout << "[客户端] 使用 iovec 发送 " << sent << " 字节\n";
        std::cout << "    头部: \"" << header << "\" (" << strlen(header) << " 字节)\n";
        std::cout << "    内容: \"" << body << "\" (" << strlen(body) << " 字节)\n";

        client->close(); });

    // 服务器接受连接
    sylar::Socket::ptr conn = server->accept();

    // 接收数据
    char buffer[1024] = {0};
    int received = conn->recv(buffer, sizeof(buffer) - 1);
    if (received > 0)
    {
        buffer[received] = '\0';
        std::cout << "\n[服务器] 收到 " << received << " 字节: \"" << buffer << "\"\n";
        std::cout << "    （数据被合并成一块了，因为 TCP 是字节流）\n";
    }

    client_thread.join();
    std::cout << "\n";
}

// ============================================================================
// 测试8：Socket 状态检查
// ============================================================================

/**
 * @brief 测试 Socket 的各种状态检查方法
 */
void test_socket_status()
{
    std::cout << "\n========== Socket 状态检查测试 ==========\n";

    // -------------------------------------------------------------------------
    // 1. 新创建的 Socket
    // -------------------------------------------------------------------------
    sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
    std::cout << "[1] 新创建的 Socket\n";
    std::cout << "    isValid: " << (sock->isValid() ? "是" : "否") << " (m_sock != -1)\n";
    std::cout << "    isConnected: " << (sock->isConnected() ? "是" : "否") << " (还未连接)\n";

    // -------------------------------------------------------------------------
    // 2. 绑定后的 Socket
    // -------------------------------------------------------------------------
    sylar::IPv4Address::ptr addr = sylar::IPv4Address::Create("127.0.0.1", 18085);
    sock->bind(addr);
    std::cout << "\n[2] bind 后\n";
    std::cout << "    isValid: " << (sock->isValid() ? "是" : "否") << "\n";
    std::cout << "    isConnected: " << (sock->isConnected() ? "是" : "否") << " (bind 不算连接)\n";

    // -------------------------------------------------------------------------
    // 3. 关闭后的 Socket
    // -------------------------------------------------------------------------
    sock->close();
    std::cout << "\n[3] close 后\n";
    std::cout << "    isValid: " << (sock->isValid() ? "是" : "否") << " (m_sock == -1)\n";
    std::cout << "    isConnected: " << (sock->isConnected() ? "是" : "否") << "\n";

    std::cout << "\n";
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char **argv)
{
    std::cout << "========================================\n";
    std::cout << "        Socket 模块测试程序\n";
    std::cout << "========================================\n";

    // 基础功能测试
    test_socket_create();  // Socket 创建
    test_socket_options(); // Socket 选项
    test_socket_timeout(); // 超时设置
    test_socket_status();  // 状态检查

    // 网络通信测试
    test_tcp_server_bind_listen(); // TCP 服务器 bind/listen
    test_tcp_client_server();      // TCP 客户端/服务器通信
    test_udp_communication();      // UDP 通信
    test_iovec_send_recv();        // iovec 分散/聚集 I/O

    std::cout << "========================================\n";
    std::cout << "        所有测试完成\n";
    std::cout << "========================================\n";

    return 0;
}
