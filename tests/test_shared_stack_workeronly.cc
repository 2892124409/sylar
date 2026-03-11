/**
 * @file test_shared_stack_workeronly.cc
 * @brief V1 在 use_caller=false worker-only 模式下的共享栈测试
 */

#include "config/config.h"
#include "sylar/base/util.h"
#include "sylar/fiber/fiber.h"
#include "sylar/fiber/scheduler.h"
#include "log/logger.h"

#include <atomic>
#include <cassert>
#include <iostream>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();
static std::atomic<int> g_worker_steps(0);
static std::atomic<int> g_worker_first_tid(0);
static std::atomic<int> g_worker_thread_mismatch(0);

static void workeronly_job()
{
    for (int i = 0; i < 3; ++i)
    {
        const int tid = sylar::GetThreadId();
        int expected = 0;
        if (g_worker_first_tid.compare_exchange_strong(expected, tid))
        {
        }
        else if (expected != tid)
        {
            g_worker_thread_mismatch.store(1);
        }

        int step = g_worker_steps.fetch_add(1) + 1;
        SYLAR_LOG_INFO(g_logger) << "workeronly_job step=" << step
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
    g_worker_steps.store(0);
    g_worker_first_tid.store(0);
    g_worker_thread_mismatch.store(0);
    sylar::Fiber::ResetSharedStackStats();

    sylar::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(true);

    sylar::Scheduler sc(2, false, "shared_stack_workeronly");
    sc.start();
    sc.schedule(&workeronly_job);
    sc.stop();

    sylar::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(false);

    sylar::Fiber::SharedStackStats stats = sylar::Fiber::GetSharedStackStats();
    std::cout << "shared_stack_workeronly steps=" << g_worker_steps.load()
              << " first_tid=" << g_worker_first_tid.load()
              << " mismatch=" << g_worker_thread_mismatch.load()
              << " stats=" << sylar::Fiber::GetSharedStackStatsString()
              << std::endl;

    assert(g_worker_steps.load() == 3);
    assert(g_worker_first_tid.load() != 0);
    assert(g_worker_thread_mismatch.load() == 0);
    assert(stats.unsupported_mode_fallback_count == 0);
    assert(stats.save_count > 0);
    assert(stats.restore_count > 0);

    SYLAR_LOG_INFO(g_logger) << "test_shared_stack_workeronly passed";
    return 0;
}
