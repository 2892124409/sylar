/**
 * @file fiber_pool.cc
 * @brief 协程池实现
 * @author sylar.yin
 * @date 2026-03-06
 */
#include "fiber_pool.h"
#include "sylar/fiber/fiber_framework_config.h"
#include "sylar/base/util.h"
#include "sylar/fiber/scheduler.h"
#include "sylar/log/logger.h"

namespace sylar
{

    FiberPool::FiberPool()
        : m_maxPoolSize(FiberFrameworkConfig::GetFiberPoolMaxSize()),
          m_idleTimeout(FiberFrameworkConfig::GetFiberPoolIdleTimeoutMs()),
          m_minKeepSize(FiberFrameworkConfig::GetFiberPoolMinKeep()),
          m_totalAlloc(0),
          m_poolHit(0),
          m_poolMiss(0),
          m_totalReuse(0)
    {
    }

    FiberPool::~FiberPool()
    {
        // 清空所有池
        m_independentPools.clear();
        m_sharedStackPool.clear();
        m_lastUseTime.clear();
    }

    FiberPool *FiberPool::GetThreadLocal()
    {
        static thread_local FiberPool pool;
        return &pool;
    }

    size_t FiberPool::normalizeStackSize(size_t size)
    {
        static const size_t sizes[] = {
            128 * 1024, // 128KB
            256 * 1024, // 256KB
            512 * 1024, // 512KB
            1024 * 1024 // 1MB
        };

        for (size_t s : sizes)
        {
            if (size <= s)
                return s;
        }

        // 超大栈不池化
        return size;
    }

    bool FiberPool::shouldUseSharedStack() const
    {
        if (!FiberFrameworkConfig::GetFiberUseSharedStack())
        {
            return false;
        }

        Scheduler *scheduler = Scheduler::GetThis();
        return scheduler && scheduler->supportsSharedStackV1();
    }

    size_t FiberPool::getTotalPooledCount() const
    {
        size_t count = m_sharedStackPool.size();
        for (const auto &pair : m_independentPools)
        {
            count += pair.second.size();
        }
        return count;
    }

    Fiber::ptr FiberPool::acquire(std::function<void()> cb,
                                  size_t stacksize)
    {
        bool use_shared_stack = shouldUseSharedStack();

        // 检查是否启用协程池
        if (!FiberFrameworkConfig::GetFiberPoolEnabled())
        {
            ++m_totalAlloc;
            ++m_poolMiss;
            return Fiber::ptr(new Fiber(cb, stacksize, true));
        }

        ++m_totalAlloc;

        if (use_shared_stack)
        {
            // 共享栈路径
            if (!m_sharedStackPool.empty())
            {
                auto fiber = m_sharedStackPool.front();
                m_sharedStackPool.pop_front();
                m_lastUseTime.erase(fiber);

                fiber->reset(cb);
                ++m_poolHit;
                ++m_totalReuse;

                return fiber;
            }
        }
        else
        {
            // 独立栈路径
            size_t actual_size = stacksize ? stacksize : FiberFrameworkConfig::GetFiberStackSize();

            // 超大栈不池化
            if (actual_size > 1024 * 1024)
            {
                ++m_poolMiss;
                return Fiber::ptr(new Fiber(cb, actual_size, true));
            }

            size_t norm_size = normalizeStackSize(actual_size);

            auto &pool = m_independentPools[norm_size];
            if (!pool.empty())
            {
                auto fiber = pool.front();
                pool.pop_front();
                m_lastUseTime.erase(fiber);

                fiber->reset(cb);
                ++m_poolHit;
                ++m_totalReuse;

                return fiber;
            }
        }

        // 池未命中，创建新协程
        ++m_poolMiss;
        if (use_shared_stack)
        {
            return Fiber::ptr(new Fiber(cb, stacksize, true));
        }

        size_t actual_size = stacksize ? stacksize : FiberFrameworkConfig::GetFiberStackSize();
        size_t pooled_size = actual_size > 1024 * 1024 ? actual_size : normalizeStackSize(actual_size);
        return Fiber::ptr(new Fiber(cb, pooled_size, true));
    }

    void FiberPool::release(Fiber::ptr fiber)
    {
        if (!fiber)
            return;

        // 检查是否启用协程池
        if (!FiberFrameworkConfig::GetFiberPoolEnabled())
        {
            return;
        }

        // 只接受已结束的协程
        auto state = fiber->getState();
        if (state != Fiber::TERM && state != Fiber::EXCEPT)
        {
            return;
        }

        // 检查池容量
        size_t max_pool_size = FiberFrameworkConfig::GetFiberPoolMaxSize();
        size_t total_pooled = getTotalPooledCount();
        if (total_pooled >= max_pool_size)
        {
            return; // 达到上限，丢弃
        }

        // 清理回调（避免闭包持有资源）
        fiber->clearCallback();

        // 记录时间戳
        m_lastUseTime[fiber] = GetCurrentMS();

        // 放入对应池
        if (fiber->isSharedStackEnabled())
        {
            m_sharedStackPool.push_back(fiber);
        }
        else
        {
            size_t stacksize = fiber->getStackSize();
            if (stacksize > 1024 * 1024)
            {
                m_lastUseTime.erase(fiber);
                return;
            }

            stacksize = normalizeStackSize(stacksize);
            m_independentPools[stacksize].push_back(fiber);
        }
    }

    void FiberPool::shrink()
    {
        uint64_t now = GetCurrentMS();
        uint64_t idle_timeout = FiberFrameworkConfig::GetFiberPoolIdleTimeoutMs();
        size_t min_keep_size = FiberFrameworkConfig::GetFiberPoolMinKeep();

        // 清理独立栈池
        for (auto &pair : m_independentPools)
        {
            auto &pool = pair.second;
            if (pool.size() <= min_keep_size)
                continue;

            auto it = pool.begin();
            while (it != pool.end() && pool.size() > min_keep_size)
            {
                if (now - m_lastUseTime[*it] > idle_timeout)
                {
                    m_lastUseTime.erase(*it);
                    it = pool.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // 清理共享栈池
        if (m_sharedStackPool.size() > min_keep_size)
        {
            auto it = m_sharedStackPool.begin();
            while (it != m_sharedStackPool.end() && m_sharedStackPool.size() > min_keep_size)
            {
                if (now - m_lastUseTime[*it] > idle_timeout)
                {
                    m_lastUseTime.erase(*it);
                    it = m_sharedStackPool.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    FiberPool::PoolStats FiberPool::getStats() const
    {
        PoolStats stats;
        stats.total_alloc = m_totalAlloc;
        stats.pool_hit = m_poolHit;
        stats.pool_miss = m_poolMiss;
        stats.total_reuse = m_totalReuse;

        // 计算当前池中数量
        stats.shared_pooled = m_sharedStackPool.size();
        stats.independent_pooled = 0;
        for (const auto &pair : m_independentPools)
        {
            stats.independent_pooled += pair.second.size();
        }
        stats.current_pooled = stats.independent_pooled + stats.shared_pooled;

        // 计算命中率
        if (m_totalAlloc > 0)
        {
            stats.hit_rate = (double)m_poolHit / m_totalAlloc * 100.0;
        }

        return stats;
    }

    void FiberPool::resetStats()
    {
        m_totalAlloc = 0;
        m_poolHit = 0;
        m_poolMiss = 0;
        m_totalReuse = 0;
    }

    void FiberPool::setMaxPoolSize(size_t size)
    {
        m_maxPoolSize = size;
        FiberFrameworkConfig::SetFiberPoolMaxSize(static_cast<uint32_t>(size));
    }

    void FiberPool::setIdleTimeout(uint64_t ms)
    {
        m_idleTimeout = ms;
        FiberFrameworkConfig::SetFiberPoolIdleTimeoutMs(static_cast<uint32_t>(ms));
    }

    void FiberPool::setMinKeepSize(size_t size)
    {
        m_minKeepSize = size;
        FiberFrameworkConfig::SetFiberPoolMinKeep(static_cast<uint32_t>(size));
    }

} // namespace sylar
