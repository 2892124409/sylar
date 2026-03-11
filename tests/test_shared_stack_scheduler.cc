/**
 * @file test_shared_stack_scheduler.cc
 * @brief V1 线程绑定共享栈最小实验测试（Scheduler 单线程路径）
 */

#include "config/config.h"
#include "sylar/fiber/scheduler.h"
#include "log/logger.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <unistd.h>

static base::Logger::ptr g_logger = BASE_LOG_ROOT();
static std::atomic<int> g_steps(0);

static void shared_stack_job()
{
    int step = g_steps.fetch_add(1) + 1;
    BASE_LOG_INFO(g_logger) << "shared_stack_job step=" << step
                             << " fiber_id=" << sylar::Fiber::GetFiberId();
    sylar::Fiber::YieldToReady();

    step = g_steps.fetch_add(1) + 1;
    BASE_LOG_INFO(g_logger) << "shared_stack_job step=" << step
                             << " fiber_id=" << sylar::Fiber::GetFiberId();
    sylar::Fiber::YieldToReady();

    step = g_steps.fetch_add(1) + 1;
    BASE_LOG_INFO(g_logger) << "shared_stack_job step=" << step
                             << " fiber_id=" << sylar::Fiber::GetFiberId();
}

int main()
{
    base::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(true);

    sylar::Scheduler sc(1, true, "shared_stack_scheduler");
    sc.start();
    sc.schedule(&shared_stack_job);
    sc.stop();

    base::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(false);

    std::cout << "shared_stack_scheduler steps=" << g_steps.load() << std::endl;
    assert(g_steps.load() == 3);
    BASE_LOG_INFO(g_logger) << "test_shared_stack_scheduler passed";
    return 0;
}
