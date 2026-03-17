/**
 * @file bytearray.h
 * @brief 二进制数组，提供序列化/反序列化功能
 * @author sylar.yin
 * @date 2019-06-05
 */

#ifndef __SYLAR_BYTEARRAY_H__
#define __SYLAR_BYTEARRAY_H__

#include <memory>
#include <stdint.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

namespace sylar
{

/**
 * @brief 二进制数组，提供基础类型的序列化、反序列化功能
 * @details 使用链式内存块管理数据，支持动态扩容
 */
class ByteArray
{
  public:
    typedef std::shared_ptr<ByteArray> ptr;

    /**
     * @brief ByteArray 的存储节点
     */
    struct Node
    {
        /**
         * @brief 构造指定大小的内存块
         * @param[in] s 内存块字节数
         */
        Node(size_t s);

        /**
         * @brief 无参构造函数
         */
        Node();

        /**
         * @brief 析构函数，释放内存
         */
        ~Node();

        /// 内存块地址指针
        char* ptr;
        /// 下一个内存块地址
        Node* next;
        /// 内存块大小
        size_t size;
    };

    /**
     * @brief 使用指定长度的内存块构造 ByteArray
     * @param[in] base_size 内存块大小，默认 4096
     */
    ByteArray(size_t base_size = 4096);

    /**
     * @brief 析构函数
     */
    ~ByteArray();

    // ============================================================================
    // 固定长度写入
    // ============================================================================

    /**
     * @brief 写入固定长度 int8_t 类型数据
     * @post m_position += sizeof(value)
     *       如果 m_position > m_size 则 m_size = m_position
     */
    void writeFint8(int8_t value);

    /**
     * @brief 写入固定长度 uint8_t 类型数据
     */
    void writeFuint8(uint8_t value);

    /**
     * @brief 写入固定长度 int16_t 类型数据（大端/小端）
     */
    void writeFint16(int16_t value);

    /**
     * @brief 写入固定长度 uint16_t 类型数据（大端/小端）
     */
    void writeFuint16(uint16_t value);

    /**
     * @brief 写入固定长度 int32_t 类型数据（大端/小端）
     */
    void writeFint32(int32_t value);

    /**
     * @brief 写入固定长度 uint32_t 类型数据（大端/小端）
     */
    void writeFuint32(uint32_t value);

    /**
     * @brief 写入固定长度 int64_t 类型数据（大端/小端）
     */
    void writeFint64(int64_t value);

    /**
     * @brief 写入固定长度 uint64_t 类型数据（大端/小端）
     */
    void writeFuint64(uint64_t value);

    // ============================================================================
    // 变长编码写入 (Varint)
    // ============================================================================

    /**
     * @brief 写入有符号 Varint32 类型数据
     * @post m_position += 实际占用内存 (1~5)
     */
    void writeInt32(int32_t value);

    /**
     * @brief 写入无符号 Varint32 类型数据
     * @post m_position += 实际占用内存 (1~5)
     */
    void writeUint32(uint32_t value);

    /**
     * @brief 写入有符号 Varint64 类型数据
     * @post m_position += 实际占用内存 (1~10)
     */
    void writeInt64(int64_t value);

    /**
     * @brief 写入无符号 Varint64 类型数据
     * @post m_position += 实际占用内存 (1~10)
     */
    void writeUint64(uint64_t value);

    // ============================================================================
    // 浮点数写入
    // ============================================================================

    /**
     * @brief 写入 float 类型数据
     */
    void writeFloat(float value);

    /**
     * @brief 写入 double 类型数据
     */
    void writeDouble(double value);

    // ============================================================================
    // 字符串写入
    // ============================================================================

    /**
     * @brief 写入 std::string 类型数据，用 uint16_t 作为长度类型
     * @post m_position += 2 + value.size()
     */
    void writeStringF16(const std::string& value);

    /**
     * @brief 写入 std::string 类型数据，用 uint32_t 作为长度类型
     * @post m_position += 4 + value.size()
     */
    void writeStringF32(const std::string& value);

    /**
     * @brief 写入 std::string 类型数据，用 uint64_t 作为长度类型
     * @post m_position += 8 + value.size()
     */
    void writeStringF64(const std::string& value);

    /**
     * @brief 写入 std::string 类型数据，用无符号 Varint64 作为长度类型
     * @post m_position += Varint64长度 + value.size()
     */
    void writeStringVint(const std::string& value);

    /**
     * @brief 写入 std::string 类型数据，无长度
     * @post m_position += value.size()
     */
    void writeStringWithoutLength(const std::string& value);

    // ============================================================================
    // 固定长度读取
    // ============================================================================

    /**
     * @brief 读取 int8_t 类型数据
     * @pre getReadSize() >= sizeof(int8_t)
     * @post m_position += sizeof(int8_t)
     * @exception 如果 getReadSize() < sizeof(int8_t) 抛出 std::out_of_range
     */
    int8_t readFint8();

    /**
     * @brief 读取 uint8_t 类型数据
     */
    uint8_t readFuint8();

    /**
     * @brief 读取 int16_t 类型数据
     */
    int16_t readFint16();

    /**
     * @brief 读取 uint16_t 类型数据
     */
    uint16_t readFuint16();

    /**
     * @brief 读取 int32_t 类型数据
     */
    int32_t readFint32();

    /**
     * @brief 读取 uint32_t 类型数据
     */
    uint32_t readFuint32();

    /**
     * @brief 读取 int64_t 类型数据
     */
    int64_t readFint64();

    /**
     * @brief 读取 uint64_t 类型数据
     */
    uint64_t readFuint64();

    // ============================================================================
    // 变长编码读取 (Varint)
    // ============================================================================

    /**
     * @brief 读取有符号 Varint32 类型数据
     */
    int32_t readInt32();

    /**
     * @brief 读取无符号 Varint32 类型数据
     */
    uint32_t readUint32();

    /**
     * @brief 读取有符号 Varint64 类型数据
     */
    int64_t readInt64();

    /**
     * @brief 读取无符号 Varint64 类型数据
     */
    uint64_t readUint64();

    // ============================================================================
    // 浮点数读取
    // ============================================================================

    /**
     * @brief 读取 float 类型数据
     */
    float readFloat();

    /**
     * @brief 读取 double 类型数据
     */
    double readDouble();

    // ============================================================================
    // 字符串读取
    // ============================================================================

    /**
     * @brief 读取 std::string 类型数据，用 uint16_t 作为长度
     */
    std::string readStringF16();

    /**
     * @brief 读取 std::string 类型数据，用 uint32_t 作为长度
     */
    std::string readStringF32();

    /**
     * @brief 读取 std::string 类型数据，用 uint64_t 作为长度
     */
    std::string readStringF64();

    /**
     * @brief 读取 std::string 类型数据，用无符号 Varint64 作为长度
     */
    std::string readStringVint();

    // ============================================================================
    // 缓冲区操作
    // ============================================================================

    /**
     * @brief 清空 ByteArray
     * @post m_position = 0, m_size = 0
     */
    void clear();

    /**
     * @brief 写入 size 长度的数据
     * @param[in] buf 内存缓存指针
     * @param[in] size 数据大小
     * @post m_position += size, 如果 m_position > m_size 则 m_size = m_position
     */
    void write(const void* buf, size_t size);

    /**
     * @brief 读取 size 长度的数据
     * @param[out] buf 内存缓存指针
     * @param[in] size 数据大小
     * @post m_position += size
     * @exception 如果 getReadSize() < size 则抛出 std::out_of_range
     */
    void read(void* buf, size_t size);

    /**
     * @brief 读取 size 长度的数据（从指定位置）
     * @param[out] buf 内存缓存指针
     * @param[in] size 数据大小
     * @param[in] position 读取开始位置
     * @exception 如果 (m_size - position) < size 则抛出 std::out_of_range
     */
    void read(void* buf, size_t size, size_t position) const;

    // ============================================================================
    // 位置和容量
    // ============================================================================

    /**
     * @brief 返回 ByteArray 当前位置
     */
    size_t getPosition() const
    {
        return m_position;
    }

    /**
     * @brief 设置 ByteArray 当前位置
     * @post 如果 m_position > m_size 则 m_size = m_position
     * @exception 如果 m_position > m_capacity 则抛出 std::out_of_range
     */
    void setPosition(size_t v);

    /**
     * @brief 返回内存块的大小
     */
    size_t getBaseSize() const
    {
        return m_baseSize;
    }

    /**
     * @brief 返回可读取数据大小
     */
    size_t getReadSize() const
    {
        return m_size - m_position;
    }

    /**
     * @brief 返回数据的长度
     */
    size_t getSize() const
    {
        return m_size;
    }

    /**
     * @brief 是否是小端
     */
    bool isLittleEndian() const;

    /**
     * @brief 设置是否为小端
     */
    void setIsLittleEndian(bool val);

    // ============================================================================
    // 文件操作
    // ============================================================================

    /**
     * @brief 把 ByteArray 的数据写入到文件中
     * @param[in] name 文件名
     */
    bool writeToFile(const std::string& name) const;

    /**
     * @brief 从文件中读取数据
     * @param[in] name 文件名
     */
    bool readFromFile(const std::string& name);

    // ============================================================================
    // 工具方法
    // ============================================================================

    /**
     * @brief 将 ByteArray 里面的数据 [m_position, m_size) 转成 std::string
     */
    std::string toString() const;

    /**
     * @brief 将 ByteArray 里面的数据 [m_position, m_size) 转成 16 进制的 std::string
     */
    std::string toHexString() const;

    // ============================================================================
    // iovec 支持
    // ============================================================================

    /**
     * @brief 获取可读取的缓存，保存成 iovec 数组
     * @param[out] buffers 保存可读取数据的 iovec 数组
     * @param[in] len 读取数据的长度，如果 len > getReadSize() 则 len = getReadSize()
     * @return 返回实际数据的长度
     */
    uint64_t getReadBuffers(std::vector<iovec>& buffers, uint64_t len = ~0ull) const;

    /**
     * @brief 获取可读取的缓存，保存成 iovec 数组，从 position 位置开始
     * @param[out] buffers 保存可读取数据的 iovec 数组
     * @param[in] len 读取数据的长度
     * @param[in] position 读取数据的位置
     * @return 返回实际数据的长度
     */
    uint64_t getReadBuffers(std::vector<iovec>& buffers, uint64_t len, uint64_t position) const;

    /**
     * @brief 获取可写入的缓存，保存成 iovec 数组
     * @param[out] buffers 保存可写入的内存的 iovec 数组
     * @param[in] len 写入的长度
     * @return 返回实际的长度
     * @post 如果 (m_position + len) > m_capacity 则扩容以容纳 len 长度
     */
    uint64_t getWriteBuffers(std::vector<iovec>& buffers, uint64_t len);

  private:
    /**
     * @brief 扩容 ByteArray，使其可以容纳 size 个数据
     */
    void addCapacity(size_t size);

    /**
     * @brief 获取当前的可写入容量
     */
    size_t getCapacity() const
    {
        return m_capacity - m_position;
    }

  private:
    /// 内存块的大小
    size_t m_baseSize;
    /// 当前操作位置
    size_t m_position;
    /// 当前的总容量
    size_t m_capacity;
    /// 当前数据的大小
    size_t m_size;
    /// 字节序，默认大端
    int8_t m_endian;
    /// 第一个内存块指针
    Node* m_root;
    /// 当前操作的内存块指针
    Node* m_cur;
};

} // namespace sylar

#endif
