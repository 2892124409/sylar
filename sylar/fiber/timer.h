/**
 * @file timer.h
 * @brief 定时器模块
 */
#ifndef __SYLAR_TIMER_H__
#define __SYLAR_TIMER_H__

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include "sylar/concurrency/mutex/mutex.h"

namespace sylar
{

    class TimerManager;

    class Timer : public std::enable_shared_from_this<Timer>
    {
        friend class TimerManager;

    public:
        typedef std::shared_ptr<Timer> ptr;

        bool cancel();
        bool refresh();
        bool reset(uint64_t ms, bool from_now);

    private:
        Timer(uint64_t ms, std::function<void()> cb, bool recurring,
              TimerManager *manager, size_t worker, uint64_t next, uint64_t sequence);

    private:
        bool m_recurring = false;
        uint64_t m_ms = 0;
        uint64_t m_next = 0;
        std::function<void()> m_cb;
        TimerManager *m_manager = nullptr;
        size_t m_worker = 0;
        size_t m_heapIndex = static_cast<size_t>(-1);
        uint64_t m_sequence = 0;
        bool m_inHeap = false;
    };

    class TimerManager
    {
        friend class Timer;

    public:
        TimerManager();
        virtual ~TimerManager();

        Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);
        Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb,
                                     std::weak_ptr<void> weak_cond, bool recurring = false);
        uint64_t getNextTimer(size_t worker);
        void listExpiredCb(size_t worker, std::vector<std::function<void()>> &cbs);
        bool hasTimer();

    protected:
        void initTimerBuckets(size_t count);
        size_t getTimerBucketCount() const { return m_buckets.size(); }
        Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recurring, size_t worker);
        virtual size_t getTimerWorkerIndex();
        virtual void onTimerInsertedAtFront(size_t worker) = 0;

    private:
        struct TimerBucket
        {
            Mutex mutex;
            std::vector<Timer::ptr> heap;
        };

        void insertTimerLocked(TimerBucket &bucket, const Timer::ptr &timer);
        void removeTimerLocked(TimerBucket &bucket, size_t index);
        void siftUpLocked(TimerBucket &bucket, size_t index);
        void siftDownLocked(TimerBucket &bucket, size_t index);
        void swapNodesLocked(TimerBucket &bucket, size_t lhs, size_t rhs);

    private:
        std::vector<std::unique_ptr<TimerBucket>> m_buckets;
        std::atomic<size_t> m_rr;
        std::atomic<uint64_t> m_sequence;
    };

}

#endif
