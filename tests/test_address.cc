#include "sylar/net/address.h"
#include "log/logger.h"
#include "sylar/base/endian.h"
#include <iostream>

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// ============================================================================
// IPv4Address 测试
// ============================================================================

void test_ipv4_constructors()
{
    std::cout << "\n========== IPv4Address 构造函数测试 ==========" << std::endl;

    // 测试1：默认构造
    sylar::IPv4Address addr1;
    std::cout << "默认构造: " << addr1.toString() << std::endl;

    // 测试2：从数值构造
    sylar::IPv4Address addr2(0x7F000001, 8080); // 127.0.0.1:8080
    std::cout << "数值构造: " << addr2.toString() << std::endl;

    // 测试3：从 sockaddr_in 构造
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = sylar::byteswapOnLittleEndian((uint16_t)80);
    sa.sin_addr.s_addr = sylar::byteswapOnLittleEndian((uint32_t)0xC0A80164); // 192.168.1.100
    sylar::IPv4Address addr3(sa);
    std::cout << "sockaddr_in构造: " << addr3.toString() << std::endl;
}

void test_ipv4_create()
{
    std::cout << "\n========== IPv4Address::Create 测试 ==========" << std::endl;

    // 测试1：合法地址
    sylar::IPv4Address::ptr addr1 = sylar::IPv4Address::Create("192.168.1.100", 8080);
    if (addr1)
    {
        std::cout << "合法地址: " << addr1->toString() << std::endl;
    }
    else
    {
        std::cout << "合法地址: 创建失败" << std::endl;
    }

    // 测试2：非法地址
    sylar::IPv4Address::ptr addr2 = sylar::IPv4Address::Create("invalid", 8080);
    if (addr2)
    {
        std::cout << "非法地址: " << addr2->toString() << std::endl;
    }
    else
    {
        std::cout << "非法地址: 正确返回 nullptr" << std::endl;
    }

    // 测试3：localhost
    sylar::IPv4Address::ptr addr3 = sylar::IPv4Address::Create("127.0.0.1", 0);
    if (addr3)
    {
        std::cout << "localhost: " << addr3->toString() << std::endl;
    }
}

void test_ipv4_subnet()
{
    std::cout << "\n========== IPv4Address 网段计算测试 ==========" << std::endl;

    // 测试地址：192.168.1.100
    sylar::IPv4Address::ptr addr = sylar::IPv4Address::Create("192.168.1.100", 0);

    // 测试1：广播地址 /24
    sylar::IPAddress::ptr broadcast = addr->broadcastAddress(24);
    std::cout << "广播地址 (/24): " << broadcast->toString() << std::endl;
    std::cout << "预期: 192.168.1.255:0" << std::endl;

    // 测试2：网段地址 /24
    sylar::IPAddress::ptr network = addr->networkAddress(24);
    std::cout << "网段地址 (/24): " << network->toString() << std::endl;
    std::cout << "预期: 192.168.1.0:0" << std::endl;

    // 测试3：子网掩码 /24
    sylar::IPAddress::ptr mask = addr->subnetMask(24);
    std::cout << "子网掩码 (/24): " << mask->toString() << std::endl;
    std::cout << "预期: 255.255.255.0:0" << std::endl;

    // 测试4：不同前缀长度 /16
    auto broadcast16 = addr->broadcastAddress(16);
    std::cout << "广播地址 (/16): " << broadcast16->toString() << std::endl;
    std::cout << "预期: 192.168.255.255:0" << std::endl;
}

void test_ipv4_comparison()
{
    std::cout << "\n========== IPv4Address 比较测试 ==========" << std::endl;

    sylar::IPv4Address::ptr addr1 = sylar::IPv4Address::Create("192.168.1.100", 80);
    sylar::IPv4Address::ptr addr2 = sylar::IPv4Address::Create("192.168.1.100", 80);
    sylar::IPv4Address::ptr addr3 = sylar::IPv4Address::Create("192.168.1.101", 80);

    std::cout << "addr1: " << addr1->toString() << std::endl;
    std::cout << "addr2: " << addr2->toString() << std::endl;
    std::cout << "addr3: " << addr3->toString() << std::endl;

    std::cout << "addr1 == addr2: " << (*addr1 == *addr2) << " (预期: 1)" << std::endl;
    std::cout << "addr1 < addr3: " << (*addr1 < *addr3) << " (预期: 1)" << std::endl;
    std::cout << "addr1 != addr3: " << (*addr1 != *addr3) << " (预期: 1)" << std::endl;
}

// ============================================================================
// IPv6Address 测试
// ============================================================================

void test_ipv6_constructors()
{
    std::cout << "\n========== IPv6Address 构造函数测试 ==========" << std::endl;

    // 测试1：默认构造（::）
    sylar::IPv6Address addr1;
    std::cout << "默认构造: " << addr1.toString() << std::endl;

    // 测试2：从字节数组构造
    uint8_t bytes[16] = {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    sylar::IPv6Address addr2(bytes, 8080);
    std::cout << "字节数组构造: " << addr2.toString() << std::endl;
    std::cout << "预期: [2001:db8::1]:8080" << std::endl;
}

void test_ipv6_create()
{
    std::cout << "\n========== IPv6Address::Create 测试 ==========" << std::endl;

    // 测试1：localhost
    sylar::IPv6Address::ptr addr1 = sylar::IPv6Address::Create("::1", 80);
    if (addr1)
    {
        std::cout << "localhost: " << addr1->toString() << std::endl;
    }

    // 测试2：完整地址
    sylar::IPv6Address::ptr addr2 = sylar::IPv6Address::Create("2001:db8::1", 443);
    if (addr2)
    {
        std::cout << "完整地址: " << addr2->toString() << std::endl;
    }
}

void test_ipv6_zero_compression()
{
    std::cout << "\n========== IPv6Address 零压缩测试 ==========" << std::endl;

    // 测试1：开头零压缩
    uint8_t bytes1[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    sylar::IPv6Address addr1(bytes1, 0);
    std::cout << "开头零: " << addr1.toString() << " (预期: [::1]:0)" << std::endl;

    // 测试2：中间零压缩
    uint8_t bytes2[16] = {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    sylar::IPv6Address addr2(bytes2, 0);
    std::cout << "中间零: " << addr2.toString() << " (预期: [2001:db8::1]:0)" << std::endl;

    // 测试3：末尾零压缩
    uint8_t bytes3[16] = {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x01, 0x00, 0x00,
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    sylar::IPv6Address addr3(bytes3, 0);
    std::cout << "末尾零: " << addr3.toString() << " (预期: [2001:db8:1::]:0)" << std::endl;
}

// ============================================================================
// Address 工厂函数测试
// ============================================================================

void test_address_factory()
{
    std::cout << "\n========== Address::Create 工厂函数测试 ==========" << std::endl;

    // 测试1：IPv4
    struct sockaddr_in sa_in;
    sa_in.sin_family = AF_INET;
    sa_in.sin_port = sylar::byteswapOnLittleEndian((uint16_t)80);
    sa_in.sin_addr.s_addr = sylar::byteswapOnLittleEndian((uint32_t)0xC0A80101);
    sylar::Address::ptr addr1 = sylar::Address::Create((struct sockaddr *)&sa_in, sizeof(sa_in));
    std::cout << "IPv4工厂: " << addr1->toString() << " (类型: " << addr1->getFamily() << ")" << std::endl;

    // 测试2：IPv6
    struct sockaddr_in6 sa_in6;
    sa_in6.sin6_family = AF_INET6;
    sa_in6.sin6_port = sylar::byteswapOnLittleEndian((uint16_t)443);
    memset(&sa_in6.sin6_addr, 0, 16);
    sylar::Address::ptr addr2 = sylar::Address::Create((struct sockaddr *)&sa_in6, sizeof(sa_in6));
    std::cout << "IPv6工厂: " << addr2->toString() << " (类型: " << addr2->getFamily() << ")" << std::endl;
}

// ============================================================================
// DNS 解析测试
// ============================================================================

void test_dns_lookup()
{
    std::cout << "\n========== DNS 解析测试 ==========" << std::endl;

    // 测试1：域名解析
    std::vector<sylar::Address::ptr> result;
    if (sylar::Address::Lookup(result, "www.baidu.com", AF_INET))
    {
        std::cout << "www.baidu.com 解析成功:" << std::endl;
        for (size_t i = 0; i < result.size() && i < 3; ++i)
        {
            std::cout << "  [" << i << "] " << result[i]->toString() << std::endl;
        }
    }
    else
    {
        std::cout << "www.baidu.com 解析失败" << std::endl;
    }

    // 测试2：LookupAny
    sylar::Address::ptr addr = sylar::Address::LookupAny("localhost", AF_INET);
    if (addr)
    {
        std::cout << "LookupAny(localhost): " << addr->toString() << std::endl;
    }

    // 测试3：host:port 格式
    std::vector<sylar::Address::ptr> result2;
    if (sylar::Address::Lookup(result2, "www.baidu.com:80", AF_INET))
    {
        std::cout << "带端口解析: " << result2[0]->toString() << std::endl;
    }
}

// ============================================================================
// 网卡地址测试
// ============================================================================

void test_interface_addresses()
{
    std::cout << "\n========== 网卡地址测试 ==========" << std::endl;

    // 测试1：获取所有网卡
    std::multimap<std::string, std::pair<sylar::Address::ptr, uint32_t>> results;
    if (sylar::Address::GetInterfaceAddresses(results, AF_INET))
    {
        std::cout << "本机网卡信息:" << std::endl;
        for (auto &kv : results)
        {
            std::cout << "  " << kv.first << ": "
                      << kv.second.first->toString()
                      << "/" << kv.second.second << std::endl;
        }
    }
    else
    {
        std::cout << "获取网卡信息失败" << std::endl;
    }

    // 测试2：获取任意地址
    std::vector<std::pair<sylar::Address::ptr, uint32_t>> any_addrs;
    if (sylar::Address::GetInterfaceAddresses(any_addrs, "*", AF_INET))
    {
        std::cout << "任意地址:" << std::endl;
        for (auto &p : any_addrs)
        {
            std::cout << "  " << p.first->toString() << std::endl;
        }
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char **argv)
{
    std::cout << "========================================" << std::endl;
    std::cout << "        Address 模块测试程序" << std::endl;
    std::cout << "========================================" << std::endl;

    // IPv4 测试
    test_ipv4_constructors();
    test_ipv4_create();
    test_ipv4_subnet();
    test_ipv4_comparison();

    // IPv6 测试
    test_ipv6_constructors();
    test_ipv6_create();
    test_ipv6_zero_compression();

    // 工厂函数测试
    test_address_factory();

    // DNS 解析测试
    test_dns_lookup();

    // 网卡地址测试
    test_interface_addresses();

    std::cout << "\n========================================" << std::endl;
    std::cout << "        测试完成" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
