#include "http/core/http_framework_config.h"
#include "http/core/http_request.h"
#include "http/core/http_response.h"
#include "http/router/servlet.h"
#include "http/server/http_server.h"
#include "http/server/http_session.h"
#include "log/logger.h"
#include "sylar/fiber/iomanager.h"
#include "sylar/net/address.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace
{

std::atomic<bool> g_stop(false);

struct Options
{
    std::string host = "127.0.0.1";
    uint16_t port = 18080;
    uint32_t io_threads = 4;
    uint32_t accept_threads = 1;
    uint32_t max_connections = 0;
    uint64_t keepalive_timeout_ms = 5000;
    uint32_t keepalive_max_requests = 0;
    bool session_enabled = false;
};

void PrintUsage(const char* prog)
{
    std::cout
        << "Usage: " << prog << " [options]\n"
        << "  --host <host>                 Bind host (default: 127.0.0.1)\n"
        << "  --port <port>                 Bind port (default: 18080)\n"
        << "  --io-threads <n>              HTTP IO worker threads (default: 4)\n"
        << "  --accept-threads <n>          HTTP accept worker threads (default: 1)\n"
        << "  --max-connections <n>         Max concurrent connections, 0=unlimited\n"
        << "  --keepalive-timeout-ms <ms>   Keep-alive idle timeout in ms (default: 5000)\n"
        << "  --keepalive-max-requests <n>  Max keep-alive requests, 0=unlimited\n"
        << "  --session-enabled <0|1>       Enable automatic HTTP sessions (default: 0)\n"
        << "  --help                        Show this help\n";
}

bool ParseBool(const std::string& value)
{
    return value == "1" || value == "true" || value == "TRUE";
}

bool ParseUInt32(const char* value, uint32_t& out)
{
    char* end = nullptr;
    unsigned long parsed = std::strtoul(value, &end, 10);
    if (!end || *end != '\0' || parsed > 0xffffffffUL)
    {
        return false;
    }
    out = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseUInt16(const char* value, uint16_t& out)
{
    uint32_t parsed = 0;
    if (!ParseUInt32(value, parsed) || parsed > 65535u)
    {
        return false;
    }
    out = static_cast<uint16_t>(parsed);
    return true;
}

bool ParseUInt64(const char* value, uint64_t& out)
{
    char* end = nullptr;
    unsigned long long parsed = std::strtoull(value, &end, 10);
    if (!end || *end != '\0')
    {
        return false;
    }
    out = static_cast<uint64_t>(parsed);
    return true;
}

enum class ParseStatus
{
    OK,
    HELP,
    ERROR,
};

ParseStatus ParseOptions(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help")
        {
            PrintUsage(argv[0]);
            return ParseStatus::HELP;
        }
        if (i + 1 >= argc)
        {
            std::cerr << "missing value for " << arg << std::endl;
            return ParseStatus::ERROR;
        }

        const char* value = argv[++i];
        if (arg == "--host")
        {
            options.host = value;
        }
        else if (arg == "--port")
        {
            if (!ParseUInt16(value, options.port))
            {
                std::cerr << "invalid port: " << value << std::endl;
                return ParseStatus::ERROR;
            }
        }
        else if (arg == "--io-threads")
        {
            if (!ParseUInt32(value, options.io_threads) || options.io_threads == 0)
            {
                std::cerr << "invalid io-threads: " << value << std::endl;
                return ParseStatus::ERROR;
            }
        }
        else if (arg == "--accept-threads")
        {
            if (!ParseUInt32(value, options.accept_threads) || options.accept_threads == 0)
            {
                std::cerr << "invalid accept-threads: " << value << std::endl;
                return ParseStatus::ERROR;
            }
        }
        else if (arg == "--max-connections")
        {
            if (!ParseUInt32(value, options.max_connections))
            {
                std::cerr << "invalid max-connections: " << value << std::endl;
                return ParseStatus::ERROR;
            }
        }
        else if (arg == "--keepalive-timeout-ms")
        {
            if (!ParseUInt64(value, options.keepalive_timeout_ms))
            {
                std::cerr << "invalid keepalive-timeout-ms: " << value << std::endl;
                return ParseStatus::ERROR;
            }
        }
        else if (arg == "--keepalive-max-requests")
        {
            if (!ParseUInt32(value, options.keepalive_max_requests))
            {
                std::cerr << "invalid keepalive-max-requests: " << value << std::endl;
                return ParseStatus::ERROR;
            }
        }
        else if (arg == "--session-enabled")
        {
            options.session_enabled = ParseBool(value);
        }
        else
        {
            std::cerr << "unknown argument: " << arg << std::endl;
            return ParseStatus::ERROR;
        }
    }
    return ParseStatus::OK;
}

void HandleSignal(int)
{
    g_stop.store(true, std::memory_order_release);
}

void InstallSignals()
{
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    ParseStatus parse_status = ParseOptions(argc, argv, options);
    if (parse_status == ParseStatus::HELP)
    {
        return 0;
    }
    if (parse_status == ParseStatus::ERROR)
    {
        return 1;
    }

    sylar::Logger::ptr root = BASE_LOG_ROOT();
    root->setLevel(sylar::LogLevel::WARN);
    BASE_LOG_NAME("system")->setLevel(sylar::LogLevel::WARN);

    http::HttpFrameworkConfig::SetSessionEnabled(options.session_enabled);
    http::HttpFrameworkConfig::SetMaxConnections(options.max_connections);
    http::HttpFrameworkConfig::SetKeepAliveTimeoutMs(options.keepalive_timeout_ms);
    http::HttpFrameworkConfig::SetKeepAliveMaxRequests(options.keepalive_max_requests);

    std::shared_ptr<sylar::IOManager> io_worker(
        new sylar::IOManager(options.io_threads, false, "http-bench-io"));
    std::shared_ptr<sylar::IOManager> accept_worker(
        new sylar::IOManager(options.accept_threads, false, "http-bench-accept"));

    http::HttpServer::ptr server(new http::HttpServer(io_worker.get(), accept_worker.get()));
    server->getServletDispatch()->addServlet(
        "/ping",
        [](http::HttpRequest::ptr, http::HttpResponse::ptr resp, http::HttpSession::ptr) -> int32_t
        {
            resp->setStatus(http::HttpStatus::OK);
            resp->setHeader("Content-Type", "text/plain");
            resp->setBody("pong");
            return 0;
        });

    server->getServletDispatch()->addServlet(
        http::HttpMethod::POST,
        "/echo",
        [](http::HttpRequest::ptr req, http::HttpResponse::ptr resp, http::HttpSession::ptr) -> int32_t
        {
            resp->setStatus(http::HttpStatus::OK);
            resp->setHeader("Content-Type", "text/plain");
            resp->setBody(req->getBody());
            return 0;
        });

    sylar::Address::ptr addr = sylar::Address::LookupAny(options.host + ":" + std::to_string(options.port));
    if (!addr)
    {
        std::cerr << "failed to resolve bind address: " << options.host << ":" << options.port << std::endl;
        return 1;
    }
    if (!server->bind(addr))
    {
        std::cerr << "failed to bind: " << addr->toString() << std::endl;
        return 1;
    }
    if (!server->start())
    {
        std::cerr << "failed to start http bench server" << std::endl;
        return 1;
    }

    InstallSignals();

    std::cout
        << "http_bench_server listening on " << options.host << ":" << options.port << "\n"
        << "  io_threads=" << options.io_threads
        << " accept_threads=" << options.accept_threads
        << " session_enabled=" << (options.session_enabled ? 1 : 0)
        << " keepalive_timeout_ms=" << options.keepalive_timeout_ms
        << " keepalive_max_requests=" << options.keepalive_max_requests
        << " max_connections=" << options.max_connections << "\n"
        << "  routes: GET /ping, POST /echo\n"
        << std::flush;

    while (!g_stop.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server->stop();
    accept_worker->stop();
    io_worker->stop();
    return 0;
}
