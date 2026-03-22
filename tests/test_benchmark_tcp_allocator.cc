#include "sylar/fiber/iomanager.h"
#include "sylar/base/util.h"
#include "sylar/log/logger.h"
#include "sylar/net/socket.h"
#include "sylar/net/address.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    uint64_t nowUs()
    {
        return sylar::GetCurrentUS();
    }

    bool debugEnabled()
    {
        static bool enabled = (std::getenv("BENCH_DEBUG") != nullptr);
        return enabled;
    }

    void debugLog(const std::string &msg)
    {
        if (debugEnabled())
        {
            std::cerr << "[bench_debug] " << msg << std::endl;
        }
    }

    uint64_t percentile(const std::vector<uint64_t> &sorted, double p)
    {
        if (sorted.empty())
        {
            return 0;
        }
        size_t idx = static_cast<size_t>((p / 100.0) * static_cast<double>(sorted.size() - 1));
        return sorted[idx];
    }

    std::vector<int> parseThreadSet(const std::string &csv)
    {
        std::vector<int> out;
        std::stringstream ss(csv);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            if (item.empty())
            {
                continue;
            }
            int v = std::atoi(item.c_str());
            if (v > 0)
            {
                out.push_back(v);
            }
        }
        return out;
    }

    std::string getArgValue(const std::string &arg, const std::string &key)
    {
        const std::string prefix = "--" + key + "=";
        if (arg.find(prefix) == 0)
        {
            return arg.substr(prefix.size());
        }
        return "";
    }

    bool sendAll(const sylar::Socket::ptr &sock, const char *data, size_t len)
    {
        size_t off = 0;
        while (off < len)
        {
            int rt = sock->send(data + off, len - off, 0);
            if (rt <= 0)
            {
                return false;
            }
            off += static_cast<size_t>(rt);
        }
        return true;
    }

    bool recvAll(const sylar::Socket::ptr &sock, char *data, size_t len)
    {
        size_t off = 0;
        while (off < len)
        {
            int rt = sock->recv(data + off, len - off, 0);
            if (rt <= 0)
            {
                return false;
            }
            off += static_cast<size_t>(rt);
        }
        return true;
    }

    struct Options
    {
        std::string allocatorLabel = "baseline";
        std::string mode = "all";          // a/b/all (兼容 on/off)
        std::string workload = "all";      // persistent/short/all
        std::vector<int> threadSet = {2, 4, 8}; // io_threads 集合
        int repeat = 3;
        int runIdBase = 1;
        size_t payloadBytes = 256;
        int connections = 64;
        int requestsPerConn = 200;
        int shortTotalRequests = 10000;
        int startupDelayMs = 80;
    };

    struct BenchResult
    {
        std::string allocator;
        std::string mode;
        std::string workload;
        int ioThreads = 0;
        int runId = 0;
        uint64_t totalRequests = 0;
        uint64_t totalUs = 0;
        std::vector<uint64_t> latUs;
        uint64_t errors = 0;
        std::string status = "ok";
        int exitCode = 0;
    };

    uint64_t expectedTotalRequests(const Options &opt, const std::string &workload)
    {
        return (workload == "persistent")
                   ? static_cast<uint64_t>(opt.connections) * static_cast<uint64_t>(opt.requestsPerConn)
                   : static_cast<uint64_t>(opt.shortTotalRequests);
    }

    BenchResult initBenchResult(const Options &opt,
                                const std::string &mode,
                                const std::string &workload,
                                int ioThreads,
                                int runId)
    {
        BenchResult r;
        r.allocator = opt.allocatorLabel;
        r.mode = mode;
        r.workload = workload;
        r.ioThreads = ioThreads;
        r.runId = runId;
        r.totalRequests = expectedTotalRequests(opt, workload);
        return r;
    }

    class EchoBenchServer : public std::enable_shared_from_this<EchoBenchServer>
    {
    public:
        typedef std::shared_ptr<EchoBenchServer> ptr;

        EchoBenchServer(sylar::IOManager *ioWorker,
                        sylar::IOManager *acceptWorker,
                        size_t payloadBytes,
                        bool singleShotPerConn)
            : m_ioWorker(ioWorker),
              m_acceptWorker(acceptWorker),
              m_payloadBytes(payloadBytes),
              m_singleShotPerConn(singleShotPerConn)
        {
        }

        bool start()
        {
            if (!m_ioWorker || !m_acceptWorker)
            {
                debugLog("EchoBenchServer start failed: null io/accept worker");
                return false;
            }
            if (m_ioWorker == m_acceptWorker)
            {
                debugLog("EchoBenchServer start failed: accept_worker and io_worker must be different");
                return false;
            }

            const uint64_t seed = nowUs() ^ static_cast<uint64_t>(sylar::GetThreadId());
            const int kBasePort = 20000;
            const int kPortSpan = 30000;
            const int kTryCount = 256;

            bool ok = false;
            for (int i = 0; i < kTryCount; ++i)
            {
                int port = kBasePort + static_cast<int>((seed + static_cast<uint64_t>(i * 97)) % kPortSpan);
                std::string endpoint = "127.0.0.1:" + std::to_string(port);
                sylar::Address::ptr bindAddr = sylar::Address::LookupAny(endpoint);
                if (!bindAddr)
                {
                    continue;
                }

                sylar::Socket::ptr sock = sylar::Socket::CreateTCP(bindAddr);
                if (!sock)
                {
                    continue;
                }
                if (!sock->bind(bindAddr))
                {
                    sock->close();
                    continue;
                }
                if (!sock->listen())
                {
                    sock->close();
                    continue;
                }

                m_listenSock = sock;
                m_endpoint = endpoint;
                ok = true;
                break;
            }

            if (!ok)
            {
                return false;
            }

            m_running.store(true, std::memory_order_release);

            auto self = shared_from_this();
            m_acceptWorker->schedule([self]()
                                     { self->acceptLoop(); });
            return true;
        }

        void stop()
        {
            bool expected = true;
            if (!m_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
            {
                return;
            }

            // 用一次本地连接把阻塞中的 accept 唤醒出来，避免 stop 阶段悬挂。
            sylar::Address::ptr wakeAddr = sylar::Address::LookupAny(m_endpoint);
            if (wakeAddr)
            {
                sylar::Socket::ptr wakeSock = sylar::Socket::CreateTCP(wakeAddr);
                if (wakeSock)
                {
                    wakeSock->connect(wakeAddr, 100);
                    wakeSock->close();
                }
            }

            auto self = shared_from_this();
            m_acceptWorker->schedule([this, self]()
                                     {
                debugLog("EchoBenchServer stop cleanup begin");
                if (m_listenSock)
                {
                    m_listenSock->close();
                }
                debugLog("EchoBenchServer stop cleanup end"); });
        }

        const std::string &endpoint() const { return m_endpoint; }

        uint64_t handledRequests() const
        {
            return m_handledRequests.load(std::memory_order_relaxed);
        }

    private:
        void acceptLoop()
        {
            while (m_running.load(std::memory_order_acquire))
            {
                sylar::Socket::ptr client = m_listenSock->accept();
                if (!client)
                {
                    if (!m_running.load(std::memory_order_acquire))
                    {
                        break;
                    }
                    if (errno == EBADF || errno == EINVAL || errno == ENOTSOCK)
                    {
                        break;
                    }
                    continue;
                }
                // Short-connection benchmark prefers quick fail-fast to avoid long drain at stop.
                const int64_t io_timeout = m_singleShotPerConn ? 1000 : 5000;
                client->setRecvTimeout(io_timeout);
                client->setSendTimeout(io_timeout);
                if (!m_running.load(std::memory_order_acquire))
                {
                    client->close();
                    break;
                }
                auto self = shared_from_this();
                m_ioWorker->schedule([self, client]()
                                     { self->handleClient(client); });
            }
            debugLog("EchoBenchServer acceptLoop exit");
        }

        void handleClient(const sylar::Socket::ptr &client)
        {
            std::vector<char> buf(m_payloadBytes, 0);
            if (m_singleShotPerConn)
            {
                if (!recvAll(client, buf.data(), m_payloadBytes))
                {
                    client->close();
                    return;
                }
                if (!sendAll(client, buf.data(), m_payloadBytes))
                {
                    client->close();
                    return;
                }
                m_handledRequests.fetch_add(1, std::memory_order_relaxed);
                client->close();
                return;
            }

            while (true)
            {
                if (!recvAll(client, buf.data(), m_payloadBytes))
                {
                    break;
                }
                if (!sendAll(client, buf.data(), m_payloadBytes))
                {
                    break;
                }
                m_handledRequests.fetch_add(1, std::memory_order_relaxed);
            }
            client->close();
        }

    private:
        sylar::IOManager *m_ioWorker = nullptr;
        sylar::IOManager *m_acceptWorker = nullptr;
        sylar::Socket::ptr m_listenSock;
        size_t m_payloadBytes = 0;
        bool m_singleShotPerConn = false;
        std::string m_endpoint;
        std::atomic<bool> m_running = {false};
        std::atomic<uint64_t> m_handledRequests = {0};
    };

    uint64_t runPersistentClients(const std::string &endpoint,
                                  const Options &opt,
                                  std::vector<uint64_t> &latUs)
    {
        sylar::Address::ptr target = sylar::Address::LookupAny(endpoint);
        if (!target)
        {
            return static_cast<uint64_t>(opt.connections) * static_cast<uint64_t>(opt.requestsPerConn);
        }

        const uint64_t total = static_cast<uint64_t>(opt.connections) * static_cast<uint64_t>(opt.requestsPerConn);
        latUs.resize(static_cast<size_t>(total), 0);
        std::atomic<size_t> succCount(0);
        std::atomic<uint64_t> errCount(0);

        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(opt.connections));
        for (int i = 0; i < opt.connections; ++i)
        {
            workers.emplace_back([&]()
                                 {
                sylar::Socket::ptr sock = sylar::Socket::CreateTCP(target);
                if (!sock || !sock->connect(target, 2000))
                {
                    errCount.fetch_add(static_cast<uint64_t>(opt.requestsPerConn), std::memory_order_relaxed);
                    return;
                }
                sock->setRecvTimeout(2000);
                sock->setSendTimeout(2000);

                std::vector<char> req(opt.payloadBytes, 'x');
                std::vector<char> resp(opt.payloadBytes, 0);

                for (int r = 0; r < opt.requestsPerConn; ++r)
                {
                    uint64_t t0 = nowUs();
                    if (!sendAll(sock, req.data(), req.size()) || !recvAll(sock, resp.data(), resp.size()))
                    {
                        errCount.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    size_t idx = succCount.fetch_add(1, std::memory_order_relaxed);
                    if (idx < latUs.size())
                    {
                        latUs[idx] = nowUs() - t0;
                    }
                }
                sock->close(); });
        }

        for (size_t i = 0; i < workers.size(); ++i)
        {
            workers[i].join();
        }

        latUs.resize(succCount.load(std::memory_order_relaxed));
        return errCount.load(std::memory_order_relaxed);
    }

    uint64_t runShortConnectionClients(const std::string &endpoint,
                                       const Options &opt,
                                       std::vector<uint64_t> &latUs)
    {
        sylar::Address::ptr target = sylar::Address::LookupAny(endpoint);
        if (!target)
        {
            return static_cast<uint64_t>(opt.shortTotalRequests);
        }

        const uint64_t total = static_cast<uint64_t>(opt.shortTotalRequests);
        latUs.resize(static_cast<size_t>(total), 0);
        std::atomic<uint64_t> nextReq(0);
        std::atomic<size_t> succCount(0);
        std::atomic<uint64_t> errCount(0);

        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(opt.connections));
        for (int i = 0; i < opt.connections; ++i)
        {
            workers.emplace_back([&]()
                                 {
                std::vector<char> req(opt.payloadBytes, 'y');
                std::vector<char> resp(opt.payloadBytes, 0);
                while (true)
                {
                    uint64_t id = nextReq.fetch_add(1, std::memory_order_relaxed);
                    if (id >= total)
                    {
                        break;
                    }

                    sylar::Socket::ptr sock = sylar::Socket::CreateTCP(target);
                    uint64_t t0 = nowUs();
                    if (!sock || !sock->connect(target, 2000))
                    {
                        errCount.fetch_add(1, std::memory_order_relaxed);
                        if (sock)
                        {
                            sock->close();
                        }
                        continue;
                    }
                    sock->setRecvTimeout(2000);
                    sock->setSendTimeout(2000);
                    if (!sendAll(sock, req.data(), req.size()) || !recvAll(sock, resp.data(), resp.size()))
                    {
                        errCount.fetch_add(1, std::memory_order_relaxed);
                        sock->close();
                        continue;
                    }
                    size_t idx = succCount.fetch_add(1, std::memory_order_relaxed);
                    if (idx < latUs.size())
                    {
                        latUs[idx] = nowUs() - t0;
                    }
                    sock->close();
                } });
        }

        for (size_t i = 0; i < workers.size(); ++i)
        {
            workers[i].join();
        }

        latUs.resize(succCount.load(std::memory_order_relaxed));
        return errCount.load(std::memory_order_relaxed);
    }

    uint64_t runClientsForWorkload(const std::string &workload,
                                   const std::string &endpoint,
                                   const Options &opt,
                                   std::vector<uint64_t> &latUs)
    {
        if (workload == "persistent")
        {
            return runPersistentClients(endpoint, opt, latUs);
        }
        return runShortConnectionClients(endpoint, opt, latUs);
    }

    BenchResult runModeA(const Options &opt, const std::string &workload, int ioThreads, int runId)
    {
        debugLog("runModeA begin workload=" + workload + " io_threads=" + std::to_string(ioThreads));
        BenchResult r = initBenchResult(opt, "a", workload, ioThreads, runId);
        sylar::IOManager ioIom(static_cast<size_t>(ioThreads), false, "bench_a_io");
        sylar::IOManager acceptIom(1, true, "bench_a_accept");
        EchoBenchServer::ptr server(new EchoBenchServer(&ioIom, &acceptIom, opt.payloadBytes, workload == "short"));
        if (!server->start())
        {
            debugLog("runModeA server start failed");
            r.errors = r.totalRequests;
            r.status = "start_fail";
            r.exitCode = 2;
            return r;
        }
        std::string endpoint = server->endpoint();
        debugLog("runModeA endpoint=" + endpoint);

        std::atomic<uint64_t> errCount(0);
        std::atomic<uint64_t> workUs(0);
        std::thread clientThread([&]()
                                 {
            std::this_thread::sleep_for(std::chrono::milliseconds(opt.startupDelayMs));
            debugLog("runModeA client begin");
            uint64_t w0 = nowUs();
            uint64_t err = runClientsForWorkload(workload, endpoint, opt, r.latUs);
            uint64_t w1 = nowUs();
            workUs.store(w1 - w0, std::memory_order_relaxed);
            errCount.store(err, std::memory_order_relaxed);
            debugLog("runModeA client done err=" + std::to_string(err) + " succ_lat=" + std::to_string(r.latUs.size()));
            acceptIom.schedule([server, &acceptIom]()
                               {
                server->stop();
                acceptIom.stop(); });
        });

        debugLog("runModeA acceptIom.runCaller enter");
        acceptIom.runCaller();
        debugLog("runModeA acceptIom.runCaller leave");

        clientThread.join();
        debugLog("runModeA ioIom.stop enter");
        ioIom.stop();
        debugLog("runModeA ioIom.stop leave");
        r.totalUs = workUs.load(std::memory_order_relaxed);
        r.errors = errCount.load(std::memory_order_relaxed);
        return r;
    }

    BenchResult runModeB(const Options &opt, const std::string &workload, int ioThreads, int runId)
    {
        debugLog("runModeB begin workload=" + workload + " io_threads=" + std::to_string(ioThreads));
        BenchResult r = initBenchResult(opt, "b", workload, ioThreads, runId);
        sylar::IOManager ioIom(static_cast<size_t>(ioThreads), false, "bench_b_io");
        sylar::IOManager acceptIom(1, false, "bench_b_accept");

        EchoBenchServer::ptr server(new EchoBenchServer(&ioIom, &acceptIom, opt.payloadBytes, workload == "short"));
        if (!server->start())
        {
            debugLog("runModeB server start failed");
            r.errors = r.totalRequests;
            r.status = "start_fail";
            r.exitCode = 2;
            return r;
        }
        std::string endpoint = server->endpoint();
        debugLog("runModeB endpoint=" + endpoint);

        std::atomic<uint64_t> errCount(0);
        std::atomic<uint64_t> workUs(0);
        std::thread clientThread([&]()
                                 {
            std::this_thread::sleep_for(std::chrono::milliseconds(opt.startupDelayMs));
            debugLog("runModeB client begin");
            uint64_t w0 = nowUs();
            uint64_t err = runClientsForWorkload(workload, endpoint, opt, r.latUs);
            uint64_t w1 = nowUs();
            workUs.store(w1 - w0, std::memory_order_relaxed);
            errCount.store(err, std::memory_order_relaxed);
            debugLog("runModeB client done err=" + std::to_string(err) + " succ_lat=" + std::to_string(r.latUs.size())); });

        clientThread.join();
        debugLog("runModeB stopping");
        server->stop();
        acceptIom.stop();
        ioIom.stop();
        debugLog("runModeB stopped");

        r.totalUs = workUs.load(std::memory_order_relaxed);
        r.errors = errCount.load(std::memory_order_relaxed);
        return r;
    }

    void printResult(const BenchResult &r)
    {
        std::vector<uint64_t> sorted = r.latUs;
        std::sort(sorted.begin(), sorted.end());
        uint64_t p50 = percentile(sorted, 50.0);
        uint64_t p95 = percentile(sorted, 95.0);
        uint64_t p99 = percentile(sorted, 99.0);
        uint64_t maxv = sorted.empty() ? 0 : sorted.back();

        double totalMs = static_cast<double>(r.totalUs) / 1000.0;
        double qps = (r.totalUs == 0) ? 0.0
                                      : static_cast<double>(r.totalRequests) * 1000000.0 / static_cast<double>(r.totalUs);

        std::cout << r.allocator << ","
                  << r.mode << ","
                  << r.workload << ","
                  << r.ioThreads << ","
                  << r.runId << ","
                  << r.totalRequests << ","
                  << std::fixed << std::setprecision(3) << totalMs << ","
                  << std::fixed << std::setprecision(3) << qps << ","
                  << p50 << ","
                  << p95 << ","
                  << p99 << ","
                  << maxv << ","
                  << r.errors << ","
                  << r.status << ","
                  << r.exitCode << "\n";
    }

    bool parseOptions(int argc, char **argv, Options &opt)
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string arg(argv[i]);
            std::string v;

            v = getArgValue(arg, "allocator_label");
            if (!v.empty())
            {
                opt.allocatorLabel = v;
                continue;
            }

            v = getArgValue(arg, "mode");
            if (!v.empty())
            {
                opt.mode = v;
                continue;
            }

            v = getArgValue(arg, "workload");
            if (!v.empty())
            {
                opt.workload = v;
                continue;
            }

            v = getArgValue(arg, "threads");
            if (!v.empty())
            {
                opt.threadSet = parseThreadSet(v);
                continue;
            }

            v = getArgValue(arg, "io_threads");
            if (!v.empty())
            {
                opt.threadSet = parseThreadSet(v);
                continue;
            }

            v = getArgValue(arg, "repeat");
            if (!v.empty())
            {
                opt.repeat = std::atoi(v.c_str());
                continue;
            }

            v = getArgValue(arg, "run_id");
            if (!v.empty())
            {
                opt.runIdBase = std::atoi(v.c_str());
                continue;
            }

            v = getArgValue(arg, "payload_bytes");
            if (!v.empty())
            {
                opt.payloadBytes = static_cast<size_t>(std::atoi(v.c_str()));
                continue;
            }

            v = getArgValue(arg, "connections");
            if (!v.empty())
            {
                opt.connections = std::atoi(v.c_str());
                continue;
            }

            v = getArgValue(arg, "requests_per_conn");
            if (!v.empty())
            {
                opt.requestsPerConn = std::atoi(v.c_str());
                continue;
            }

            v = getArgValue(arg, "short_total_requests");
            if (!v.empty())
            {
                opt.shortTotalRequests = std::atoi(v.c_str());
                continue;
            }

            v = getArgValue(arg, "startup_delay_ms");
            if (!v.empty())
            {
                opt.startupDelayMs = std::atoi(v.c_str());
                continue;
            }

            std::cerr << "unknown arg: " << arg << std::endl;
            return false;
        }

        if (opt.mode == "on")
        {
            opt.mode = "a";
        }
        else if (opt.mode == "off")
        {
            opt.mode = "b";
        }

        if (opt.mode != "a" && opt.mode != "b" && opt.mode != "all")
        {
            std::cerr << "invalid mode: " << opt.mode << std::endl;
            return false;
        }
        if (opt.workload != "persistent" && opt.workload != "short" && opt.workload != "all")
        {
            std::cerr << "invalid workload: " << opt.workload << std::endl;
            return false;
        }
        if (opt.threadSet.empty())
        {
            std::cerr << "threads set is empty" << std::endl;
            return false;
        }
        if (opt.repeat <= 0 || opt.runIdBase <= 0 || opt.payloadBytes == 0 || opt.connections <= 0 || opt.requestsPerConn <= 0 || opt.shortTotalRequests <= 0)
        {
            std::cerr << "invalid numeric options" << std::endl;
            return false;
        }
        return true;
    }
} // namespace

int main(int argc, char **argv)
{
    SYLAR_LOG_ROOT()->setLevel(sylar::LogLevel::FATAL);

    Options opt;
    if (!parseOptions(argc, argv, opt))
    {
        std::cerr << "Usage:\n"
                  << "  --allocator_label=baseline|jemalloc|tcmalloc\n"
                  << "  --mode=a|b|all (兼容 on/off)\n"
                  << "  --workload=persistent|short|all\n"
                  << "  --io_threads=2,4,8 (兼容 --threads)\n"
                  << "  --repeat=3\n"
                  << "  --run_id=1\n"
                  << "  --payload_bytes=256\n"
                  << "  --connections=64\n"
                  << "  --requests_per_conn=200\n"
                  << "  --short_total_requests=10000\n"
                  << "  --startup_delay_ms=80\n";
        return 1;
    }

    std::vector<std::string> modes;
    if (opt.mode == "all")
    {
        modes.push_back("a");
        modes.push_back("b");
    }
    else
    {
        modes.push_back(opt.mode);
    }

    std::vector<std::string> workloads;
    if (opt.workload == "all")
    {
        workloads.push_back("persistent");
        workloads.push_back("short");
    }
    else
    {
        workloads.push_back(opt.workload);
    }

    std::cout << "allocator,mode,workload,io_threads,run_id,total_requests,total_ms,req_per_sec,p50_us,p95_us,p99_us,max_us,errors,status,exit_code\n";
    for (size_t mi = 0; mi < modes.size(); ++mi)
    {
        for (size_t wi = 0; wi < workloads.size(); ++wi)
        {
            for (size_t ti = 0; ti < opt.threadSet.size(); ++ti)
            {
                int ioThreads = opt.threadSet[ti];
                if (ioThreads < 1)
                {
                    std::cerr << "skip invalid io_threads=" << ioThreads << " (must >=1)" << std::endl;
                    continue;
                }
                for (int runOffset = 0; runOffset < opt.repeat; ++runOffset)
                {
                    int runId = opt.runIdBase + runOffset;
                    BenchResult r = initBenchResult(opt, modes[mi], workloads[wi], ioThreads, runId);
                    try
                    {
                        if (modes[mi] == "a")
                        {
                            r = runModeA(opt, workloads[wi], ioThreads, runId);
                        }
                        else
                        {
                            r = runModeB(opt, workloads[wi], ioThreads, runId);
                        }
                    }
                    catch (const std::exception &ex)
                    {
                        r.errors = r.totalRequests;
                        r.status = "exception";
                        r.exitCode = 3;
                        std::cerr << "benchmark exception: " << ex.what() << std::endl;
                    }
                    catch (...)
                    {
                        r.errors = r.totalRequests;
                        r.status = "exception";
                        r.exitCode = 3;
                        std::cerr << "benchmark unknown exception" << std::endl;
                    }
                    printResult(r);
                    std::cout.flush();
                }
            }
        }
    }

    return 0;
}
