#include "http/server/http_server.h"
#include "http/core/http_error.h"
#include "http/core/http_framework_config.h"
#include "log/logger.h"
#include "sylar/fiber/hook.h"
#include "sylar/net/address.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{

struct Options
{
    std::string host = "127.0.0.1";
    uint16_t port = 18080;
    uint32_t io_threads = 4;
    uint32_t accept_threads = 1;
    uint32_t max_connections = 0;
    uint64_t keepalive_timeout_ms = 5000;
    uint32_t keepalive_max_requests = 0;
};

volatile std::sig_atomic_t g_stop_requested = 0;

void HandleStopSignal(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}

void PrintUsage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << " [options]\n"
        << "  --host <host>                    Bind host (default: 127.0.0.1)\n"
        << "  --port <port>                    Bind port (default: 18080)\n"
        << "  --io-threads <n>                 HTTP IO worker threads (default: 4)\n"
        << "  --accept-threads <n>             HTTP accept worker threads (default: 1)\n"
        << "  --max-connections <n>            Max concurrent connections, 0=unlimited (default: 0)\n"
        << "  --keepalive-timeout-ms <ms>      Keep-alive idle timeout in ms (default: 5000)\n"
        << "  --keepalive-max-requests <n>     Max requests per keep-alive connection, 0=unlimited (default: 0)\n"
        << "  --help                           Show this help\n";
}

bool ParseUint32(const std::string& text, uint32_t& out)
{
    errno = 0;
    char* end = nullptr;
    unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0' || value > std::numeric_limits<uint32_t>::max())
    {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

bool ParseUint64(const std::string& text, uint64_t& out)
{
    errno = 0;
    char* end = nullptr;
    unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0')
    {
        return false;
    }
    out = static_cast<uint64_t>(value);
    return true;
}

bool ParsePort(const std::string& text, uint16_t& out)
{
    uint32_t value = 0;
    if (!ParseUint32(text, value) || value > std::numeric_limits<uint16_t>::max())
    {
        return false;
    }
    out = static_cast<uint16_t>(value);
    return true;
}

bool ReadOptionValue(const std::string& arg, int& index, int argc, char** argv,
                     const std::string& name, std::string& value)
{
    const std::string prefix = name + "=";
    if (arg == name)
    {
        if (index + 1 >= argc)
        {
            return false;
        }
        value = argv[++index];
        return true;
    }
    if (arg.compare(0, prefix.size(), prefix) == 0)
    {
        value = arg.substr(prefix.size());
        return !value.empty();
    }
    return false;
}

bool ParseArgs(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help")
        {
            PrintUsage(argv[0]);
            return false;
        }

        std::string value;
        if (ReadOptionValue(arg, i, argc, argv, "--host", value))
        {
            options.host = value;
            continue;
        }
        if (ReadOptionValue(arg, i, argc, argv, "--port", value))
        {
            if (!ParsePort(value, options.port))
            {
                std::cerr << "Invalid --port value: " << value << std::endl;
                return false;
            }
            continue;
        }
        if (ReadOptionValue(arg, i, argc, argv, "--io-threads", value))
        {
            if (!ParseUint32(value, options.io_threads) || options.io_threads == 0)
            {
                std::cerr << "Invalid --io-threads value: " << value << std::endl;
                return false;
            }
            continue;
        }
        if (ReadOptionValue(arg, i, argc, argv, "--accept-threads", value))
        {
            if (!ParseUint32(value, options.accept_threads) || options.accept_threads == 0)
            {
                std::cerr << "Invalid --accept-threads value: " << value << std::endl;
                return false;
            }
            continue;
        }
        if (ReadOptionValue(arg, i, argc, argv, "--max-connections", value))
        {
            if (!ParseUint32(value, options.max_connections))
            {
                std::cerr << "Invalid --max-connections value: " << value << std::endl;
                return false;
            }
            continue;
        }
        if (ReadOptionValue(arg, i, argc, argv, "--keepalive-timeout-ms", value))
        {
            if (!ParseUint64(value, options.keepalive_timeout_ms))
            {
                std::cerr << "Invalid --keepalive-timeout-ms value: " << value << std::endl;
                return false;
            }
            continue;
        }
        if (ReadOptionValue(arg, i, argc, argv, "--keepalive-max-requests", value))
        {
            if (!ParseUint32(value, options.keepalive_max_requests))
            {
                std::cerr << "Invalid --keepalive-max-requests value: " << value << std::endl;
                return false;
            }
            continue;
        }

        std::cerr << "Unknown argument: " << arg << std::endl;
        PrintUsage(argv[0]);
        return false;
    }

    return true;
}

bool ParseSleepMs(const std::string& text, uint32_t& out)
{
    uint32_t parsed = 0;
    if (!ParseUint32(text, parsed))
    {
        return false;
    }
    out = std::min<uint32_t>(parsed, 10000);
    return true;
}

void InstallSignalHandlers()
{
    std::signal(SIGINT, HandleStopSignal);
    std::signal(SIGTERM, HandleStopSignal);
}

void RegisterRoutes(const http::HttpServer::ptr& server)
{
    server->getServletDispatch()->addPreInterceptor([](http::HttpRequest::ptr req,
                                                      http::HttpResponse::ptr rsp,
                                                      http::HttpSession::ptr) {
        if (req->getPath() == "/blocked")
        {
            http::ApplyErrorResponse(rsp, http::HttpStatus::BAD_REQUEST, "Blocked", "blocked by pre interceptor");
            return false;
        }
        return true;
    });

    server->getServletDispatch()->addServlet("/ping", [](http::HttpRequest::ptr,
                                                         http::HttpResponse::ptr rsp,
                                                         http::HttpSession::ptr) {
        rsp->setHeader("Content-Type", "text/plain");
        rsp->setBody("pong");
        return 0;
    });

    server->getServletDispatch()->addServlet("/user/me", [](http::HttpRequest::ptr,
                                                            http::HttpResponse::ptr rsp,
                                                            http::HttpSession::ptr) {
        rsp->setHeader("Content-Type", "text/plain");
        rsp->setBody("exact-user");
        return 0;
    });

    server->getServletDispatch()->addParamServlet("/user/:id", [](http::HttpRequest::ptr req,
                                                                  http::HttpResponse::ptr rsp,
                                                                  http::HttpSession::ptr) {
        rsp->setHeader("Content-Type", "text/plain");
        rsp->setBody("user:" + req->getRouteParam("id"));
        return 0;
    });

    server->getServletDispatch()->addParamServlet("/sleep/:ms", [](http::HttpRequest::ptr req,
                                                                   http::HttpResponse::ptr rsp,
                                                                   http::HttpSession::ptr) {
        uint32_t sleep_ms = 0;
        if (!ParseSleepMs(req->getRouteParam("ms"), sleep_ms))
        {
            http::ApplyErrorResponse(rsp, http::HttpStatus::BAD_REQUEST, "Bad Request", "invalid sleep duration");
            return 0;
        }

        if (sleep_ms > 0)
        {
            usleep(static_cast<useconds_t>(sleep_ms) * 1000);
        }
        rsp->setHeader("Content-Type", "text/plain");
        rsp->setBody("slept:" + std::to_string(sleep_ms));
        return 0;
    });
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!ParseArgs(argc, argv, options))
    {
        return argc > 1 && std::string(argv[1]) == "--help" ? 0 : 1;
    }

    base::LoggerMgr::GetInstance()->getRoot()->setLevel(base::LogLevel::FATAL);
    base::LoggerMgr::GetInstance()->getLogger("system")->setLevel(base::LogLevel::FATAL);

    sylar::set_hook_enable(true);
    InstallSignalHandlers();

    http::HttpFrameworkConfig::SetErrorResponseFormat(http::HttpFrameworkConfig::ERROR_FORMAT_JSON);
    http::HttpFrameworkConfig::SetIOWorkerThreads(options.io_threads);
    http::HttpFrameworkConfig::SetAcceptWorkerThreads(options.accept_threads);
    http::HttpFrameworkConfig::SetMaxConnections(options.max_connections);
    http::HttpFrameworkConfig::SetKeepAliveTimeoutMs(options.keepalive_timeout_ms);
    http::HttpFrameworkConfig::SetKeepAliveMaxRequests(options.keepalive_max_requests);

    http::HttpServer::ptr server = http::HttpServer::CreateWithConfig();
    RegisterRoutes(server);

    std::vector<sylar::Address::ptr> addrs;
    std::vector<sylar::Address::ptr> fails;
    addrs.push_back(sylar::Address::LookupAny(options.host + ":" + std::to_string(options.port)));
    if (!addrs[0] || !server->bind(addrs, fails) || !server->start())
    {
        std::cerr << "Failed to start http_bench_server on "
                  << options.host << ":" << options.port << std::endl;
        return 2;
    }

    std::cout << "http_bench_server listening on " << options.host << ":" << options.port
              << " io_threads=" << options.io_threads
              << " accept_threads=" << options.accept_threads
              << " max_connections=" << options.max_connections
              << " keepalive_timeout_ms=" << options.keepalive_timeout_ms
              << " keepalive_max_requests=" << options.keepalive_max_requests
              << std::endl;

    while (!g_stop_requested)
    {
        usleep(200 * 1000);
    }

    std::cout << "http_bench_server shutting down" << std::endl;
    server->stop();
    usleep(200 * 1000);
    return 0;
}
