/**
 * @file mutex.h
 * @brief 互斥锁封装
 * @date 2026-02-04
 */
#ifndef __SYLAR_CONCURRENCY_MUTEX_MUTEX_H__
#define __SYLAR_CONCURRENCY_MUTEX_MUTEX_H__

#include <pthread.h>
#include "sylar/base/noncopyable.h"
#include "lock_guard.h"

namespace sylar
{

    /**
     * @brief 互斥量
     */
    class Mutex : Noncopyable
    {
    public:
        /// 局部锁
        typedef ScopedLockImpl<Mutex> Lock;

        /**
         * @brief 构造函数
         */
        Mutex()
        {
            pthread_mutex_init(&m_mutex, nullptr);
        }

        /**
         * @brief 析构函数
         */
        ~Mutex()
        {
            pthread_mutex_destroy(&m_mutex);
        }

        /**
         * @brief 加锁
         */
        void lock()
        {
            pthread_mutex_lock(&m_mutex);
        }

        /**
         * @brief 解锁
         */
        void unlock()
        {
            pthread_mutex_unlock(&m_mutex);
        }

    private:
        /// pthread_mutex_t
        pthread_mutex_t m_mutex;
    };

    /**
     * @brief 空锁 (用于调试或不需要锁的场景)
     */
    class NullMutex : Noncopyable
    {
    public:
        /// 局部锁
        typedef ScopedLockImpl<NullMutex> Lock;

        /**
         * @brief 构造函数
         */
        NullMutex() {}

        /**
         * @brief 析构函数
         */
        ~NullMutex() {}

        /**
         * @brief 加锁
         */
        void lock() {}

        /**
         * @brief 解锁
         */
        void unlock() {}
    };

}

#endif
