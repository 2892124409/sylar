/**
 * @file semaphore.cc
 * @brief 信号量实现
 * @date 2026-02-04
 */
#include "semaphore.h"
#include <stdexcept>
#include <errno.h>

namespace sylar
{

    Semaphore::Semaphore(uint32_t count)
    {
        // sem_init 初始化一个定位在 m_semaphore 的匿名信号量。
        // 第二个参数 pshared 为 0，表示信号量是在线程间共享。
        // 第三个参数 count 是信号量的初始值。
        if (sem_init(&m_semaphore, 0, count))
        {
            throw std::logic_error("sem_init error");
        }
    }

    Semaphore::~Semaphore()
    {
        // 销毁信号量，释放相关系统资源
        sem_destroy(&m_semaphore);
    }

    void Semaphore::wait()
    {
        // sem_wait 函数递减（锁定）信号量。
        // 如果信号量当前值为 0，则调用线程将阻塞，直到可以执行递减操作（即信号量值 > 0）。

        // EINTR: 如果在阻塞等待期间被信号处理程序中断，函数会返回 -1 且 errno 设置为 EINTR。
        // 我们需要通过循环确保在被中断后能自动重新进入等待状态。
        while (sem_wait(&m_semaphore) == -1 && errno == EINTR)
        {
        }
    }

    void Semaphore::notify()
    {
        // sem_post 函数递增（解锁）信号量。
        // 如果递增后的信号量值大于 0，则另一个正在阻塞等待该信号量的线程将被唤醒。
        if (sem_post(&m_semaphore))
        {
            throw std::logic_error("sem_post error");
        }
    }

}
