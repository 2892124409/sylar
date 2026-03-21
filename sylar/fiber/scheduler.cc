#include "sylar/fiber/scheduler.h"
#include <algorithm>
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
            m_workers[i].reset(new Worker());
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

        if (m_useCaller && m_workers.size() > 1 &&
            !m_callerActive.load(std::memory_order_acquire))
        {
            return 1 + (m_workerCursor.fetch_add(1, std::memory_order_relaxed) % (m_workers.size() - 1));
        }

        return m_workerCursor.fetch_add(1, std::memory_order_relaxed) % m_workers.size();
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
            scheduleTask(std::move(tasks[i]));
            m_pendingTaskCount.fetch_sub(1, std::memory_order_release);
        }
    }

    void Scheduler::enqueueLocal(Worker &worker, FiberAndThread task)
    {
        Worker::QueueMutexType::Lock lock(worker.queueMutex);
        worker.localQueue.push_back(std::move(task));
        m_pendingTaskCount.fetch_add(1, std::memory_order_release);
    }

    void Scheduler::enqueueRemote(Worker &worker, FiberAndThread task)
    {
        RemoteTaskNode *node = new RemoteTaskNode(std::move(task));
        RemoteTaskNode *old_head = worker.remoteQueue.load(std::memory_order_relaxed);
        do
        {
            node->next = old_head;
        } while (!worker.remoteQueue.compare_exchange_weak(
            old_head, node, std::memory_order_release, std::memory_order_relaxed));
        m_pendingTaskCount.fetch_add(1, std::memory_order_release);
    }

    void Scheduler::scheduleTask(FiberAndThread task)
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

        Worker &worker = *m_workers[worker_index];
        bool local = (t_scheduler == this && t_scheduler_worker_index == worker_index);
        if (local)
        {
            enqueueLocal(worker, std::move(task));
            return;
        }

        enqueueRemote(worker, std::move(task));
        tickle(worker_index);
    }

    void Scheduler::drainRemoteQueue(Worker &worker)
    {
        RemoteTaskNode *head = worker.remoteQueue.exchange(nullptr, std::memory_order_acquire);
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

    bool Scheduler::popTask(Worker &worker, FiberAndThread &task)
    {
        drainRemoteQueue(worker);

        Worker::QueueMutexType::Lock lock(worker.queueMutex);
        if (worker.localQueue.empty())
        {
            return false;
        }

        task = std::move(worker.localQueue.front());
        worker.localQueue.pop_front();
        m_pendingTaskCount.fetch_sub(1, std::memory_order_release);
        return true;
    }

    bool Scheduler::stealTask(Worker &worker, FiberAndThread &task)
    {
        for (size_t i = 0; i < m_workers.size(); ++i)
        {
            Worker *victim = m_workers[i].get();
            if (!victim || victim == &worker)
            {
                continue;
            }

            Worker::QueueMutexType::Lock lock(victim->queueMutex);
            if (victim->localQueue.empty())
            {
                continue;
            }

            for (std::deque<FiberAndThread>::reverse_iterator it = victim->localQueue.rbegin();
                 it != victim->localQueue.rend(); ++it)
            {
                if (it->thread != -1)
                {
                    continue;
                }

                task = std::move(*it);
                victim->localQueue.erase(std::next(it).base());
                m_pendingTaskCount.fetch_sub(1, std::memory_order_release);
                return true;
            }
        }
        return false;
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

            if (!popTask(worker, task))
            {
                stealTask(worker, task);
            }

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
