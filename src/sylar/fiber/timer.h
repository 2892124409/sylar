/**
 * @file timer.h
 * @brief 定时器模块
 */
#ifndef __SYLAR_TIMER_H__
#define __SYLAR_TIMER_H__

#include <memory>
#include <vector>
#include <set>
#include <functional>
#include "sylar/concurrency/mutex/rw_mutex.h"

namespace sylar
{

    class TimerManager;

    /**
     * @brief 定时器类
     */
    class Timer : public std::enable_shared_from_this<Timer>
    {
        friend class TimerManager;

    public:
        typedef std::shared_ptr<Timer> ptr;

        /**
         * @brief 取消定时器
         */
        bool cancel();

        /**
         * @brief 刷新定时器的执行时间
         */
        bool refresh();

        /**
         * @brief 重置定时器时间
         * @param[in] ms 定时器执行间隔(毫秒)
         * @param[in] from_now 是否从当前时间开始计算
         */
        bool reset(uint64_t ms, bool from_now);

    private:
        /**
         * @brief 构造函数
         * @param[in] ms 执行频率(毫秒)
         * @param[in] cb 回调函数
         * @param[in] recurring 是否循环
         * @param[in] manager 管理器
         */
        Timer(uint64_t ms, std::function<void()> cb,
              bool recurring, TimerManager *manager);

        /**
         * @brief 按照过期时间构造的空定时器
         * @param[in] next 过期时间戳(毫秒)
         */
        Timer(uint64_t next);

    private:
        /// 是否循环执行
        bool m_recurring = false;
        /// 执行周期(ms)
        uint64_t m_ms = 0;
        /// 绝对过期时间点(ms)
        uint64_t m_next = 0;
        /// 回调函数
        std::function<void()> m_cb;
        /// 管理器指针
        TimerManager *m_manager = nullptr;

    private:
        /**
         * @brief 定时器比较仿函数
         * @details 按照绝对过期时间进行升序排列
         */
        struct Comparator
        {
            bool operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const;
        };
    };

    /**
     * @brief 定时器管理器类
     */
    class TimerManager
    {
        friend class Timer;

    public:
        typedef RWMutex RWMutexType;

        TimerManager();
        virtual ~TimerManager();

        /**
         * @brief 添加定时器
         * @param[in] ms 定时器执行间隔(ms)
         * @param[in] cb 回调函数
         * @param[in] recurring 是否循环执行
         */
        Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);

        /**
         * @brief 添加条件定时器
         * @param[in] ms 定时器执行间隔(ms)
         * @param[in] cb 回调函数
         * @param[in] weak_cond 条件变量（通常是某个对象的弱引用，对象销毁则定时器失效）
         * @param[in] recurring 是否循环执行
         */
        Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false);

        /**
         * @brief 获取距离最近一个定时器执行的时间间隔(ms)
         */
        uint64_t getNextTimer();

        /**
         * @brief 获取所有已过期的回调函数
         * @param[out] cbs 存放过期的回调函数
         */
        void listExpiredCb(std::vector<std::function<void()>> &cbs);

        /**
         * @brief 是否有定时器
         */
        bool hasTimer();

    protected:
        /**
         * @brief 当新的定时器插入到集合的最前面时，执行该函数
         * @details 这是一个虚函数，由 IOManager 实现，用于唤醒 epoll_wait
         */
        virtual void onTimerInsertedAtFront() = 0;

        /**
         * @brief 将定时器添加到管理器中
         */
        void addTimer(Timer::ptr val, RWMutexType::WriteLock &lock);

    private:
        /**
         * @brief 检测服务器时间是否被调后了（防止定时器永久失效）
         */
        bool detectClockRollover(uint64_t now_ms);

    private:
        RWMutexType m_mutex;
        /// 定时器集合，使用 set 自动排序（内部是平衡二叉树，查找/插入复杂度 O(logN)）
        std::set<Timer::ptr, Timer::Comparator> m_timers;
        /// 上次检查时间（用于检测时间翻转）
        uint64_t m_previouseTime = 0;
    };

}

#endif
