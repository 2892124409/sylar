/**
 * @file scheduler.h
 * @brief 协程调度器封装
 * @author sylar.yin
 * @date 2026-02-08
 */
#ifndef __SYLAR_SCHEDULER_H__
#define __SYLAR_SCHEDULER_H__

#include <memory>
#include <vector>
#include <list>
#include <functional>
#include <atomic>
#include "sylar/fiber/fiber.h"
#include "sylar/concurrency/thread.h"
#include "sylar/concurrency/mutex/mutex.h"

namespace sylar
{

    /**
     * @brief 协程调度器
     * @details 封装的是 N:M 的协程调度器
     *          内部有一个线程池，支持协程在线程池中切换
     */
    class Scheduler
    {
    public:
        typedef std::shared_ptr<Scheduler> ptr;
        typedef Mutex MutexType;

        /**
         * @brief 构造函数
         * @param[in] threads 线程数量
         * @param[in] use_caller 是否使用当前调用线程
         * @param[in] name 调度器名称
         */
        Scheduler(size_t threads = 1, bool use_caller = true, const std::string &name = "");

        /**
         * @brief 析构函数
         */
        virtual ~Scheduler();

        /**
         * @brief 返回调度器名称
         */
        const std::string &getName() const { return m_name; }

        /**
         * @brief 返回当前调度器
         */
        static Scheduler *GetThis();

        /**
         * @brief 返回当前线程的调度协程
         */
        static Fiber *GetMainFiber();

        /**
         * @brief 启动调度器
         */
        void start();

        /**
         * @brief 停止调度器
         */
        void stop();

        /**
         * @brief 调度协程
         * @param[in] fc 协程或函数
         * @param[in] thread 指定运行的线程id，-1表示任意线程
         */
        template <class FiberOrCb>
        void schedule(FiberOrCb fc, int thread = -1)
        {
            bool need_tickle = false;
            {
                MutexType::Lock lock(m_mutex);
                need_tickle = scheduleNoLock(fc, thread);
            }

            if (need_tickle)
            {
                tickle();
            }
        }

        /**
         * @brief 批量调度协程
         * @param[in] begin 迭代器开始
         * @param[in] end 迭代器结束
         */
        template <class InputIterator>
        void schedule(InputIterator begin, InputIterator end)
        {
            bool need_tickle = false;
            {
                MutexType::Lock lock(m_mutex);
                while (begin != end)
                {
                    need_tickle = scheduleNoLock(&*begin, -1) || need_tickle;
                    ++begin;
                }
            }
            if (need_tickle)
            {
                tickle();
            }
        }

    protected:
        /**
         * @brief 通知协程调度器有任务了
         */
        virtual void tickle();

        /**
         * @brief 协程调度函数
         */
        void run();

        /**
         * @brief 返回是否可以停止
         */
        virtual bool stopping();

        /**
         * @brief 协程无任务可调度时执行的协程
         */
        virtual void idle();

        /**
         * @brief 设置当前的协程调度器
         */
        void setThis();

        /**
         * @brief 是否有空闲线程
         */
        bool hasIdleThreads() { return m_idleThreadCount > 0; }

    private:
        /**
         * @brief 协程调度单元(协程/函数)
         */
        struct FiberAndThread
        {
            /// 协程
            Fiber::ptr fiber;
            /// 协程执行函数
            std::function<void()> cb;
            /// 指定线程id，在这个线程上跑，-1表示任意线程
            int thread;

            /**
             * @brief 构造函数
             * @param[in] f 协程
             * @param[in] thr 线程id
             */
            FiberAndThread(Fiber::ptr f, int thr)
                : fiber(f), thread(thr)
            {
            }

            /**
             * @brief 构造函数
             * @param[in] f 协程指针
             * @param[in] thr 线程id
             * @details 用于 shared_ptr 的 swap 函数，减少引用计数
             */
            FiberAndThread(Fiber::ptr *f, int thr)
                : thread(thr)
            {
                fiber.swap(*f);
            }

            /**
             * @brief 构造函数
             * @param[in] f 协程执行函数
             * @param[in] thr 线程id
             */
            FiberAndThread(std::function<void()> f, int thr)
                : cb(f), thread(thr)
            {
            }

            /**
             * @brief 构造函数
             * @param[in] f 协程执行函数指针
             * @param[in] thr 线程id
             */
            FiberAndThread(std::function<void()> *f, int thr)
                : thread(thr)
            {
                cb.swap(*f);
            }

            /**
             * @brief 无参构造函数
             */
            FiberAndThread()
                : thread(-1)
            {
            }

            /**
             * @brief 重置数据
             */
            void reset()
            {
                fiber = nullptr;
                cb = nullptr;
                thread = -1;
            }
        };

    private:
        /**
         * @brief 协程调度(无锁)
         */
        template <class FiberOrCb>
        bool scheduleNoLock(FiberOrCb fc, int thread)
        {
            bool need_tickle = m_fibers.empty();
            FiberAndThread ft(fc, thread);
            if (ft.fiber || ft.cb)
            {
                m_fibers.push_back(ft);
            }
            return need_tickle;
        }

    private:
        /// 调度器名称
        std::string m_name;
        /// 互斥锁
        MutexType m_mutex;
        /// 线程池
        std::vector<Thread::ptr> m_threads;
        /// 待执行的协程队列
        std::list<FiberAndThread> m_fibers;
        /// use_caller 为 true 时，调度器所在线程的调度协程
        Fiber::ptr m_rootFiber;
        /// 线程池的线程ID数组
        std::vector<int> m_threadIds;
        /// 工作线程数量
        size_t m_threadCount = 0;
        /// 活跃线程数量
        std::atomic<size_t> m_activeThreadCount = {0};
        /// 空闲线程数量
        std::atomic<size_t> m_idleThreadCount = {0};
        /// 是否正在停止
        bool m_stopping = true;
        /// 是否自动停止
        bool m_autoStop = false;
        /// 主线程id(use_caller)
        int m_rootThread = 0;
    };

}

#endif