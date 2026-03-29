#include "sylar/fiber/iomanager.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include "sylar/base/macro.h"
#include "sylar/fiber/fd_manager.h"
#include "sylar/log/logger.h"

namespace sylar
{

    static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

    namespace
    {
        uint32_t ToEpollEvents(IOManager::Event events)
        {
            uint32_t out = EPOLLET;
            if (events & IOManager::READ)
            {
                out |= EPOLLIN;
            }
            if (events & IOManager::WRITE)
            {
                out |= EPOLLOUT;
            }
            return out;
        }

        void LogOpThreadViolation(const char *op_name)
        {
            static std::atomic<uint64_t> s_log_count(0);
            uint64_t n = s_log_count.fetch_add(1, std::memory_order_relaxed);
            if (n < 16 || (n % 1024) == 0)
            {
                SYLAR_LOG_WARN(g_logger) << op_name << " called outside current IOManager worker thread";
            }
        }
    }

    IOManager::FdContext::EventContext &IOManager::FdContext::getEventContext(IOManager::Event event)
    {
        switch (event)
        {
        case IOManager::READ:
            return read;
        case IOManager::WRITE:
            return write;
        default:
            SYLAR_ASSERT2(false, "getEventContext invalid event");
        }
        throw std::invalid_argument("getEventContext invalid event");
    }

    void IOManager::FdContext::resetEventContext(EventContext &ctx)
    {
        ctx.scheduler = nullptr;
        ctx.fiber.reset();
        ctx.cb = nullptr;
        ctx.timeoutTimer.reset();
        ctx.waitToken = 0;
    }

    void IOManager::FdContext::triggerEvent(IOManager::Event event, Fiber::WaitResult result)
    {
        SYLAR_ASSERT(events & event);
        events = static_cast<Event>(events & ~event);

        EventContext &ctx = getEventContext(event);
        Timer::ptr timeout = ctx.timeoutTimer;
        ctx.timeoutTimer.reset();
        if (timeout)
        {
            timeout->cancel();
        }

        if (ctx.fiber)
        {
            if (ctx.waitToken)
            {
                ctx.fiber->setWaitResult(ctx.waitToken, result);
            }
            ctx.scheduler->schedule(ctx.fiber);
        }
        else if (ctx.cb)
        {
            ctx.scheduler->schedule(ctx.cb);
        }

        resetEventContext(ctx);
    }

    IOManager::IOManager(size_t threads, bool use_caller, const std::string &name)
        : Scheduler(threads, use_caller, name),
          m_pendingEventCount(0),
          m_timerWorkerCursor(0)
    {
        initTimerBuckets(getWorkerCount());

        m_workerContexts.resize(getWorkerCount());
        for (size_t i = 0; i < m_workerContexts.size(); ++i)
        {
            WorkerContext &worker = m_workerContexts[i];
            worker.epfd = epoll_create1(EPOLL_CLOEXEC);
            SYLAR_ASSERT(worker.epfd > 0);

            worker.wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            SYLAR_ASSERT(worker.wakeFd > 0);

            epoll_event event;
            memset(&event, 0, sizeof(event));
            event.events = EPOLLIN | EPOLLET;
            event.data.fd = worker.wakeFd;

            int rt = epoll_ctl(worker.epfd, EPOLL_CTL_ADD, worker.wakeFd, &event);
            SYLAR_ASSERT(rt == 0);
        }

        contextResize(64);
        start();
    }

    IOManager::~IOManager()
    {
        stop();

        for (size_t i = 0; i < m_workerContexts.size(); ++i)
        {
            WorkerContext &worker = m_workerContexts[i];
            if (worker.epfd >= 0)
            {
                close(worker.epfd);
                worker.epfd = -1;
            }
            if (worker.wakeFd >= 0)
            {
                close(worker.wakeFd);
                worker.wakeFd = -1;
            }
        }

        for (size_t i = 0; i < m_fdContexts.size(); ++i)
        {
            delete m_fdContexts[i];
        }
    }

    void IOManager::contextResize(size_t size)
    {
        size = std::max(size, m_fdContexts.size());
        m_fdContexts.resize(size);
        for (size_t i = 0; i < m_fdContexts.size(); ++i)
        {
            if (!m_fdContexts[i])
            {
                m_fdContexts[i] = new FdContext;
                m_fdContexts[i]->fd = static_cast<int>(i);
            }
        }
    }

    IOManager::FdContext *IOManager::getFdContext(int fd, bool auto_create)
    {
        if (fd < 0)
        {
            return nullptr;
        }

        RWMutexType::ReadLock lock(m_mutex);
        if (static_cast<int>(m_fdContexts.size()) > fd)
        {
            FdContext *ctx = m_fdContexts[fd];
            if (ctx || !auto_create)
            {
                return ctx;
            }
        }
        else if (!auto_create)
        {
            return nullptr;
        }
        lock.unlock();

        RWMutexType::WriteLock lock2(m_mutex);
        if (static_cast<int>(m_fdContexts.size()) <= fd)
        {
            size_t target = std::max<size_t>(fd + 1, m_fdContexts.size() * 2);
            contextResize(target);
        }
        return m_fdContexts[fd];
    }

    size_t IOManager::getCurrentWorkerForOp() const
    {
        if (Scheduler::GetThis() != this)
        {
            return Scheduler::kInvalidWorker;
        }
        size_t worker = getCurrentWorkerIndex();
        if (worker == Scheduler::kInvalidWorker || worker >= m_workerContexts.size())
        {
            return Scheduler::kInvalidWorker;
        }
        if (getWorkerThreadId(worker) != sylar::GetThreadId())
        {
            return Scheduler::kInvalidWorker;
        }
        return worker;
    }

    bool IOManager::ensureFdAffinity(int fd, const char *op_name, bool auto_create)
    {
        sylar::FdCtx::ptr fd_ctx = sylar::FdMgr::GetInstance()->get(fd, auto_create);
        if (!fd_ctx || fd_ctx->isClose() || !fd_ctx->isInit())
        {
            errno = EBADF;
            return false;
        }

        int tid = sylar::GetThreadId();
        if (fd_ctx->bindAffinityIfUnset(tid))
        {
            return true;
        }

        errno = EXDEV;
        static std::atomic<uint64_t> s_affinity_log_count(0);
        uint64_t n = s_affinity_log_count.fetch_add(1, std::memory_order_relaxed);
        if (n < 16 || (n % 1024) == 0)
        {
            SYLAR_LOG_WARN(g_logger) << op_name
                                     << " fd affinity mismatch"
                                     << " fd=" << fd
                                     << " current_tid=" << tid
                                     << " affinity_tid=" << fd_ctx->getAffinityThread();
        }
        return false;
    }

    int IOManager::addEvent(int fd, Event event, std::function<void()> cb)
    {
        return addEventInternal(fd, event, std::move(cb), nullptr, ~0ull, nullptr);
    }

    int IOManager::waitEvent(int fd, Event event, uint64_t timeout_ms)
    {
        Fiber::ptr fiber = Fiber::GetThis();
        uint64_t wait_token = 0;
        int rt = addEventInternal(fd, event, nullptr, fiber, timeout_ms, &wait_token);
        if (rt)
        {
            return rt;
        }

        sylar::Fiber::YieldToHold();

        Fiber::WaitResult result = Fiber::GetThisRaw()->consumeWaitResult(wait_token);
        if (result == Fiber::WAIT_TIMEOUT)
        {
            errno = ETIMEDOUT;
            return 1;
        }
        return 0;
    }

    int IOManager::addEventInternal(int fd, Event event, std::function<void()> cb,
                                    Fiber::ptr fiber, uint64_t timeout_ms, uint64_t *wait_token)
    {
        size_t worker = getCurrentWorkerForOp();
        if (worker == Scheduler::kInvalidWorker)
        {
            errno = EXDEV;
            LogOpThreadViolation("addEvent");
            return -1;
        }

        FdContext *fd_ctx = getFdContext(fd, true);
        if (!fd_ctx)
        {
            errno = EBADF;
            return -1;
        }
        if (!ensureFdAffinity(fd, "addEvent", true))
        {
            return -1;
        }

        FdContext::MutexType::Lock lock(fd_ctx->mutex);
        if (SYLAR_UNLIKELY(fd_ctx->events & event))
        {
            errno = EEXIST;
            return -1;
        }

        WorkerContext &worker_ctx = m_workerContexts[worker];
        int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
        epoll_event epevent;
        memset(&epevent, 0, sizeof(epevent));
        epevent.events = ToEpollEvents(static_cast<Event>(fd_ctx->events | event));
        epevent.data.ptr = fd_ctx;

        int rt = epoll_ctl(worker_ctx.epfd, op, fd, &epevent);
        if (rt)
        {
            SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << worker_ctx.epfd << ", "
                                      << op << "," << fd << "," << epevent.events << "):"
                                      << rt << " (" << errno << ") (" << strerror(errno) << ")";
            return -1;
        }

        ++m_pendingEventCount;
        fd_ctx->events = static_cast<Event>(fd_ctx->events | event);

        FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
        SYLAR_ASSERT(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);
        event_ctx.scheduler = this;

        if (cb)
        {
            event_ctx.cb.swap(cb);
        }
        else
        {
            event_ctx.fiber = fiber ? fiber : Fiber::GetThis();
            SYLAR_ASSERT2(event_ctx.fiber->getState() == Fiber::EXEC,
                          "state=" << event_ctx.fiber->getState());
            event_ctx.waitToken = event_ctx.fiber->beginWait();
            if (wait_token)
            {
                *wait_token = event_ctx.waitToken;
            }
        }

        if (timeout_ms != ~0ull)
        {
            uint64_t wait_key = event_ctx.waitToken;
            event_ctx.timeoutTimer = addTimer(
                timeout_ms,
                std::bind(&IOManager::onEventTimeout, this, fd, event, wait_key),
                false,
                worker);
        }

        return 0;
    }

    bool IOManager::delEvent(int fd, Event event)
    {
        size_t worker = getCurrentWorkerForOp();
        if (worker == Scheduler::kInvalidWorker)
        {
            errno = EXDEV;
            LogOpThreadViolation("delEvent");
            return false;
        }

        FdContext *fd_ctx = getFdContext(fd, false);
        if (!fd_ctx)
        {
            errno = EBADF;
            return false;
        }
        if (!ensureFdAffinity(fd, "delEvent", false))
        {
            return false;
        }

        FdContext::MutexType::Lock lock(fd_ctx->mutex);
        if (!(fd_ctx->events & event))
        {
            return false;
        }

        WorkerContext &worker_ctx = m_workerContexts[worker];
        Event new_events = static_cast<Event>(fd_ctx->events & ~event);
        int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        epoll_event epevent;
        memset(&epevent, 0, sizeof(epevent));
        epevent.events = ToEpollEvents(new_events);
        epevent.data.ptr = fd_ctx;

        int rt = epoll_ctl(worker_ctx.epfd, op, fd, &epevent);
        if (rt)
        {
            SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << worker_ctx.epfd << ", "
                                      << op << "," << fd << "," << epevent.events << "):"
                                      << rt << " (" << errno << ") (" << strerror(errno) << ")";
            return false;
        }

        --m_pendingEventCount;
        fd_ctx->events = new_events;
        FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
        if (event_ctx.timeoutTimer)
        {
            event_ctx.timeoutTimer->cancel();
        }
        fd_ctx->resetEventContext(event_ctx);
        return true;
    }

    bool IOManager::cancelEvent(int fd, Event event)
    {
        return cancelEventInternal(fd, event, Fiber::WAIT_CANCELLED);
    }

    bool IOManager::cancelEventInternal(int fd, Event event, Fiber::WaitResult result)
    {
        size_t worker = getCurrentWorkerForOp();
        if (worker == Scheduler::kInvalidWorker)
        {
            errno = EXDEV;
            LogOpThreadViolation("cancelEvent");
            return false;
        }

        FdContext *fd_ctx = getFdContext(fd, false);
        if (!fd_ctx)
        {
            errno = EBADF;
            return false;
        }
        if (!ensureFdAffinity(fd, "cancelEvent", false))
        {
            return false;
        }

        FdContext::MutexType::Lock lock(fd_ctx->mutex);
        if (!(fd_ctx->events & event))
        {
            return false;
        }

        WorkerContext &worker_ctx = m_workerContexts[worker];
        Event new_events = static_cast<Event>(fd_ctx->events & ~event);
        int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        epoll_event epevent;
        memset(&epevent, 0, sizeof(epevent));
        epevent.events = ToEpollEvents(new_events);
        epevent.data.ptr = fd_ctx;

        int rt = epoll_ctl(worker_ctx.epfd, op, fd, &epevent);
        if (rt)
        {
            SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << worker_ctx.epfd << ", "
                                      << op << "," << fd << "," << epevent.events << "):"
                                      << rt << " (" << errno << ") (" << strerror(errno) << ")";
            return false;
        }

        fd_ctx->triggerEvent(event, result);
        --m_pendingEventCount;
        return true;
    }

    bool IOManager::cancelAll(int fd)
    {
        size_t worker = getCurrentWorkerForOp();
        if (worker == Scheduler::kInvalidWorker)
        {
            errno = EXDEV;
            LogOpThreadViolation("cancelAll");
            return false;
        }

        FdContext *fd_ctx = getFdContext(fd, false);
        if (!fd_ctx)
        {
            errno = EBADF;
            return false;
        }
        if (!ensureFdAffinity(fd, "cancelAll", false))
        {
            return false;
        }

        FdContext::MutexType::Lock lock(fd_ctx->mutex);
        if (!fd_ctx->events)
        {
            return false;
        }

        WorkerContext &worker_ctx = m_workerContexts[worker];
        epoll_event epevent;
        memset(&epevent, 0, sizeof(epevent));
        epevent.events = 0;
        epevent.data.ptr = fd_ctx;

        int rt = epoll_ctl(worker_ctx.epfd, EPOLL_CTL_DEL, fd, &epevent);
        if (rt)
        {
            SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << worker_ctx.epfd << ", "
                                      << EPOLL_CTL_DEL << "," << fd << "," << epevent.events << "):"
                                      << rt << " (" << errno << ") (" << strerror(errno) << ")";
            return false;
        }

        if (fd_ctx->events & READ)
        {
            fd_ctx->triggerEvent(READ, Fiber::WAIT_CANCELLED);
            --m_pendingEventCount;
        }
        if (fd_ctx->events & WRITE)
        {
            fd_ctx->triggerEvent(WRITE, Fiber::WAIT_CANCELLED);
            --m_pendingEventCount;
        }
        return true;
    }

    void IOManager::tickle(size_t worker)
    {
        if (worker >= m_workerContexts.size())
        {
            return;
        }

        uint64_t one = 1;
        ssize_t rt = write(m_workerContexts[worker].wakeFd, &one, sizeof(one));
        if (rt < 0 && errno != EAGAIN)
        {
            SYLAR_ASSERT(false);
        }
    }

    bool IOManager::stopping()
    {
        uint64_t timeout = 0;
        return stopping(timeout);
    }

    bool IOManager::stopping(uint64_t &timeout)
    {
        size_t worker = getCurrentWorkerIndex();
        if (worker == Scheduler::kInvalidWorker)
        {
            timeout = ~0ull;
        }
        else
        {
            timeout = getNextTimer(worker);
        }

        return timeout == ~0ull &&
               m_pendingEventCount.load(std::memory_order_acquire) == 0 &&
               !hasTimer() &&
               Scheduler::stopping();
    }

    size_t IOManager::getTimerWorkerIndex()
    {
        size_t current = getCurrentWorkerIndex();
        if (current != Scheduler::kInvalidWorker)
        {
            return current;
        }
        return m_timerWorkerCursor.fetch_add(1, std::memory_order_relaxed) % getWorkerCount();
    }

    void IOManager::onTimerInsertedAtFront(size_t worker)
    {
        tickle(worker);
    }

    void IOManager::onEventTimeout(int fd, Event event, uint64_t wait_token)
    {
        size_t worker = getCurrentWorkerForOp();
        if (worker == Scheduler::kInvalidWorker)
        {
            return;
        }

        FdContext *fd_ctx = getFdContext(fd, false);
        if (!fd_ctx)
        {
            return;
        }
        if (!ensureFdAffinity(fd, "onEventTimeout", false))
        {
            return;
        }

        FdContext::MutexType::Lock lock(fd_ctx->mutex);
        if (!(fd_ctx->events & event))
        {
            return;
        }

        FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
        if (event_ctx.waitToken != wait_token)
        {
            return;
        }

        WorkerContext &worker_ctx = m_workerContexts[worker];
        Event new_events = static_cast<Event>(fd_ctx->events & ~event);
        int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        epoll_event epevent;
        memset(&epevent, 0, sizeof(epevent));
        epevent.events = ToEpollEvents(new_events);
        epevent.data.ptr = fd_ctx;

        int rt = epoll_ctl(worker_ctx.epfd, op, fd, &epevent);
        if (rt)
        {
            return;
        }

        fd_ctx->triggerEvent(event, Fiber::WAIT_TIMEOUT);
        --m_pendingEventCount;
    }

    void IOManager::idle()
    {
        static const int MAX_EVENTS = 256;
        std::vector<epoll_event> events(MAX_EVENTS);
        size_t worker = getCurrentWorkerIndex();
        SYLAR_ASSERT(worker != Scheduler::kInvalidWorker);

        WorkerContext &ctx = m_workerContexts[worker];
        while (true)
        {
            uint64_t next_timeout = 0;
            if (stopping(next_timeout))
            {
                break;
            }

            int timeout = -1;
            if (next_timeout != ~0ull)
            {
                timeout = next_timeout > static_cast<uint64_t>(INT_MAX)
                              ? INT_MAX
                              : static_cast<int>(next_timeout);
            }

            int rt = 0;
            do
            {
                rt = epoll_wait(ctx.epfd, &events[0], MAX_EVENTS, timeout);
            } while (rt < 0 && errno == EINTR);

            for (int i = 0; i < rt; ++i)
            {
                epoll_event &event = events[i];
                if (event.data.fd == ctx.wakeFd)
                {
                    uint64_t dummy = 0;
                    while (read(ctx.wakeFd, &dummy, sizeof(dummy)) > 0)
                    {
                    }
                    continue;
                }

                FdContext *fd_ctx = static_cast<FdContext *>(event.data.ptr);
                FdContext::MutexType::Lock lock(fd_ctx->mutex);

                if (event.events & (EPOLLERR | EPOLLHUP))
                {
                    event.events |= (EPOLLIN | EPOLLOUT);
                }

                uint32_t real_events = NONE;
                if (event.events & EPOLLIN)
                {
                    real_events |= READ;
                }
                if (event.events & EPOLLOUT)
                {
                    real_events |= WRITE;
                }
                real_events &= fd_ctx->events;

                if (real_events == NONE)
                {
                    continue;
                }

                Event left_events = static_cast<Event>(fd_ctx->events & ~real_events);
                int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
                epoll_event epevent;
                memset(&epevent, 0, sizeof(epevent));
                epevent.events = ToEpollEvents(left_events);
                epevent.data.ptr = fd_ctx;

                int rt2 = epoll_ctl(ctx.epfd, op, fd_ctx->fd, &epevent);
                if (rt2)
                {
                    SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << ctx.epfd << ", "
                                              << op << "," << fd_ctx->fd << "," << epevent.events << "):"
                                              << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                    continue;
                }

                if (real_events & READ)
                {
                    fd_ctx->triggerEvent(READ, Fiber::WAIT_READY);
                    --m_pendingEventCount;
                }
                if (real_events & WRITE)
                {
                    fd_ctx->triggerEvent(WRITE, Fiber::WAIT_READY);
                    --m_pendingEventCount;
                }
            }

            std::vector<std::function<void()>> cbs;
            listExpiredCb(worker, cbs);
            if (!cbs.empty())
            {
                schedule(cbs.begin(), cbs.end());
            }

            Fiber::GetThisRaw()->yield();
        }
    }

    IOManager *IOManager::GetThis()
    {
        return dynamic_cast<IOManager *>(Scheduler::GetThis());
    }

}
