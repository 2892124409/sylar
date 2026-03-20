#include "sylar/fiber/iomanager.h"
#include "sylar/base/util.h"
#include "sylar/log/logger.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    uint64_t nowUs()
    {
        return sylar::GetCurrentUS();
    }

    const char *schemeName()
    {
        return "baseline";
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

    struct BenchResult
    {
        std::string scenario;
        int threads = 0;
        int runId = 0;
        uint64_t ops = 0;
        uint64_t totalUs = 0;
        std::vector<uint64_t> latUs;
    };

    void printResult(const BenchResult &r)
    {
        std::vector<uint64_t> sorted = r.latUs;
        std::sort(sorted.begin(), sorted.end());
        uint64_t p50 = percentile(sorted, 50.0);
        uint64_t p95 = percentile(sorted, 95.0);
        uint64_t p99 = percentile(sorted, 99.0);
        uint64_t maxv = sorted.empty() ? 0 : sorted.back();
        double totalMs = static_cast<double>(r.totalUs) / 1000.0;
        double opsPerSec = r.totalUs == 0 ? 0.0
                                          : static_cast<double>(r.ops) * 1000000.0 / static_cast<double>(r.totalUs);

        std::cout << schemeName() << ","
                  << r.scenario << ","
                  << r.threads << ","
                  << r.runId << ","
                  << r.ops << ","
                  << std::fixed << std::setprecision(3) << totalMs << ","
                  << std::fixed << std::setprecision(3) << opsPerSec << ","
                  << p50 << ","
                  << p95 << ","
                  << p99 << ","
                  << maxv << "\n";
        std::cout.flush();
    }

    BenchResult benchCreateDestroy(int threads, int runId, size_t fiberCount)
    {
        sylar::IOManager iom(threads, true, "bench_create_destroy");
        std::atomic<size_t> done(0);
        std::vector<uint64_t> latency(fiberCount, 0);

        uint64_t t0 = nowUs();
        for (size_t i = 0; i < fiberCount; ++i)
        {
            uint64_t enqTs = nowUs();
            iom.schedule([&done, &latency, i, enqTs]()
                         {
                latency[i] = nowUs() - enqTs;
                done.fetch_add(1, std::memory_order_relaxed); });
        }
        iom.stop();
        uint64_t t1 = nowUs();

        if (done.load(std::memory_order_relaxed) != fiberCount)
        {
            std::cerr << "benchCreateDestroy lost tasks: done="
                      << done.load(std::memory_order_relaxed)
                      << " expected=" << fiberCount << std::endl;
        }

        BenchResult r;
        r.scenario = "create_destroy";
        r.threads = threads;
        r.runId = runId;
        r.ops = fiberCount;
        r.totalUs = t1 - t0;
        r.latUs.swap(latency);
        return r;
    }

    BenchResult benchYieldSwitch(int threads, int runId, size_t fiberCount, size_t yieldsPerFiber)
    {
        sylar::IOManager iom(threads, true, "bench_yield_switch");
        std::atomic<size_t> done(0);
        std::atomic<uint64_t> switches(0);
        std::vector<uint64_t> latency(fiberCount, 0);

        uint64_t t0 = nowUs();
        for (size_t i = 0; i < fiberCount; ++i)
        {
            iom.schedule([&done, &switches, &latency, i, yieldsPerFiber]()
                         {
                uint64_t begin = nowUs();
                for (size_t j = 0; j < yieldsPerFiber; ++j)
                {
                    switches.fetch_add(1, std::memory_order_relaxed);
                    sylar::Fiber::YieldToReady();
                }
                latency[i] = nowUs() - begin;
                done.fetch_add(1, std::memory_order_relaxed); });
        }
        iom.stop();
        uint64_t t1 = nowUs();

        if (done.load(std::memory_order_relaxed) != fiberCount)
        {
            std::cerr << "benchYieldSwitch lost tasks: done="
                      << done.load(std::memory_order_relaxed)
                      << " expected=" << fiberCount << std::endl;
        }

        BenchResult r;
        r.scenario = "yield_switch";
        r.threads = threads;
        r.runId = runId;
        r.ops = switches.load(std::memory_order_relaxed);
        r.totalUs = t1 - t0;
        r.latUs.swap(latency);
        return r;
    }

    BenchResult benchTimerDense(int threads, int runId, size_t fiberCount, uint64_t timeoutMs)
    {
        sylar::IOManager iom(threads, true, "bench_timer_dense");
        std::atomic<size_t> done(0);
        std::atomic<uint64_t> timerOps(0);
        std::vector<uint64_t> latency(fiberCount, 0);

        uint64_t t0 = nowUs();
        for (size_t i = 0; i < fiberCount; ++i)
        {
            iom.schedule([&done, &timerOps, &latency, i, timeoutMs, &iom]()
                         {
                uint64_t begin = nowUs();
                sylar::Fiber::ptr self = sylar::Fiber::GetThis();

                timerOps.fetch_add(1, std::memory_order_relaxed); // create
                iom.addTimer(timeoutMs, [&iom, self, &timerOps]()
                             {
                    timerOps.fetch_add(1, std::memory_order_relaxed); // fire
                    iom.schedule(self); });

                sylar::Fiber::YieldToHold();

                latency[i] = nowUs() - begin;
                done.fetch_add(1, std::memory_order_relaxed); });
        }
        iom.stop();
        uint64_t t1 = nowUs();

        if (done.load(std::memory_order_relaxed) != fiberCount)
        {
            std::cerr << "benchTimerDense lost tasks: done="
                      << done.load(std::memory_order_relaxed)
                      << " expected=" << fiberCount << std::endl;
        }

        BenchResult r;
        r.scenario = "timer_dense";
        r.threads = threads;
        r.runId = runId;
        r.ops = timerOps.load(std::memory_order_relaxed);
        r.totalUs = t1 - t0;
        r.latUs.swap(latency);
        return r;
    }
} // namespace

int main()
{
    SYLAR_LOG_ROOT()->setLevel(sylar::LogLevel::ERROR);

    const std::vector<int> threadSet = {1, 2, 4, 8};
    const int repeat = 3;

    const size_t createFiberCount = 10000;
    const size_t yieldFiberCount = 200;
    const size_t yieldsPerFiber = 10;
    const size_t timerFiberCount = 1000;
    const uint64_t timerTimeoutMs = 1;

    std::cout << "scheme,scenario,threads,run_id,ops,total_ms,ops_per_sec,p50_us,p95_us,p99_us,max_us\n";
    std::cout.flush();

    for (int threads : threadSet)
    {
        for (int runId = 1; runId <= repeat; ++runId)
        {
            printResult(benchCreateDestroy(threads, runId, createFiberCount));
            printResult(benchYieldSwitch(threads, runId, yieldFiberCount, yieldsPerFiber));
            printResult(benchTimerDense(threads, runId, timerFiberCount, timerTimeoutMs));
        }
    }

    return 0;
}
