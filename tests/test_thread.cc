/**
 * @file test_thread.cc
 * @brief 线程模块测试
 * @author sylar.yin (Modified by Gemini Agent)
 * @date 2026-02-07
 */
#include "sylar/concurrency/thread.h"
#include "sylar/concurrency/mutex/mutex.h"
#include "log/logger.h"
#include <vector>

int count = 0;
sylar::Mutex s_mutex;

void func1()
{
    BASE_LOG_INFO(BASE_LOG_ROOT()) << "thread name: " << sylar::Thread::GetName()
                                     << " this.name: " << sylar::Thread::GetThis()->getName()
                                     << " id: " << sylar::GetThreadId()
                                     << " this.id: " << sylar::Thread::GetThis()->getId();

    for (int i = 0; i < 1000000; ++i)
    {
        sylar::Mutex::Lock lock(s_mutex);
        ++count;
    }
}

void func2()
{
    while (true)
    {
        BASE_LOG_INFO(BASE_LOG_ROOT()) << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    }
}

void func3()
{
    while (true)
    {
        BASE_LOG_INFO(BASE_LOG_ROOT()) << "========================================";
    }
}

int main(int argc, char **argv)
{
    BASE_LOG_INFO(BASE_LOG_ROOT()) << "thread test begin";

    std::vector<sylar::Thread::ptr> thrs;
    for (int i = 0; i < 5; ++i)
    {
        sylar::Thread::ptr thr(new sylar::Thread(&func1, "name_" + std::to_string(i)));
        thrs.push_back(thr);
    }

    for (size_t i = 0; i < thrs.size(); ++i)
    {
        thrs[i]->join();
    }

    BASE_LOG_INFO(BASE_LOG_ROOT()) << "thread test end";
    BASE_LOG_INFO(BASE_LOG_ROOT()) << "count=" << count;

    return 0;
}
