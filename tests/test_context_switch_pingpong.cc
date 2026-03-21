#include "sylar/fiber/context.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    struct Options
    {
        uint64_t iterations = 10000000ULL;
        int repeat = 5;
        int warmup = 1;
        size_t stackSize = 128 * 1024;
        bool measureBaseline = true;
        bool showHelp = false;
    };

    struct Sample
    {
        uint64_t pingPongNs = 0;
        uint64_t baselineNs = 0;
        double nsPerSwitch = 0.0;
        double nsPerSwitchNet = 0.0;
    };

    struct BenchState
    {
        uint64_t iterations = 0;
        sylar::fiber_context::Context mainCtx;
        sylar::fiber_context::Context fiberACtx;
        sylar::fiber_context::Context fiberBCtx;
        void *stackA = nullptr;
        void *stackB = nullptr;
    };

    thread_local BenchState *t_state = nullptr;

    bool startsWith(const std::string &s, const char *prefix)
    {
        const size_t n = std::strlen(prefix);
        return s.size() >= n && s.compare(0, n, prefix) == 0;
    }

    bool parseUint64(const std::string &text, uint64_t &out)
    {
        if (text.empty())
        {
            return false;
        }
        errno = 0;
        char *end = nullptr;
        unsigned long long v = std::strtoull(text.c_str(), &end, 10);
        if (errno != 0 || end == text.c_str() || *end != '\0')
        {
            return false;
        }
        out = static_cast<uint64_t>(v);
        return true;
    }

    bool parseInt(const std::string &text, int &out)
    {
        if (text.empty())
        {
            return false;
        }
        errno = 0;
        char *end = nullptr;
        long v = std::strtol(text.c_str(), &end, 10);
        if (errno != 0 || end == text.c_str() || *end != '\0')
        {
            return false;
        }
        if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
        {
            return false;
        }
        out = static_cast<int>(v);
        return true;
    }

    void printUsage(const char *prog)
    {
        std::cout << "Usage: " << prog << " [options]\n"
                  << "  --iterations=<N>    ping-pong rounds per fiber (default: 10000000)\n"
                  << "  --repeat=<N>        measured runs (default: 5)\n"
                  << "  --warmup=<N>        warmup runs (default: 1)\n"
                  << "  --stack-size=<B>    stack bytes per fiber (default: 131072)\n"
                  << "  --no-baseline       disable empty-loop baseline\n"
                  << "  --help              show this help\n";
    }

    bool parseArgs(int argc, char **argv, Options &opt)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg(argv[i]);
            if (arg == "--help")
            {
                opt.showHelp = true;
                return true;
            }
            if (arg == "--no-baseline")
            {
                opt.measureBaseline = false;
                continue;
            }
            if (startsWith(arg, "--iterations="))
            {
                uint64_t v = 0;
                if (!parseUint64(arg.substr(std::strlen("--iterations=")), v))
                {
                    return false;
                }
                opt.iterations = v;
                continue;
            }
            if (startsWith(arg, "--repeat="))
            {
                int v = 0;
                if (!parseInt(arg.substr(std::strlen("--repeat=")), v))
                {
                    return false;
                }
                opt.repeat = v;
                continue;
            }
            if (startsWith(arg, "--warmup="))
            {
                int v = 0;
                if (!parseInt(arg.substr(std::strlen("--warmup=")), v))
                {
                    return false;
                }
                opt.warmup = v;
                continue;
            }
            if (startsWith(arg, "--stack-size="))
            {
                uint64_t v = 0;
                if (!parseUint64(arg.substr(std::strlen("--stack-size=")), v))
                {
                    return false;
                }
                if (v > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
                {
                    return false;
                }
                opt.stackSize = static_cast<size_t>(v);
                continue;
            }
            return false;
        }

        if (opt.iterations == 0)
        {
            return false;
        }
        if (opt.iterations > std::numeric_limits<uint64_t>::max() / 2ULL)
        {
            return false;
        }
        if (opt.repeat <= 0 || opt.warmup < 0)
        {
            return false;
        }
        if (opt.stackSize < 16)
        {
            return false;
        }
        return true;
    }

    void fiberAEntry()
    {
        BenchState &state = *t_state;
        for (uint64_t i = 0; i < state.iterations; ++i)
        {
            sylar::fiber_context::SwapContext(state.fiberACtx, state.fiberBCtx);
        }
        sylar::fiber_context::SwapContext(state.fiberACtx, state.mainCtx);
        std::abort();
    }

    void fiberBEntry()
    {
        BenchState &state = *t_state;
        for (uint64_t i = 0; i < state.iterations; ++i)
        {
            sylar::fiber_context::SwapContext(state.fiberBCtx, state.fiberACtx);
        }
        sylar::fiber_context::SwapContext(state.fiberBCtx, state.mainCtx);
        std::abort();
    }

    uint64_t runPingPong(uint64_t iterations, size_t stackSize)
    {
        BenchState state;
        state.iterations = iterations;
        state.stackA = std::malloc(stackSize);
        state.stackB = std::malloc(stackSize);
        if (!state.stackA || !state.stackB)
        {
            std::free(state.stackA);
            std::free(state.stackB);
            throw std::runtime_error("malloc failed for benchmark fiber stack");
        }

        t_state = &state;
        sylar::fiber_context::InitMainContext(state.mainCtx);
        sylar::fiber_context::InitChildContext(state.fiberACtx, state.stackA, stackSize, &fiberAEntry);
        sylar::fiber_context::InitChildContext(state.fiberBCtx, state.stackB, stackSize, &fiberBEntry);

        auto begin = std::chrono::steady_clock::now();
        sylar::fiber_context::SwapContext(state.mainCtx, state.fiberACtx);
        auto end = std::chrono::steady_clock::now();

        t_state = nullptr;
        std::free(state.stackA);
        std::free(state.stackB);
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
    }

    uint64_t runBaselineLoop(uint64_t totalSwitches)
    {
        auto begin = std::chrono::steady_clock::now();
        for (uint64_t i = 0; i < totalSwitches; ++i)
        {
            asm volatile("" ::: "memory");
        }
        auto end = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
    }

    Sample runOne(uint64_t iterations, uint64_t totalSwitches, size_t stackSize, bool measureBaseline)
    {
        Sample sample;
        sample.pingPongNs = runPingPong(iterations, stackSize);
        if (measureBaseline)
        {
            sample.baselineNs = runBaselineLoop(totalSwitches);
        }

        sample.nsPerSwitch = static_cast<double>(sample.pingPongNs) /
                             static_cast<double>(totalSwitches);
        sample.nsPerSwitchNet = sample.nsPerSwitch;
        if (measureBaseline)
        {
            uint64_t net = sample.pingPongNs > sample.baselineNs
                               ? (sample.pingPongNs - sample.baselineNs)
                               : 0ULL;
            sample.nsPerSwitchNet = static_cast<double>(net) /
                                    static_cast<double>(totalSwitches);
        }
        return sample;
    }
} // namespace

int main(int argc, char **argv)
{
    Options options;
    if (!parseArgs(argc, argv, options))
    {
        printUsage(argv[0]);
        return 1;
    }
    if (options.showHelp)
    {
        printUsage(argv[0]);
        return 0;
    }

    const uint64_t totalSwitches = options.iterations * 2ULL;

    for (int i = 0; i < options.warmup; ++i)
    {
        runOne(options.iterations, totalSwitches, options.stackSize, options.measureBaseline);
    }

    std::vector<Sample> samples;
    samples.reserve(static_cast<size_t>(options.repeat));

    std::cout << "backend: " << sylar::fiber_context::BackendName() << "\n";
    std::cout << "iterations: " << options.iterations
              << ", total_switches: " << totalSwitches
              << ", repeat: " << options.repeat
              << ", stack_size: " << options.stackSize << " bytes\n";
    std::cout << "baseline: " << (options.measureBaseline ? "enabled" : "disabled") << "\n";

    double sumTotalMs = 0.0;
    double sumBaselineMs = 0.0;
    double sumNsPerSwitch = 0.0;
    double sumNsPerSwitchNet = 0.0;

    for (int runId = 1; runId <= options.repeat; ++runId)
    {
        Sample sample = runOne(options.iterations, totalSwitches, options.stackSize, options.measureBaseline);
        samples.push_back(sample);

        const double totalMs = static_cast<double>(sample.pingPongNs) / 1e6;
        const double baselineMs = static_cast<double>(sample.baselineNs) / 1e6;

        sumTotalMs += totalMs;
        sumBaselineMs += baselineMs;
        sumNsPerSwitch += sample.nsPerSwitch;
        sumNsPerSwitchNet += sample.nsPerSwitchNet;

        std::cout << std::fixed << std::setprecision(3)
                  << "run " << runId
                  << ": total time = " << totalMs << " ms, baseline = " << baselineMs
                  << " ms, ns per switch = " << sample.nsPerSwitch
                  << ", ns per switch(net) = " << sample.nsPerSwitchNet << "\n";
    }

    const double avgTotalMs = sumTotalMs / static_cast<double>(options.repeat);
    const double avgBaselineMs = sumBaselineMs / static_cast<double>(options.repeat);
    const double avgNsPerSwitch = sumNsPerSwitch / static_cast<double>(options.repeat);
    const double avgNsPerSwitchNet = sumNsPerSwitchNet / static_cast<double>(options.repeat);

    std::cout << "[" << sylar::fiber_context::BackendName() << "]\n";
    std::cout << std::fixed << std::setprecision(3)
              << "total time: " << avgTotalMs << " ms\n"
              << "ns per switch: " << avgNsPerSwitch << "\n";
    if (options.measureBaseline)
    {
        std::cout << "baseline loop: " << avgBaselineMs << " ms\n"
                  << "ns per switch(net): " << avgNsPerSwitchNet << "\n";
    }

    std::cout << std::fixed << std::setprecision(6)
              << "RESULT backend=" << sylar::fiber_context::BackendName()
              << " iterations=" << options.iterations
              << " repeat=" << options.repeat
              << " avg_total_ms=" << avgTotalMs
              << " avg_baseline_ms=" << avgBaselineMs
              << " avg_ns_per_switch=" << avgNsPerSwitch
              << " avg_ns_per_switch_net=" << avgNsPerSwitchNet
              << "\n";

    return 0;
}
