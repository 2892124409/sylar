/**
 * @file semaphore.h
 * @brief 信号量封装
 * @date 2026-02-04
 */
#ifndef __SYLAR_CONCURRENCY_MUTEX_SEMAPHORE_H__
#define __SYLAR_CONCURRENCY_MUTEX_SEMAP_H__

#include <semaphore.h>
#include <stdint.h>
#include "sylar/base/noncopyable.h"

namespace sylar {

/**
 * @brief 信号量
 * @details 信号量是一种同步原语，用于多线程或多进程间的资源管理和同步。
 *          它继承自 Noncopyable，确保信号量对象不会被意外拷贝。
 */
class Semaphore : Noncopyable {
public:
    /**
     * @brief 构造函数
     * @param[in] count 信号量初始值
     */
    Semaphore(uint32_t count = 0);

    /**
     * @brief 析构函数
     */
    ~Semaphore();

    /**
     * @brief 获取信号量 (P操作)
     * @details 如果信号量的值大于0，则减1并立即返回。
     *          如果信号量的值为0，则当前线程将阻塞等待。
     */
    void wait();

    /**
     * @brief 发布信号量 (V操作)
     * @details 信号量的值加1。如果有线程正在阻塞等待该信号量，则唤醒其中一个线程。
     */
    void notify();

private:
    sem_t m_semaphore;
};

}

#endif
