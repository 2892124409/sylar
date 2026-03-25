/**
 * @file iomanager.h
 * @brief IO协程调度器
 */

#ifndef __SYLAR_IOMANAGER_H__
#define __SYLAR_IOMANAGER_H__

#include <atomic>
#include <vector>
#include "sylar/concurrency/mutex/rw_mutex.h"
#include "sylar/fiber/scheduler.h"
#include "sylar/fiber/timer.h"

namespace sylar
{

    class IOManager : public Scheduler, public TimerManager
    {
    public:
        typedef std::shared_ptr<IOManager> ptr;
        typedef RWMutex RWMutexType;

        enum Event
        {
            NONE = 0x0,
            READ = 0x1,
            WRITE = 0x4,
        };

    private:
        struct FdContext
        {
            typedef Mutex MutexType;

            struct EventContext
            {
                Scheduler *scheduler = nullptr;
                Fiber::ptr fiber;
                std::function<void()> cb;
                Timer::ptr timeoutTimer;
                int thread = -1;
                uint64_t waitToken = 0;
            };

            EventContext &getEventContext(Event event);
            void resetEventContext(EventContext &ctx);
            void triggerEvent(Event event, Fiber::WaitResult result);

            EventContext read;
            EventContext write;
            int fd = 0;
            Event events = NONE;
            MutexType mutex;
        };

        struct WorkerContext
        {
            int epfd = -1;
            int wakeFd = -1;
        };

    public:
        IOManager(size_t threads = 1, bool use_caller = true, const std::string &name = "");
        ~IOManager();

        int addEvent(int fd, Event event, std::function<void()> cb = nullptr);
        bool delEvent(int fd, Event event);
        bool cancelEvent(int fd, Event event);
        bool cancelAll(int fd);
        int waitEvent(int fd, Event event, uint64_t timeout_ms = ~0ull);

        static IOManager *GetThis();

    protected:
        void tickle(size_t worker) override;
        bool stopping() override;
        bool stopping(uint64_t &timeout);
        void idle() override;
        void onTimerInsertedAtFront(size_t worker) override;
        size_t getTimerWorkerIndex() override;
        void contextResize(size_t size);

    private:
        int addEventInternal(int fd, Event event, std::function<void()> cb,
                             Fiber::ptr fiber, uint64_t timeout_ms, uint64_t *wait_token);
        bool cancelEventInternal(int fd, Event event, Fiber::WaitResult result);
        void onEventTimeout(int fd, Event event, uint64_t wait_token);
        FdContext *getFdContext(int fd, bool auto_create);
        size_t getCurrentWorkerForOp() const;
        bool ensureFdAffinity(int fd, const char *op_name, bool auto_create);

    private:
        std::vector<WorkerContext> m_workerContexts;
        std::atomic<size_t> m_pendingEventCount;
        std::atomic<size_t> m_timerWorkerCursor;
        RWMutexType m_mutex;
        std::vector<FdContext *> m_fdContexts;
    };

}

#endif
