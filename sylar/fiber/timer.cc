#include "timer.h"
#include "sylar/base/util.h"
#include "sylar/base/macro.h"

namespace sylar
{

    /**
     * @brief 定时器比较仿函数的实现
     * @details
     * 1. 比较绝对过期时间。
     * 2. 如果时间相同，则比较指针地址，确保两个不同的定时器对象在 set 中共存。
     */
    bool Timer::Comparator::operator()(const Timer::ptr &lhs,
                                       const Timer::ptr &rhs) const
    {
        if (!lhs && !rhs)
        {
            return false;
        }
        if (!lhs)
        {
            return true; // lhs 为空认为小
        }
        if (!rhs)
        {
            return false; // rhs 为空认为大
        }
        // 核心：按绝对过期时间升序排列
        if (lhs->m_next < rhs->m_next)
        {
            return true;
        }
        if (rhs->m_next < lhs->m_next)
        {
            return false;
        }
        // 重要：时间相同时，利用指针地址做去重判断，防止 set 误删
        return lhs.get() < rhs.get();
    }

    Timer::Timer(uint64_t ms, std::function<void()> cb,
                 bool recurring, TimerManager *manager)
        : m_recurring(recurring), m_ms(ms), m_cb(cb), m_manager(manager)
    {
        // 初始化时计算绝对过期时间
        m_next = sylar::GetCurrentMS() + m_ms;
    }

    Timer::Timer(uint64_t next)
        : m_next(next)
    {
        // 用于 lower_bound 查找的临时空对象
    }

    /**
     * @brief 取消定时器
     */
    bool Timer::cancel()
    {
        TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
        if (m_cb)
        {
            m_cb = nullptr; // 销毁回调，打破循环引用
            auto it = m_manager->m_timers.find(shared_from_this());
            if (it != m_manager->m_timers.end())
            {
                m_manager->m_timers.erase(it);
            }
            return true;
        }
        return false;
    }

    /**
     * @brief 刷新定时器
     * @details 将定时器的执行时间顺延一个周期
     */
    bool Timer::refresh()
    {
        TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
        if (!m_cb)
        {
            return false;
        }
        auto it = m_manager->m_timers.find(shared_from_this());
        if (it == m_manager->m_timers.end())
        {
            return false;
        }
        // 必须先删除再插入，因为 m_next 改变了，会导致在 set 中的位置变化
        m_manager->m_timers.erase(it);
        m_next = sylar::GetCurrentMS() + m_ms;
        m_manager->m_timers.insert(shared_from_this());
        return true;
    }

    /**
     * @brief 重置定时器时间
     */
    bool Timer::reset(uint64_t ms, bool from_now)
    {
        if (ms == m_ms && !from_now)
        {
            return true;
        }
        TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
        if (!m_cb)
        {
            return false;
        }
        auto it = m_manager->m_timers.find(shared_from_this());
        if (it == m_manager->m_timers.end())
        {
            return false;
        }
        m_manager->m_timers.erase(it);

        uint64_t start = 0;
        if (from_now)
        {
            start = sylar::GetCurrentMS();
        }
        else
        {
            // 基于原定的开始时间计算
            start = m_next - m_ms;
        }
        m_ms = ms;
        m_next = start + m_ms;
        // 重新通过管理器添加（涉及通知逻辑）
        m_manager->addTimer(shared_from_this(), lock);
        return true;
    }

    TimerManager::TimerManager()
    {
        m_previouseTime = sylar::GetCurrentMS();
    }

    TimerManager::~TimerManager()
    {
    }

    /**
     * @brief 添加普通的定时器
     */
    Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring)
    {
        Timer::ptr timer(new Timer(ms, cb, recurring, this));
        RWMutexType::WriteLock lock(m_mutex);
        addTimer(timer, lock);
        return timer;
    }

    /**
     * @brief 条件定时器回调包装
     */
    static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb)
    {
        // 尝试将弱引用提升为强引用
        std::shared_ptr<void> tmp = weak_cond.lock();
        if (tmp)
        {
            // 对象依然存活，执行回调
            cb();
        }
    }

    /**
     * @brief 添加条件定时器
     * @details 只有当 weak_cond 指向的对象未销毁时，才执行 cb
     */
    Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb,
                                               std::weak_ptr<void> weak_cond,
                                               bool recurring)
    {
        // 利用 bind 将条件检查封装进回调
        return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
    }

    /**
     * @brief 获取距离下一个定时器触发还有多久
     * @return 毫秒数。如果没有定时器返回最大值；如果已过期返回 0
     */
    uint64_t TimerManager::getNextTimer()
    {
        RWMutexType::ReadLock lock(m_mutex);
        if (m_timers.empty())
        {
            return ~0ull; // 返回 0xFFFFFF...
        }
        uint64_t now_ms = sylar::GetCurrentMS();
        const Timer::ptr &next = *m_timers.begin(); // 取出最早的
        if (now_ms >= next->m_next)
        {
            return 0; // 已经过期了，应该立刻处理
        }
        else
        {
            return next->m_next - now_ms; // 还剩多久
        }
    }

    /**
     * @brief 核心：获取所有过期的任务回调
     */
    void TimerManager::listExpiredCb(std::vector<std::function<void()>> &cbs)
    {
        uint64_t now_ms = sylar::GetCurrentMS();
        std::vector<Timer::ptr> expired;
        {
            RWMutexType::ReadLock lock(m_mutex);
            if (m_timers.empty())
            {
                return;
            }
        }
        RWMutexType::WriteLock lock(m_mutex);
        if (m_timers.empty())
        {
            return;
        }

        // 1. 检查系统时间是否回退（校时异常处理）
        bool rollover = detectClockRollover(now_ms);
        // 如果没校时，且还没到第一个定时器的时间，直接返回
        if (!rollover && ((*m_timers.begin())->m_next > now_ms))
        {
            return;
        }

        Timer::ptr now_timer(new Timer(now_ms));
        /**
         * 2. 确定过期范围：
         * - 如果时间被往回调了，认为全部定时器都“由于时空混乱”而立即过期。
         * - 否则，找到所有小于等于当前时间的定时器。
         */
        auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);
        while (it != m_timers.end() && (*it)->m_next == now_ms)
        {
            ++it;
        }

        // 3. 将过期的定时器从 set 中剥离
        expired.insert(expired.begin(), m_timers.begin(), it);
        m_timers.erase(m_timers.begin(), it);
        cbs.reserve(expired.size());

        // 4. 处理循环定时器
        for (auto &timer : expired)
        {
            cbs.push_back(timer->m_cb);
            if (timer->m_recurring)
            {
                // 重新计算下一次执行时间，并放回队列
                timer->m_next = now_ms + timer->m_ms;
                m_timers.insert(timer);
            }
            else
            {
                // 一次性任务，清理回调释放内存
                timer->m_cb = nullptr;
            }
        }
    }

    /**
     * @brief 内部实现：将定时器插入容器
     */
    void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock &lock)
    {
        auto it = m_timers.insert(val).first;
        // 如果插入的位置是集合的最前面，说明它成了“全村最早过期的”
        bool at_front = (it == m_timers.begin());
        if (at_front)
        {
            // 调用虚函数，通知 IOManager 重新设置 epoll_wait 超时
            onTimerInsertedAtFront();
        }
    }

    /**
     * @brief 检测系统时间是否回退
     */
    bool TimerManager::detectClockRollover(uint64_t now_ms)
    {
        bool rollover = false;
        // 如果当前时间比上次记录时间小了 1 小时以上，认为发生了手动校时
        if (now_ms < m_previouseTime &&
            now_ms < (m_previouseTime - 60 * 60 * 1000))
        {
            rollover = true;
        }
        m_previouseTime = now_ms;
        return rollover;
    }

    /**
     * @brief 是否还有待处理的定时器
     */
    bool TimerManager::hasTimer()
    {
        RWMutexType::ReadLock lock(m_mutex);
        return !m_timers.empty();
    }

}