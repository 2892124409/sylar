/**
 * @file test_scheduler.cc
 * @brief 协程调度器测试：演示多线程任务抢占、任务裂变及 use_caller 逻辑
 * @author sylar.yin (Modified by Gemini Agent)
 * @date 2026-02-08
 */
#include "sylar/fiber/scheduler.h"
#include "sylar/log/logger.h"
#include <unistd.h>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

/**
 * @brief 演示任务函数
 * @details 打印当前线程 ID，并动态派生新任务
 */
void test_fiber()
{
    static int s_count = 5;
    SYLAR_LOG_INFO(g_logger) << "【协程任务】执行中, fiber_id="
                             << sylar::Fiber::GetFiberId()
                             << ", thread_id=" << sylar::GetThreadId()
                             << ", count=" << s_count;

    /**
     * 模拟耗时或等待逻辑
     * 注意：在真实的协程框架中，严禁调同步 sleep，因为它会卡死整根线程。
     * 这里仅为了演示多个线程抢活的宏观过程。
     */
    if (--s_count >= 0)
    {
        // 核心点：任务裂变！在协程内部获取当前调度器并添加新任务
        sylar::Scheduler::GetThis()->schedule(&test_fiber);
    }
}

int main(int argc, char **argv)
{
    SYLAR_LOG_INFO(g_logger) << "--- [TEST BEGIN] ---";

    /**
     * 1. 创建调度器
     * 参数 threads=3: 创建 3 个工作线程
     * 参数 use_caller=true: 当前 main 线程也将作为第 4 个工人参与调度
     */
    sylar::Scheduler sc(3, true, "my_scheduler");

    /**
     * 2. 启动调度器
     * 此时线程池中的线程已经创建并进入 Scheduler::run() 循环
     * 但因为队列是空的，它们都在 idle() 协程里待命。
     */
    SYLAR_LOG_INFO(g_logger) << "【主线程】启动调度器 (Non-blocking)";
    sc.start();

    /**
     * 3. 投放种子任务
     * 调度器会通过 tickle() 唤醒正在睡觉的线程
     */
    sleep(2); // 稍等片刻，观察 idle 状态
    SYLAR_LOG_INFO(g_logger) << "【主线程】投放第一个业务协程";
    sc.schedule(&test_fiber);

    /**
     * 4. 停止调度器
     * 关键点：由于 use_caller=true，主线程会在这里调用 call()
     * 瞬间“变身”为调度工人，进入 run() 循环。
     * 只有当所有任务（包括裂变出来的）都跑完，stop() 才会返回。
     */
    SYLAR_LOG_INFO(g_logger) << "【主线程】执行 stop()，主线程加入战斗...";
    sc.stop();

    SYLAR_LOG_INFO(g_logger) << "--- [TEST OVER] ---";
    return 0;
}
