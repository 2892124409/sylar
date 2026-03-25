#include "sylar/fiber/scheduler.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include "sylar/base/macro.h"
#include "sylar/fiber/hook.h"
#include "sylar/log/logger.h"

namespace sylar
{

    static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

    static thread_local Scheduler *t_scheduler = nullptr;
    static thread_local Fiber *t_scheduler_fiber = nullptr;
    static thread_local size_t t_scheduler_worker_index = Scheduler::kInvalidWorker;

    Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name)
        : m_name(name),
          m_workerCursor(0),
          m_pendingTaskCount(0),
          m_activeThreadCount(0),
          m_idleThreadCount(0)
    {
        SYLAR_ASSERT(threads > 0);

        m_useCaller = use_caller;
        m_workerCount = threads;
        m_threadCount = use_caller ? threads - 1 : threads;

        if (use_caller)
        {
            sylar::Fiber::GetThis();
            SYLAR_ASSERT(GetThis() == nullptr);
            t_scheduler = this;
            t_scheduler_worker_index = kInvalidWorker;

            m_rootThread = sylar::GetThreadId();
            m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this, size_t(0)), 0, false));
            sylar::Thread::SetName(m_name);
            t_scheduler_fiber = m_rootFiber.get();
            m_threadIds.push_back(m_rootThread);
            m_autoStop = true;
        }
    }

    Scheduler::~Scheduler()
    {
        SYLAR_ASSERT(m_stopping);
        if (GetThis() == this)
        {
            t_scheduler = nullptr;
        }
    }

    Scheduler *Scheduler::GetThis()
    {
        return t_scheduler;
    }

    Fiber *Scheduler::GetMainFiber()
    {
        return t_scheduler_fiber;
    }

    std::vector<Scheduler::WorkerStats> Scheduler::getWorkerStatsSnapshot() const
    {
        std::vector<WorkerStats> stats;
        stats.reserve(m_workers.size());
        for (size_t i = 0; i < m_workers.size(); ++i)
        {
            const Worker *worker = m_workers[i].get();
            if (!worker)
            {
                continue;
            }
            WorkerStats stat;
            stat.threadId = worker->threadId;
            stat.queuedTasks = worker->queuedTasks.load(std::memory_order_relaxed);
            stat.sleeping = worker->sleeping.load(std::memory_order_relaxed);
            stats.push_back(stat);
        }
        return stats;
    }

    void Scheduler::scheduleCommand(const Fiber::ptr &fiber, int thread)
    {
        FiberAndThread task(fiber, thread);
        if (!task.fiber)
        {
            return;
        }
        scheduleTask(std::move(task), true);
    }

    void Scheduler::scheduleCommand(const std::function<void()> &cb, int thread)
    {
        FiberAndThread task(cb, thread);
        if (!task.cb)
        {
            return;
        }
        scheduleTask(std::move(task), true);
    }

    void Scheduler::setThis()
    {
        t_scheduler = this;
    }

    size_t Scheduler::getCurrentWorkerIndex() const
    {
        return t_scheduler_worker_index;
    }

    int Scheduler::getWorkerThreadId(size_t worker) const
    {
        if (worker >= m_workers.size() || !m_workers[worker])
        {
            return -1;
        }
        return m_workers[worker]->threadId;
    }

    void Scheduler::start()
    {
        if (m_started)
        {
            return;
        }

        m_stopping = false;
        m_workers.resize(m_workerCount);
        for (size_t i = 0; i < m_workerCount; ++i)
        {
            m_workers[i].reset(new Worker(kMailboxRingSize));
            m_workers[i]->index = i;
        }

        size_t next_worker = 0;
        if (m_useCaller)
        {
            Worker &caller = *m_workers[0];
            caller.isCaller = true;
            caller.threadId = m_rootThread;
            next_worker = 1;
        }

        for (; next_worker < m_workerCount; ++next_worker)
        {
            Worker &worker = *m_workers[next_worker];
            worker.thread.reset(new Thread(
                std::bind(&Scheduler::run, this, next_worker),
                m_name + "_" + std::to_string(next_worker)));
            worker.threadId = worker.thread->getId();
            m_threadIds.push_back(worker.threadId);
        }

        m_started = true;
        flushStartupTasks();
    }

    void Scheduler::stop()
    {
        m_autoStop = true;
        m_stopping = true;

        if (!m_started)
        {
            m_started = true;
            return;
        }

        tickleAll();

        if (m_rootFiber)
        {
            Fiber::State root_state = m_rootFiber->getState();
            if (!stopping() && root_state != Fiber::EXEC &&
                root_state != Fiber::TERM && root_state != Fiber::EXCEPT)
            {
                m_rootFiber->call();
            }
        }

        for (size_t i = 0; i < m_workers.size(); ++i)
        {
            Worker *worker = m_workers[i].get();
            if (!worker || worker->isCaller || !worker->thread)
            {
                continue;
            }
            worker->thread->join();
        }
    }

    void Scheduler::runCaller()
    {
        SYLAR_ASSERT(m_useCaller);
        SYLAR_ASSERT(m_rootFiber);
        SYLAR_ASSERT(sylar::GetThreadId() == m_rootThread);

        if (!m_started)
        {
            start();
        }

        Fiber::State root_state = m_rootFiber->getState();
        if (root_state == Fiber::EXEC || root_state == Fiber::TERM || root_state == Fiber::EXCEPT)
        {
            return;
        }

        m_rootFiber->call();
    }

    void Scheduler::tickle(size_t)
    {
    }

    void Scheduler::tickleAll()
    {
        for (size_t i = 0; i < m_workers.size(); ++i)
        {
            tickle(i);
        }
    }

    bool Scheduler::stopping()
    {
        return m_autoStop && m_stopping &&
               m_pendingTaskCount.load(std::memory_order_acquire) == 0 &&
               m_activeThreadCount.load(std::memory_order_acquire) == 0;
    }

    void Scheduler::idle()
    {
        while (!stopping())
        {
            sylar::Fiber::YieldToHold();
        }
    }

    size_t Scheduler::selectWorker(int thread) const
    {
        if (!m_started || m_workers.empty())
        {
            return kInvalidWorker;
        }

        if (thread != -1)
        {
            for (size_t i = 0; i < m_workers.size(); ++i)
            {
                const Worker *worker = m_workers[i].get();
                if (worker && worker->threadId == thread)
                {
                    return i;
                }
            }
        }

        if (t_scheduler == this && t_scheduler_worker_index != kInvalidWorker)
        {
            return t_scheduler_worker_index;
        }

        size_t base = 0;
        size_t count = m_workers.size();
        if (m_useCaller && m_workers.size() > 1 &&
            !m_callerActive.load(std::memory_order_acquire))
        {
            base = 1;
            count = m_workers.size() - 1;
        }

        if (count == 1)
        {
            return base;
        }

        size_t pick1 = base + (m_workerCursor.fetch_add(1, std::memory_order_relaxed) % count);
        size_t pick2 = base + (m_workerCursor.fetch_add(1, std::memory_order_relaxed) % count);
        if (pick1 == pick2)
        {
            pick2 = base + ((pick2 - base + 1) % count);
        }

        uint32_t load1 = m_workers[pick1]->queuedTasks.load(std::memory_order_relaxed);
        uint32_t load2 = m_workers[pick2]->queuedTasks.load(std::memory_order_relaxed);
        if (load1 < load2)
        {
            return pick1;
        }
        if (load2 < load1)
        {
            return pick2;
        }

        // tie-break：负载相同则交替选择，避免长期偏向同一候选。
        return (m_workerCursor.fetch_add(1, std::memory_order_relaxed) & 1) ? pick1 : pick2;
    }

    void Scheduler::enqueueStartupTask(FiberAndThread task)
    {
        Mutex::Lock lock(m_startupMutex);
        m_startupTasks.push_back(std::move(task));
        m_pendingTaskCount.fetch_add(1, std::memory_order_release);
    }

    void Scheduler::flushStartupTasks()
    {
        std::vector<FiberAndThread> tasks;
        {
            Mutex::Lock lock(m_startupMutex);
            tasks.swap(m_startupTasks);
        }

        for (size_t i = 0; i < tasks.size(); ++i)
        {
            scheduleTask(std::move(tasks[i]), false);
            m_pendingTaskCount.fetch_sub(1, std::memory_order_release);
        }
    }

    void Scheduler::enqueueLocal(Worker &worker, FiberAndThread task)
    {
        Worker::QueueMutexType::Lock lock(worker.queueMutex);
        worker.localQueue.push_back(std::move(task));
        worker.queuedTasks.fetch_add(1, std::memory_order_relaxed);
        m_pendingTaskCount.fetch_add(1, std::memory_order_release);
    }

    bool Scheduler::tryEnqueueMailboxRing(Worker &worker, FiberAndThread &task)
    {
        size_t pos = worker.mailboxEnqueuePos.load(std::memory_order_relaxed);
        for (;;)
        {
            Worker::MailboxRingSlot &slot = worker.mailboxRing[pos & worker.mailboxRingMask];
            size_t seq = slot.seq.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0)
            {
                if (worker.mailboxEnqueuePos.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed))
                {
                    slot.task = std::move(task);
                    slot.seq.store(pos + 1, std::memory_order_release);
                    worker.queuedTasks.fetch_add(1, std::memory_order_relaxed);
                    m_pendingTaskCount.fetch_add(1, std::memory_order_release);
                    return true;
                }
                continue;
            }

            if (diff < 0)
            {
                return false;
            }

            pos = worker.mailboxEnqueuePos.load(std::memory_order_relaxed);
        }
    }

    void Scheduler::enqueueMailboxFallback(Worker &worker, FiberAndThread task)
    {
        RemoteTaskNode *node = new RemoteTaskNode(std::move(task));
        RemoteTaskNode *old_head = worker.mailboxFallback.load(std::memory_order_relaxed);
        do
        {
            node->next = old_head;
        } while (!worker.mailboxFallback.compare_exchange_weak(
            old_head, node, std::memory_order_release, std::memory_order_relaxed));
        worker.queuedTasks.fetch_add(1, std::memory_order_relaxed);
        m_pendingTaskCount.fetch_add(1, std::memory_order_release);
    }

    void Scheduler::scheduleTask(FiberAndThread task, bool allow_cross_worker)
    {
        if (!task.fiber && !task.cb)
        {
            return;
        }

        if (!m_started)
        {
            enqueueStartupTask(std::move(task));
            return;
        }

        size_t worker_index = selectWorker(task.thread);
        if (worker_index == kInvalidWorker)
        {
            enqueueStartupTask(std::move(task));
            return;
        }

        size_t current = (t_scheduler == this) ? t_scheduler_worker_index : kInvalidWorker;
        if (current != kInvalidWorker)
        {
            if (current == worker_index)
            {
                enqueueLocal(*m_workers[current], std::move(task));
                return;
            }

            if (!allow_cross_worker)
            {
                static std::atomic<uint64_t> s_cross_worker_downgrade_log_count(0);
                uint64_t n = s_cross_worker_downgrade_log_count.fetch_add(1, std::memory_order_relaxed);
                if (n < 16)
                {
                    SYLAR_LOG_WARN(g_logger) << "cross-worker schedule downgraded to local queue"
                                             << " scheduler=" << m_name
                                             << " current_worker=" << current
                                             << " target_worker=" << worker_index;
                }
                enqueueLocal(*m_workers[current], std::move(task));
                return;
            }
        }

        Worker &worker = *m_workers[worker_index];
        if (!tryEnqueueMailboxRing(worker, task))
        {
            enqueueMailboxFallback(worker, std::move(task));
        }
        // 远程投递只在目标 worker 处于睡眠态时唤醒，减少高频 eventfd 写入。
        if (worker.sleeping.load(std::memory_order_acquire))
        {
            tickle(worker_index);
        }
    }

    void Scheduler::drainMailboxRing(Worker &worker, size_t max_batch)
    {
        std::vector<FiberAndThread> batch;
        batch.reserve(max_batch);

        size_t pos = worker.mailboxDequeuePos.load(std::memory_order_relaxed);
        while (batch.size() < max_batch)
        {
            Worker::MailboxRingSlot &slot = worker.mailboxRing[pos & worker.mailboxRingMask];
            size_t seq = slot.seq.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (diff < 0)
            {
                break;
            }
            if (diff > 0)
            {
                pos = worker.mailboxDequeuePos.load(std::memory_order_relaxed);
                continue;
            }

            batch.push_back(std::move(slot.task));
            slot.seq.store(pos + worker.mailboxRingSize, std::memory_order_release);
            ++pos;
        }

        worker.mailboxDequeuePos.store(pos, std::memory_order_relaxed);

        if (batch.empty())
        {
            return;
        }

        Worker::QueueMutexType::Lock lock(worker.queueMutex);
        for (size_t i = 0; i < batch.size(); ++i)
        {
            worker.localQueue.push_back(std::move(batch[i]));
        }
    }

    void Scheduler::drainMailboxFallback(Worker &worker)
    {
        RemoteTaskNode *head = worker.mailboxFallback.exchange(nullptr, std::memory_order_acquire);
        if (!head)
        {
            return;
        }

        RemoteTaskNode *reversed = nullptr;
        while (head)
        {
            RemoteTaskNode *next = head->next;
            head->next = reversed;
            reversed = head;
            head = next;
        }

        Worker::QueueMutexType::Lock lock(worker.queueMutex);
        while (reversed)
        {
            RemoteTaskNode *next = reversed->next;
            worker.localQueue.push_back(std::move(reversed->task));
            delete reversed;
            reversed = next;
        }
    }

    bool Scheduler::hasMailboxWork(const Worker &worker) const
    {
        if (worker.mailboxFallback.load(std::memory_order_acquire) != nullptr)
        {
            return true;
        }

        size_t enq = worker.mailboxEnqueuePos.load(std::memory_order_acquire);
        size_t deq = worker.mailboxDequeuePos.load(std::memory_order_acquire);
        return enq != deq;
    }

    bool Scheduler::popTask(Worker &worker, FiberAndThread &task)
    {
        drainMailboxRing(worker, kMailboxDrainBatch);
        drainMailboxFallback(worker);

        Worker::QueueMutexType::Lock lock(worker.queueMutex);
        if (worker.localQueue.empty())
        {
            return false;
        }

        task = std::move(worker.localQueue.front());
        worker.localQueue.pop_front();
        worker.queuedTasks.fetch_sub(1, std::memory_order_relaxed);
        m_pendingTaskCount.fetch_sub(1, std::memory_order_release);
        return true;
    }

    void Scheduler::run(size_t worker_index)
    {
        setThis();
        set_hook_enable(true);

        SYLAR_ASSERT(worker_index < m_workers.size());
        Worker &worker = *m_workers[worker_index];
        t_scheduler_worker_index = worker.index;
        if (worker.isCaller)
        {
            m_callerActive.store(true, std::memory_order_release);
        }

        if (sylar::GetThreadId() != m_rootThread || !m_useCaller)
        {
            t_scheduler_fiber = Fiber::GetThisRaw();
        }

        Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
        Fiber::ptr cb_fiber;
        FiberAndThread task;

        while (true)
        {
            task.reset();

            popTask(worker, task);

            if (task.fiber && task.fiber->getState() != Fiber::TERM &&
                task.fiber->getState() != Fiber::EXCEPT)
            {
                worker.sleeping.store(false, std::memory_order_release);
                m_activeThreadCount.fetch_add(1, std::memory_order_release);
                task.fiber->resume();
                m_activeThreadCount.fetch_sub(1, std::memory_order_release);

                if (task.fiber->getState() == Fiber::READY)
                {
                    schedule(task.fiber);
                }
                else if (task.fiber->getState() != Fiber::TERM &&
                         task.fiber->getState() != Fiber::EXCEPT)
                {
                    task.fiber->setState(Fiber::HOLD);
                }
                task.reset();
                if (m_stopping &&
                    m_pendingTaskCount.load(std::memory_order_acquire) == 0 &&
                    m_activeThreadCount.load(std::memory_order_acquire) == 0)
                {
                    tickleAll();
                }
                continue;
            }

            if (task.cb)
            {
                worker.sleeping.store(false, std::memory_order_release);
                if (cb_fiber && (cb_fiber->getState() == Fiber::TERM ||
                                 cb_fiber->getState() == Fiber::EXCEPT))
                {
                    cb_fiber->reset(task.cb);
                }
                else if (!cb_fiber)
                {
                    cb_fiber.reset(new Fiber(task.cb));
                }
                else
                {
                    cb_fiber.reset(new Fiber(task.cb));
                }

                task.reset();
                m_activeThreadCount.fetch_add(1, std::memory_order_release);
                cb_fiber->resume();
                m_activeThreadCount.fetch_sub(1, std::memory_order_release);

                if (cb_fiber->getState() == Fiber::READY)
                {
                    schedule(cb_fiber);
                    cb_fiber.reset();
                }
                else if (cb_fiber->getState() == Fiber::TERM ||
                         cb_fiber->getState() == Fiber::EXCEPT)
                {
                    cb_fiber.reset();
                }
                else
                {
                    cb_fiber->setState(Fiber::HOLD);
                    cb_fiber.reset();
                }
                if (m_stopping &&
                    m_pendingTaskCount.load(std::memory_order_acquire) == 0 &&
                    m_activeThreadCount.load(std::memory_order_acquire) == 0)
                {
                    tickleAll();
                }
                continue;
            }

            if (stopping())
            {
                break;
            }

            if (idle_fiber->getState() == Fiber::TERM)
            {
                break;
            }

            worker.sleeping.store(true, std::memory_order_release);
            // 进入 idle 前再检查一次，避免与并发投递竞态导致漏唤醒。
            if (hasMailboxWork(worker) ||
                m_pendingTaskCount.load(std::memory_order_acquire) > 0)
            {
                worker.sleeping.store(false, std::memory_order_release);
                continue;
            }
            m_idleThreadCount.fetch_add(1, std::memory_order_release);
            idle_fiber->resume();
            m_idleThreadCount.fetch_sub(1, std::memory_order_release);
            worker.sleeping.store(false, std::memory_order_release);

            if (idle_fiber->getState() != Fiber::TERM &&
                idle_fiber->getState() != Fiber::EXCEPT)
            {
                idle_fiber->setState(Fiber::HOLD);
            }
        }

        t_scheduler_worker_index = kInvalidWorker;
        if (worker.isCaller)
        {
            m_callerActive.store(false, std::memory_order_release);
        }
    }

}
