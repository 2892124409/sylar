/**
 * @file lock_guard.h
 * @brief RAII 局部锁模板实现
 * @author sylar.yin (Modified by Gemini Agent)
 * @date 2026-02-04
 */
#ifndef __SYLAR_CONCURRENCY_MUTEX_LOCK_GUARD_H__
#define __SYLAR_CONCURRENCY_MUTEX_LOCK_GUARD_H__

namespace sylar
{

    /**
     * @brief 局部锁模板实现
     * @details 构造时自动加锁，析构时自动释放
     */
    template <class T>
    class ScopedLockImpl
    {
    public:
        /**
         * @brief 带参构造函数
         * @param[in] mutex 锁对象
         */
        ScopedLockImpl(T &mutex)
            : m_mutex(mutex)
        {
            m_mutex.lock(); // 执行的是模板T中的lock()函数
            m_locked = true;
        }

        /**
         * @brief 析构函数
         */
        ~ScopedLockImpl()
        {
            unlock();
        }

        /**
         * @brief 加锁
         */
        void lock()
        {
            if (!m_locked)
            {
                m_mutex.lock();
                m_locked = true;
            }
        }

        /**
         * @brief 解锁
         */
        void unlock()
        {
            if (m_locked)
            {
                m_mutex.unlock();
                m_locked = false;
            }
        }

    private:
        /// 锁对象引用
        T &m_mutex;
        /// 是否已上锁
        bool m_locked;
    };

    /**
     * @brief 局部读锁模板实现
     */
    template <class T>
    class ReadScopedLockImpl
    {
    public:
        /**
         * @brief 带参构造函数
         * @param[in] mutex 读写锁对象
         */
        ReadScopedLockImpl(T &mutex)
            : m_mutex(mutex)
        {
            m_mutex.rdlock();
            m_locked = true;
        }

        /**
         * @brief 析构函数
         */
        ~ReadScopedLockImpl()
        {
            unlock();
        }

        /**
         * @brief 加锁
         */
        void lock()
        {
            if (!m_locked)
            {
                m_mutex.rdlock();
                m_locked = true;
            }
        }

        /**
         * @brief 解锁
         */
        void unlock()
        {
            if (m_locked)
            {
                m_mutex.unlock();
                m_locked = false;
            }
        }

    private:
        /// 锁对象引用
        T &m_mutex;
        /// 是否已上锁
        bool m_locked;
    };

    /**
     * @brief 局部写锁模板实现
     */
    template <class T>
    class WriteScopedLockImpl
    {
    public:
        /**
         * @brief 带参构造函数
         * @param[in] mutex 读写锁对象
         */
        WriteScopedLockImpl(T &mutex)
            : m_mutex(mutex)
        {
            m_mutex.wrlock();
            m_locked = true;
        }

        /**
         * @brief 析构函数
         */
        ~WriteScopedLockImpl()
        {
            unlock();
        }

        /**
         * @brief 加锁
         */
        void lock()
        {
            if (!m_locked)
            {
                m_mutex.wrlock();
                m_locked = true;
            }
        }

        /**
         * @brief 解锁
         */
        void unlock()
        {
            if (m_locked)
            {
                m_mutex.unlock();
                m_locked = false;
            }
        }

    private:
        /// 锁对象引用
        T &m_mutex;
        /// 是否已上锁
        bool m_locked;
    };

}

#endif