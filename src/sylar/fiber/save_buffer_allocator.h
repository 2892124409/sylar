/**
 * @file save_buffer_allocator.h
 * @brief V1 共享栈保存缓冲区分配器（骨架）
 */
#ifndef __SYLAR_SAVE_BUFFER_ALLOCATOR_H__
#define __SYLAR_SAVE_BUFFER_ALLOCATOR_H__

#include <cstddef>
#include "memorypool/memory_pool.h"

namespace sylar
{

    class SaveBufferAllocator
    {
    public:
        static void *Alloc(size_t size)
        {
            if (size == 0)
            {
                return nullptr;
            }
            return sylar::HashBucket::useMemory(static_cast<int>(size));
        }

        static void Dealloc(void *ptr, size_t size)
        {
            if (!ptr || size == 0)
            {
                return;
            }
            sylar::HashBucket::freeMemory(ptr, static_cast<int>(size));
        }
    };

}

#endif
