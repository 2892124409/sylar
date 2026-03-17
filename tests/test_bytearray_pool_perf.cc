/**
 * @file test_bytearray_pool_perf.cc
 * @brief ByteArray 内存池性能测试
 */

#include "sylar/net/bytearray.h"
#include <chrono>
#include <iostream>
#include <vector>

void test_allocation_performance()
{
    std::cout << "\n========== ByteArray 内存池性能测试 ==========\n";

    const int ITERATIONS = 10000;
    const size_t BASE_SIZE = 4096;

    auto start = std::chrono::high_resolution_clock::now();

    // 测试1：频繁创建销毁 ByteArray
    for (int i = 0; i < ITERATIONS; ++i)
    {
        sylar::ByteArray::ptr ba(new sylar::ByteArray(BASE_SIZE));
        ba->writeInt32(i);
        ba->writeStringF32("test data");
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "创建/销毁 " << ITERATIONS << " 个 ByteArray: " << duration.count() << " ms\n";

    start = std::chrono::high_resolution_clock::now();

    // 测试2：单个 ByteArray 频繁扩容
    sylar::ByteArray::ptr ba(new sylar::ByteArray(BASE_SIZE));
    for (int i = 0; i < ITERATIONS; ++i)
    {
        ba->writeInt32(i);
        ba->writeStringF32("test data that will trigger node allocation");
    }

    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "单个 ByteArray 写入 " << ITERATIONS << " 次（触发扩容）: " << duration.count() << " ms\n";
    std::cout << "最终数据大小: " << ba->getSize() << " 字节\n";

    start = std::chrono::high_resolution_clock::now();

    // 测试3：批量创建后批量销毁
    std::vector<sylar::ByteArray::ptr> batch;
    batch.reserve(ITERATIONS);
    for (int i = 0; i < ITERATIONS; ++i)
    {
        sylar::ByteArray::ptr ba(new sylar::ByteArray(BASE_SIZE));
        ba->writeInt32(i);
        batch.push_back(ba);
    }
    batch.clear();

    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "批量创建/销毁 " << ITERATIONS << " 个 ByteArray: " << duration.count() << " ms\n";

    std::cout << "\n✓ 性能测试完成\n";
    std::cout << "注：使用内存池后，分配/释放速度应明显快于传统 new/delete\n";
}

int main()
{
    test_allocation_performance();
    return 0;
}
