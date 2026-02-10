#include "sylar/fiber/iomanager.h"
#include "sylar/log/logger.h"
#include "sylar/fiber/timer.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

static int count = 0;
sylar::Timer::ptr s_timer;

/**
 * @brief 周期性任务回调
 */
void test_timer_callback() {
    SYLAR_LOG_INFO(g_logger) << "定时器触发，count = " << count;
    if(++count == 5) {
        // 执行 5 次后，将该定时器重置为 2 秒触发一次，且不循环
        SYLAR_LOG_INFO(g_logger) << "重置定时器，改为 2 秒触发一次且不再循环";
        s_timer->reset(2000, true);
    }
    if(count == 10) {
        // 虽然重置了不循环，但由于我们逻辑里 reset 是重插，这里作为演示
        SYLAR_LOG_INFO(g_logger) << "达到 10 次，彻底取消定时器";
        s_timer->cancel();
    }
}

int main() {
    // 开启 1 个线程的调度器
    sylar::IOManager iom(1);

    // 1. 添加一个 1 秒触发一次的循环定时器
    SYLAR_LOG_INFO(g_logger) << "添加 1 秒循环定时器";
    s_timer = iom.addTimer(1000, test_timer_callback, true);

    // 2. 添加一个一次性定时器
    iom.addTimer(3000, [](){
        SYLAR_LOG_INFO(g_logger) << "【一次性任务】3 秒钟到了！";
    });

    // 3. 添加一个 5 秒后的任务，用于关闭调度器
    iom.addTimer(15000, [](){
        SYLAR_LOG_INFO(g_logger) << "【自毁任务】15 秒到了，准备结束程序";
    });

    return 0;
}
