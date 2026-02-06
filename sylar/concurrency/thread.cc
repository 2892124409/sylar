/**
 * @file thread.cc
 * @brief 线程封装实现
 * @author sylar.yin (Modified by Gemini Agent)
 * @date 2026-02-07
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "thread.h"
#include "sylar/base/util.h"
#include <iostream>

namespace sylar
{

    // 定义线程局部变量 (TLS)
    // t_thread 指向当前线程的 Thread 对象指针
    // thread_local 关键字作用参考《C++11新特性3.3》
    static thread_local Thread *t_thread = nullptr;
    // t_thread_name 存储当前线程的名称
    static thread_local std::string t_thread_name = "UNKNOWN";

    Thread *Thread::GetThis()
    {
        return t_thread;
    }

    const std::string &Thread::GetName()
    {
        return t_thread_name;
    }

    void Thread::SetName(const std::string &name)
    {
        if (t_thread)
        {
            t_thread->m_name = name; // t_thread是当前线程Thread对象的指针
        }
        t_thread_name = name;
        // 设置 Linux 系统内核识别的线程名称 (最长16字符)
        pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
    }

    Thread::Thread(std::function<void()> cb, const std::string &name)
        : m_cb(cb), m_name(name)
    {
        if (name.empty())
        {
            m_name = "UNKNOWN";
        }

            // 创建线程，执行pthread_create会创建执行Thread::run函数的一个子线程

            // 参数 1: &m_thread, 线程句柄，用于存储系统分配的线程标识。

            // 参数 2: nullptr, 线程属性，nullptr 表示使用系统默认属性（如默认栈大小）。

            // 参数 3: &Thread::run, 线程入口函数，静态成员函数符合 C 风格的函数指针要求。

            // 参数 4: this, 传给入口函数的参数。我们将当前对象的指针传进去，使静态函数 run 能访问成员变量。

            int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);

        
        if (rt)
        {
            std::cerr << "pthread_create error, rt=" << rt << " name=" << name << std::endl;
            throw std::logic_error("pthread_create error");
        }

        // 等待子线程真正运行起来并初始化完 TLS 环境
        // 成员变量的初始化是在构造函数体执行之前完成的，所以此时m_semaphore已经执行了自己的默认构造函数
        m_semaphore.wait();
    }

    Thread::~Thread()
    {
        if (m_thread)
        {
            // 如果线程还在运行且没有被 join，则将其分离，让系统自动回收资源
            pthread_detach(m_thread);
        }
    }

    void Thread::join()
    {
        if (m_thread)
        {
            int rt = pthread_join(m_thread, nullptr);
            if (rt)
            {
                std::cerr << "pthread_join error, rt=" << rt << " name=" << m_name << std::endl;
                throw std::logic_error("pthread_join error");
            }
            m_thread = 0;
        }
    }

    void *Thread::run(void *arg)
    {
        Thread *thread = (Thread *)arg;//arg是Thread对象的指针，这里强转一下

        // 初始化 TLS 环境
        t_thread = thread;
        t_thread_name = thread->m_name;
        thread->m_id = sylar::GetThreadId(); // 获取真实系统线程 ID

        // 设置线程名称 (Linux 系统)
        pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

        // 通过 swap 将业务逻辑转移到局部变量 cb 中。
        // 1. 减少引用计数：确保 m_cb 捕获的智能指针在业务执行完后立即析构，防止生命周期被拉长。
        // 2. 语义安全：确保业务逻辑只执行一次，执行后 thread->m_cb 变为空。
        std::function<void()> cb;
        cb.swap(thread->m_cb);

        // 环境初始化完成，通知主线程构造函数可以返回了
        thread->m_semaphore.notify();
        

        // 执行真实的业务逻辑
        cb();

        return 0;
    }

}
