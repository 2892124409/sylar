/**
 * @file test_shared_stack_lifecycle.cc
 * @brief V1 共享栈异常与 reset 生命周期测试
 */

#include "config/config.h"
#include "sylar/fiber/fiber.h"
#include "sylar/fiber/scheduler.h"
#include "log/logger.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <stdexcept>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();
static std::atomic<int> g_lifecycle_steps(0);
static sylar::Fiber::ptr g_lifecycle_fiber;

static void throwing_job()
{
    g_lifecycle_steps.fetch_add(1);
    SYLAR_LOG_INFO(g_logger) << "throwing_job fiber_id=" << sylar::Fiber::GetFiberId();
    throw std::runtime_error("shared stack lifecycle test exception");
}

static void reset_job()
{
    for (int i = 0; i < 2; ++i)
    {
        int step = g_lifecycle_steps.fetch_add(1) + 1;
        SYLAR_LOG_INFO(g_logger) << "reset_job step=" << step
                                 << " fiber_id=" << sylar::Fiber::GetFiberId();
        if (i == 0)
        {
            sylar::Fiber::YieldToReady();
        }
    }
}

int main()
{
    sylar::Fiber::ResetSharedStackStats();
    g_lifecycle_steps.store(0);

    sylar::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(true);

    {
        sylar::Scheduler sc(1, true, "shared_stack_lifecycle_throw");
        sc.start();
        sc.schedule([]() {
            g_lifecycle_fiber.reset(new sylar::Fiber(&throwing_job, 0, true));
            sylar::Scheduler::GetThis()->schedule(g_lifecycle_fiber);
        });
        sc.stop();
    }

    sylar::Fiber::ptr f = g_lifecycle_fiber;
    assert(f);
    assert(f->getState() == sylar::Fiber::EXCEPT);

    f->reset(&reset_job);
    {
        sylar::Scheduler sc(1, true, "shared_stack_lifecycle_reset");
        sc.start();
        sc.schedule(f);
        sc.stop();
    }

    assert(f->getState() == sylar::Fiber::TERM);
    assert(g_lifecycle_steps.load() == 3);

    sylar::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(false);

    std::cout << "shared_stack_lifecycle steps=" << g_lifecycle_steps.load()
              << " stats=" << sylar::Fiber::GetSharedStackStatsString() << std::endl;

    sylar::Fiber::SharedStackStats stats = sylar::Fiber::GetSharedStackStats();

    assert(stats.save_count > 0);
    assert(stats.restore_count > 0);

    SYLAR_LOG_INFO(g_logger) << "test_shared_stack_lifecycle passed";
    return 0;
}
