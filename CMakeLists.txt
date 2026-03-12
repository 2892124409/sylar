cmake_minimum_required(VERSION 3.0)

project(sylar)

set(CMAKE_CXX_STANDARD 11)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O0 -g -Wall -rdynamic -fPIC")

# 包含项目根目录与源码根目录
include_directories(.)
include_directories(src)
include_directories(src/base)

# 所有的核心模块源文件
set(SYLAR_SOURCES
    src/base/log/log_level.cc
    src/base/log/log_formatter.cc
    src/base/log/log_appender.cc
    src/base/log/logger.cc
    src/sylar/base/util.cc
    src/base/config/config.cc
    src/sylar/concurrency/mutex/semaphore.cc
    src/sylar/concurrency/thread.cc
    src/base/memorypool/memory_pool.cpp
    src/sylar/fiber/fiber.cc
    src/sylar/fiber/fiber_framework_config.cc
    src/sylar/fiber/fiber_pool.cc
    src/sylar/fiber/scheduler.cc
    src/sylar/fiber/iomanager.cc
    src/sylar/fiber/timer.cc
    src/sylar/fiber/fd_manager.cc
    src/sylar/fiber/hook.cc
    src/sylar/net/address.cc
    src/sylar/net/socket.cc
    src/sylar/net/bytearray.cc
    src/sylar/net/stream.cc
    src/sylar/net/socket_stream.cc
    src/sylar/net/tcp_server.cc
    src/sylar/net/udp_server.cc
    src/http/core/http.cc
    src/http/core/http_request.cc
    src/http/core/http_response.cc
    src/http/core/http_memory_pool.cc
    src/http/core/http_framework_config.cc
    src/http/core/http_error.cc
    src/http/ssl/ssl_config.cc
    src/http/ssl/ssl_context.cc
    src/http/ssl/ssl_socket.cc
    src/http/core/http_context.cc
    src/http/core/http_parser.cc
    src/http/server/http_session.cc
    src/http/router/router.cc
    src/http/router/servlet.cc
    src/http/server/http_server.cc
    src/http/session/session.cc
    src/http/session/session_storage.cc
    src/http/session/session_manager.cc
    src/http/middleware/middleware.cc
    src/http/middleware/cors/cors_config.cc
    src/http/middleware/cors/cors_middleware.cc
    src/http/stream/sse.cc
)

# 生成共享库 libsylar.so
add_library(sylar SHARED ${SYLAR_SOURCES})

# 链接库
target_link_libraries(sylar yaml-cpp pthread dl ssl crypto)

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

add_executable(test_http_parser tests/test_http_parser.cc)
target_link_libraries(test_http_parser sylar)

add_executable(test_http_context tests/test_http_context.cc)
target_link_libraries(test_http_context sylar)

add_executable(test_http_router tests/test_http_router.cc)
target_link_libraries(test_http_router sylar)

add_executable(test_http_server tests/test_http_server.cc)
target_link_libraries(test_http_server sylar)

add_executable(test_http_ssl_server tests/test_http_ssl_server.cc)
target_link_libraries(test_http_ssl_server sylar)

add_executable(test_cors_middleware tests/test_cors_middleware.cc)
target_link_libraries(test_cors_middleware sylar)

add_executable(test_http_dispatch tests/test_http_dispatch.cc)
target_link_libraries(test_http_dispatch sylar)

add_executable(test_http_framework_config tests/test_http_framework_config.cc)
target_link_libraries(test_http_framework_config sylar)

add_executable(test_memory_pool tests/test_memory_pool.cc)
target_link_libraries(test_memory_pool sylar)

add_executable(test_session_manager tests/test_session_manager.cc)
target_link_libraries(test_session_manager sylar)

add_executable(test_sse_writer tests/test_sse_writer.cc)
target_link_libraries(test_sse_writer sylar)

add_executable(test_sse_server tests/test_sse_server.cc)
target_link_libraries(test_sse_server sylar)

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
