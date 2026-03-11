/**
 * @file test_endian.cc
 * @brief Endian 模块测试
 * @author sylar.yin
 * @date 2026-02-14
 */

#include "sylar/base/endian.h"
#include "log/logger.h"
#include <iostream>

static base::Logger::ptr g_logger = BASE_LOG_ROOT();

void test_byteswap()
{
    BASE_LOG_INFO(g_logger) << "========== 测试字节序转换 ==========";

    // 测试 16 位
    uint16_t val16 = 0x1234;
    uint16_t swapped16 = sylar::byteswap(val16);
    BASE_LOG_INFO(g_logger) << "uint16_t: 0x" << std::hex << val16
                             << " -> 0x" << swapped16 << std::dec;

    // 测试 32 位
    uint32_t val32 = 0x12345678;
    uint32_t swapped32 = sylar::byteswap(val32);
    BASE_LOG_INFO(g_logger) << "uint32_t: 0x" << std::hex << val32
                             << " -> 0x" << swapped32 << std::dec;

    // 测试 64 位
    uint64_t val64 = 0x123456789ABCDEF0;
    uint64_t swapped64 = sylar::byteswap(val64);
    BASE_LOG_INFO(g_logger) << "uint64_t: 0x" << std::hex << val64
                             << " -> 0x" << swapped64 << std::dec;
}

void test_conditional_swap()
{
    BASE_LOG_INFO(g_logger) << "\n========== 测试条件字节序转换 ==========";

#if SYLAR_BYTE_ORDER == SYLAR_LITTLE_ENDIAN
    BASE_LOG_INFO(g_logger) << "当前系统: 小端 (Little Endian)";
#else
    BASE_LOG_INFO(g_logger) << "当前系统: 大端 (Big Endian)";
#endif

    uint32_t val = 0x12345678;

    // 在小端机器上会转换，大端机器上不转换
    uint32_t result1 = sylar::byteswapOnLittleEndian(val);
    BASE_LOG_INFO(g_logger) << "byteswapOnLittleEndian(0x" << std::hex << val
                             << ") = 0x" << result1 << std::dec;

    // 在大端机器上会转换，小端机器上不转换
    uint32_t result2 = sylar::byteswapOnBigEndian(val);
    BASE_LOG_INFO(g_logger) << "byteswapOnBigEndian(0x" << std::hex << val
                             << ") = 0x" << result2 << std::dec;
}

void test_network_byte_order()
{
    BASE_LOG_INFO(g_logger) << "\n========== 测试网络字节序转换 ==========";

    // 模拟端口号转换（网络字节序是大端）
    uint16_t port = 8080;
    uint16_t network_port = sylar::byteswapOnLittleEndian(port);
    BASE_LOG_INFO(g_logger) << "主机字节序端口: " << port;
    BASE_LOG_INFO(g_logger) << "网络字节序端口: 0x" << std::hex << network_port << std::dec;

    // 模拟 IP 地址转换
    uint32_t ip = 0xC0A80101; // 192.168.1.1
    uint32_t network_ip = sylar::byteswapOnLittleEndian(ip);
    BASE_LOG_INFO(g_logger) << "主机字节序 IP: 0x" << std::hex << ip;
    BASE_LOG_INFO(g_logger) << "网络字节序 IP: 0x" << network_ip << std::dec;
}

int main()
{
    test_byteswap();
    test_conditional_swap();
    test_network_byte_order();

    BASE_LOG_INFO(g_logger) << "\n========== Endian 模块测试完成 ==========";
    return 0;
}
