/**
 * @file fiber_pool.h
 * @brief 协程池 - 复用已完成的协程对象，减少创建销毁开销
 * @author sylar.yin
 * @date 2026-03-06
 */
#ifndef __SYLAR_FIBER_POOL_H__
#define __SYLAR_FIBER_POOL_H__

#include "fiber.h"
#include <map>
#include <list>
#include <unordered_map>
#include <memory>

namespace sylar
{

    /**
     * @brief 协程池 - 线程本地池，支持独立栈和共享栈两种模式
     */
    class FiberPool
    {
    public:
        /**
         * @brief 协程池统计信息
         */
        struct PoolStats
        {
            uint64_t total_alloc = 0;      // 总分配次数
            uint64_t pool_hit = 0;         // 池命中次数
            uint64_t pool_miss = 0;        // 池未命中次数
            uint64_t total_reuse = 0;      // 总复用次数
            uint64_t current_pooled = 0;   // 当前池中数量
            double hit_rate = 0.0;         // 命中率

            // 分类统计
            uint64_t independent_pooled = 0; // 独立栈池中数量
            uint64_t shared_pooled = 0;      // 共享栈池中数量
        };

        /**
         * @brief 获取线程本地协程池实例
         */
        static FiberPool *GetThreadLocal();

        /**
         * @brief 从池中获取协程（优先复用）
         * @param cb 协程执行函数
         * @param stacksize 栈大小（0表示使用默认值）
         * @return 协程智能指针
         */
        Fiber::ptr acquire(std::function<void()> cb,
                           size_t stacksize = 0);

        /**
         * @brief 归还协程到池
         * @param fiber 协程智能指针
         * @note 只接受TERM或EXCEPT状态的协程
         */
        void release(Fiber::ptr fiber);

        /**
         * @brief 清理空闲协程
         * @note 清理超过idle_timeout的协程，保留min_keep个
         */
        void shrink();

        /**
         * @brief 获取统计信息
         */
        PoolStats getStats() const;

        /**
         * @brief 重置统计信息
         */
        void resetStats();

        /**
         * @brief 设置最大池大小
         */
        void setMaxPoolSize(size_t size) { m_maxPoolSize = size; }

        /**
         * @brief 设置空闲超时时间（毫秒）
         */
        void setIdleTimeout(uint64_t ms) { m_idleTimeout = ms; }

        /**
         * @brief 设置最小保留数量
         */
        void setMinKeepSize(size_t size) { m_minKeepSize = size; }

    private:
        FiberPool();
        ~FiberPool();

        /**
         * @brief 标准化栈大小
         * @note 向上取整到128KB/256KB/512KB/1MB
         */
        size_t normalizeStackSize(size_t size);

        /**
         * @brief 当前环境下是否应走共享栈池路径
         */
        bool shouldUseSharedStack() const;

        /**
         * @brief 获取当前池中总数量
         */
        size_t getTotalPooledCount() const;

    private:
        // 独立栈协程池（按栈大小分桶）
        std::map<size_t, std::list<Fiber::ptr>> m_independentPools;

        // 共享栈协程池
        std::list<Fiber::ptr> m_sharedStackPool;

        // 时间戳记录（用于LRU清理）
        std::unordered_map<Fiber::ptr, uint64_t> m_lastUseTime;

        // 配置参数
        size_t m_maxPoolSize;
        uint64_t m_idleTimeout;
        size_t m_minKeepSize;

        // 统计信息
        uint64_t m_totalAlloc;
        uint64_t m_poolHit;
        uint64_t m_poolMiss;
        uint64_t m_totalReuse;
    };

} // namespace sylar

#endif
