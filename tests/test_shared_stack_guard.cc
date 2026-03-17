/**
 * @file test_shared_stack_guard.cc
 * @brief 未验证模式下共享栈自动回退测试
 */

#include "config/config.h"
#include "log/logger.h"
#include "sylar/fiber/fiber.h"
#include "sylar/fiber/scheduler.h"

#include <atomic>
#include <cassert>
#include <iostream>

static base::Logger::ptr g_logger = BASE_LOG_ROOT();
static std::atomic<int> g_guard_steps(0);

static void guard_job()
{
    for (int i = 0; i < 3; ++i)
    {
        int step = g_guard_steps.fetch_add(1) + 1;
        BASE_LOG_INFO(g_logger) << "guard_job step=" << step
                                << " fiber_id=" << sylar::Fiber::GetFiberId();
        if (i < 2)
        {
            sylar::Fiber::YieldToReady();
        }
    }
}

int main()
{
    g_guard_steps.store(0);
    sylar::Fiber::ResetSharedStackStats();

    base::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(true);

    sylar::Fiber::GetThis();
    sylar::Fiber::ptr fiber(new sylar::Fiber(&guard_job));
    while (fiber->getState() != sylar::Fiber::TERM && fiber->getState() != sylar::Fiber::EXCEPT)
    {
        fiber->resume();
    }

    base::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(false);

    sylar::Fiber::SharedStackStats stats = sylar::Fiber::GetSharedStackStats();
    std::cout << "shared_stack_guard steps=" << g_guard_steps.load()
              << " unsupported_fallbacks=" << stats.unsupported_mode_fallback_count
              << std::endl;

    assert(g_guard_steps.load() == 3);
    assert(stats.unsupported_mode_fallback_count > 0);
    assert(stats.prepare_count == 0);

    BASE_LOG_INFO(g_logger) << "test_shared_stack_guard passed";
    return 0;
}
