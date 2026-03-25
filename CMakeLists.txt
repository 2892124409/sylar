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

# 网络层实现变体：local / upstream_ref
set(SYLAR_NET_VARIANT "local" CACHE STRING "Network runtime variant: local or upstream_ref")
set_property(CACHE SYLAR_NET_VARIANT PROPERTY STRINGS local upstream_ref)

if(NOT SYLAR_NET_VARIANT STREQUAL "local" AND NOT SYLAR_NET_VARIANT STREQUAL "upstream_ref")
    message(FATAL_ERROR "Invalid SYLAR_NET_VARIANT: ${SYLAR_NET_VARIANT}")
endif()

# 仅本地网络层支持协程上下文后端开关
if(SYLAR_NET_VARIANT STREQUAL "local")
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
else()
    add_definitions(-DSYLAR_NET_VARIANT_UPSTREAM_REF)
endif()

# 包含项目根目录与新源码根目录
include_directories(.)
include_directories(src)
include_directories(src/base)
if(SYLAR_NET_VARIANT STREQUAL "upstream_ref")
    include_directories(BEFORE src/compat/upstream_ref)
endif()

# 核心公共源码（两种网络层都会复用）
set(SYLAR_COMMON_SOURCES
    src/base/log/log_level.cc
    src/base/log/log_formatter.cc
    src/base/log/log_appender.cc
    src/base/log/logger.cc
    src/sylar/base/util.cc
    src/base/config/config.cc
)

# 网络层源码按变体分流
if(SYLAR_NET_VARIANT STREQUAL "local")
    set(SYLAR_SOURCES
        ${SYLAR_COMMON_SOURCES}
        src/sylar/concurrency/mutex/semaphore.cc
        src/sylar/concurrency/thread.cc
        src/sylar/fiber/fiber.cc
        src/sylar/fiber/context.cc
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
    )

    if(SYLAR_FIBER_CONTEXT_BACKEND STREQUAL "libco_asm")
        list(APPEND SYLAR_SOURCES src/sylar/fiber/coctx_swap_x86_64.S)
    endif()
else()
    set(SYLAR_SOURCES
        ${SYLAR_COMMON_SOURCES}
        src/sylar/net/bytearray.cc
        src/sylar_upstream_ref/address.cc
        src/sylar_upstream_ref/fd_manager.cc
        src/sylar_upstream_ref/fiber.cc
        src/sylar_upstream_ref/hook.cc
        src/sylar_upstream_ref/iomanager.cc
        src/sylar_upstream_ref/mutex.cc
        src/sylar_upstream_ref/scheduler.cc
        src/sylar_upstream_ref/socket.cc
        src/sylar_upstream_ref/stream.cc
        src/sylar_upstream_ref/tcp_server.cc
        src/sylar_upstream_ref/thread.cc
        src/sylar_upstream_ref/timer.cc
        src/sylar_upstream_ref/streams/socket_stream.cc
    )
endif()

find_package(OpenSSL REQUIRED)

# 生成共享库 libsylar.so
add_library(sylar SHARED ${SYLAR_SOURCES})

# 链接库
target_link_libraries(sylar yaml-cpp pthread dl OpenSSL::SSL OpenSSL::Crypto)

# HTTP 框架源文件
set(HTTP_SOURCES
    src/http/core/http.cc
    src/http/core/http_context.cc
    src/http/core/http_error.cc
    src/http/core/http_framework_config.cc
    src/http/core/http_parser.cc
    src/http/core/http_request.cc
    src/http/core/http_response.cc
    src/http/middleware/cors/cors_config.cc
    src/http/middleware/cors/cors_middleware.cc
    src/http/middleware/middleware.cc
    src/http/router/router.cc
    src/http/router/servlet.cc
    src/http/server/http_server.cc
    src/http/server/http_session.cc
    src/http/session/session.cc
    src/http/session/session_manager.cc
    src/http/session/session_storage.cc
    src/http/ssl/ssl_config.cc
    src/http/ssl/ssl_context.cc
    src/http/ssl/ssl_socket.cc
    src/http/stream/sse.cc
)

add_library(http SHARED ${HTTP_SOURCES})
target_link_libraries(http sylar OpenSSL::SSL OpenSSL::Crypto)

# 定义输出路径
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)

# HTTP 相关可执行（两种变体都需要）
add_executable(test_http_server tests/test_http_server.cc)
target_link_libraries(test_http_server sylar http)

add_executable(test_http_parser tests/test_http_parser.cc)
target_link_libraries(test_http_parser sylar http)

add_executable(http_bench_server tests/http_bench_server.cc)
target_link_libraries(http_bench_server sylar http)

# 其余测试目标：上游参考变体仅保留网络层关键回归
if(SYLAR_NET_VARIANT STREQUAL "local")
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

    add_executable(test_fd_manager tests/test_fd_manager.cc)
    target_link_libraries(test_fd_manager sylar)
else()
    add_executable(test_iomanager tests/test_iomanager.cc)
    target_link_libraries(test_iomanager sylar)

    add_executable(test_hook tests/test_hook.cc)
    target_link_libraries(test_hook sylar)

    add_executable(test_socket tests/test_socket.cc)
    target_link_libraries(test_socket sylar)

    add_executable(test_fd_manager tests/test_fd_manager.cc)
    target_link_libraries(test_fd_manager sylar)
endif()
