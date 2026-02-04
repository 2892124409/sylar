#include <iostream>
#include <time.h>
#include "../sylar/log/logger.h"
#include "../sylar/base/util.h"

int main(int argc, char** argv) {
    // 1. 获取 root 日志器
    sylar::Logger::ptr logger = sylar::LoggerMgr::GetInstance()->getRoot();
    
    std::cout << "--- Test 1: Default Root Logger ---" << std::endl;
    // 使用 sylar::GetThreadId() 获取真实的线程 ID，增加线程名称 "main"
    sylar::LogEvent::ptr event(new sylar::LogEvent(logger, sylar::LogLevel::DEBUG, __FILE__, __LINE__, 0, sylar::GetThreadId(), 2, time(0), "main"));
    event->getSS() << "Hello Sylar Log from Root!";
    logger->log(sylar::LogLevel::DEBUG, event);

    std::cout << "\n--- Test 2: Custom Logger with FileAppender ---" << std::endl;
    sylar::Logger::ptr l(sylar::LoggerMgr::GetInstance()->getLogger("system"));
    sylar::FileLogAppender::ptr file_appender(new sylar::FileLogAppender("./log.txt"));
    l->addAppender(file_appender);

    sylar::LogEvent::ptr event2(new sylar::LogEvent(l, sylar::LogLevel::ERROR, __FILE__, __LINE__, 0, sylar::GetThreadId(), 2, time(0), "main"));
    event2->getSS() << "This is a system error log!";
    l->log(sylar::LogLevel::ERROR, event2);

    std::cout << "\n--- Test 3: Backtrace ---" << std::endl;
    std::cout << sylar::BacktraceToString(10, 0, "    ") << std::endl;

    return 0;
}
