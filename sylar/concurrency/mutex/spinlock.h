/**
 * @file spinlock.h
 * @brief 自旋锁与原子锁封装
 * @author sylar.yin (Modified by Gemini Agent)
 * @date 2026-02-05
 */
#ifndef __SYLAR_CONCURRENCY_MUTEX_SPINLOCK_H__
#define __SYLAR_CONCURRENCY_MUTEX_SPINLOCK_H__

#include <pthread.h>
#include <atomic>
#include "sylar/base/noncopyable.h"
#include "lock_guard.h"

namespace sylar {

/**
 * @brief 自旋锁
 */
class Spinlock : Noncopyable {
public:
    /// 局部锁
    typedef ScopedLockImpl<Spinlock> Lock;

    /**
     * @brief 构造函数
     */
    Spinlock() {
        pthread_spin_init(&m_mutex, 0);
    }

    /**
     * @brief 析构函数
     */
    ~Spinlock() {
        pthread_spin_destroy(&m_mutex);
    }

    /**
     * @brief 加锁
     */
    void lock() {
        pthread_spin_lock(&m_mutex);
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        pthread_spin_unlock(&m_mutex);
    }
private:
    /// 自旋锁系统句柄
    pthread_spinlock_t m_mutex;
};

/**
 * @brief 原子锁 (基于 std::atomic_flag)
 */
class CASLock : Noncopyable {
public:
    /// 局部锁
    typedef ScopedLockImpl<CASLock> Lock;

    /**
     * @brief 构造函数
     */
    CASLock() {
        m_mutex.clear();
    }

    /**
     * @brief 析构函数
     */
    ~CASLock() {
    }

    /**
     * @brief 加锁
     */
    void lock() {
        // 使用 acquire 语义确保存储一致性
        while(std::atomic_flag_test_and_set_explicit(&m_mutex, std::memory_order_acquire));
    }

    /**
     * @brief 解锁
     */
    void unlock() {
        // 使用 release 语义
        std::atomic_flag_clear_explicit(&m_mutex, std::memory_order_release);
    }
private:
    /// 原子标志位
    volatile std::atomic_flag m_mutex;
};

}

#endif
