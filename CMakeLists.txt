cmake_minimum_required(VERSION 3.0)

project(sylar)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 编译选项（按构建类型控制优化级别）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -rdynamic -fPIC")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -DNDEBUG")

# 协程上下文后端：libco_asm / ucontext
set(SYLAR_FIBER_CONTEXT_BACKEND "libco_asm" CACHE STRING "Fiber context backend: libco_asm or ucontext")
set_property(CACHE SYLAR_FIBER_CONTEXT_BACKEND PROPERTY STRINGS libco_asm ucontext)

if(SYLAR_FIBER_CONTEXT_BACKEND STREQUAL "libco_asm")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "libco_asm backend only supports Linux")
    endif()
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        message(FATAL_ERROR "libco_asm backend only supports x86_64")
    endif()
    enable_language(ASM)
    add_definitions(-DSYLAR_FIBER_CONTEXT_BACKEND_LIBCO_ASM)
elseif(SYLAR_FIBER_CONTEXT_BACKEND STREQUAL "ucontext")
    add_definitions(-DSYLAR_FIBER_CONTEXT_BACKEND_UCONTEXT)
else()
    message(FATAL_ERROR "Invalid SYLAR_FIBER_CONTEXT_BACKEND: ${SYLAR_FIBER_CONTEXT_BACKEND}")
endif()

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
    sylar/fiber/context.cc
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

if(SYLAR_FIBER_CONTEXT_BACKEND STREQUAL "libco_asm")
    list(APPEND SYLAR_SOURCES sylar/fiber/coctx_swap_x86_64.S)
endif()

# 生成共享库 libsylar.so
add_library(sylar SHARED ${SYLAR_SOURCES})

# 链接库
target_link_libraries(sylar yaml-cpp pthread dl)

# 定义输出路径
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)

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

add_executable(test_memorypool tests/test_memorypool.cc)
target_link_libraries(test_memorypool pthread)

add_executable(test_benchmark_fiber tests/test_benchmark_fiber.cc)
target_link_libraries(test_benchmark_fiber sylar)

add_executable(test_benchmark_tcp_allocator tests/test_benchmark_tcp_allocator.cc)
target_link_libraries(test_benchmark_tcp_allocator sylar)

add_executable(test_context_switch_pingpong tests/test_context_switch_pingpong.cc)
target_link_libraries(test_context_switch_pingpong sylar)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_context_switch_pingpong PRIVATE -O3)
endif()
