/**
 * @file test_shared_stack_longrun.cc
 * @brief V1 共享栈长循环切换测试
 */

#include "config/config.h"
#include "sylar/fiber/fiber.h"
#include "sylar/fiber/scheduler.h"
#include "log/logger.h"

#include <atomic>
#include <cassert>
#include <iostream>

static base::Logger::ptr g_logger = BASE_LOG_ROOT();
static std::atomic<int> g_longrun_steps(0);

static void longrun_job()
{
    const int kRounds = 1000;
    for (int i = 0; i < kRounds; ++i)
    {
        int step = g_longrun_steps.fetch_add(1) + 1;
        if ((step % 200) == 0)
        {
            BASE_LOG_INFO(g_logger) << "shared_stack_longrun step=" << step
                                     << " fiber_id=" << sylar::Fiber::GetFiberId();
        }
        if (i < kRounds - 1)
        {
            sylar::Fiber::YieldToReady();
        }
    }
}

int main()
{
    g_longrun_steps.store(0);
    sylar::Fiber::ResetSharedStackStats();

    base::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(true);

    sylar::Scheduler sc(1, true, "shared_stack_longrun");
    sc.start();
    sc.schedule(&longrun_job);
    sc.stop();

    base::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(false);

    sylar::Fiber::SharedStackStats stats = sylar::Fiber::GetSharedStackStats();
    std::cout << "shared_stack_longrun steps=" << g_longrun_steps.load()
              << " stats=" << sylar::Fiber::GetSharedStackStatsString()
              << std::endl;

    assert(g_longrun_steps.load() == 1000);
    assert(stats.save_count >= 999);
    assert(stats.restore_count >= 999);
    assert(stats.prepare_count >= 1000);
    assert(stats.finalize_count >= 1000);

    BASE_LOG_INFO(g_logger) << "test_shared_stack_longrun passed";
    return 0;
}
