/**
 * @file test_udp_server.cc
 * @brief UdpServer 模块测试程序
 *
 * 测试内容：
 * 1. Echo UDP 服务器示例
 * 2. 多地址绑定
 */

#include "sylar/net/udp_server.h"
#include "sylar/net/socket.h"
#include "sylar/net/address.h"
#include "sylar/log/logger.h"
#include "sylar/fiber/iomanager.h"
#include "sylar/fiber/hook.h"
#include <iostream>
#include <cstring>

// ============================================================================
// 全局日志器
// ============================================================================

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// ============================================================================
// EchoUdpServer - 继承 UdpServer 实现 Echo 服务
// ============================================================================

/**
 * @brief Echo UDP 服务器 - 将收到的数据原样返回
 */
class EchoUdpServer : public sylar::net::UdpServer
{
public:
    typedef std::shared_ptr<EchoUdpServer> ptr;

    EchoUdpServer(sylar::IOManager *io_worker = sylar::IOManager::GetThis(),
                  sylar::IOManager *recv_worker = sylar::IOManager::GetThis())
        : UdpServer(io_worker, recv_worker)
    {
        setName("EchoUdpServer/1.0.0");
    }

protected:
    /**
     * @brief 处理数据报 - Echo 实现
     */
    virtual void handleDatagram(const void *data, size_t len,
                                sylar::Address::ptr from, sylar::Socket::ptr sock) override
    {
        std::cout << "[EchoUdpServer] received " << len << " bytes from "
                  << from->toString() << ": "
                  << std::string((const char *)data, len) << "\n";

        // Echo 回去
        int n = sock ? sock->sendTo(data, len, from) : -1;
        if (n < 0)
        {
            std::cout << "[EchoUdpServer] sendto failed\n";
        }
        else
        {
            std::cout << "[EchoUdpServer] sent " << n << " bytes back\n";
        }
    }
};

// ============================================================================
// 测试：Echo UDP 服务器
// ============================================================================

void test_echo_udp_server()
{
    std::cout << "\n========== Echo UDP 服务器测试 ==========\n";

    // 启用 hook
    sylar::set_hook_enable(true);

    sylar::IOManager iom(2);

    // 创建 Echo UDP 服务器
    EchoUdpServer::ptr server(new EchoUdpServer(&iom, &iom));

    // 绑定多个地址
    std::vector<sylar::Address::ptr> addrs;
    addrs.push_back(sylar::Address::LookupAny("0.0.0.0:9090"));
    addrs.push_back(sylar::Address::LookupAny("0.0.0.0:9091"));

    std::vector<sylar::Address::ptr> fails;
    if (!server->bind(addrs, fails))
    {
        std::cout << "[服务器] bind fail\n";
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
    std::cout << "[服务器] Echo UDP 服务器启动成功\n";

    // 客户端测试协程
    iom.schedule([server]()
                 {
        sleep(1);

        std::cout << "\n---------- 客户端测试 ----------\n";

        // 创建 UDP socket
        sylar::Socket::ptr sock = sylar::Socket::CreateUDPSocket();

        // 发送到端口 9090
        sylar::Address::ptr addr1 = sylar::Address::LookupAny("127.0.0.1:9090");
        const char* msg1 = "Hello from client to port 9090!";
        sock->sendTo(msg1, std::strlen(msg1), addr1);
        std::cout << "[客户端] 发送到 9090: " << msg1 << "\n";

        char buf[1024];
        sylar::Address::ptr from;
        int len = sock->recvFrom(buf, sizeof(buf) - 1, from);
        if (len > 0) {
            buf[len] = '\0';
            std::cout << "[客户端] 收到回显: " << buf << "\n";
        }

        // 发送到端口 9091
        sylar::Address::ptr addr2 = sylar::Address::LookupAny("127.0.0.1:9091");
        const char* msg2 = "Hello from client to port 9091!";
        sock->sendTo(msg2, std::strlen(msg2), addr2);
        std::cout << "[客户端] 发送到 9091: " << msg2 << "\n";

        len = sock->recvFrom(buf, sizeof(buf) - 1, from);
        if (len > 0) {
            buf[len] = '\0';
            std::cout << "[客户端] 收到回显: " << buf << "\n";
        }

        std::cout << "---------- 测试完成 ----------\n"; });

    // 等待测试完成后停止服务器
    iom.schedule([server]()
                 {
        sleep(3);  // 等待测试完成
        std::cout << "\n[服务器] 准备停止...\n";
        server->stop();
        std::cout << "[服务器] 已停止\n"; });

    // IOManager 析构时会等待所有任务完成
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char **argv)
{
    std::cout << "========================================\n";
    std::cout << "  UdpServer 模块测试\n";
    std::cout << "========================================\n";

    test_echo_udp_server();

    std::cout << "\n========================================\n";
    std::cout << "  所有测试完成\n";
    std::cout << "========================================\n";

    return 0;
}
