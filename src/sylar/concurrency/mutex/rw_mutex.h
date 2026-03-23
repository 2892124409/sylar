/**
 * @file rw_mutex.h
 * @brief 读写锁封装
 * @author sylar.yin (Modified by Gemini Agent)
 * @date 2026-02-05
 */
#ifndef __SYLAR_CONCURRENCY_MUTEX_RW_MUTEX_H__
#define __SYLAR_CONCURRENCY_MUTEX_RW_MUTEX_H__

#include <pthread.h>
#include "sylar/base/noncopyable.h"
#include "lock_guard.h"

namespace sylar
{

    /**
     * @brief 读写互斥量
     */
    class RWMutex : Noncopyable
    {
    public:
        // 两种锁虽然内核一样，但是模板中执行的内核函数不一样
        /// 局部读锁
        typedef ReadScopedLockImpl<RWMutex> ReadLock;
        /// 局部写锁
        typedef WriteScopedLockImpl<RWMutex> WriteLock;

        /**
         * @brief 默认构造函数
         */
        RWMutex()
        {
            pthread_rwlock_init(&m_lock, nullptr);
        }

        /**
         * @brief 析构函数
         */
        ~RWMutex()
        {
            pthread_rwlock_destroy(&m_lock);
        }

        /**
         * @brief 上读锁
         */
        void rdlock()
        {
            pthread_rwlock_rdlock(&m_lock);
        }

        /**
         * @brief 上写锁
         */
        void wrlock()
        {
            pthread_rwlock_wrlock(&m_lock);
        }

        /**
         * @brief 解锁
         */
        void unlock()
        {
            pthread_rwlock_unlock(&m_lock);
        }

    private:
        /// 读写锁系统句柄
        pthread_rwlock_t m_lock;
    };

    /**
     * @brief 空读写锁 (用于调试或不需要锁的场景)
     */
    class NullRWMutex : Noncopyable
    {
    public:
        /// 局部读锁
        typedef ReadScopedLockImpl<NullRWMutex> ReadLock;
        /// 局部写锁
        typedef WriteScopedLockImpl<NullRWMutex> WriteLock;

        NullRWMutex() {}
        ~NullRWMutex() {}

        void rdlock() {}
        void wrlock() {}
        void unlock() {}
    };

}

#endif
