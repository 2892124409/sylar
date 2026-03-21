/**
 * @file test_tcp_server.cc
 * @brief TcpServer 模块测试程序
 *
 * 测试内容：
 * 1. Echo 服务器示例
 * 2. 多地址绑定
 */

#include "sylar/net/tcp_server.h"
#include "sylar/net/socket_stream.h"
#include "sylar/net/socket.h"
#include "sylar/net/address.h"
#include "sylar/log/logger.h"
#include "sylar/fiber/iomanager.h"
#include <thread>
#include <iostream>
#include <cstring>

// ============================================================================
// 全局日志器
// ============================================================================

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// ============================================================================
// EchoServer - 继承 TcpServer 实现 Echo 服务
// ============================================================================

/**
 * @brief Echo 服务器 - 将收到的数据原样返回
 */
class EchoServer : public sylar::net::TcpServer
{
public:
    typedef std::shared_ptr<EchoServer> ptr;

    EchoServer(sylar::IOManager *io_worker = sylar::IOManager::GetThis(),
               sylar::IOManager *accept_worker = sylar::IOManager::GetThis())
        : TcpServer(io_worker, accept_worker)
    {
        setName("EchoServer/1.0.0");
    }

protected:
    /**
     * @brief 处理客户端连接 - Echo 实现
     */
    virtual void handleClient(sylar::Socket::ptr client) override
    {
        SYLAR_LOG_INFO(g_logger) << "EchoServer::handleClient: " << *client;

        // 使用 SocketStream 包装 Socket
        sylar::SocketStream::ptr stream(new sylar::SocketStream(client));

        char buf[1024];
        while (true)
        {
            // 读取数据
            int len = stream->read(buf, sizeof(buf) - 1);
            if (len <= 0)
            {
                SYLAR_LOG_INFO(g_logger) << "client disconnected: " << client->toString();
                break;
            }

            buf[len] = '\0';
            SYLAR_LOG_INFO(g_logger) << "received: " << buf << " (" << len << " bytes)";

            // Echo 回去
            stream->write(buf, len);
        }

        stream->close();
    }
};

// ============================================================================
// 测试：Echo 服务器
// ============================================================================

void test_echo_server()
{
    std::cout << "\n========== Echo 服务器测试 ==========\n";
    sylar::IOManager io_iom(2, false, "echo_io");
    sylar::IOManager accept_iom(1, true, "echo_accept");

    // 创建 Echo 服务器
    EchoServer::ptr server(new EchoServer(&io_iom, &accept_iom));

    // 绑定多个地址
    std::vector<sylar::Address::ptr> addrs;
    addrs.push_back(sylar::Address::LookupAny("0.0.0.0:8080"));
    addrs.push_back(sylar::Address::LookupAny("0.0.0.0:8081"));

    std::vector<sylar::Address::ptr> fails;
    if (!server->bind(addrs, fails))
    {
        SYLAR_LOG_ERROR(g_logger) << "bind fail";
        for (auto &addr : fails)
        {
            std::cout << "  绑定失败: " << addr->toString() << "\n";
        }
        return;
    }

    std::cout << "[服务器] 多地址绑定成功:\n"
              << server->toString("  ");

    // 启动服务器
    server->start();
    std::cout << "[服务器] Echo 服务器启动成功，accept 在主线程 reactor，IO 在 worker 池\n";

    std::thread client_thread([server, &accept_iom, &io_iom]()
                              {
        sleep(1);

        std::cout << "\n---------- 客户端测试 ----------\n";

        sylar::Socket::ptr sock1 = sylar::Socket::CreateTCPSocket();
        if (sock1->connect(sylar::Address::LookupAny("127.0.0.1:8080"))) {
            std::cout << "[客户端1] 连接 8080 成功\n";

            sylar::SocketStream::ptr stream(new sylar::SocketStream(sock1));

            const char* msg = "Hello from client 1!";
            stream->write(msg, strlen(msg));
            std::cout << "[客户端1] 发送: " << msg << "\n";

            char buf[1024];
            int len = stream->read(buf, sizeof(buf) - 1);
            if (len > 0) {
                buf[len] = '\0';
                std::cout << "[客户端1] 收到回显: " << buf << "\n";
            }
            stream->close();
        }

        sylar::Socket::ptr sock2 = sylar::Socket::CreateTCPSocket();
        if (sock2->connect(sylar::Address::LookupAny("127.0.0.1:8081"))) {
            std::cout << "[客户端2] 连接 8081 成功\n";

            sylar::SocketStream::ptr stream(new sylar::SocketStream(sock2));

            const char* msg = "Hello from client 2!";
            stream->write(msg, strlen(msg));
            std::cout << "[客户端2] 发送: " << msg << "\n";

            char buf[1024];
            int len = stream->read(buf, sizeof(buf) - 1);
            if (len > 0) {
                buf[len] = '\0';
                std::cout << "[客户端2] 收到回显: " << buf << "\n";
            }
            stream->close();
        }

        std::cout << "---------- 测试完成 ----------\n";
        sleep(1);
        std::cout << "\n[服务器] 准备停止...\n";
        server->stop();
        accept_iom.stop();
        io_iom.stop();
        std::cout << "[服务器] 已停止\n"; });

    accept_iom.runCaller();
    client_thread.join();
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char **argv)
{
    std::cout << "========================================\n";
    std::cout << "  TcpServer 模块测试\n";
    std::cout << "========================================\n";

    test_echo_server();

    std::cout << "\n========================================\n";
    std::cout << "  所有测试完成\n";
    std::cout << "========================================\n";

    return 0;
}
