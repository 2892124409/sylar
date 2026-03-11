#include "address.h"
#include "log/logger.h"
#include "sylar/base/endian.h"
#include <sstream>
#include <netdb.h>
#include <ifaddrs.h>
#include <stddef.h>
#include <cstring>

namespace sylar
{

    // ============================================================================
    // 全局日志器 - 用于记录网络地址相关的日志信息
    // static: 文件作用域，只在当前编译单元内可见，避免命名冲突
    // ============================================================================
    static base::Logger::ptr g_logger = BASE_LOG_NAME("system");

    // ============================================================================
    // 辅助函数：创建掩码
    // 用于计算子网掩码
    // 例如：prefix_len=24 时，CreateMask<uint32_t>(24) 返回 0x00FFFFFF
    // 原理：(1 << (32-24)) - 1 = (1 << 8) - 1 = 256 - 1 = 255 = 0x00FFFFFF
    // ============================================================================
    template <class T>
    static T CreateMask(uint32_t bits)
    {
        return (1 << (sizeof(T) * 8 - bits)) - 1;
    }

    // ============================================================================
    // 辅助函数：计算二进制中 1 的个数
    // 用于从子网掩码计算前缀长度
    // 例如：0xFFFFFF00 有 24 个 1，所以前缀长度是 24
    // 算法：利用 n &= (n-1) 可以消除最右边的 1 的特性
    // ============================================================================
    template <class T>
    static uint32_t CountBytes(T value)
    {
        uint32_t result = 0;
        for (; value; ++result)
        {
            value &= value - 1; // 每次消除最右边的 1
        }
        return result;
    }

    // ============================================================================
    // Address 基类实现
    // ============================================================================

    // 获取地址族 (AF_INET, AF_INET6, AF_UNIX 等)
    int Address::getFamily() const
    {
        return getAddr()->sa_family;
    }

    // 将地址转换为字符串形式
    // 内部调用虚函数 insert() 实现多态输出
    std::string Address::toString() const
    {
        std::stringstream ss;
        insert(ss);
        return ss.str();
    }

    // 小于运算符重载 - 用于地址排序
    // 比较规则：先按字节序比较，短的地址排在前面
    bool Address::operator<(const Address &rhs) const
    {
        // 取两个地址长度的较小值
        socklen_t minlen = std::min(getAddrLen(), rhs.getAddrLen());
        // 逐字节比较
        int result = memcmp(getAddr(), rhs.getAddr(), minlen);
        if (result < 0)
        {
            return true;
        }
        else if (result > 0)
        {
            return false;
        }
        else if (getAddrLen() < rhs.getAddrLen())
        {
            // 内容相同但长度不同，短的排在前面
            return true;
        }
        return false;
    }

    // 相等运算符重载 - 用于地址比较
    bool Address::operator==(const Address &rhs) const
    {
        return getAddrLen() == rhs.getAddrLen() &&
               memcmp(getAddr(), rhs.getAddr(), getAddrLen()) == 0;
    }

    // 不等运算符重载
    bool Address::operator!=(const Address &rhs) const
    {
        return !(*this == rhs);
    }

    // ============================================================================
    // 工厂函数：根据 sockaddr 创建对应的 Address 子类对象
    // 这是实现多态的核心方法
    // ============================================================================
    Address::ptr Address::Create(const sockaddr *addr, socklen_t addrlen)
    {
        if (addr == nullptr)
        {
            return nullptr;
        }

        Address::ptr result;
        // 根据地址族 (sa_family) 判断具体类型，创建对应的子类对象
        switch (addr->sa_family)
        {
        case AF_INET: // IPv4 地址，使用智能指针的reset释放原来的对象，并接管新对象
            result.reset(new IPv4Address(*(const sockaddr_in *)addr));
            break;
        case AF_INET6: // IPv6 地址
            result.reset(new IPv6Address(*(const sockaddr_in6 *)addr));
            break;
        default: // 未知地址类型
            result.reset(new UnknownAddress(*addr));
            break;
        }
        return result;
    }

    // ============================================================================
    // DNS 解析函数 - 将域名转换为地址列表
    // 支持格式：
    //   - 域名: "www.baidu.com"
    //   - 域名:端口: "www.baidu.com:80"
    //   - IPv6: "[2001:db8::1]:8080"
    // ============================================================================
    bool Address::Lookup(std::vector<Address::ptr> &result, const std::string &host,
                         int family, int type, int protocol)
    {
        addrinfo hints, *results, *next;
        // 初始化查询提示结构
        hints.ai_flags = 0;           // 标志位
        hints.ai_family = family;     // 地址族 (AF_INET, AF_INET6, AF_UNSPEC)
        hints.ai_socktype = type;     // Socket 类型 (SOCK_STREAM, SOCK_DGRAM)
        hints.ai_protocol = protocol; // 协议类型 (IPPROTO_TCP, IPPROTO_UDP)
        hints.ai_addrlen = 0;
        hints.ai_canonname = NULL;
        hints.ai_addr = NULL;
        hints.ai_next = NULL;

        std::string node;
        const char *service = NULL;

        // 解析 IPv6 地址格式 [IPv6]:port
        // 例如："[2001:db8::1]:8080" -> node="2001:db8::1", service="8080"
        if (!host.empty() && host[0] == '[')
        {
            const char *endipv6 = (const char *)memchr(host.c_str() + 1, ']', host.size() - 1);
            if (endipv6)
            {
                if (*(endipv6 + 1) == ':')
                {
                    service = endipv6 + 2;
                }
                node = host.substr(1, endipv6 - host.c_str() - 1);
            }
        }

        // 解析 host:port 格式
        // 例如："www.baidu.com:80" -> node="www.baidu.com", service="80"
        if (node.empty())
        {
            service = (const char *)memchr(host.c_str(), ':', host.size());
            if (service)
            {
                // 检查是否有多个冒号（IPv6 地址），避免误判
                if (!memchr(service + 1, ':', host.c_str() + host.size() - service - 1))
                {
                    node = host.substr(0, service - host.c_str());
                    ++service;
                }
            }
        }

        // 如果没有解析出 node，则整个 host 就是 node
        if (node.empty())
        {
            node = host;
        }

        // 调用系统的 DNS 解析函数
        int error = getaddrinfo(node.c_str(), service, &hints, &results);
        if (error)
        {
            BASE_LOG_DEBUG(g_logger) << "Address::Lookup getaddress(" << host << ", "
                                      << family << ", " << type << ") err=" << error
                                      << " errstr=" << gai_strerror(error);
            return false;
        }

        // 遍历解析结果，创建 Address 对象
        next = results;
        while (next)
        {
            result.push_back(Create(next->ai_addr, (socklen_t)next->ai_addrlen));
            BASE_LOG_DEBUG(g_logger) << "family:" << next->ai_family
                                      << ", sock type:" << next->ai_socktype;
            next = next->ai_next;
        }

        // 释放 getaddrinfo 分配的内存
        freeaddrinfo(results);
        return !result.empty();
    }

    // ============================================================================
    // DNS 解析便捷函数 - 返回任意一个匹配的地址
    // ============================================================================
    Address::ptr Address::LookupAny(const std::string &host,
                                    int family, int type, int protocol)
    {
        std::vector<Address::ptr> result;
        if (Lookup(result, host, family, type, protocol))
        {
            return result[0]; // 返回第一个结果
        }
        return nullptr;
    }

    // ============================================================================
    // DNS 解析便捷函数 - 返回任意一个 IP 地址（排除 Unix 域套接字等）
    // ============================================================================
    IPAddress::ptr Address::LookupAnyIPAddress(const std::string &host,
                                               int family, int type, int protocol)
    {
        std::vector<Address::ptr> result;
        if (Lookup(result, host, family, type, protocol))
        {
            for (auto &i : result)
            {
                // 尝试转换为 IPAddress 指针
                IPAddress::ptr v = std::dynamic_pointer_cast<IPAddress>(i);
                if (v)
                {
                    return v;
                }
            }
        }
        return nullptr;
    }

    // ============================================================================
    // 获取本机所有网卡的地址信息
    // 返回格式：multimap<网卡名, (地址, 前缀长度)>
    // ============================================================================
    bool Address::GetInterfaceAddresses(std::multimap<std::string,
                                                      std::pair<Address::ptr, uint32_t>> &result,
                                        int family)
    {
        struct ifaddrs *next, *results;
        // 获取本机网卡地址信息
        if (getifaddrs(&results) != 0)
        {
            BASE_LOG_DEBUG(g_logger) << "Address::GetInterfaceAddresses getifaddrs "
                                      << " err=" << errno
                                      << " errstr=" << strerror(errno);
            return false;
        }

        try
        {
            // 遍历所有网卡
            for (next = results; next; next = next->ifa_next)
            {
                Address::ptr addr;
                uint32_t prefix_len = ~0u;
                // 如果指定了地址族，跳过不匹配的网卡
                if (family != AF_UNSPEC && family != next->ifa_addr->sa_family)
                {
                    continue;
                }
                switch (next->ifa_addr->sa_family)
                {
                case AF_INET: // IPv4 地址
                {
                    addr = Create(next->ifa_addr, sizeof(sockaddr_in));
                    // 计算子网前缀长度
                    uint32_t netmask = ((sockaddr_in *)next->ifa_netmask)->sin_addr.s_addr;
                    prefix_len = CountBytes(netmask);
                }
                break;
                case AF_INET6: // IPv6 地址
                {
                    addr = Create(next->ifa_addr, sizeof(sockaddr_in6));
                    // IPv6 子网掩码计算（16 字节逐个计算）
                    in6_addr &netmask = ((sockaddr_in6 *)next->ifa_netmask)->sin6_addr;
                    prefix_len = 0;
                    for (int i = 0; i < 16; ++i)
                    {
                        prefix_len += CountBytes(netmask.s6_addr[i]);
                    }
                }
                break;
                default:
                    break;
                }

                if (addr)
                {
                    // 插入结果：网卡名 -> (地址, 前缀长度)
                    result.insert(std::make_pair(next->ifa_name,
                                                 std::make_pair(addr, prefix_len)));
                }
            }
        }
        catch (...)
        {
            BASE_LOG_ERROR(g_logger) << "Address::GetInterfaceAddresses exception";
            freeifaddrs(results);
            return false;
        }
        freeifaddrs(results);
        return !result.empty();
    }

    // ============================================================================
    // 获取指定网卡的地址信息
    // iface: 网卡名称，空或 "*" 表示任意地址 (0.0.0.0 或 ::)
    // ============================================================================
    bool Address::GetInterfaceAddresses(std::vector<std::pair<Address::ptr, uint32_t>> &result,
                                        const std::string &iface, int family)
    {
        // 如果是通配符，返回任意地址
        if (iface.empty() || iface == "*")
        {
            if (family == AF_INET || family == AF_UNSPEC)
            {
                // IPv4 任意地址：0.0.0.0
                result.push_back(std::make_pair(Address::ptr(new IPv4Address()), 0u));
            }
            if (family == AF_INET6 || family == AF_UNSPEC)
            {
                // IPv6 任意地址::
                result.push_back(std::make_pair(Address::ptr(new IPv6Address()), 0u));
            }
            return true;
        }

        // 获取所有网卡信息
        std::multimap<std::string, std::pair<Address::ptr, uint32_t>> results;

        if (!GetInterfaceAddresses(results, family))
        {
            return false;
        }

        // 提取指定网卡的信息
        auto its = results.equal_range(iface);
        for (; its.first != its.second; ++its.first)
        {
            result.push_back(its.first->second);
        }
        return !result.empty();
    }

    // ============================================================================
    // IPAddress 类实现
    // ============================================================================

    // 通过 IP 地址字符串创建 IPAddress 对象
    // 使用 AI_NUMERICHOST 标志，要求 address 是数字格式的 IP 地址
    IPAddress::ptr IPAddress::Create(const char *address, uint16_t port)
    {
        addrinfo hints, *results;
        memset(&hints, 0, sizeof(addrinfo));

        hints.ai_flags = AI_NUMERICHOST; // 不进行 DNS 解析，只接受数字 IP
        hints.ai_family = AF_UNSPEC;     // 不限制地址族

        int error = getaddrinfo(address, NULL, &hints, &results);
        if (error)
        {
            BASE_LOG_DEBUG(g_logger) << "IPAddress::Create(" << address
                                      << ", " << port << ") error=" << error
                                      << " errno=" << errno
                                      << " errstr=" << strerror(errno);
            return nullptr;
        }

        try
        {
            // 创建 IPAddress 对象并设置端口
            IPAddress::ptr result = std::dynamic_pointer_cast<IPAddress>(
                Address::Create(results->ai_addr, (socklen_t)results->ai_addrlen));
            if (result)
            {
                result->setPort(port);
            }
            freeaddrinfo(results);
            return result;
        }
        catch (...)
        {
            freeaddrinfo(results);
            return nullptr;
        }
    }

    // ============================================================================
    // IPv4Address 类实现
    // ============================================================================

    // 通过 IPv4 地址字符串创建 IPv4Address 对象
    // 例如：IPv4Address::Create("192.168.1.100", 8080)
    IPv4Address::ptr IPv4Address::Create(const char *address, uint16_t port)
    {
        // 等价于 IPv4Address::ptr rt = std::make_shared<IPv4Address>();
        IPv4Address::ptr rt(new IPv4Address);
        // 设置端口（转换为网络字节序）
        rt->m_addr.sin_port = byteswapOnLittleEndian(port);
        // 解析 IP 地址字符串
        int result = inet_pton(AF_INET, address, &rt->m_addr.sin_addr);
        if (result <= 0)
        {
            BASE_LOG_DEBUG(g_logger) << "IPv4Address::Create(" << address << ", "
                                      << port << ") rt=" << result
                                      << " errno=" << errno
                                      << " errstr=" << strerror(errno);
            return nullptr;
        }
        return rt;
    }

    // 通过 sockaddr_in 结构构造
    IPv4Address::IPv4Address(const sockaddr_in &address)
    {
        m_addr = address;
    }

    // 通过 IP 地址（主机序）和端口构造
    IPv4Address::IPv4Address(uint32_t address, uint16_t port)
    {
        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sin_family = AF_INET;
        // 端口转为网络字节序
        m_addr.sin_port = byteswapOnLittleEndian(port);
        // IP 地址转为网络字节序
        m_addr.sin_addr.s_addr = byteswapOnLittleEndian(address);
    }

    // 获取 sockaddr 指针（常量版本）
    const sockaddr *IPv4Address::getAddr() const
    {
        return (sockaddr *)&m_addr;
    }

    // 获取 sockaddr 指针（可修改版本）
    sockaddr *IPv4Address::getAddr()
    {
        return (sockaddr *)&m_addr;
    }

    // 获取 sockaddr 长度
    socklen_t IPv4Address::getAddrLen() const
    {
        return sizeof(m_addr);
    }

    // 流式输出 IPv4 地址
    // 格式：xxx.xxx.xxx.xxx:port
    std::ostream &IPv4Address::insert(std::ostream &os) const
    {
        // 将网络字节序的 IP 转为主机字节序
        uint32_t addr = byteswapOnLittleEndian(m_addr.sin_addr.s_addr);
        // 按字节输出，每字节用点分隔
        os << ((addr >> 24) & 0xff) << "."
           << ((addr >> 16) & 0xff) << "."
           << ((addr >> 8) & 0xff) << "."
           << (addr & 0xff);
        // 输出端口
        os << ":" << byteswapOnLittleEndian(m_addr.sin_port);
        return os; // 返回的是原参数的引用，支持链式调用
    }

    // 获取端口号（主机字节序）
    uint32_t IPv4Address::getPort() const
    {
        return byteswapOnLittleEndian(m_addr.sin_port);
    }

    // 设置端口号（自动转为网络字节序）
    void IPv4Address::setPort(uint16_t v)
    {
        m_addr.sin_port = byteswapOnLittleEndian(v);
    }

    // ============================================================================
    // 计算广播地址
    // 广播地址 = IP 地址 | (~子网掩码)
    // 例如：192.168.1.100/24 的广播地址是 192.168.1.255
    // ============================================================================
    IPAddress::ptr IPv4Address::broadcastAddress(uint32_t prefix_len)
    {
        if (prefix_len > 32)
        {
            return nullptr; // 前缀长度不能超过 32
        }

        sockaddr_in baddr(m_addr);
        // 将主机部分全部置 1
        baddr.sin_addr.s_addr |= byteswapOnLittleEndian(
            CreateMask<uint32_t>(prefix_len));
        return IPv4Address::ptr(new IPv4Address(baddr));
    }

    // ============================================================================
    // 计算网段地址
    // 网段地址 = IP 地址 & 子网掩码
    // 例如：192.168.1.100/24 的网段地址是 192.168.1.0
    // ============================================================================
    IPAddress::ptr IPv4Address::networkAddress(uint32_t prefix_len)
    {
        if (prefix_len > 32)
        {
            return nullptr; // 前缀长度不能超过 32
        }

        sockaddr_in baddr(m_addr);
        // 将主机部分全部置 0
        baddr.sin_addr.s_addr &= byteswapOnLittleEndian(
            ~CreateMask<uint32_t>(prefix_len));
        return IPv4Address::ptr(new IPv4Address(baddr));
    }

    // ============================================================================
    // 计算子网掩码
    // 例如：/24 的子网掩码是 255.255.255.0
    // ============================================================================
    IPAddress::ptr IPv4Address::subnetMask(uint32_t prefix_len)
    {
        sockaddr_in subnet;
        memset(&subnet, 0, sizeof(subnet));
        subnet.sin_family = AF_INET;
        // 子网掩码：网络部分全 1，主机部分全 0
        subnet.sin_addr.s_addr = ~byteswapOnLittleEndian(CreateMask<uint32_t>(prefix_len));
        return IPv4Address::ptr(new IPv4Address(subnet));
    }

    // ============================================================================
    // IPv6Address 类实现
    // ============================================================================

    // 通过 IPv6 地址字符串创建 IPv6Address 对象
    IPv6Address::ptr IPv6Address::Create(const char *address, uint16_t port)
    {
        IPv6Address::ptr rt(new IPv6Address);
        rt->m_addr.sin6_port = byteswapOnLittleEndian(port);
        // 解析 IPv6 地址字符串
        int result = inet_pton(AF_INET6, address, &rt->m_addr.sin6_addr);
        if (result <= 0)
        {
            BASE_LOG_DEBUG(g_logger) << "IPv6Address::Create(" << address << ", "
                                      << port << ") rt=" << result
                                      << " errno=" << errno
                                      << " errstr=" << strerror(errno);
            return nullptr;
        }
        return rt;
    }

    // 默认构造 - 创建 IPv6 任意地址 (::)
    IPv6Address::IPv6Address()
    {
        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sin6_family = AF_INET6;
    }

    // 通过 sockaddr_in6 结构构造
    IPv6Address::IPv6Address(const sockaddr_in6 &address)
    {
        m_addr = address;
    }

    // 通过 16 字节地址数组和端口构造
    IPv6Address::IPv6Address(const uint8_t address[16], uint16_t port)
    {
        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sin6_family = AF_INET6;
        m_addr.sin6_port = byteswapOnLittleEndian(port);
        memcpy(&m_addr.sin6_addr.s6_addr, address, 16);
    }

    const sockaddr *IPv6Address::getAddr() const
    {
        return (sockaddr *)&m_addr;
    }

    sockaddr *IPv6Address::getAddr()
    {
        return (sockaddr *)&m_addr;
    }

    socklen_t IPv6Address::getAddrLen() const
    {
        return sizeof(m_addr);
    }

    // ============================================================================
    // 流式输出 IPv6 地址
    // 格式：[xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx]:port
    // 支持零压缩 (::)
    // ============================================================================
    std::ostream &IPv6Address::insert(std::ostream &os) const
    {
        os << "[";
        // IPv6 地址是 128 位，可以看作 8 个 16 位段
        uint16_t *addr = (uint16_t *)m_addr.sin6_addr.s6_addr;
        bool used_zeros = false; // 标记是否使用了零压缩
        for (size_t i = 0; i < 8; ++i)
        {
            // 跳过前导零
            if (addr[i] == 0 && !used_zeros)
            {
                continue;
            }
            // 检测到连续零，使用零压缩
            if (i && addr[i - 1] == 0 && !used_zeros)
            {
                os << ":";
                used_zeros = true;
            }
            if (i)
            {
                os << ":";
            }
            // 输出 16 位段（十六进制）
            os << std::hex << (int)byteswapOnLittleEndian(addr[i]) << std::dec;
        }

        // 如果最后一段是零，添加 ::
        if (!used_zeros && addr[7] == 0)
        {
            os << "::";
        }

        os << "]:" << byteswapOnLittleEndian(m_addr.sin6_port);
        return os;
    }

    uint32_t IPv6Address::getPort() const
    {
        return byteswapOnLittleEndian(m_addr.sin6_port);
    }

    void IPv6Address::setPort(uint16_t v)
    {
        m_addr.sin6_port = byteswapOnLittleEndian(v);
    }

    // ============================================================================
    // 计算 IPv6 广播地址
    // ============================================================================
    IPAddress::ptr IPv6Address::broadcastAddress(uint32_t prefix_len)
    {
        sockaddr_in6 baddr(m_addr);
        // 处理边界字节
        baddr.sin6_addr.s6_addr[prefix_len / 8] |=
            CreateMask<uint8_t>(prefix_len % 8);
        // 后续字节全部置为 0xFF
        for (int i = prefix_len / 8 + 1; i < 16; ++i)
        {
            baddr.sin6_addr.s6_addr[i] = 0xff;
        }
        return IPv6Address::ptr(new IPv6Address(baddr));
    }

    // ============================================================================
    // 计算 IPv6 网段地址
    // ============================================================================
    IPAddress::ptr IPv6Address::networkAddress(uint32_t prefix_len)
    {
        sockaddr_in6 baddr(m_addr);
        // 处理边界字节
        baddr.sin6_addr.s6_addr[prefix_len / 8] &=
            CreateMask<uint8_t>(prefix_len % 8);
        // 后续字节全部置为 0x00
        for (int i = prefix_len / 8 + 1; i < 16; ++i)
        {
            baddr.sin6_addr.s6_addr[i] = 0x00;
        }
        return IPv6Address::ptr(new IPv6Address(baddr));
    }

    // ============================================================================
    // 计算 IPv6 子网掩码
    // ============================================================================
    IPAddress::ptr IPv6Address::subnetMask(uint32_t prefix_len)
    {
        sockaddr_in6 subnet;
        memset(&subnet, 0, sizeof(subnet));
        subnet.sin6_family = AF_INET6;
        // 边界字节
        subnet.sin6_addr.s6_addr[prefix_len / 8] =
            ~CreateMask<uint8_t>(prefix_len % 8);

        // 前面字节全部置为 0xFF
        for (uint32_t i = 0; i < prefix_len / 8; ++i)
        {
            subnet.sin6_addr.s6_addr[i] = 0xff;
        }
        return IPv6Address::ptr(new IPv6Address(subnet));
    }

    // ============================================================================
    // UnixAddress 类实现 - Unix 域套接字地址
    // 用于本地进程间通信 (IPC)
    // ============================================================================

    // Unix 域套接字路径的最大长度
    static const size_t MAX_PATH_LEN = sizeof(((sockaddr_un *)0)->sun_path) - 1;

    // 默认构造 - 创建空的 Unix 地址
    UnixAddress::UnixAddress()
    {
        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sun_family = AF_UNIX;
        m_length = offsetof(sockaddr_un, sun_path) + MAX_PATH_LEN;
    }

    // 通过路径构造
    UnixAddress::UnixAddress(const std::string &path)
    {
        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sun_family = AF_UNIX;
        m_length = path.size() + 1;

        // 处理抽象命名空间（路径以 '\0' 开头）
        if (!path.empty() && path[0] == '\0')
        {
            --m_length;
        }

        // 检查路径长度
        if (m_length > sizeof(m_addr.sun_path))
        {
            throw std::logic_error("path too long");
        }
        memcpy(m_addr.sun_path, path.c_str(), m_length);
        m_length += offsetof(sockaddr_un, sun_path);
    }

    // 设置地址长度
    void UnixAddress::setAddrLen(uint32_t v)
    {
        m_length = v;
    }

    const sockaddr *UnixAddress::getAddr() const
    {
        return (sockaddr *)&m_addr;
    }

    sockaddr *UnixAddress::getAddr()
    {
        return (sockaddr *)&m_addr;
    }

    socklen_t UnixAddress::getAddrLen() const
    {
        return m_length;
    }

    // 获取路径字符串
    std::string UnixAddress::getPath() const
    {
        std::stringstream ss;
        // 处理抽象命名空间
        if (m_length > offsetof(sockaddr_un, sun_path) && m_addr.sun_path[0] == '\0')
        {
            ss << "\\0" << std::string(m_addr.sun_path + 1, m_length - offsetof(sockaddr_un, sun_path) - 1);
        }
        else
        {
            ss << m_addr.sun_path;
        }
        return ss.str();
    }

    // 流式输出 Unix 地址
    std::ostream &UnixAddress::insert(std::ostream &os) const
    {
        if (m_length > offsetof(sockaddr_un, sun_path) && m_addr.sun_path[0] == '\0')
        {
            return os << "\\0" << std::string(m_addr.sun_path + 1, m_length - offsetof(sockaddr_un, sun_path) - 1);
        }
        return os << m_addr.sun_path;
    }

    // ============================================================================
    // UnknownAddress 类实现 - 未知地址类型的兜底处理
    // ============================================================================

    UnknownAddress::UnknownAddress(int family)
    {
        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sa_family = family;
    }

    UnknownAddress::UnknownAddress(const sockaddr &addr)
    {
        m_addr = addr;
    }

    const sockaddr *UnknownAddress::getAddr() const
    {
        return &m_addr;
    }

    sockaddr *UnknownAddress::getAddr()
    {
        return &m_addr;
    }

    socklen_t UnknownAddress::getAddrLen() const
    {
        return sizeof(m_addr);
    }

    std::ostream &UnknownAddress::insert(std::ostream &os) const
    {
        os << "[UnknownAddress family=" << m_addr.sa_family << "]";
        return os;
    }

    // ============================================================================
    // 全局函数实现
    // ============================================================================

    // 流式输出地址 - 重载 << 运算符
    std::ostream &operator<<(std::ostream &os, const Address &addr)
    {
        return addr.insert(os);
    }

} // namespace sylar
