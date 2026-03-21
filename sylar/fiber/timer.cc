#include "timer.h"
#include <algorithm>
#include <ctime>
#include "sylar/base/macro.h"

namespace sylar
{

    namespace
    {
        uint64_t GetMonotonicMS()
        {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return static_cast<uint64_t>(ts.tv_sec) * 1000ull +
                   static_cast<uint64_t>(ts.tv_nsec) / 1000000ull;
        }

    }

    Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring,
                 TimerManager *manager, size_t worker, uint64_t next, uint64_t sequence)
        : m_recurring(recurring),
          m_ms(ms),
          m_next(next),
          m_cb(std::move(cb)),
          m_manager(manager),
          m_worker(worker),
          m_sequence(sequence)
    {
    }

    bool Timer::cancel()
    {
        if (!m_manager || !m_inHeap)
        {
            return false;
        }

        TimerManager::TimerBucket &bucket = *m_manager->m_buckets[m_worker];
        bool removed = false;
        {
            Mutex::Lock lock(bucket.mutex);
            if (m_inHeap)
            {
                m_manager->removeTimerLocked(bucket, m_heapIndex);
                removed = true;
            }
        }

        if (removed)
        {
            m_cb = nullptr;
        }
        return removed;
    }

    bool Timer::refresh()
    {
        if (!m_manager || !m_cb)
        {
            return false;
        }

        TimerManager::TimerBucket &bucket = *m_manager->m_buckets[m_worker];
        bool at_front = false;
        {
            Mutex::Lock lock(bucket.mutex);
            if (!m_inHeap)
            {
                return false;
            }

            m_manager->removeTimerLocked(bucket, m_heapIndex);
            m_next = GetMonotonicMS() + m_ms;
            m_sequence = m_manager->m_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            m_manager->insertTimerLocked(bucket, shared_from_this());
            at_front = (m_heapIndex == 0);
        }

        if (at_front)
        {
            m_manager->onTimerInsertedAtFront(m_worker);
        }
        return true;
    }

    bool Timer::reset(uint64_t ms, bool from_now)
    {
        if (!m_manager || !m_cb)
        {
            return false;
        }

        TimerManager::TimerBucket &bucket = *m_manager->m_buckets[m_worker];
        bool at_front = false;
        {
            Mutex::Lock lock(bucket.mutex);
            if (!m_inHeap)
            {
                return false;
            }

            uint64_t start = from_now ? GetMonotonicMS() : (m_next - m_ms);
            m_manager->removeTimerLocked(bucket, m_heapIndex);
            m_ms = ms;
            m_next = start + m_ms;
            m_sequence = m_manager->m_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            m_manager->insertTimerLocked(bucket, shared_from_this());
            at_front = (m_heapIndex == 0);
        }

        if (at_front)
        {
            m_manager->onTimerInsertedAtFront(m_worker);
        }
        return true;
    }

    TimerManager::TimerManager()
        : m_rr(0), m_sequence(0)
    {
        initTimerBuckets(1);
    }

    TimerManager::~TimerManager()
    {
    }

    void TimerManager::initTimerBuckets(size_t count)
    {
        if (count == 0)
        {
            count = 1;
        }

        m_buckets.clear();
        m_buckets.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            m_buckets.emplace_back(new TimerBucket());
        }
    }

    size_t TimerManager::getTimerWorkerIndex()
    {
        return m_rr.fetch_add(1, std::memory_order_relaxed) % m_buckets.size();
    }

    Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring)
    {
        return addTimer(ms, std::move(cb), recurring, getTimerWorkerIndex());
    }

    Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb,
                                      bool recurring, size_t worker)
    {
        if (m_buckets.empty())
        {
            initTimerBuckets(1);
        }
        worker %= m_buckets.size();

        uint64_t now = GetMonotonicMS();
        Timer::ptr timer(new Timer(ms, std::move(cb), recurring, this, worker,
                                   now + ms,
                                   m_sequence.fetch_add(1, std::memory_order_relaxed) + 1));

        TimerBucket &bucket = *m_buckets[worker];
        bool at_front = false;
        {
            Mutex::Lock lock(bucket.mutex);
            insertTimerLocked(bucket, timer);
            at_front = (timer->m_heapIndex == 0);
        }

        if (at_front)
        {
            onTimerInsertedAtFront(worker);
        }
        return timer;
    }

    static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb)
    {
        std::shared_ptr<void> tmp = weak_cond.lock();
        if (tmp)
        {
            cb();
        }
    }

    Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb,
                                               std::weak_ptr<void> weak_cond,
                                               bool recurring)
    {
        return addTimer(ms, std::bind(&OnTimer, weak_cond, std::move(cb)), recurring);
    }

    uint64_t TimerManager::getNextTimer(size_t worker)
    {
        if (worker >= m_buckets.size())
        {
            return ~0ull;
        }

        TimerBucket &bucket = *m_buckets[worker];
        Mutex::Lock lock(bucket.mutex);
        if (bucket.heap.empty())
        {
            return ~0ull;
        }

        uint64_t now = GetMonotonicMS();
        if (bucket.heap.front()->m_next <= now)
        {
            return 0;
        }
        return bucket.heap.front()->m_next - now;
    }

    void TimerManager::listExpiredCb(size_t worker, std::vector<std::function<void()>> &cbs)
    {
        if (worker >= m_buckets.size())
        {
            return;
        }

        TimerBucket &bucket = *m_buckets[worker];
        std::vector<Timer::ptr> expired;
        uint64_t now = GetMonotonicMS();
        {
            Mutex::Lock lock(bucket.mutex);
            while (!bucket.heap.empty() && bucket.heap.front()->m_next <= now)
            {
                Timer::ptr timer = bucket.heap.front();
                removeTimerLocked(bucket, 0);
                expired.push_back(timer);
            }
        }

        cbs.reserve(cbs.size() + expired.size());
        for (size_t i = 0; i < expired.size(); ++i)
        {
            Timer::ptr &timer = expired[i];
            if (!timer->m_cb)
            {
                continue;
            }

            cbs.push_back(timer->m_cb);
            if (timer->m_recurring)
            {
                timer->m_next = now + timer->m_ms;
                timer->m_sequence = m_sequence.fetch_add(1, std::memory_order_relaxed) + 1;

                TimerBucket &resched_bucket = *m_buckets[timer->m_worker];
                Mutex::Lock lock(resched_bucket.mutex);
                insertTimerLocked(resched_bucket, timer);
            }
            else
            {
                timer->m_cb = nullptr;
            }
        }
    }

    bool TimerManager::hasTimer()
    {
        for (size_t i = 0; i < m_buckets.size(); ++i)
        {
            TimerBucket &bucket = *m_buckets[i];
            Mutex::Lock lock(bucket.mutex);
            if (!bucket.heap.empty())
            {
                return true;
            }
        }
        return false;
    }

    void TimerManager::insertTimerLocked(TimerBucket &bucket, const Timer::ptr &timer)
    {
        timer->m_inHeap = true;
        timer->m_heapIndex = bucket.heap.size();
        bucket.heap.push_back(timer);
        siftUpLocked(bucket, timer->m_heapIndex);
    }

    void TimerManager::removeTimerLocked(TimerBucket &bucket, size_t index)
    {
        if (index >= bucket.heap.size())
        {
            return;
        }

        Timer::ptr removed = bucket.heap[index];
        size_t last = bucket.heap.size() - 1;
        if (index != last)
        {
            swapNodesLocked(bucket, index, last);
        }

        bucket.heap.pop_back();
        removed->m_heapIndex = static_cast<size_t>(-1);
        removed->m_inHeap = false;

        if (index < bucket.heap.size())
        {
            siftDownLocked(bucket, index);
            siftUpLocked(bucket, index);
        }
    }

    void TimerManager::siftUpLocked(TimerBucket &bucket, size_t index)
    {
        while (index > 0)
        {
            size_t parent = (index - 1) / 2;
            const Timer::ptr &cur = bucket.heap[index];
            const Timer::ptr &par = bucket.heap[parent];
            if (cur->m_next > par->m_next ||
                (cur->m_next == par->m_next && cur->m_sequence >= par->m_sequence))
            {
                break;
            }
            swapNodesLocked(bucket, index, parent);
            index = parent;
        }
    }

    void TimerManager::siftDownLocked(TimerBucket &bucket, size_t index)
    {
        size_t size = bucket.heap.size();
        while (true)
        {
            size_t left = index * 2 + 1;
            if (left >= size)
            {
                break;
            }

            size_t smallest = left;
            size_t right = left + 1;
            if (right < size)
            {
                const Timer::ptr &rhs = bucket.heap[right];
                const Timer::ptr &lhs = bucket.heap[left];
                if (rhs->m_next < lhs->m_next ||
                    (rhs->m_next == lhs->m_next && rhs->m_sequence < lhs->m_sequence))
                {
                    smallest = right;
                }
            }

            const Timer::ptr &child = bucket.heap[smallest];
            const Timer::ptr &cur = bucket.heap[index];
            if (child->m_next > cur->m_next ||
                (child->m_next == cur->m_next && child->m_sequence >= cur->m_sequence))
            {
                break;
            }

            swapNodesLocked(bucket, index, smallest);
            index = smallest;
        }
    }

    void TimerManager::swapNodesLocked(TimerBucket &bucket, size_t lhs, size_t rhs)
    {
        if (lhs == rhs)
        {
            return;
        }

        std::swap(bucket.heap[lhs], bucket.heap[rhs]);
        bucket.heap[lhs]->m_heapIndex = lhs;
        bucket.heap[rhs]->m_heapIndex = rhs;
    }

}
