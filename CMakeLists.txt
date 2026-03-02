cmake_minimum_required(VERSION 3.0)

project(sylar)

set(CMAKE_CXX_STANDARD 11)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O0 -g -Wall -rdynamic -fPIC")

# 包含项目根目录
include_directories(.)

# 所有的核心模块源文件
set(SYLAR_SOURCES
    sylar/log/log_level.cc
    sylar/log/log_formatter.cc
    sylar/log/log_appender.cc
    sylar/log/logger.cc
    sylar/base/util.cc
    sylar/base/config.cc
    sylar/concurrency/mutex/semaphore.cc
    sylar/concurrency/thread.cc
    sylar/fiber/fiber.cc
    sylar/fiber/scheduler.cc
    sylar/fiber/iomanager.cc
    sylar/fiber/timer.cc
    sylar/fiber/fd_manager.cc
    sylar/fiber/hook.cc
    sylar/net/address.cc
    sylar/net/socket.cc
    sylar/net/bytearray.cc
    sylar/net/stream.cc
    sylar/net/socket_stream.cc
    sylar/net/tcp_server.cc
)

# 生成共享库 libsylar.so
add_library(sylar SHARED ${SYLAR_SOURCES})

# 链接库
target_link_libraries(sylar yaml-cpp pthread dl)

# 定义输出路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 编译测试程序
add_executable(test_log tests/test_log.cc)
target_link_libraries(test_log sylar)

add_executable(test_config tests/test_config.cc)
target_link_libraries(test_config sylar)

add_executable(test_thread tests/test_thread.cc)
target_link_libraries(test_thread sylar)

add_executable(test_fiber tests/test_fiber.cc)
target_link_libraries(test_fiber sylar)

add_executable(test_scheduler tests/test_scheduler.cc)
target_link_libraries(test_scheduler sylar)

add_executable(test_iomanager tests/test_iomanager.cc)

target_link_libraries(test_iomanager sylar)



add_executable(test_timer tests/test_timer.cc)

target_link_libraries(test_timer sylar)

add_executable(test_hook tests/test_hook.cc)

target_link_libraries(test_hook sylar)

add_executable(test_endian tests/test_endian.cc)
target_link_libraries(test_endian sylar)

add_executable(test_address tests/test_address.cc)
target_link_libraries(test_address sylar)

add_executable(test_socket tests/test_socket.cc)
target_link_libraries(test_socket sylar)

add_executable(test_bytearray tests/test_bytearray.cc)
target_link_libraries(test_bytearray sylar)

add_executable(test_stream tests/test_stream.cc)
target_link_libraries(test_stream sylar)

add_executable(test_tcp_server tests/test_tcp_server.cc)
target_link_libraries(test_tcp_server sylar)

