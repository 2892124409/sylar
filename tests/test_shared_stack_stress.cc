/**
 * @file test_shared_stack_stress.cc
 * @brief V1 共享栈深栈与多次切换压力小测试
 */

#include "config/config.h"
#include "sylar/fiber/scheduler.h"
#include "log/logger.h"

#include <atomic>
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();
static std::atomic<int> g_stress_steps(0);
static std::atomic<int> g_stress_checksum(0);

static void deep_stack_job(int depth)
{
    char local_buf[2048];
    std::memset(local_buf, depth, sizeof(local_buf));

    int checksum = 0;
    for (size_t i = 0; i < sizeof(local_buf); i += 128)
    {
        checksum += local_buf[i];
    }
    g_stress_checksum.fetch_add(checksum);

    if (depth > 0)
    {
        deep_stack_job(depth - 1);
        return;
    }

    for (int i = 0; i < 10; ++i)
    {
        int step = g_stress_steps.fetch_add(1) + 1;
        SYLAR_LOG_INFO(g_logger) << "shared_stack_stress step=" << step
                                 << " fiber_id=" << sylar::Fiber::GetFiberId();
        if (i < 9)
        {
            sylar::Fiber::YieldToReady();
        }
    }
}

int main()
{
    g_stress_steps = 0;
    g_stress_checksum = 0;

    sylar::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(true);

    sylar::Scheduler sc(1, true, "shared_stack_stress");
    sc.start();
    sc.schedule([]() { deep_stack_job(8); });
    sc.stop();

    sylar::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(false);

    std::cout << "shared_stack_stress steps=" << static_cast<int>(g_stress_steps)
              << " checksum=" << static_cast<int>(g_stress_checksum) << std::endl;

    assert(static_cast<int>(g_stress_steps) == 10);
    assert(static_cast<int>(g_stress_checksum) != 0);

    SYLAR_LOG_INFO(g_logger) << "test_shared_stack_stress passed";
    return 0;
}
