cmake_minimum_required(VERSION 3.10)

project(sylar LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认未指定构建类型时使用 Debug。
# 如果要走 Release，建议使用独立构建目录，避免和 Debug 共用缓存：
#   cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-release -j
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(yaml-cpp REQUIRED)

set(SYLAR_OUTPUT_BIN_DIR "${PROJECT_SOURCE_DIR}/bin")
set(SYLAR_OUTPUT_LIB_DIR "${PROJECT_SOURCE_DIR}/lib")

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

set(SYLAR_PUBLIC_LINK_LIBRARIES Threads::Threads)
if(TARGET yaml-cpp::yaml-cpp)
    list(APPEND SYLAR_PUBLIC_LINK_LIBRARIES yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
    list(APPEND SYLAR_PUBLIC_LINK_LIBRARIES yaml-cpp)
elseif(YAML_CPP_LIBRARIES)
    list(APPEND SYLAR_PUBLIC_LINK_LIBRARIES ${YAML_CPP_LIBRARIES})
else()
    message(FATAL_ERROR "yaml-cpp was found, but no usable target or library variable was exported")
endif()

set(SYLAR_PRIVATE_LINK_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
if(CMAKE_DL_LIBS)
    list(APPEND SYLAR_PRIVATE_LINK_LIBRARIES ${CMAKE_DL_LIBS})
endif()

add_library(sylar SHARED ${SYLAR_SOURCES})

target_include_directories(sylar
    PUBLIC
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/src/base"
)

target_link_libraries(sylar
    PUBLIC
        ${SYLAR_PUBLIC_LINK_LIBRARIES}
    PRIVATE
        ${SYLAR_PRIVATE_LINK_LIBRARIES}
)

set_target_properties(sylar PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${SYLAR_OUTPUT_LIB_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${SYLAR_OUTPUT_LIB_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${SYLAR_OUTPUT_LIB_DIR}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sylar PRIVATE -Wall)
endif()

add_executable(http_bench_server "benchmarks/http/http_bench_server.cc")
target_link_libraries(http_bench_server PRIVATE sylar)

set_target_properties(http_bench_server PROPERTIES
    BUILD_RPATH "${SYLAR_OUTPUT_LIB_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${SYLAR_OUTPUT_BIN_DIR}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(http_bench_server PRIVATE -Wall)
endif()

option(BUILD_AI_CHAT_SERVER "Build AI chat server application target" OFF)

if(BUILD_AI_CHAT_SERVER)
    find_package(CURL QUIET)
    if(NOT CURL_FOUND)
        message(FATAL_ERROR "BUILD_AI_CHAT_SERVER=ON requires libcurl development files. Install libcurl4-openssl-dev (or equivalent).")
    endif()

    find_library(MYSQLCLIENT_LIBRARY NAMES mysqlclient)
    if(NOT MYSQLCLIENT_LIBRARY)
        message(FATAL_ERROR "BUILD_AI_CHAT_SERVER=ON requires libmysqlclient. Install libmysqlclient-dev (or equivalent).")
    endif()

    set(AI_CHAT_SERVER_SOURCES
        src/ai/common/ai_utils.cc
        src/ai/config/ai_app_config.cc
        src/ai/http/ai_http_api.cc
        src/ai/llm/deepseek_client.cc
        src/ai/service/chat_service.cc
        src/ai/storage/chat_repository.cc
        src/ai/storage/async_mysql_writer.cc
        src/ai/main.cc
    )

    add_executable(ai_chat_server ${AI_CHAT_SERVER_SOURCES})
    target_link_libraries(ai_chat_server PRIVATE sylar ${MYSQLCLIENT_LIBRARY})

    if(TARGET CURL::libcurl)
        target_link_libraries(ai_chat_server PRIVATE CURL::libcurl)
    else()
        target_include_directories(ai_chat_server PRIVATE ${CURL_INCLUDE_DIRS})
        target_link_libraries(ai_chat_server PRIVATE ${CURL_LIBRARIES})
    endif()

    set_target_properties(ai_chat_server PROPERTIES
        BUILD_RPATH "${SYLAR_OUTPUT_LIB_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${SYLAR_OUTPUT_BIN_DIR}"
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ai_chat_server PRIVATE -Wall)
    endif()

    add_executable(ai_chat_client
        src/ai/client/chat_client.cc
    )
    if(TARGET CURL::libcurl)
        target_link_libraries(ai_chat_client PRIVATE CURL::libcurl)
    else()
        target_include_directories(ai_chat_client PRIVATE ${CURL_INCLUDE_DIRS})
        target_link_libraries(ai_chat_client PRIVATE ${CURL_LIBRARIES})
    endif()
    set_target_properties(ai_chat_client PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${SYLAR_OUTPUT_BIN_DIR}"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ai_chat_client PRIVATE -Wall)
    endif()
endif()

function(add_sylar_test target_name)
    add_executable(${target_name} "tests/${target_name}.cc")
    target_link_libraries(${target_name} PRIVATE sylar)

    set_target_properties(${target_name} PROPERTIES
        ENABLE_EXPORTS ON
        BUILD_RPATH "${SYLAR_OUTPUT_LIB_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${SYLAR_OUTPUT_BIN_DIR}"
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE -Wall)
    endif()

    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

if(BUILD_TESTING)
    set(SYLAR_TESTS
        test_log
        test_config
        test_thread
        test_fiber
        test_scheduler
        test_fiber_pool
        test_iomanager
        test_timer
        test_hook
        test_hook_timeout_mt
        test_endian
        test_address
        test_socket
        test_bytearray
        test_stream
        test_tcp_server
        test_udp_server
        test_server_mode_bench
        test_http_parser
        test_http_context
        test_http_router
        test_http_server
        test_http_ssl_server
        test_cors_middleware
        test_http_dispatch
        test_http_framework_config
        test_memory_pool
        test_session_manager
        test_sse_writer
        test_sse_server
        test_shared_stack_helpers
        test_shared_stack_scheduler
        test_shared_stack_scheduler_mt
        test_shared_stack_stress
        test_shared_stack_guard
        test_shared_stack_lifecycle
        test_shared_stack_longrun
        test_shared_stack_workeronly
        test_bytearray_pool_perf
    )

    foreach(test_name IN LISTS SYLAR_TESTS)
        add_sylar_test(${test_name})
    endforeach()

    add_executable(test_ai_chat_service
        tests/test_ai_chat_service.cc
        src/ai/common/ai_utils.cc
        src/ai/service/chat_service.cc
    )
    target_link_libraries(test_ai_chat_service PRIVATE sylar)
    set_target_properties(test_ai_chat_service PROPERTIES
        ENABLE_EXPORTS ON
        BUILD_RPATH "${SYLAR_OUTPUT_LIB_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${SYLAR_OUTPUT_BIN_DIR}"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_ai_chat_service PRIVATE -Wall)
    endif()
    add_test(NAME test_ai_chat_service COMMAND test_ai_chat_service)
endif()
