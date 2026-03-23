/**
 * @file thread.h
 * @brief 线程封装
 * @author sylar.yin (Modified by Gemini Agent)
 * @date 2026-02-07
 */
#ifndef __SYLAR_CONCURRENCY_THREAD_H__
#define __SYLAR_CONCURRENCY_THREAD_H__

#include <functional>
#include <memory>
#include <pthread.h>
#include <string>
#include "sylar/base/noncopyable.h"
#include "mutex/semaphore.h"

namespace sylar
{

    /**
     * @brief 线程类
     * @details 封装了 pthread 的核心功能，并提供线程局部变量管理
     */
    class Thread : Noncopyable
    {
    public:
        /// 线程智能指针类型
        typedef std::shared_ptr<Thread> ptr;

        /**
         * @brief 构造函数
         * @param[in] cb 线程执行函数
         * @param[in] name 线程名称
         */
        Thread(std::function<void()> cb, const std::string &name);

        /**
         * @brief 析构函数
         */
        ~Thread();

        /**
         * @brief 获取线程系统 ID
         */
        pid_t getId() const { return m_id; }

        /**
         * @brief 获取线程名称
         */
        const std::string &getName() const { return m_name; }

        /**
         * @brief 等待线程执行完成
         */
        void join();

        /**
         * @brief 获取当前线程指针
         */
        static Thread *GetThis();

        /**
         * @brief 获取当前线程名称
         */
        static const std::string &GetName();

        /**
         * @brief 设置当前线程名称
         * @param[in] name 线程名称
         */
        static void SetName(const std::string &name);

    private:
        /**
         * @brief 线程真正运行的函数（静态函数，符合 pthread 接口要求）
         */
        static void *run(void *arg);

    private:
        /// 线程系统 ID
        pid_t m_id = -1;
        /// 线程句柄
        pthread_t m_thread = 0;
        /// 业务回调函数
        std::function<void()> m_cb;
        /// 线程名称
        std::string m_name;
        /// 信号量（用于构造同步）
        /// 如果不加信号量，主线程构造完 Thread对象后可能立即执行后续代码，而此时子线程可能没运行起来，TLS还没初始化。如果此时主线程去读取子线程的 ID或名称，可能会拿到错误的数据。
        Semaphore m_semaphore;
    };

}

#endif
