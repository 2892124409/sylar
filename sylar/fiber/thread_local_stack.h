/**
 * @file thread_local_stack.h
 * @brief V1 线程绑定共享栈的线程本地栈管理器（骨架）
 */
#ifndef __SYLAR_THREAD_LOCAL_STACK_H__
#define __SYLAR_THREAD_LOCAL_STACK_H__

#include <cstddef>
#include <cstdlib>

namespace sylar
{

    class ThreadLocalSharedStack
    {
    public:
        static const size_t STACK_SIZE = 128 * 1024;
        static const size_t STACK_COUNT = 1;

        static ThreadLocalSharedStack *GetInstance()
        {
            static thread_local ThreadLocalSharedStack instance;
            return &instance;
        }

        void *acquire()
        {
            ensureStacksAllocated();
            for (size_t i = 0; i < STACK_COUNT; ++i)
            {
                if (!m_inUse[i])
                {
                    m_inUse[i] = true;
                    return m_stacks[i];
                }
            }
            return nullptr;
        }

        void release(void *stack)
        {
            if (!stack)
            {
                return;
            }
            for (size_t i = 0; i < STACK_COUNT; ++i)
            {
                if (m_stacks[i] == stack)
                {
                    m_inUse[i] = false;
                    return;
                }
            }
        }

        bool hasIdleStack() const
        {
            for (size_t i = 0; i < STACK_COUNT; ++i)
            {
                if (!m_inUse[i])
                {
                    return true;
                }
            }
            return false;
        }

    private:
        ThreadLocalSharedStack()
        {
            for (size_t i = 0; i < STACK_COUNT; ++i)
            {
                m_stacks[i] = nullptr;
                m_inUse[i] = false;
            }
        }

        ~ThreadLocalSharedStack()
        {
            for (size_t i = 0; i < STACK_COUNT; ++i)
            {
                if (m_stacks[i])
                {
                    std::free(m_stacks[i]);
                }
            }
        }

        void ensureStacksAllocated()
        {
            for (size_t i = 0; i < STACK_COUNT; ++i)
            {
                if (!m_stacks[i])
                {
                    m_stacks[i] = std::malloc(STACK_SIZE);
                }
            }
        }

    private:
        void *m_stacks[STACK_COUNT];
        bool m_inUse[STACK_COUNT];
    };

}

#endif
