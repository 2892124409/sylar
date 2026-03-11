/**
 * @file test_shared_stack_scheduler_mt.cc
 * @brief V1 线程绑定共享栈多线程调度测试
 */

#include "config/config.h"
#include "sylar/base/util.h"
#include "sylar/fiber/scheduler.h"
#include "log/logger.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <unistd.h>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();
static std::atomic<int> g_mt_steps(0);
static std::atomic<int> g_mt_first_tid(0);
static std::atomic<int> g_mt_thread_mismatch(0);

static void shared_stack_mt_job()
{
    for (int i = 0; i < 3; ++i)
    {
        const int tid = sylar::GetThreadId();
        int expected = 0;
        if (g_mt_first_tid.compare_exchange_strong(expected, tid))
        {
            // first step binds thread id
        }
        else if (expected != tid)
        {
            g_mt_thread_mismatch.store(1);
        }

        int step = g_mt_steps.fetch_add(1) + 1;
        SYLAR_LOG_INFO(g_logger) << "shared_stack_mt_job step=" << step
                                 << " fiber_id=" << sylar::Fiber::GetFiberId()
                                 << " thread_id=" << tid;

        if (i < 2)
        {
            sylar::Fiber::YieldToReady();
        }
    }
}

int main()
{
    g_mt_steps.store(0);
    g_mt_first_tid.store(0);
    g_mt_thread_mismatch.store(0);

    sylar::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(true);

    sylar::Scheduler sc(2, true, "shared_stack_mt_scheduler");
    sc.start();
    sc.schedule(&shared_stack_mt_job);
    sc.stop();

    sylar::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(false);

    std::cout << "shared_stack_mt steps=" << g_mt_steps.load()
              << " first_tid=" << g_mt_first_tid.load()
              << " mismatch=" << g_mt_thread_mismatch.load() << std::endl;

    assert(g_mt_steps.load() == 3);
    assert(g_mt_first_tid.load() != 0);
    assert(g_mt_thread_mismatch.load() == 0);

    SYLAR_LOG_INFO(g_logger) << "test_shared_stack_scheduler_mt passed";
    return 0;
}
