#ifndef __SYLAR_BASE_UTIL_H__
#define __SYLAR_BASE_UTIL_H__

#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <string>

namespace sylar
{

    /**
     * @brief 返回当前线程的系统级ID
     */
    pid_t GetThreadId();

    /**
     * @brief 返回当前协程的ID
     */
    uint64_t GetFiberId();

    /**
     * @brief 获取当前的调用栈
     * @param[out] bt 保存调用栈的容器
     * @param[in] size 最大层数
     * @param[in] skip 跳过的层数
     */
    void Backtrace(std::vector<std::string> &bt, int size = 64, int skip = 1);

    /**
     * @brief 获取当前调用栈并转为字符串
     * @param[in] size 最大层数
     * @param[in] skip 跳过的层数
     * @param[in] prefix 每行前缀
     */
    std::string BacktraceToString(int size = 64, int skip = 2, const std::string &prefix = "");

    /**
     * @brief 获取当前毫秒时间戳
     */
    uint64_t GetCurrentMS();

    /**
     * @brief 获取当前微秒时间戳
     */
    uint64_t GetCurrentUS();

}

#endif