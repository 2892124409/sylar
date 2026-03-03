/**
 * @file test_stream.cc
 * @brief Stream 和 SocketStream 模块测试程序
 *
 * 测试内容：
 * 1. SocketStream 基本读写
 * 2. readFixSize/writeFixSize 保证完整读写
 * 3. ByteArray 与 SocketStream 配合
 * 4. 地址信息获取
 */

#include "sylar/net/socket_stream.h"
#include "sylar/net/socket.h"
#include "sylar/net/address.h"
#include "sylar/net/bytearray.h"
#include "sylar/log/logger.h"
#include "sylar/fiber/iomanager.h"
#include <iostream>
#include <cstring>

// ============================================================================
// 全局日志器
// ============================================================================

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// ============================================================================
// 测试1：SocketStream 基本读写
// ============================================================================

/**
 * @brief 测试 SocketStream 的基本读写功能
 *
 * 测试场景：
 * 1. 创建 TCP 服务器和客户端
 * 2. 使用 SocketStream 包装 Socket
 * 3. 测试基本的 read/write 操作
 */
void test_basic_read_write()
{
    std::cout << "\n========== SocketStream 基本读写测试 ==========\n";

    sylar::IOManager iom(2);

    // 服务端
    iom.schedule([]()
                 {
        // 创建服务器 Socket
        sylar::Socket::ptr server = sylar::Socket::CreateTCPSocket();
        sylar::Address::ptr addr = sylar::Address::LookupAny("127.0.0.1:8090");

        if (!server->bind(addr)) {
            SYLAR_LOG_ERROR(g_logger) << "服务器 bind 失败";
            return;
        }

        if (!server->listen()) {
            SYLAR_LOG_ERROR(g_logger) << "服务器 listen 失败";
            return;
        }

        std::cout << "[服务端] 监听 " << addr->toString() << "\n";

        // 接受连接
        sylar::Socket::ptr client = server->accept();
        if (!client) {
            SYLAR_LOG_ERROR(g_logger) << "accept 失败";
            return;
        }

        std::cout << "[服务端] 接受连接: " << client->getRemoteAddress()->toString() << "\n";

        // 创建 SocketStream
        sylar::SocketStream::ptr stream(new sylar::SocketStream(client));

        // 读取数据
        char buf[1024];
        int len = stream->read(buf, sizeof(buf));
        if (len > 0) {
            buf[len] = '\0';
            std::cout << "[服务端] 收到: " << buf << " (" << len << " 字节)\n";

            // 回复数据
            const char* response = "Hello from server";
            stream->write(response, strlen(response));
            std::cout << "[服务端] 发送: " << response << "\n";
        }

        stream->close(); });

    // 客户端（延迟启动，确保服务器先启动）
    iom.schedule([]()
                 {
        sleep(1);  // 等待服务器启动

        // 创建客户端 Socket
        sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
        sylar::Address::ptr addr = sylar::Address::LookupAny("127.0.0.1:8090");

        if (!sock->connect(addr)) {
            SYLAR_LOG_ERROR(g_logger) << "客户端 connect 失败";
            return;
        }

        std::cout << "[客户端] 连接到 " << addr->toString() << "\n";

        // 创建 SocketStream
        sylar::SocketStream::ptr stream(new sylar::SocketStream(sock));

        // 发送数据
        const char* message = "Hello from client";
        stream->write(message, strlen(message));
        std::cout << "[客户端] 发送: " << message << "\n";

        // 接收回复
        char buf[1024];
        int len = stream->read(buf, sizeof(buf));
        if (len > 0) {
            buf[len] = '\0';
            std::cout << "[客户端] 收到: " << buf << " (" << len << " 字节)\n";
        }

        stream->close(); });
}

// ============================================================================
// 测试2：readFixSize/writeFixSize 保证完整读写
// ============================================================================

/**
 * @brief 测试 readFixSize/writeFixSize 保证完整读写
 *
 * 测试场景：
 * 1. 发送固定长度的数据
 * 2. 使用 readFixSize 保证读取完整
 * 3. 验证数据完整性
 */
void test_fix_size()
{
    std::cout << "\n========== readFixSize/writeFixSize 测试 ==========\n";

    sylar::IOManager iom(2);

    // 服务端
    iom.schedule([]()
                 {
        sylar::Socket::ptr server = sylar::Socket::CreateTCPSocket();
        sylar::Address::ptr addr = sylar::Address::LookupAny("127.0.0.1:8091");

        server->bind(addr);
        server->listen();

        std::cout << "[服务端] 监听 " << addr->toString() << "\n";

        sylar::Socket::ptr client = server->accept();
        sylar::SocketStream::ptr stream(new sylar::SocketStream(client));

        std::cout << "[服务端] 接受连接\n";

        // 先读取长度（4字节）
        uint32_t len;
        int rt = stream->readFixSize(&len, sizeof(len));
        if (rt <= 0) {
            std::cout << "[服务端] 读取长度失败\n";
            return;
        }

        std::cout << "[服务端] 消息长度: " << len << " 字节\n";

        // 根据长度读取消息内容
        char* buf = new char[len + 1];
        rt = stream->readFixSize(buf, len);
        if (rt <= 0) {
            std::cout << "[服务端] 读取消息失败\n";
            delete[] buf;
            return;
        }

        buf[len] = '\0';
        std::cout << "[服务端] 收到完整消息: " << buf << "\n";

        delete[] buf;
        stream->close(); });

    // 客户端
    iom.schedule([]()
                 {
        sleep(1);

        sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
        sylar::Address::ptr addr = sylar::Address::LookupAny("127.0.0.1:8091");

        sock->connect(addr);
        sylar::SocketStream::ptr stream(new sylar::SocketStream(sock));

        std::cout << "[客户端] 连接成功\n";

        // 发送协议：[4字节长度][消息内容]
        const char* message = "This is a test message for readFixSize!";
        uint32_t len = strlen(message);

        // 先发送长度
        stream->writeFixSize(&len, sizeof(len));
        std::cout << "[客户端] 发送长度: " << len << "\n";

        // 再发送消息内容
        stream->writeFixSize(message, len);
        std::cout << "[客户端] 发送消息: " << message << "\n";

        stream->close(); });
}

// ============================================================================
// 测试3：ByteArray 与 SocketStream 配合
// ============================================================================

/**
 * @brief 测试 ByteArray 与 SocketStream 的配合使用
 *
 * 测试场景：
 * 1. 使用 ByteArray 序列化数据
 * 2. 通过 SocketStream 发送
 * 3. 接收端用 ByteArray 反序列化
 */
void test_bytearray()
{
    std::cout << "\n========== ByteArray 与 SocketStream 配合测试 ==========\n";

    sylar::IOManager iom(2);

    // 服务端
    iom.schedule([]()
                 {
        sylar::Socket::ptr server = sylar::Socket::CreateTCPSocket();
        sylar::Address::ptr addr = sylar::Address::LookupAny("127.0.0.1:8092");

        server->bind(addr);
        server->listen();

        std::cout << "[服务端] 监听 " << addr->toString() << "\n";

        sylar::Socket::ptr client = server->accept();
        sylar::SocketStream::ptr stream(new sylar::SocketStream(client));

        std::cout << "[服务端] 接受连接\n";

        // 读取数据到 ByteArray
        sylar::ByteArray::ptr ba(new sylar::ByteArray());

        // 先读取长度
        uint32_t len;
        stream->readFixSize(&len, sizeof(len));
        std::cout << "[服务端] 数据长度: " << len << " 字节\n";

        // 读取数据到 ByteArray
        int rt = stream->read(ba, len);
        if (rt > 0) {
            ba->setPosition(0);  // 重置位置以便读取

            // 反序列化
            int32_t id = ba->readFint32();
            std::string name = ba->readStringF32();
            int32_t age = ba->readFint32();

            std::cout << "[服务端] 反序列化数据:\n";
            std::cout << "    ID: " << id << "\n";
            std::cout << "    Name: " << name << "\n";
            std::cout << "    Age: " << age << "\n";
        }

        stream->close(); });

    // 客户端
    iom.schedule([]()
                 {
        sleep(1);

        sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
        sylar::Address::ptr addr = sylar::Address::LookupAny("127.0.0.1:8092");

        sock->connect(addr);
        sylar::SocketStream::ptr stream(new sylar::SocketStream(sock));

        std::cout << "[客户端] 连接成功\n";

        // 使用 ByteArray 序列化数据
        sylar::ByteArray::ptr ba(new sylar::ByteArray());
        ba->writeFint32(12345);           // ID
        ba->writeStringF32("Alice");      // Name
        ba->writeFint32(25);              // Age

        uint32_t len = ba->getSize();
        std::cout << "[客户端] 序列化数据大小: " << len << " 字节\n";

        // 先发送长度
        stream->writeFixSize(&len, sizeof(len));

        // 发送 ByteArray 数据
        ba->setPosition(0);  // 重置位置以便读取
        stream->write(ba, len);

        std::cout << "[客户端] 发送完成\n";

        stream->close(); });
}

// ============================================================================
// 测试4：地址信息获取
// ============================================================================

/**
 * @brief 测试 SocketStream 的地址信息获取
 *
 * 测试场景：
 * 1. 获取本地地址和远端地址
 * 2. 验证地址信息的正确性
 */
void test_address_info()
{
    std::cout << "\n========== 地址信息获取测试 ==========\n";

    sylar::IOManager iom(2);

    // 服务端
    iom.schedule([]()
                 {
        sylar::Socket::ptr server = sylar::Socket::CreateTCPSocket();
        sylar::Address::ptr addr = sylar::Address::LookupAny("127.0.0.1:8093");

        server->bind(addr);
        server->listen();

        std::cout << "[服务端] 监听 " << addr->toString() << "\n";

        sylar::Socket::ptr client = server->accept();
        sylar::SocketStream::ptr stream(new sylar::SocketStream(client));

        std::cout << "[服务端] 接受连接\n";
        std::cout << "    本地地址: " << stream->getLocalAddressString() << "\n";
        std::cout << "    远端地址: " << stream->getRemoteAddressString() << "\n";

        sleep(1);
        stream->close(); });

    // 客户端
    iom.schedule([]()
                 {
        sleep(1);

        sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
        sylar::Address::ptr addr = sylar::Address::LookupAny("127.0.0.1:8093");

        sock->connect(addr);
        sylar::SocketStream::ptr stream(new sylar::SocketStream(sock));

        std::cout << "[客户端] 连接成功\n";
        std::cout << "    本地地址: " << stream->getLocalAddressString() << "\n";
        std::cout << "    远端地址: " << stream->getRemoteAddressString() << "\n";

        sleep(2);
        stream->close(); });
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char **argv)
{
    std::cout << "========================================\n";
    std::cout << "  Stream + SocketStream 模块测试\n";
    std::cout << "========================================\n";

    // 测试1：基本读写
    test_basic_read_write();
    sleep(3);

    // 测试2：固定长度读写
    test_fix_size();
    sleep(3);

    // 测试3：ByteArray 配合
    test_bytearray();
    sleep(3);

    // 测试4：地址信息
    test_address_info();
    sleep(3);

    std::cout << "\n========================================\n";
    std::cout << "  所有测试完成\n";
    std::cout << "========================================\n";

    return 0;
}
