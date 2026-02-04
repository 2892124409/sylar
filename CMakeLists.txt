cmake_minimum_required(VERSION 3.0)

project(sylar)

set(CMAKE_CXX_STANDARD 11)

# 编译选项：-O0 关闭优化方便调试，-g 生成调试信息，-Wall 开启警告，-rdynamic 让链接器导出所有符号
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O0 -g -Wall -rdynamic -fPIC")

# 包含项目根目录
include_directories(.)

# 所有的日志模块源文件
set(LOG_SOURCES
    sylar/log/log_level.cc
    sylar/log/log_formatter.cc
    sylar/log/log_appender.cc
    sylar/log/logger.cc
    sylar/base/util.cc
    sylar/base/config.cc # 新增配置模块源文件
)

# 生成共享库 libsylar.so
add_library(sylar SHARED ${LOG_SOURCES})

# 链接 yaml-cpp 库
target_link_libraries(sylar yaml-cpp)

# 编译测试程序
add_executable(test_log tests/test_log.cc)
target_link_libraries(test_log sylar)

# 编译配置模块测试程序
add_executable(test_config tests/test_config.cc)
target_link_libraries(test_config sylar)

# 定义输出路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)