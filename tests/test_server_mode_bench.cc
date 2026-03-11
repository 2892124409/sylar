#include "config/config.h"
#include "sylar/fiber/fiber.h"
#include "sylar/fiber/hook.h"
#include "sylar/fiber/iomanager.h"
#include "log/logger.h"
#include "sylar/net/address.h"
#include "sylar/net/socket.h"
#include "sylar/net/socket_stream.h"
#include "sylar/net/tcp_server.h"
#include "sylar/net/udp_server.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Mode {
    bool pool_enabled;
    bool shared_stack;
    bool use_caller;
};

struct BenchConfig {
    size_t server_threads = 4;
    size_t client_threads = 6;
    size_t tcp_messages_per_client = 300;
    size_t udp_messages_per_client = 600;
    size_t payload_size = 256;
    uint32_t shared_stack_size = 128 * 1024;
    int base_tcp_port = 18080;
    int base_udp_port = 19080;
};

struct BenchResult {
    std::string protocol;
    Mode mode;
    bool success = false;
    std::string error;
    size_t expected_messages = 0;
    size_t completed_messages = 0;
    size_t server_messages = 0;
    double elapsed_ms = 0.0;
    double qps = 0.0;
    double mib_per_sec = 0.0;
    sylar::Fiber::SharedStackStats shared_stats{};
};

std::string boolLabel(bool value) {
    return value ? "on" : "off";
}

std::string modeLabel(const Mode& mode) {
    std::ostringstream os;
    os << "pool=" << boolLabel(mode.pool_enabled)
       << ",shared=" << boolLabel(mode.shared_stack)
       << ",use_caller=" << boolLabel(mode.use_caller);
    return os.str();
}

void applyMode(const Mode& mode, const BenchConfig& config) {
    sylar::Config::Lookup<bool>("fiber.pool.enabled", true, "enable fiber pool")
        ->setValue(mode.pool_enabled);
    sylar::Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack")
        ->setValue(mode.shared_stack);
    sylar::Config::Lookup<uint32_t>("fiber.shared_stack_size", 128 * 1024, "fiber shared stack size")
        ->setValue(config.shared_stack_size);
}

template <class Getter>
bool waitForCount(Getter getter, size_t expected, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (getter() >= expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return getter() >= expected;
}

void spinWaitMs(int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
}

class BenchTcpServer : public sylar::net::TcpServer {
public:
    typedef std::shared_ptr<BenchTcpServer> ptr;

    BenchTcpServer(size_t payload_size, sylar::IOManager* io_worker, sylar::IOManager* accept_worker)
        : sylar::net::TcpServer(io_worker, accept_worker)
        , m_payloadSize(payload_size) {
        setName("BenchTcpServer");
    }

    size_t messages() const { return m_messages.load(); }
    size_t bytes() const { return m_bytes.load(); }

protected:
    void handleClient(sylar::Socket::ptr client) override {
        sylar::SocketStream stream(client);
        std::vector<char> buf(m_payloadSize);
        while (true) {
            int rt = stream.readFixSize(buf.data(), buf.size());
            if (rt <= 0) {
                break;
            }
            if (stream.writeFixSize(buf.data(), buf.size()) <= 0) {
                break;
            }
            m_messages.fetch_add(1);
            m_bytes.fetch_add(buf.size());
        }
        stream.close();
    }

private:
    size_t m_payloadSize;
    std::atomic<size_t> m_messages{0};
    std::atomic<size_t> m_bytes{0};
};

class BenchUdpServer : public sylar::net::UdpServer {
public:
    typedef std::shared_ptr<BenchUdpServer> ptr;

    BenchUdpServer(sylar::IOManager* io_worker, sylar::IOManager* recv_worker)
        : sylar::net::UdpServer(io_worker, recv_worker) {
        setName("BenchUdpServer");
    }

    size_t messages() const { return m_messages.load(); }
    size_t bytes() const { return m_bytes.load(); }

protected:
    void handleDatagram(const void* data, size_t len, sylar::Address::ptr from, sylar::Socket::ptr sock) override {
        if (sock->sendTo(data, len, from) > 0) {
            m_messages.fetch_add(1);
            m_bytes.fetch_add(len);
        }
    }

private:
    std::atomic<size_t> m_messages{0};
    std::atomic<size_t> m_bytes{0};
};

BenchResult runTcpBench(const Mode& mode, const BenchConfig& config, int port) {
    BenchResult result;
    result.protocol = "tcp";
    result.mode = mode;
    result.expected_messages = config.client_threads * config.tcp_messages_per_client;

    applyMode(mode, config);
    sylar::Fiber::ResetSharedStackStats();
    sylar::set_hook_enable(true);

    sylar::IOManager iom(config.server_threads, mode.use_caller, "tcp_mode_bench");
    BenchTcpServer::ptr server(new BenchTcpServer(config.payload_size, &iom, &iom));
    std::vector<sylar::Address::ptr> addrs;
    std::vector<sylar::Address::ptr> fails;
    addrs.push_back(sylar::Address::LookupAny("127.0.0.1:" + std::to_string(port)));

    if (!server->bind(addrs, fails)) {
        result.error = "tcp bind failed";
        return result;
    }
    server->start();
    spinWaitMs(150);

    std::atomic<size_t> completed(0);
    std::atomic<size_t> failures_count(0);

    auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> clients;
    for (size_t i = 0; i < config.client_threads; ++i) {
        clients.emplace_back([&, i]() {
            sylar::Socket::ptr sock = sylar::Socket::CreateTCPSocket();
            sock->setRecvTimeout(5000);
            sock->setSendTimeout(5000);
            if (!sock->connect(sylar::Address::LookupAny("127.0.0.1:" + std::to_string(port)))) {
                failures_count.fetch_add(1);
                return;
            }
            sylar::SocketStream stream(sock);
            std::vector<char> payload(config.payload_size, 'T');
            std::vector<char> recv_buf(config.payload_size);
            payload[0] = static_cast<char>('A' + (i % 26));
            for (size_t j = 0; j < config.tcp_messages_per_client; ++j) {
                if (stream.writeFixSize(payload.data(), payload.size()) <= 0) {
                    failures_count.fetch_add(1);
                    break;
                }
                if (stream.readFixSize(recv_buf.data(), recv_buf.size()) <= 0) {
                    failures_count.fetch_add(1);
                    break;
                }
                if (std::memcmp(payload.data(), recv_buf.data(), recv_buf.size()) != 0) {
                    failures_count.fetch_add(1);
                    break;
                }
                completed.fetch_add(1);
            }
            stream.close();
        });
    }
    for (size_t i = 0; i < clients.size(); ++i) {
        clients[i].join();
    }
    auto end = std::chrono::steady_clock::now();

    waitForCount([&server]() { return server->messages(); }, result.expected_messages, 3000);
    result.completed_messages = completed.load();
    result.server_messages = server->messages();
    result.shared_stats = sylar::Fiber::GetSharedStackStats();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

    server->stop();
    spinWaitMs(150);

    if (failures_count.load() == 0 && result.completed_messages == result.expected_messages) {
        result.success = true;
        double seconds = result.elapsed_ms / 1000.0;
        result.qps = seconds > 0 ? result.completed_messages / seconds : 0.0;
        result.mib_per_sec = seconds > 0 ? (result.completed_messages * config.payload_size) / seconds / (1024.0 * 1024.0) : 0.0;
    } else {
        std::ostringstream os;
        os << "tcp client failures=" << failures_count.load()
           << ", completed=" << result.completed_messages
           << ", expected=" << result.expected_messages
           << ", server_messages=" << result.server_messages;
        result.error = os.str();
    }
    return result;
}

BenchResult runUdpBench(const Mode& mode, const BenchConfig& config, int port) {
    BenchResult result;
    result.protocol = "udp";
    result.mode = mode;
    result.expected_messages = config.client_threads * config.udp_messages_per_client;

    applyMode(mode, config);
    sylar::Fiber::ResetSharedStackStats();
    sylar::set_hook_enable(true);

    sylar::IOManager iom(config.server_threads, mode.use_caller, "udp_mode_bench");
    BenchUdpServer::ptr server(new BenchUdpServer(&iom, &iom));
    std::vector<sylar::Address::ptr> addrs;
    std::vector<sylar::Address::ptr> fails;
    addrs.push_back(sylar::Address::LookupAny("127.0.0.1:" + std::to_string(port)));

    if (!server->bind(addrs, fails)) {
        result.error = "udp bind failed";
        return result;
    }
    server->start();
    spinWaitMs(150);

    std::vector<char> base_payload(config.payload_size, 'U');
    std::atomic<size_t> completed(0);
    std::atomic<size_t> failures_count(0);

    auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> clients;
    for (size_t i = 0; i < config.client_threads; ++i) {
        clients.emplace_back([&, i]() {
            sylar::Socket::ptr sock = sylar::Socket::CreateUDPSocket();
            sock->setRecvTimeout(5000);
            sock->setSendTimeout(5000);
            std::vector<char> payload = base_payload;
            std::vector<char> recv_buf(config.payload_size);
            payload[0] = static_cast<char>('a' + (i % 26));
            sylar::Address::ptr target = sylar::Address::LookupAny("127.0.0.1:" + std::to_string(port));
            for (size_t j = 0; j < config.udp_messages_per_client; ++j) {
                if (sock->sendTo(payload.data(), payload.size(), target) <= 0) {
                    failures_count.fetch_add(1);
                    break;
                }
                sylar::Address::ptr from;
                int rt = sock->recvFrom(recv_buf.data(), recv_buf.size(), from);
                if (rt != static_cast<int>(recv_buf.size())) {
                    failures_count.fetch_add(1);
                    break;
                }
                if (std::memcmp(payload.data(), recv_buf.data(), recv_buf.size()) != 0) {
                    failures_count.fetch_add(1);
                    break;
                }
                completed.fetch_add(1);
            }
            sock->close();
        });
    }
    for (size_t i = 0; i < clients.size(); ++i) {
        clients[i].join();
    }
    auto end = std::chrono::steady_clock::now();

    spinWaitMs(200);
    result.completed_messages = completed.load();
    result.server_messages = server->messages();
    result.shared_stats = sylar::Fiber::GetSharedStackStats();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

    server->stop();
    spinWaitMs(150);

    if (failures_count.load() == 0 && result.completed_messages == result.expected_messages) {
        result.success = true;
        double seconds = result.elapsed_ms / 1000.0;
        result.qps = seconds > 0 ? result.completed_messages / seconds : 0.0;
        result.mib_per_sec = seconds > 0 ? (result.completed_messages * config.payload_size) / seconds / (1024.0 * 1024.0) : 0.0;
    } else {
        std::ostringstream os;
        os << "udp client failures=" << failures_count.load()
           << ", completed=" << result.completed_messages
           << ", expected=" << result.expected_messages
           << ", server_messages=" << result.server_messages;
        result.error = os.str();
    }
    return result;
}

void printResult(const BenchResult& result) {
    std::cout << std::fixed << std::setprecision(2)
              << "RESULT protocol=" << result.protocol
              << " mode={" << modeLabel(result.mode) << "}"
              << " success=" << (result.success ? "yes" : "no")
              << " elapsed_ms=" << result.elapsed_ms
              << " qps=" << result.qps
              << " mib_per_sec=" << result.mib_per_sec
              << " completed=" << result.completed_messages << "/" << result.expected_messages
              << " server_messages=" << result.server_messages
              << " shared_save=" << result.shared_stats.save_count
              << " shared_restore=" << result.shared_stats.restore_count;
    if (!result.success) {
        std::cout << " error=\"" << result.error << "\"";
    }
    std::cout << std::endl;
}

void printSummary(const std::vector<BenchResult>& results, const std::string& protocol) {
    double best_qps = 0.0;
    const BenchResult* best = nullptr;
    const BenchResult* baseline = nullptr;
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i].protocol != protocol || !results[i].success) {
            continue;
        }
        if (!baseline && !results[i].mode.pool_enabled && !results[i].mode.shared_stack && results[i].mode.use_caller) {
            baseline = &results[i];
        }
        if (results[i].qps > best_qps) {
            best_qps = results[i].qps;
            best = &results[i];
        }
    }
    std::cout << "SUMMARY protocol=" << protocol;
    if (best) {
        std::cout << " best_mode={" << modeLabel(best->mode) << "}"
                  << " best_qps=" << std::fixed << std::setprecision(2) << best->qps;
    }
    if (baseline && best) {
        std::cout << " speedup_vs_baseline=" << (best->qps / baseline->qps);
    }
    std::cout << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    sylar::LoggerMgr::GetInstance()->getRoot()->setLevel(sylar::LogLevel::FATAL);
    sylar::LoggerMgr::GetInstance()->getLogger("system")->setLevel(sylar::LogLevel::FATAL);

    BenchConfig config;

    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <tcp|udp> <pool:0|1> <shared:0|1> <use_caller:0|1>" << std::endl;
        return 1;
    }

    std::string protocol = argv[1];
    Mode mode;
    mode.pool_enabled = std::atoi(argv[2]) != 0;
    mode.shared_stack = std::atoi(argv[3]) != 0;
    mode.use_caller = std::atoi(argv[4]) != 0;

    std::cout << "Server mode benchmark settings: server_threads=" << config.server_threads
              << " client_threads=" << config.client_threads
              << " tcp_messages_per_client=" << config.tcp_messages_per_client
              << " udp_messages_per_client=" << config.udp_messages_per_client
              << " payload_size=" << config.payload_size << std::endl;

    BenchResult result;
    if (protocol == "tcp") {
        int port = config.base_tcp_port + (mode.pool_enabled ? 4 : 0) + (mode.shared_stack ? 2 : 0) + (mode.use_caller ? 1 : 0);
        result = runTcpBench(mode, config, port);
    } else if (protocol == "udp") {
        int port = config.base_udp_port + (mode.pool_enabled ? 4 : 0) + (mode.shared_stack ? 2 : 0) + (mode.use_caller ? 1 : 0);
        result = runUdpBench(mode, config, port);
    } else {
        std::cerr << "Invalid protocol: " << protocol << std::endl;
        return 1;
    }

    printResult(result);
    return result.success ? 0 : 2;
}
