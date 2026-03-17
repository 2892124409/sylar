#ifndef SYLAR_NET_ADDRESS_H
#define SYLAR_NET_ADDRESS_H

#include <arpa/inet.h>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <vector>

namespace sylar
{

class IPAddress;

/**
 * @brief 网络地址的抽象基类
 * @details 封装了 sockaddr 结构，提供统一的地址操作接口
 */
class Address
{
  public:
    typedef std::shared_ptr<Address> ptr;

    /**
     * @brief 虚析构函数
     */
    virtual ~Address() {}

    /**
     * @brief 获取 sockaddr 指针
     */
    virtual const sockaddr* getAddr() const = 0;
    virtual sockaddr* getAddr() = 0;

    /**
     * @brief 获取 sockaddr 长度
     */
    virtual socklen_t getAddrLen() const = 0;

    /**
     * @brief 获取地址族 (AF_INET, AF_INET6, AF_UNIX 等)
     */
    int getFamily() const;

    /**
     * @brief 流式输出地址
     */
    virtual std::ostream& insert(std::ostream& os) const = 0;

    /**
     * @brief 获取可读的地址字符串
     */
    std::string toString() const;

    /**
     * @brief 小于运算符，用于排序
     */
    bool operator<(const Address& rhs) const;

    /**
     * @brief 相等运算符
     */
    bool operator==(const Address& rhs) const;

    /**
     * @brief 不等运算符
     */
    bool operator!=(const Address& rhs) const;

    /**
     * @brief 通过 sockaddr 创建 Address 对象
     * @param addr sockaddr 指针
     * @param addrlen sockaddr 长度
     * @return Address::ptr 创建的 Address 对象
     */
    static Address::ptr Create(const sockaddr* addr, socklen_t addrlen);

    /**
     * @brief DNS 解析，返回所有匹配的地址
     * @param result 输出参数，存储解析结果
     * @param host 域名或 IP 地址，支持端口格式如 "www.sylar.top:80"
     * @param family 地址族 (AF_INET, AF_INET6, AF_UNSPEC)
     * @param type socket 类型 (SOCK_STREAM, SOCK_DGRAM 等)
     * @param protocol 协议类型 (IPPROTO_TCP, IPPROTO_UDP 等)
     * @return 是否解析成功
     */
    static bool Lookup(std::vector<Address::ptr>& result,
                       const std::string& host,
                       int family = AF_UNSPEC,
                       int type = 0,
                       int protocol = 0);

    /**
     * @brief DNS 解析，返回任意一个匹配的地址
     */
    static Address::ptr LookupAny(const std::string& host,
                                  int family = AF_UNSPEC,
                                  int type = 0,
                                  int protocol = 0);

    /**
     * @brief DNS 解析，返回任意一个匹配的 IP 地址
     */
    static std::shared_ptr<IPAddress> LookupAnyIPAddress(const std::string& host,
                                                         int family = AF_UNSPEC,
                                                         int type = 0,
                                                         int protocol = 0);

    /**
     * @brief 获取本机所有网卡的地址信息
     * @param result 输出参数，存储网卡名和对应的地址及前缀长度
     * @param family 地址族
     * @return 是否获取成功
     */
    static bool GetInterfaceAddresses(std::multimap<std::string,
                                                    std::pair<Address::ptr, uint32_t>>& result,
                                      int family = AF_UNSPEC);

    /**
     * @brief 获取指定网卡的地址信息
     * @param result 输出参数，存储地址及前缀长度
     * @param iface 网卡名称，空或 "*" 表示所有网卡
     * @param family 地址族
     * @return 是否获取成功
     */
    static bool GetInterfaceAddresses(std::vector<std::pair<Address::ptr, uint32_t>>& result,
                                      const std::string& iface,
                                      int family = AF_UNSPEC);
};

/**
 * @brief IP 地址抽象基类
 */
class IPAddress : public Address
{
  public:
    typedef std::shared_ptr<IPAddress> ptr;

    /**
     * @brief 获取端口号
     */
    virtual uint32_t getPort() const = 0;

    /**
     * @brief 设置端口号
     */
    virtual void setPort(uint16_t v) = 0;

    /**
     * @brief 获取广播地址
     * @param prefix_len 子网前缀长度
     * @return 广播地址
     */
    virtual IPAddress::ptr broadcastAddress(uint32_t prefix_len) = 0;

    /**
     * @brief 获取网段地址
     * @param prefix_len 子网前缀长度
     * @return 网段地址
     */
    virtual IPAddress::ptr networkAddress(uint32_t prefix_len) = 0;

    /**
     * @brief 获取子网掩码
     * @param prefix_len 子网前缀长度
     * @return 子网掩码地址
     */
    virtual IPAddress::ptr subnetMask(uint32_t prefix_len) = 0;

    /**
     * @brief 创建 IP 地址
     * @param address IP 地址字符串
     * @param port 端口号
     * @return IP 地址对象
     */
    static IPAddress::ptr Create(const char* address, uint16_t port = 0);
};

/**
 * @brief IPv4 地址类
 */
class IPv4Address : public IPAddress
{
  public:
    typedef std::shared_ptr<IPv4Address> ptr;

    /**
     * @brief 通过 sockaddr_in 创建 IPv4Address
     */
    IPv4Address(const sockaddr_in& address);

    /**
     * @brief 通过 IP 地址和端口创建 IPv4Address
     * @param address 主机序的 IP 地址
     * @param port 端口号
     */
    IPv4Address(uint32_t address = INADDR_ANY, uint16_t port = 0);

    /**
     * @brief 创建 IPv4Address
     * @param address IP 地址字符串（点分十进制）
     * @param port 端口号
     * @return IPv4Address 对象，失败返回 nullptr
     */
    static IPv4Address::ptr Create(const char* address, uint16_t port = 0);

    const sockaddr* getAddr() const override;
    sockaddr* getAddr() override;
    socklen_t getAddrLen() const override;
    std::ostream& insert(std::ostream& os) const override;

    uint32_t getPort() const override;
    void setPort(uint16_t v) override;

    IPAddress::ptr broadcastAddress(uint32_t prefix_len) override;
    IPAddress::ptr networkAddress(uint32_t prefix_len) override;
    IPAddress::ptr subnetMask(uint32_t prefix_len) override;

  private:
    sockaddr_in m_addr;
};

/**
 * @brief IPv6 地址类
 */
class IPv6Address : public IPAddress
{
  public:
    typedef std::shared_ptr<IPv6Address> ptr;

    /**
     * @brief 默认构造，创建 IPv6 任意地址
     */
    IPv6Address();

    /**
     * @brief 通过 sockaddr_in6 创建 IPv6Address
     */
    IPv6Address(const sockaddr_in6& address);

    /**
     * @brief 通过 IPv6 地址字节数组和端口创建 IPv6Address
     * @param address 16 字节的 IPv6 地址
     * @param port 端口号
     */
    IPv6Address(const uint8_t address[16], uint16_t port = 0);

    /**
     * @brief 创建 IPv6Address
     * @param address IP 地址字符串（IPv6 格式）
     * @param port 端口号
     * @return IPv6Address 对象，失败返回 nullptr
     */
    static IPv6Address::ptr Create(const char* address, uint16_t port = 0);

    const sockaddr* getAddr() const override;
    sockaddr* getAddr() override;
    socklen_t getAddrLen() const override;
    std::ostream& insert(std::ostream& os) const override;

    uint32_t getPort() const override;
    void setPort(uint16_t v) override;

    IPAddress::ptr broadcastAddress(uint32_t prefix_len) override;
    IPAddress::ptr networkAddress(uint32_t prefix_len) override;
    IPAddress::ptr subnetMask(uint32_t prefix_len) override;

  private:
    sockaddr_in6 m_addr;
};

/**
 * @brief Unix 域套接字地址类
 */
class UnixAddress : public Address
{
  public:
    typedef std::shared_ptr<UnixAddress> ptr;

    /**
     * @brief 默认构造
     */
    UnixAddress();

    /**
     * @brief 通过路径创建 UnixAddress
     * @param path Unix 域套接字路径
     */
    UnixAddress(const std::string& path);

    const sockaddr* getAddr() const override;
    sockaddr* getAddr() override;
    socklen_t getAddrLen() const override;
    std::ostream& insert(std::ostream& os) const override;

    /**
     * @brief 设置路径长度
     */
    void setAddrLen(uint32_t v);

    /**
     * @brief 获取路径
     */
    std::string getPath() const;

  private:
    sockaddr_un m_addr;
    socklen_t m_length;
};

/**
 * @brief 未知地址类（兜底处理）
 */
class UnknownAddress : public Address
{
  public:
    typedef std::shared_ptr<UnknownAddress> ptr;

    /**
     * @brief 构造函数
     * @param family 地址族
     */
    UnknownAddress(int family);

    /**
     * @brief 通过 sockaddr 构造
     */
    UnknownAddress(const sockaddr& addr);

    const sockaddr* getAddr() const override;
    sockaddr* getAddr() override;
    socklen_t getAddrLen() const override;
    std::ostream& insert(std::ostream& os) const override;

  private:
    sockaddr m_addr;
};

/**
 * @brief 流式输出 Address
 */
std::ostream& operator<<(std::ostream& os, const Address& addr);

} // namespace sylar

#endif // SYLAR_NET_ADDRESS_H
