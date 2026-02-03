#include <iostream>
#include <time.h>
#include "../sylar/log/logger.h"

int main(int argc, char** argv) {
    // 1. 获取 root 日志器
    sylar::Logger::ptr logger = sylar::LoggerMgr::GetInstance()->getRoot();
    
    std::cout << "--- Test 1: Default Root Logger ---" << std::endl;
    // 目前还没宏，我们手动构造 LogEvent
    // 参数：logger, level, file, line, elapse, thread_id, fiber_id, time
    sylar::LogEvent::ptr event(new sylar::LogEvent(logger, sylar::LogLevel::DEBUG, __FILE__, __LINE__, 0, 1, 2, time(0)));
    event->getSS() << "Hello Sylar Log from Root!";
    logger->log(sylar::LogLevel::DEBUG, event);

    std::cout << "\n--- Test 2: Custom Logger with FileAppender ---" << std::endl;
    // 创建一个自定义 Logger
    sylar::Logger::ptr l(sylar::LoggerMgr::GetInstance()->getLogger("system"));
    
    // 给自定义 Logger 添加一个文件输出
    sylar::FileLogAppender::ptr file_appender(new sylar::FileLogAppender("./log.txt"));
    l->addAppender(file_appender);

    sylar::LogEvent::ptr event2(new sylar::LogEvent(l, sylar::LogLevel::ERROR, __FILE__, __LINE__, 0, 1, 2, time(0)));
    event2->getSS() << "This is a system error log!";
    l->log(sylar::LogLevel::ERROR, event2);

    std::cout << "Check log.txt for the result." << std::endl;

    return 0;
}
