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
    sylar/memorypool/memory_pool.cpp
    sylar/fiber/fiber.cc
    sylar/fiber/fiber_pool.cc
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
    sylar/net/udp_server.cc
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

add_executable(test_fiber_pool tests/test_fiber_pool.cc)
target_link_libraries(test_fiber_pool sylar)

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

add_executable(test_udp_server tests/test_udp_server.cc)
target_link_libraries(test_udp_server sylar)

add_executable(test_server_mode_bench tests/test_server_mode_bench.cc)
target_link_libraries(test_server_mode_bench sylar)

add_executable(test_shared_stack_helpers tests/test_shared_stack_helpers.cc)
target_link_libraries(test_shared_stack_helpers sylar)

add_executable(test_shared_stack_scheduler tests/test_shared_stack_scheduler.cc)
target_link_libraries(test_shared_stack_scheduler sylar)

add_executable(test_shared_stack_scheduler_mt tests/test_shared_stack_scheduler_mt.cc)
target_link_libraries(test_shared_stack_scheduler_mt sylar)

add_executable(test_shared_stack_stress tests/test_shared_stack_stress.cc)
target_link_libraries(test_shared_stack_stress sylar)

add_executable(test_shared_stack_guard tests/test_shared_stack_guard.cc)
target_link_libraries(test_shared_stack_guard sylar)

add_executable(test_shared_stack_lifecycle tests/test_shared_stack_lifecycle.cc)
target_link_libraries(test_shared_stack_lifecycle sylar)

add_executable(test_shared_stack_longrun tests/test_shared_stack_longrun.cc)
target_link_libraries(test_shared_stack_longrun sylar)

add_executable(test_shared_stack_workeronly tests/test_shared_stack_workeronly.cc)
target_link_libraries(test_shared_stack_workeronly sylar)

add_executable(test_bytearray_pool_perf tests/test_bytearray_pool_perf.cc)
target_link_libraries(test_bytearray_pool_perf sylar)
