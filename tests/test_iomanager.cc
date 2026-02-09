#include "sylar/fiber/iomanager.h"
#include "sylar/log/logger.h"
#include <unistd.h>
#include <fcntl.h>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();
int pipe_fds[2];

/**
 * @brief 一个一直在干活的协程 (数数)
 */
void test_busy_work() {
    for(int i = 0; i < 5; ++i) {
        SYLAR_LOG_INFO(g_logger) << "数数协程正在工作中... " << i;
        sleep(1); 
        sylar::Fiber::YieldToReady(); // 主动让出一下，给别人机会
    }
}

/**
 * @brief 同步模式测试
 */
void test_fiber_sync() {
    SYLAR_LOG_INFO(g_logger) << "同步协程：准备等数据了...";
    sylar::IOManager::GetThis()->addEvent(pipe_fds[0], sylar::IOManager::READ);
    
    // 关键点！
    sylar::Fiber::YieldToHold(); 
    
    // 如果这里是传统的 read，线程就卡死了，上面的 test_busy_work 就没机会打印了。
    // 正因为我们 Yield 了，线程才会在等数据的间隙跑去执行 test_busy_work。
    
    char byte;
    read(pipe_fds[0], &byte, 1);
    SYLAR_LOG_INFO(g_logger) << "同步协程：数据终于到了，读到: " << byte;
}

void test_iomanager() {
    sylar::IOManager iom(1, false, "SINGLE_THREAD"); // 我们只用 1 个线程测试

    pipe(pipe_fds);
    fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK);

    iom.schedule(&test_fiber_sync);
    iom.schedule(&test_busy_work); // 让它们在同一个线程里跑

    iom.schedule([](){
        sleep(3);
        write(pipe_fds[1], "Z", 1);
    });
}

int main() {
    test_iomanager();
    return 0;
}