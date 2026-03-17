#include "../sylar/base/util.h"
#include "log/logger.h"
#include <iostream>

int main(int argc, char** argv)
{
    // 1. 获取 root 日志器
    base::Logger::ptr logger = BASE_LOG_ROOT();

    std::cout << "--- Test 1: Using Macros ---" << std::endl;
    // 使用宏，自动填入文件、行号、时间、线程ID
    BASE_LOG_INFO(logger) << "Hello Sylar Log from Macro!";
    BASE_LOG_ERROR(logger) << "This is an error!";

    // 测试格式化流式输出
    BASE_LOG_DEBUG(logger) << "Debug message: " << 123 << " " << 3.14;

    std::cout << "\n--- Test 2: Custom Logger ---" << std::endl;
    base::Logger::ptr l = BASE_LOG_NAME("system");
    l->addAppender(sylar::LogAppender::ptr(new sylar::FileLogAppender("./log.txt")));

    // 设置一个新的格式试试
    l->setFormatter("%d - %m%n");

    // 格式化输出测试
    BASE_LOG_INFO(l) << "System info log";
    BASE_LOG_ERROR(l) << "System error log";

    std::cout << "Check log.txt for custom format logs." << std::endl;

    return 0;
}
