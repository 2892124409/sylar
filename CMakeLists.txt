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

# 包含项目根目录与新源码根目录
include_directories(.)
include_directories(src)
include_directories(src/base)

find_package(CURL REQUIRED)
if(CURL_INCLUDE_DIRS)
    include_directories(${CURL_INCLUDE_DIRS})
endif()

find_library(MYSQLCLIENT_LIBRARY NAMES mysqlclient mariadb)
if(NOT MYSQLCLIENT_LIBRARY)
    message(FATAL_ERROR "mysqlclient or mariadb client library not found")
endif()

find_library(RABBITMQ_LIBRARY NAMES rabbitmq)
if(NOT RABBITMQ_LIBRARY)
    message(FATAL_ERROR "librabbitmq library not found")
endif()

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

# 生成共享库 libsylar.so
add_library(sylar SHARED ${SYLAR_SOURCES})

# 链接库
target_link_libraries(sylar yaml-cpp pthread dl)

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

find_package(OpenSSL REQUIRED)
add_library(http SHARED ${HTTP_SOURCES})
target_link_libraries(http sylar OpenSSL::SSL OpenSSL::Crypto)

# AI 应用层源文件（排除各可执行入口）
set(AI_SOURCES
    src/ai/common/ai_utils.cc
    src/ai/config/ai_app_config.cc
    src/ai/http/ai_http_api.cc
    src/ai/http/ai_http_handlers.cc
    src/ai/llm/anthropic_client.cc
    src/ai/llm/fiber_curl_session.cc
    src/ai/llm/llm_client_factory.cc
    src/ai/llm/llm_client_registry.cc
    src/ai/llm/llm_router.cc
    src/ai/llm/openai_compatible_client.cc
    src/ai/llm/provider_key_pool.cc
    src/ai/middleware/auth_middleware.cc
    src/ai/middleware/request_id_middleware.cc
    src/ai/mq/rabbitmq_amqp_client.cc
    src/ai/mq/rabbitmq_message_sink.cc
    src/ai/rag/embedding_client.cc
    src/ai/rag/rag_http_client.cc
    src/ai/rag/rag_indexer.cc
    src/ai/rag/rag_retriever.cc
    src/ai/rag/vector_store.cc
    src/ai/service/auth_service.cc
    src/ai/service/chat_service.cc
    src/ai/storage/api_key_pool_repository.cc
    src/ai/storage/async_mysql_writer.cc
    src/ai/storage/auth_repository.cc
    src/ai/storage/chat_message_persister.cc
    src/ai/storage/chat_repository.cc
    src/ai/storage/mysql_connection_pool.cc
)

add_library(ai SHARED ${AI_SOURCES})
target_link_libraries(ai
    PUBLIC
        http
        sylar
        ${CURL_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
        ${MYSQLCLIENT_LIBRARY}
        ${RABBITMQ_LIBRARY}
        yaml-cpp
)

# 定义输出路径
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)

# AI 应用可执行程序
add_executable(ai_chat_server src/ai/main.cc)
target_link_libraries(ai_chat_server ai)

add_executable(ai_auth_chat_client src/ai/client/auth_chat_client.cc)
target_link_libraries(ai_auth_chat_client ${CURL_LIBRARIES})

add_executable(ai_mq_consumer src/ai/mq/main_mq_consumer.cc)
target_link_libraries(ai_mq_consumer ai)

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

add_executable(test_memorypool tests/test_memory_pool.cc)
target_link_libraries(test_memorypool pthread)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_benchmark_fiber.cc)
    add_executable(test_benchmark_fiber tests/test_benchmark_fiber.cc)
    target_link_libraries(test_benchmark_fiber sylar)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_benchmark_tcp_allocator.cc)
    add_executable(test_benchmark_tcp_allocator tests/test_benchmark_tcp_allocator.cc)
    target_link_libraries(test_benchmark_tcp_allocator sylar)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_context_switch_pingpong.cc)
    add_executable(test_context_switch_pingpong tests/test_context_switch_pingpong.cc)
    target_link_libraries(test_context_switch_pingpong sylar)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_context_switch_pingpong PRIVATE -O3)
    endif()
endif()

add_executable(test_http_server tests/test_http_server.cc)
target_link_libraries(test_http_server sylar http)

add_executable(test_http_parser tests/test_http_parser.cc)
target_link_libraries(test_http_parser sylar http)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fd_manager.cc)
    add_executable(test_fd_manager tests/test_fd_manager.cc)
    target_link_libraries(test_fd_manager sylar)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/http_bench_server.cc)
    add_executable(http_bench_server tests/http_bench_server.cc)
    target_link_libraries(http_bench_server sylar http)
endif()

add_executable(test_ai_chat_service tests/test_ai_chat_service.cc)
target_link_libraries(test_ai_chat_service ai)

add_executable(test_rag_indexer tests/test_rag_indexer.cc)
target_link_libraries(test_rag_indexer ai)
