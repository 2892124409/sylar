/**
 * @file scheduler.h
 * @brief 协程调度器封装
 */
#ifndef __SYLAR_SCHEDULER_H__
#define __SYLAR_SCHEDULER_H__

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>
#include "sylar/concurrency/mutex/mutex.h"
#include "sylar/concurrency/mutex/spinlock.h"
#include "sylar/concurrency/thread.h"
#include "sylar/fiber/fiber.h"

namespace sylar
{

    class Scheduler
    {
    public:
        typedef std::shared_ptr<Scheduler> ptr;
        static const size_t kInvalidWorker = static_cast<size_t>(-1);

        struct WorkerStats
        {
            int threadId = -1;
            uint32_t queuedTasks = 0;
            bool sleeping = false;
        };

        Scheduler(size_t threads = 1, bool use_caller = true, const std::string &name = "");
        virtual ~Scheduler();

        const std::string &getName() const { return m_name; }
        size_t getWorkerCount() const { return m_workerCount; }

        static Scheduler *GetThis();
        static Fiber *GetMainFiber();

        void start();
        void runCaller();
        void stop();
        bool isCallerActive() const { return m_callerActive.load(std::memory_order_acquire); }
        std::vector<WorkerStats> getWorkerStatsSnapshot() const;

        template <class FiberOrCb>
        void schedule(FiberOrCb fc, int thread = -1)
        {
            FiberAndThread task(fc, thread);
            if (!task.fiber && !task.cb)
            {
                return;
            }
            scheduleTask(std::move(task));
        }

        template <class InputIterator>
        void schedule(InputIterator begin, InputIterator end)
        {
            while (begin != end)
            {
                schedule(&*begin, -1);
                ++begin;
            }
        }

    protected:
        struct FiberAndThread
        {
            Fiber::ptr fiber;
            std::function<void()> cb;
            int thread;

            FiberAndThread()
                : thread(-1)
            {
            }

            FiberAndThread(Fiber::ptr f, int thr)
                : fiber(f), thread(thr)
            {
            }

            FiberAndThread(Fiber::ptr *f, int thr)
                : thread(thr)
            {
                fiber.swap(*f);
            }

            FiberAndThread(std::function<void()> f, int thr)
                : cb(std::move(f)), thread(thr)
            {
            }

            FiberAndThread(std::function<void()> *f, int thr)
                : thread(thr)
            {
                cb.swap(*f);
            }

            void reset()
            {
                fiber.reset();
                cb = nullptr;
                thread = -1;
            }
        };

        virtual void tickle(size_t worker);
        virtual void tickleAll();
        virtual bool stopping();
        virtual void idle();

        void setThis();
        bool hasIdleThreads() const { return m_idleThreadCount.load(std::memory_order_acquire) > 0; }
        size_t getCurrentWorkerIndex() const;
        int getWorkerThreadId(size_t worker) const;

    private:
        struct RemoteTaskNode
        {
            FiberAndThread task;
            RemoteTaskNode *next = nullptr;

            explicit RemoteTaskNode(FiberAndThread &&val)
                : task(std::move(val))
            {
            }
        };

        struct Worker
        {
            typedef Spinlock QueueMutexType;
            struct MailboxRingSlot
            {
                std::atomic<size_t> seq;
                FiberAndThread task;

                MailboxRingSlot()
                    : seq(0)
                {
                }
            };

            size_t index = 0;
            int threadId = -1;
            bool isCaller = false;
            Thread::ptr thread;
            QueueMutexType queueMutex;
            std::deque<FiberAndThread> localQueue;
            size_t mailboxRingSize = 0;
            size_t mailboxRingMask = 0;
            std::unique_ptr<MailboxRingSlot[]> mailboxRing;
            std::atomic<size_t> mailboxEnqueuePos;
            std::atomic<size_t> mailboxDequeuePos;
            std::atomic<RemoteTaskNode *> mailboxFallback;
            std::atomic<uint32_t> queuedTasks;
            std::atomic<bool> sleeping;

            explicit Worker(size_t ring_size)
                : mailboxRingSize(ring_size),
                  mailboxRingMask(ring_size - 1),
                  mailboxRing(new MailboxRingSlot[ring_size]),
                  mailboxEnqueuePos(0),
                  mailboxDequeuePos(0),
                  mailboxFallback(nullptr),
                  queuedTasks(0),
                  sleeping(false)
            {
                for (size_t i = 0; i < mailboxRingSize; ++i)
                {
                    mailboxRing[i].seq.store(i, std::memory_order_relaxed);
                }
            }
        };

        void run(size_t worker_index);
        void scheduleTask(FiberAndThread task);
        size_t selectWorker(int thread) const;
        void enqueueStartupTask(FiberAndThread task);
        void flushStartupTasks();
        void enqueueLocal(Worker &worker, FiberAndThread task);
        bool tryEnqueueMailboxRing(Worker &worker, FiberAndThread &task);
        void enqueueMailboxFallback(Worker &worker, FiberAndThread task);
        void drainMailboxRing(Worker &worker, size_t max_batch = 64);
        void drainMailboxFallback(Worker &worker);
        bool hasMailboxWork(const Worker &worker) const;
        bool popTask(Worker &worker, FiberAndThread &task);

    private:
        static constexpr size_t kMailboxRingSize = 1024;
        static constexpr size_t kMailboxDrainBatch = 64;
        std::string m_name;
        Mutex m_startupMutex;
        std::vector<FiberAndThread> m_startupTasks;
        std::vector<std::unique_ptr<Worker>> m_workers;
        std::vector<int> m_threadIds;
        Fiber::ptr m_rootFiber;
        mutable std::atomic<size_t> m_workerCursor;
        std::atomic<size_t> m_pendingTaskCount;
        std::atomic<size_t> m_activeThreadCount;
        std::atomic<size_t> m_idleThreadCount;
        size_t m_workerCount = 0;
        size_t m_threadCount = 0;
        bool m_useCaller = false;
        bool m_started = false;
        bool m_stopping = false;
        bool m_autoStop = false;
        std::atomic<bool> m_callerActive = {false};
        int m_rootThread = -1;
    };

}

#endif
