/**
 * @file test_shared_stack_helpers.cc
 * @brief V1 共享栈辅助组件测试
 */

#include "sylar/fiber/save_buffer_allocator.h"
#include "sylar/fiber/thread_local_stack.h"
#include "memorypool/memory_pool.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>

static void test_thread_local_shared_stack_basic()
{
    sylar::ThreadLocalSharedStack *tls = sylar::ThreadLocalSharedStack::GetInstance();
    assert(tls->hasIdleStack());

    void *stack = tls->acquire();
    assert(stack != nullptr);
    assert(!tls->hasIdleStack());
    assert(tls->acquire() == nullptr);

    tls->release(stack);
    assert(tls->hasIdleStack());
}

static void test_thread_local_shared_stack_per_thread()
{
    void *main_stack = sylar::ThreadLocalSharedStack::GetInstance()->acquire();
    assert(main_stack != nullptr);

    void *other_stack = nullptr;
    std::thread th([&other_stack]() {
        other_stack = sylar::ThreadLocalSharedStack::GetInstance()->acquire();
        assert(other_stack != nullptr);
        sylar::ThreadLocalSharedStack::GetInstance()->release(other_stack);
    });
    th.join();

    assert(other_stack != nullptr);
    assert(other_stack != main_stack);

    sylar::ThreadLocalSharedStack::GetInstance()->release(main_stack);
}

static void test_save_buffer_allocator()
{
    sylar::HashBucket::initMemoryPool(4096, 8192, 16384, 32768, 65536, 131072);

    const size_t kSize = 5000;
    void *buf = sylar::SaveBufferAllocator::Alloc(kSize);
    assert(buf != nullptr);

    std::memset(buf, 0x5A, kSize);
    unsigned char *bytes = static_cast<unsigned char *>(buf);
    assert(bytes[0] == 0x5A);
    assert(bytes[kSize - 1] == 0x5A);

    sylar::SaveBufferAllocator::Dealloc(buf, kSize);
}

int main()
{
    test_thread_local_shared_stack_basic();
    test_thread_local_shared_stack_per_thread();
    test_save_buffer_allocator();

    std::cout << "test_shared_stack_helpers passed" << std::endl;
    return 0;
}
