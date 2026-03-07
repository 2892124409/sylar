/**
 * @file test_fiber_pool.cc
 * @brief 协程池功能测试
 * @author sylar.yin
 * @date 2026-03-06
 */
#include "sylar/fiber/fiber_pool.h"
#include "sylar/fiber/scheduler.h"
#include "sylar/base/config.h"
#include "sylar/log/logger.h"
#include <unistd.h>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void test_basic_pool()
{
    SYLAR_LOG_INFO(g_logger) << "=== 测试1: 基本获取和归还 ===";

    // 初始化主协程
    sylar::Fiber::GetThis();

    auto pool = sylar::FiberPool::GetThreadLocal();

    // 获取协程
    auto fiber1 = pool->acquire([]() {
        SYLAR_LOG_INFO(g_logger) << "Fiber 1 执行";
    });

    SYLAR_LOG_INFO(g_logger) << "获取fiber1, id=" << fiber1->getId();

    // 模拟执行
    fiber1->resume();

    // 归还协程
    pool->release(fiber1);
    SYLAR_LOG_INFO(g_logger) << "归还fiber1";

    // 再次获取，应该复用
    auto fiber2 = pool->acquire([]() {
        SYLAR_LOG_INFO(g_logger) << "Fiber 2 执行";
    });

    SYLAR_LOG_INFO(g_logger) << "获取fiber2, id=" << fiber2->getId();

    // 检查是否复用了同一个协程对象
    if (fiber1.get() == fiber2.get()) {
        SYLAR_LOG_INFO(g_logger) << "✓ 成功复用协程对象";
    } else {
        SYLAR_LOG_INFO(g_logger) << "✗ 未复用（可能是新创建）";
    }

    fiber2->resume();
    pool->release(fiber2);

    // 输出统计
    auto stats = pool->getStats();
    SYLAR_LOG_INFO(g_logger) << "统计: total_alloc=" << stats.total_alloc
                             << ", pool_hit=" << stats.pool_hit
                             << ", pool_miss=" << stats.pool_miss
                             << ", hit_rate=" << stats.hit_rate << "%"
                             << ", current_pooled=" << stats.current_pooled;
}

void test_scheduler_integration()
{
    SYLAR_LOG_INFO(g_logger) << "\n=== 测试2: Scheduler集成测试 ===";

    sylar::Scheduler sc(2, false, "test_pool");
    sc.start();

    // 投放多个任务
    for (int i = 0; i < 10; ++i) {
        sc.schedule([i]() {
            SYLAR_LOG_INFO(g_logger) << "任务 " << i << " 执行, fiber_id="
                                     << sylar::Fiber::GetFiberId();
        });
    }

    sc.stop();

    // 输出每个线程的池统计
    SYLAR_LOG_INFO(g_logger) << "Scheduler任务完成";
}

void test_different_stack_sizes()
{
    SYLAR_LOG_INFO(g_logger) << "\n=== 测试3: 不同栈大小分桶 ===";

    // 初始化主协程
    sylar::Fiber::GetThis();

    auto pool = sylar::FiberPool::GetThreadLocal();
    pool->resetStats();

    // 128KB栈
    auto fiber1 = pool->acquire([]() {}, 128 * 1024);
    fiber1->resume();
    pool->release(fiber1);

    // 256KB栈
    auto fiber2 = pool->acquire([]() {}, 256 * 1024);
    fiber2->resume();
    pool->release(fiber2);

    // 再次获取128KB，应该命中
    auto fiber3 = pool->acquire([]() {}, 128 * 1024);
    fiber3->resume();
    pool->release(fiber3);

    auto stats = pool->getStats();
    SYLAR_LOG_INFO(g_logger) << "统计: total_alloc=" << stats.total_alloc
                             << ", pool_hit=" << stats.pool_hit
                             << ", hit_rate=" << stats.hit_rate << "%";
}

void test_normalized_stack_buckets()
{
    SYLAR_LOG_INFO(g_logger) << "\n=== 测试4: 非标准栈大小归一化复用 ===";

    sylar::Fiber::GetThis();

    auto pool = sylar::FiberPool::GetThreadLocal();
    pool->resetStats();

    auto fiber1 = pool->acquire([]() {}, 200 * 1024);
    SYLAR_LOG_INFO(g_logger) << "fiber1 stack_size=" << fiber1->getStackSize();
    fiber1->resume();
    auto raw1 = fiber1.get();
    pool->release(fiber1);
    fiber1.reset();

    auto fiber2 = pool->acquire([]() {}, 192 * 1024);
    SYLAR_LOG_INFO(g_logger) << "fiber2 stack_size=" << fiber2->getStackSize();

    if (fiber2.get() == raw1)
    {
        SYLAR_LOG_INFO(g_logger) << "✓ 非标准栈大小命中同一分桶并复用成功";
    }
    else
    {
        SYLAR_LOG_INFO(g_logger) << "✗ 非标准栈大小未命中同一分桶";
    }

    fiber2->resume();
    pool->release(fiber2);

    auto stats = pool->getStats();
    SYLAR_LOG_INFO(g_logger) << "统计: total_alloc=" << stats.total_alloc
                             << ", pool_hit=" << stats.pool_hit
                             << ", pool_miss=" << stats.pool_miss
                             << ", hit_rate=" << stats.hit_rate << "%";
}

void test_configured_default_stack_size()
{
    SYLAR_LOG_INFO(g_logger) << "\n=== 测试5: 配置驱动默认栈大小 ===";

    sylar::Fiber::GetThis();

    auto stack_cfg = sylar::Config::Lookup<uint32_t>("fiber.stack_size", 128 * 1024, "fiber stack size");
    uint32_t old_stack_size = stack_cfg->getValue();
    stack_cfg->setValue(200 * 1024);

    auto pool = sylar::FiberPool::GetThreadLocal();
    pool->resetStats();

    auto fiber1 = pool->acquire([]() {});
    SYLAR_LOG_INFO(g_logger) << "配置后的默认栈大小=" << fiber1->getStackSize();
    fiber1->resume();
    auto raw1 = fiber1.get();
    pool->release(fiber1);
    fiber1.reset();

    auto fiber2 = pool->acquire([]() {});
    if (fiber2.get() == raw1)
    {
        SYLAR_LOG_INFO(g_logger) << "✓ 默认栈大小配置生效，且可复用";
    }
    else
    {
        SYLAR_LOG_INFO(g_logger) << "✗ 默认栈大小配置未命中复用";
    }

    fiber2->resume();
    pool->release(fiber2);

    stack_cfg->setValue(old_stack_size);
}

void test_shared_stack_pooling_with_config()
{
    SYLAR_LOG_INFO(g_logger) << "\n=== 测试6: 共享栈模式下协程池复用 ===";

    auto shared_cfg = sylar::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack");
    bool old_shared = shared_cfg->getValue();
    shared_cfg->setValue(true);

    sylar::Fiber::ResetSharedStackStats();

    auto pool = sylar::FiberPool::GetThreadLocal();
    pool->resetStats();

    int steps = 0;
    sylar::Scheduler sc(1, true, "test_shared_pool");
    sc.start();

    sc.schedule([&steps]() {
        ++steps;
        sylar::Fiber::YieldToReady();
        ++steps;
        sylar::Scheduler::GetThis()->schedule([&steps]() {
            ++steps;
            sylar::Scheduler::GetThis()->schedule([&steps]() {
                ++steps;
            });
        });
    });

    sc.stop();

    auto stats = pool->getStats();
    auto shared_stats = sylar::Fiber::GetSharedStackStats();
    SYLAR_LOG_INFO(g_logger) << "共享栈池统计: total_alloc=" << stats.total_alloc
                             << ", pool_hit=" << stats.pool_hit
                             << ", current_pooled=" << stats.current_pooled
                             << ", shared_pooled=" << stats.shared_pooled;

    if (stats.pool_hit > 0 && stats.shared_pooled > 0)
    {
        SYLAR_LOG_INFO(g_logger) << "✓ 共享栈模式下协程池复用生效";
    }
    else
    {
        SYLAR_LOG_INFO(g_logger) << "✗ 共享栈模式下协程池未命中复用";
    }

    if (shared_stats.save_count > 0 && shared_stats.restore_count > 0)
    {
        SYLAR_LOG_INFO(g_logger) << "✓ 共享栈保存/恢复路径已执行";
    }
    else
    {
        SYLAR_LOG_INFO(g_logger) << "✗ 共享栈保存/恢复路径未执行";
    }

    if (steps == 4)
    {
        SYLAR_LOG_INFO(g_logger) << "✓ 共享栈调度链路执行完成";
    }
    else
    {
        SYLAR_LOG_INFO(g_logger) << "✗ 共享栈调度链路执行步数异常, steps=" << steps;
    }

    shared_cfg->setValue(old_shared);
}

void test_shared_stack_pooling_worker_only()
{
    SYLAR_LOG_INFO(g_logger) << "\n=== 测试7: worker-only 共享栈协程池复用 ===";

    auto shared_cfg = sylar::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack");
    bool old_shared = shared_cfg->getValue();
    shared_cfg->setValue(true);

    sylar::Fiber::ResetSharedStackStats();

    std::atomic<int> steps(0);
    std::atomic<int> yielded_fiber_first_tid(0);
    std::atomic<int> yielded_fiber_thread_mismatch(0);

    sylar::Scheduler sc(2, false, "test_shared_pool_worker_only");
    sc.start();

    sc.schedule([&]() {
        const int tid = sylar::GetThreadId();
        yielded_fiber_first_tid.store(tid);

        ++steps;
        sylar::Fiber::YieldToReady();

        if (yielded_fiber_first_tid.load() != sylar::GetThreadId())
        {
            yielded_fiber_thread_mismatch.store(1);
        }

        ++steps;

        sylar::Scheduler::GetThis()->schedule([&]() {
            ++steps;
        });
    });

    sc.stop();

    auto pool = sylar::FiberPool::GetThreadLocal();
    auto stats = pool->getStats();
    auto shared_stats = sylar::Fiber::GetSharedStackStats();
    SYLAR_LOG_INFO(g_logger) << "worker-only 共享栈池统计: total_alloc=" << stats.total_alloc
                             << ", pool_hit=" << stats.pool_hit
                             << ", current_pooled=" << stats.current_pooled
                             << ", shared_pooled=" << stats.shared_pooled;

    if (steps.load() == 3)
    {
        SYLAR_LOG_INFO(g_logger) << "✓ worker-only 共享栈调度链路执行完成";
    }
    else
    {
        SYLAR_LOG_INFO(g_logger) << "✗ worker-only 共享栈调度链路执行步数异常, steps=" << steps.load();
    }

    if (yielded_fiber_thread_mismatch.load() == 0 && yielded_fiber_first_tid.load() != 0)
    {
        SYLAR_LOG_INFO(g_logger) << "✓ worker-only 模式保持线程绑定";
    }
    else
    {
        SYLAR_LOG_INFO(g_logger) << "✗ worker-only 模式线程绑定异常";
    }

    if (stats.pool_hit > 0 && shared_stats.save_count > 0 && shared_stats.restore_count > 0)
    {
        SYLAR_LOG_INFO(g_logger) << "✓ worker-only 共享栈协程池复用生效";
    }
    else
    {
        SYLAR_LOG_INFO(g_logger) << "✗ worker-only 共享栈协程池未命中复用";
    }

    shared_cfg->setValue(old_shared);
}

void test_pool_capacity()
{
    SYLAR_LOG_INFO(g_logger) << "\n=== 测试8: 池容量限制 ===";

    // 初始化主协程
    sylar::Fiber::GetThis();

    auto pool = sylar::FiberPool::GetThreadLocal();
    pool->resetStats();
    pool->setMaxPoolSize(5);

    std::vector<sylar::Fiber::ptr> fibers;

    // 创建10个协程
    for (int i = 0; i < 10; ++i) {
        auto fiber = pool->acquire([i]() {
            SYLAR_LOG_INFO(g_logger) << "Fiber " << i;
        });
        fiber->resume();
        fibers.push_back(fiber);
    }

    // 归还所有协程
    for (auto& fiber : fibers) {
        pool->release(fiber);
    }

    auto stats = pool->getStats();
    SYLAR_LOG_INFO(g_logger) << "归还10个协程后，池中数量=" << stats.current_pooled
                             << " (最大限制=5)";

    if (stats.current_pooled <= 5) {
        SYLAR_LOG_INFO(g_logger) << "✓ 容量限制生效";
    }
}

int main(int argc, char** argv)
{
    SYLAR_LOG_INFO(g_logger) << "========== 协程池功能测试 ==========";

    test_basic_pool();
    test_scheduler_integration();
    test_different_stack_sizes();
    test_normalized_stack_buckets();
    test_configured_default_stack_size();
    test_shared_stack_pooling_with_config();
    test_shared_stack_pooling_worker_only();
    test_pool_capacity();

    SYLAR_LOG_INFO(g_logger) << "\n========== 测试完成 ==========";
    return 0;
}
