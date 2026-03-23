/**
 * @file test_bytearray.cc
 * @brief ByteArray 模块测试程序
 */

#include "sylar/net/bytearray.h"
#include "sylar/log/logger.h"
#include <iostream>
#include <cassert>
#include <cstdlib>

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// ============================================================================
// 固定长度类型测试
// ============================================================================

void test_fixed_length()
{
    std::cout << "\n========== 固定长度类型测试 ==========" << std::endl;

    sylar::ByteArray::ptr ba(new sylar::ByteArray(4)); // 小块测试跨节点

    // 写入各种类型
    ba->writeFint8(-128);
    ba->writeFuint8(255);
    ba->writeFint16(-32768);
    ba->writeFuint16(65535);
    ba->writeFint32(-2147483648);
    ba->writeFuint32(4294967295);
    ba->writeFint64(-9223372036854775807LL);
    ba->writeFuint64(18446744073709551615ULL);

    std::cout << "写入数据大小: " << ba->getSize() << " 字节" << std::endl;

    // 重置位置，读取数据
    ba->setPosition(0);

    assert(ba->readFint8() == -128);
    assert(ba->readFuint8() == 255);
    assert(ba->readFint16() == -32768);
    assert(ba->readFuint16() == 65535);
    assert(ba->readFint32() == -2147483648);
    assert(ba->readFuint32() == 4294967295);
    assert(ba->readFint64() == -9223372036854775807LL);
    assert(ba->readFuint64() == 18446744073709551615ULL);

    std::cout << "✓ 固定长度类型读写测试通过" << std::endl;
}

// ============================================================================
// 浮点数测试
// ============================================================================

void test_float_double()
{
    std::cout << "\n========== 浮点数测试 ==========" << std::endl;

    sylar::ByteArray::ptr ba(new sylar::ByteArray);

    float f = 3.14159f;
    double d = 3.141592653589793;

    ba->writeFloat(f);
    ba->writeDouble(d);

    ba->setPosition(0);

    float f2 = ba->readFloat();
    double d2 = ba->readDouble();

    // 浮点数比较需要考虑精度
    assert(std::abs(f - f2) < 0.00001);
    assert(std::abs(d - d2) < 0.0000000001);

    std::cout << "写入: float=" << f << ", double=" << d << std::endl;
    std::cout << "读取: float=" << f2 << ", double=" << d2 << std::endl;
    std::cout << "✓ 浮点数读写测试通过" << std::endl;
}

// ============================================================================
// Varint 编码测试
// ============================================================================

void test_varint()
{
    std::cout << "\n========== Varint 编码测试 ==========" << std::endl;

    sylar::ByteArray::ptr ba(new sylar::ByteArray);

    // 测试不同大小的数值
    uint32_t values32[] = {0, 127, 128, 16383, 16384, 2097151, 2097152, 268435455, 268435456, 4294967295U};
    int32_t svalues32[] = {0, -1, 1, -127, 127, -128, 128, -16383, 16383, -2147483647, 2147483647};

    // 测试无符号 Varint32
    std::cout << "\n--- 无符号 Varint32 ---" << std::endl;
    for (auto v : values32)
    {
        size_t pos_before = ba->getPosition();
        ba->writeUint32(v);
        size_t written = ba->getPosition() - pos_before;
        std::cout << "值: " << v << ", 占用字节: " << written << std::endl;
    }

    ba->setPosition(0);
    for (auto v : values32)
    {
        uint32_t r = ba->readUint32();
        if (r != v)
        {
            std::cerr << "Varint32 校验失败, expect=" << v << ", actual=" << r << std::endl;
            std::abort();
        }
    }

    ba->clear();

    // 测试有符号 Varint32 (Zigzag)
    std::cout << "\n--- 有符号 Varint32 (Zigzag) ---" << std::endl;
    for (auto v : svalues32)
    {
        size_t pos_before = ba->getPosition();
        ba->writeInt32(v);
        size_t written = ba->getPosition() - pos_before;
        std::cout << "值: " << v << ", 占用字节: " << written << std::endl;
    }

    ba->setPosition(0);
    for (auto v : svalues32)
    {
        int32_t r = ba->readInt32();
        if (r != v)
        {
            std::cerr << "Zigzag32 校验失败, expect=" << v << ", actual=" << r << std::endl;
            std::abort();
        }
    }

    ba->clear();

    // 测试 Varint64
    std::cout << "\n--- Varint64 ---" << std::endl;
    ba->writeUint64(0);
    ba->writeUint64(18446744073709551615ULL);
    ba->writeInt64(-9223372036854775807LL);
    ba->writeInt64(9223372036854775807LL);

    ba->setPosition(0);
    assert(ba->readUint64() == 0);
    assert(ba->readUint64() == 18446744073709551615ULL);
    assert(ba->readInt64() == -9223372036854775807LL);
    assert(ba->readInt64() == 9223372036854775807LL);

    std::cout << "✓ Varint 编码测试通过" << std::endl;
}

// ============================================================================
// 字符串测试
// ============================================================================

void test_string()
{
    std::cout << "\n========== 字符串测试 ==========" << std::endl;

    sylar::ByteArray::ptr ba(new sylar::ByteArray);

    std::string str1 = "Hello, World!";
    std::string str2 = "这是一个测试字符串";
    std::string str3(1000, 'A'); // 长字符串

    // 测试不同长度前缀
    ba->writeStringF16(str1);
    ba->writeStringF32(str2);
    ba->writeStringF64(str3);
    ba->writeStringVint("Varint string");

    ba->setPosition(0);

    assert(ba->readStringF16() == str1);
    assert(ba->readStringF32() == str2);
    assert(ba->readStringF64() == str3);
    assert(ba->readStringVint() == "Varint string");

    std::cout << "✓ 字符串读写测试通过" << std::endl;
}

// ============================================================================
// 跨节点测试
// ============================================================================

void test_cross_node()
{
    std::cout << "\n========== 跨节点测试 ==========" << std::endl;

    // 使用小的块大小，强制数据跨节点
    sylar::ByteArray::ptr ba(new sylar::ByteArray(16));

    // 写入超过一个节点的数据
    std::string data = "This is a test string that spans multiple nodes";
    ba->writeStringF32(data);

    std::cout << "块大小: 16 字节" << std::endl;
    std::cout << "数据大小: " << data.size() << " 字节" << std::endl;

    ba->setPosition(0);

    std::string result = ba->readStringF32();
    assert(result == data);

    std::cout << "✓ 跨节点读写测试通过" << std::endl;
}

// ============================================================================
// 位置操作测试
// ============================================================================

void test_position()
{
    std::cout << "\n========== 位置操作测试 ==========" << std::endl;

    sylar::ByteArray::ptr ba(new sylar::ByteArray);

    ba->writeFint32(100);
    ba->writeFint32(200);
    ba->writeFint32(300);

    std::cout << "当前位置: " << ba->getPosition() << std::endl;
    std::cout << "数据大小: " << ba->getSize() << std::endl;
    std::cout << "可读大小: " << ba->getReadSize() << std::endl;

    // 回到开头
    ba->setPosition(0);
    assert(ba->readFint32() == 100);
    assert(ba->readFint32() == 200);
    assert(ba->readFint32() == 300);

    // 随机位置读取
    ba->setPosition(4);
    if (ba->readFint32() != 200) // 读取第二个 int32
    {
        std::cerr << "位置读取校验失败: 第二个 int32 不是 200" << std::endl;
        std::abort();
    }

    std::cout << "✓ 位置操作测试通过" << std::endl;
}

// ============================================================================
// 清空测试
// ============================================================================

void test_clear()
{
    std::cout << "\n========== 清空测试 ==========" << std::endl;

    sylar::ByteArray::ptr ba(new sylar::ByteArray);

    ba->writeFint32(12345);
    std::cout << "写入后 - 位置: " << ba->getPosition() << ", 大小: " << ba->getSize() << std::endl;

    ba->clear();
    std::cout << "清空后 - 位置: " << ba->getPosition() << ", 大小: " << ba->getSize() << std::endl;

    assert(ba->getPosition() == 0);
    assert(ba->getSize() == 0);

    std::cout << "✓ 清空测试通过" << std::endl;
}

// ============================================================================
// 字节序测试
// ============================================================================

void test_endian()
{
    std::cout << "\n========== 字节序测试 ==========" << std::endl;

    sylar::ByteArray::ptr ba(new sylar::ByteArray);

    std::cout << "默认字节序: " << (ba->isLittleEndian() ? "小端" : "大端") << std::endl;

    // 默认大端
    ba->writeFint32(0x12345678);

    ba->setPosition(0);

    // 查看实际存储的字节
    std::string hex = ba->toHexString();
    std::cout << "0x12345678 存储为: " << hex << std::endl;

    // 切换字节序
    ba->clear();
    ba->setIsLittleEndian(true);
    ba->writeFint32(0x12345678);

    ba->setPosition(0);
    hex = ba->toHexString();
    std::cout << "小端模式下 0x12345678 存储为: " << hex << std::endl;

    std::cout << "✓ 字节序测试通过" << std::endl;
}

// ============================================================================
// 文件读写测试
// ============================================================================

void test_file()
{
    std::cout << "\n========== 文件读写测试 ==========" << std::endl;

    sylar::ByteArray::ptr ba(new sylar::ByteArray);

    // 写入数据
    ba->writeFint32(12345);
    ba->writeStringF32("Hello, File!");
    ba->writeDouble(3.14159);

    // 重置位置到开头，这样 writeToFile 才能写入所有数据
    ba->setPosition(0);

    // 保存到文件
    std::string filename = "/tmp/bytearray_test.bin";
    assert(ba->writeToFile(filename));
    std::cout << "写入文件: " << filename << std::endl;

    // 从文件读取
    sylar::ByteArray::ptr ba2(new sylar::ByteArray);
    assert(ba2->readFromFile(filename));
    ba2->setPosition(0); // 重置位置到开头
    std::cout << "从文件读取成功" << std::endl;

    // 验证数据
    assert(ba2->readFint32() == 12345);
    assert(ba2->readStringF32() == "Hello, File!");
    if (std::abs(ba2->readDouble() - 3.14159) >= 0.00001)
    {
        std::cerr << "文件读回 double 校验失败" << std::endl;
        std::abort();
    }

    std::cout << "✓ 文件读写测试通过" << std::endl;
}

// ============================================================================
// iovec 测试
// ============================================================================

void test_iovec()
{
    std::cout << "\n========== iovec 测试 ==========" << std::endl;

    sylar::ByteArray::ptr ba(new sylar::ByteArray(16));

    // 写入跨节点数据
    std::string data = "This data will span multiple nodes";
    ba->write(data.c_str(), data.size());

    std::cout << "数据大小: " << data.size() << " 字节" << std::endl;
    std::cout << "块大小: 16 字节" << std::endl;

    // 获取 iovec
    ba->setPosition(0);
    std::vector<iovec> buffers;
    uint64_t len = ba->getReadBuffers(buffers, data.size());

    std::cout << "iovec 数量: " << buffers.size() << std::endl;
    std::cout << "总长度: " << len << std::endl;

    // 验证数据
    std::string result;
    for (auto &iov : buffers)
    {
        result.append((char *)iov.iov_base, iov.iov_len);
    }
    assert(result == data);

    std::cout << "✓ iovec 测试通过" << std::endl;
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char **argv)
{
    std::cout << "========================================" << std::endl;
    std::cout << "        ByteArray 模块测试程序" << std::endl;
    std::cout << "========================================" << std::endl;

    test_fixed_length();
    test_float_double();
    test_varint();
    test_string();
    test_cross_node();
    test_position();
    test_clear();
    test_endian();
    test_file();
    test_iovec();

    std::cout << "\n========================================" << std::endl;
    std::cout << "        所有测试通过！" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
