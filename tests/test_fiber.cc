/**
 * @file test_fiber.cc
 * @brief 协程模块测试：演示协程的创建、切换、挂起与复用
 * @author sylar.yin (Modified by Gemini Agent)
 * @date 2026-02-07
 */
#include "sylar/fiber/fiber.h"
#include "sylar/log/logger.h"
#include "sylar/concurrency/thread.h"
#include <vector>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

/**
 * @brief 协程内执行的业务逻辑函数
 */
void run_in_fiber()
{
    SYLAR_LOG_INFO(g_logger) << "【子协程】业务逻辑开始执行 (RunInFiber begin)";

    // 关键点 1：子协程主动让出执行权，切回主协程
    SYLAR_LOG_INFO(g_logger) << "【子协程】执行到一半，主动 Yield 让出 CPU";
    sylar::Fiber::YieldToHold();

    // 关键点 2：当主协程再次 resume 之后，会从刚才 yield 的下一行继续跑
    SYLAR_LOG_INFO(g_logger) << "【子协程】重新获得了 CPU，继续执行收尾工作 (RunInFiber end)";

    // 函数执行完毕后，控制权会自动回到 Fiber::MainFunc 的收尾逻辑，最终切回主协程
}

/**
 * @brief 测试入口函数（会被封装进一个线程运行）
 */
void test_fiber()
{
    SYLAR_LOG_INFO(g_logger) << "【主协程】测试开始 (test_fiber begin)";

    {
        // 关键点 3：初始化当前线程的主协程环境
        // 在任何子协程创建前，必须先调 GetThis()，它会把当前线程的执行流包装成“主协程”对象
        sylar::Fiber::GetThis();

        SYLAR_LOG_INFO(g_logger) << "【主协程】创建子协程对象";
        sylar::Fiber::ptr fiber(new sylar::Fiber(run_in_fiber));

        // 第一次切入：主 -> 子
        SYLAR_LOG_INFO(g_logger) << "【主协程】第一次 Resume 切入子协程";
        fiber->resume();

        // 当子协程在里面调了 YieldToHold 之后，代码会“瞬间移动”回到这里
        SYLAR_LOG_INFO(g_logger) << "【主协程】从子协程切回来了，刚才子协程在里面调了 yield";

        // 第二次切入：主 -> 子 (从上次停下的地方继续)
        SYLAR_LOG_INFO(g_logger) << "【主协程】第二次 Resume，让子协程把剩下的活干完";
        fiber->resume();

        SYLAR_LOG_INFO(g_logger) << "【主协程】子协程已经 TERM（结束）了";

        // 关键点 4：协程对象的复用 (Object Pool 思想)
        SYLAR_LOG_INFO(g_logger) << "【主协程】测试 reset 性能优化：重用刚才的栈空间跑新任务";
        fiber->reset(run_in_fiber); // 擦除旧状态，绑定新剧本
        fiber->resume();            // 再次切入跑一次
    }

    SYLAR_LOG_INFO(g_logger) << "【主协程】测试结束 (test_fiber end)";
}

int main(int argc, char **argv)
{
    sylar::Thread::SetName("main");

    // 创建一个线程来跑协程测试
    std::vector<sylar::Thread::ptr> thrs;
    for (int i = 0; i < 1; ++i)
    { // 这里开 1 个线程演示即可，逻辑更清晰
        thrs.push_back(sylar::Thread::ptr(
            new sylar::Thread(&test_fiber, "thr_" + std::to_string(i))));
    }

    for (auto i : thrs)
    {
        i->join();
    }

    return 0;
}
