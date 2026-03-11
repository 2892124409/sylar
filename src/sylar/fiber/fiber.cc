#include "fiber.h"
#include "scheduler.h"
#include "sylar/fiber/fiber_framework_config.h"
#include "sylar/base/macro.h"
#include "sylar/base/util.h"
#include "sylar/fiber/save_buffer_allocator.h"
#include "sylar/fiber/thread_local_stack.h"
#include <atomic>
#include <cstring>
#include <sstream>

namespace sylar
{

    // 全局静态变量，用于生成协程 ID 和统计协程总数，因为没加thread_local所以统计的是当前进程中的协程数和 ID
    static std::atomic<uint64_t> s_fiber_id{0};
    static std::atomic<uint64_t> s_fiber_count{0};
    static std::atomic<uint64_t> s_shared_bind_count{0};
    static std::atomic<uint64_t> s_shared_prepare_count{0};
    static std::atomic<uint64_t> s_shared_finalize_count{0};
    static std::atomic<uint64_t> s_shared_save_count{0};
    static std::atomic<uint64_t> s_shared_restore_count{0};
    static std::atomic<uint64_t> s_shared_acquire_fail_count{0};
    static std::atomic<uint64_t> s_shared_unsupported_mode_count{0};

    // 线程局部变量：当前线程正在运行的协程
    static thread_local Fiber *t_fiber = nullptr;
    // 线程局部变量：当前线程的主协程（调度协程）
    static thread_local Fiber::ptr t_thread_fiber = nullptr;

    /**
     * @brief 简单的栈内存分配器（后续可优化为内存池）
     */
    class StackAllocator
    {
    public:
        static void *Alloc(size_t size)
        {
            return malloc(size);
        }
        static void Dealloc(void *vp, size_t size)
        {
            return free(vp);
        }
    };

    /**
     * @brief 私有构造函数：用于包装当前线程的原始执行流为“主协程”
     * @details 只有 GetThis() 在发现当前线程没有协程时才会调用
     */
    Fiber::Fiber()
    {
        m_state = EXEC; // 主协程一创建就在运行中
        SetThis(this);  // 设置当前正在运行的协程

        // getcontext: 抓取当前 CPU 上下文存入 当前Fiber实例的私有成员变量m_ctx
        if (getcontext(&m_ctx))
        {
            SYLAR_ASSERT2(false, "getcontext");
        }

        ++s_fiber_count;     // 统计当前进程中活着的协程总数
        m_id = ++s_fiber_id; // 让每个协程分配一个全局唯一的 ID
        SYLAR_LOG_DEBUG(SYLAR_LOG_ROOT()) << "Fiber::Fiber main id=" << m_id;
    }

    /**
     * @brief 公有构造函数：用于创建实际执行业务逻辑的子协程
     */
    Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler)
        : m_id(++s_fiber_id), m_cb(cb), m_runInScheduler(run_in_scheduler)
    {
        ++s_fiber_count;
        bool want_shared_stack = FiberFrameworkConfig::GetFiberUseSharedStack() && run_in_scheduler;
        if (want_shared_stack)
        {
            Scheduler *scheduler = Scheduler::GetThis();
            if (!scheduler || !scheduler->supportsSharedStackV1())
            {
                ++s_shared_unsupported_mode_count;
                SYLAR_LOG_WARN(SYLAR_LOG_ROOT())
                    << "fiber.use_shared_stack is enabled, but current scheduler mode is not validated for V1; "
                    << "fallback to independent stack. fiber_id=" << m_id;
                want_shared_stack = false;
            }
        }
        m_useSharedStack = want_shared_stack;
        m_stacksize = stacksize ? stacksize : FiberFrameworkConfig::GetFiberStackSize();

        if (m_useSharedStack)
        {
            m_stacksize = FiberFrameworkConfig::GetFiberSharedStackSize();
            SYLAR_ASSERT2(ThreadLocalSharedStack::SetStackSize(m_stacksize),
                          "shared stack size must remain globally consistent");
            SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "Fiber::Fiber shared-stack V1 enabled id=" << m_id;
            return;
        }

        // 1. 为子协程分配独立的栈空间
        m_stack = StackAllocator::Alloc(m_stacksize);

        // 2. 获取当前上下文副本，作为初始模板
        if (getcontext(&m_ctx))
        {
            SYLAR_ASSERT2(false, "getcontext");
        }

        // 3. 修改上下文：指定该协程运行完后没有后继上下文（uc_link = nullptr）
        m_ctx.uc_link = nullptr;
        // 4. 修改上下文：指向我们分配的私有栈
        m_ctx.uc_stack.ss_sp = m_stack;
        m_ctx.uc_stack.ss_size = m_stacksize;

        // 5. makecontext: 修改上下文，使其入口指向静态函数 MainFunc
        makecontext(&m_ctx, &Fiber::MainFunc, 0);

        SYLAR_LOG_DEBUG(SYLAR_LOG_ROOT()) << "Fiber::Fiber sub id=" << m_id;
    }

    Fiber::~Fiber()
    {
        --s_fiber_count;
        if (m_savedStackBuf)
        {
            SaveBufferAllocator::Dealloc(m_savedStackBuf, m_savedStackLen);
            m_savedStackBuf = nullptr;
            m_savedStackLen = 0;
            m_savedStackOffset = 0;
        }
        if (m_sharedStack)
        {
            ThreadLocalSharedStack::GetInstance()->release(m_sharedStack);
            m_sharedStack = nullptr;
        }
        if (m_useSharedStack)
        {
            // 共享栈 Fiber 本身不拥有独立栈，但也不是线程主协程
            SYLAR_ASSERT(m_state == TERM || m_state == EXCEPT || m_state == INIT || m_state == READY || m_state == HOLD);
        }
        else if (m_stack)
        {
            // 子协程：必须在 TERM 或 EXCEPT 状态下析构，并释放栈内存
            SYLAR_ASSERT(m_state == TERM || m_state == EXCEPT || m_state == INIT);
            StackAllocator::Dealloc(m_stack, m_stacksize);
        }
        else
        {
            // 主协程：没有栈（使用线程原始栈），且必须在运行中析构
            SYLAR_ASSERT(!m_cb);
            SYLAR_ASSERT(m_state == EXEC);

            Fiber *cur = t_fiber;
            if (cur == this)
            {
                SetThis(nullptr);
            }
        }
        SYLAR_LOG_DEBUG(SYLAR_LOG_ROOT()) << "Fiber::~Fiber id=" << m_id
                                          << " total=" << s_fiber_count;
    }

    /**
     * @brief 重置协程：利用已有的栈空间重新绑定业务函数
     * @details 目的是减少内存分配/释放的开销（对象池思想）
     */
    void Fiber::reset(std::function<void()> cb)
    {
        SYLAR_ASSERT(m_stack || m_useSharedStack); // 子协程才能 reset
        SYLAR_ASSERT(m_state == TERM || m_state == EXCEPT || m_state == INIT);
        m_cb = cb;

        if (m_useSharedStack)
        {
            if (m_savedStackBuf)
            {
                SaveBufferAllocator::Dealloc(m_savedStackBuf, m_savedStackLen);
                m_savedStackBuf = nullptr;
            }
            if (m_sharedStack)
            {
                ThreadLocalSharedStack::GetInstance()->release(m_sharedStack);
                m_sharedStack = nullptr;
            }
            m_savedStackLen = 0;
            m_savedStackOffset = 0;
            m_needSharedStackFinalize = false;
            m_ctxInited = false;
            m_boundThread = -1;
            m_state = READY;
            return;
        }

        if (getcontext(&m_ctx))
        {
            SYLAR_ASSERT2(false, "getcontext");
        }

        m_ctx.uc_link = nullptr;
        m_ctx.uc_stack.ss_sp = m_stack;
        m_ctx.uc_stack.ss_size = m_stacksize;

        makecontext(&m_ctx, &Fiber::MainFunc, 0);
        m_state = READY;
    }

    void Fiber::initSharedStackContext()
    {
        SYLAR_ASSERT(m_useSharedStack);
        SYLAR_ASSERT(m_sharedStack);
        SYLAR_ASSERT(!m_ctxInited);

        if (getcontext(&m_ctx))
        {
            SYLAR_ASSERT2(false, "getcontext");
        }

        m_ctx.uc_link = nullptr;
        m_ctx.uc_stack.ss_sp = m_sharedStack;
        m_ctx.uc_stack.ss_size = FiberFrameworkConfig::GetFiberSharedStackSize();
        makecontext(&m_ctx, &Fiber::MainFunc, 0);
        m_ctxInited = true;
    }

    void Fiber::onSchedulerBeforeResume()
    {
        if (!m_useSharedStack)
        {
            return;
        }
        ++s_shared_prepare_count;
        prepareSharedStackBeforeResume();
    }

    void Fiber::onSchedulerAfterResume()
    {
        if (!m_useSharedStack)
        {
            return;
        }
        ++s_shared_finalize_count;
        finalizeSharedStackAfterYield();
    }

    void Fiber::ensureSharedStackBinding()
    {
        SYLAR_ASSERT(m_useSharedStack);
        int tid = sylar::GetThreadId();
        if (m_boundThread == -1)
        {
            m_boundThread = tid;
            ++s_shared_bind_count;
            return;
        }
        SYLAR_ASSERT2(m_boundThread == tid, "shared-stack fiber resumed on wrong thread");
    }

    void Fiber::prepareSharedStackBeforeResume()
    {
        SYLAR_ASSERT(m_useSharedStack);
        ensureSharedStackBinding();

        if (!m_sharedStack)
        {
            m_sharedStack = ThreadLocalSharedStack::GetInstance()->acquire();
        }
        if (!m_sharedStack)
        {
            ++s_shared_acquire_fail_count;
        }
        SYLAR_ASSERT2(m_sharedStack, "shared stack unavailable before resume");

        if (!m_ctxInited)
        {
            initSharedStackContext();
            return;
        }

        restoreSharedStackFromBuffer();
    }

    size_t Fiber::calculateStackUsage() const
    {
        if (!m_sharedStack)
        {
            return 0;
        }

        char marker;
        const char *stack_base = static_cast<const char *>(m_sharedStack);
        const char *stack_top = stack_base + ThreadLocalSharedStack::GetStackSize();
        const char *current_sp = &marker;

        if (current_sp >= stack_top || current_sp < stack_base)
        {
            return 0;
        }

        size_t used = static_cast<size_t>(stack_top - current_sp);
        const size_t align = 4096;
        return (used + align - 1) & ~(align - 1);
    }

    void Fiber::saveSharedStack()
    {
        if (!m_useSharedStack || !m_sharedStack)
        {
            return;
        }

        size_t used = calculateStackUsage();
        if (used == 0)
        {
            ThreadLocalSharedStack::GetInstance()->release(m_sharedStack);
            m_sharedStack = nullptr;
            return;
        }

        void *buf = SaveBufferAllocator::Alloc(used);
        if (!buf)
        {
            return;
        }

        std::memcpy(buf, m_sharedStack, used);
        if (m_savedStackBuf)
        {
            SaveBufferAllocator::Dealloc(m_savedStackBuf, m_savedStackLen);
        }
        m_savedStackBuf = buf;
        m_savedStackLen = used;
        ThreadLocalSharedStack::GetInstance()->release(m_sharedStack);
        m_sharedStack = nullptr;
    }

    void Fiber::saveSharedStackToBuffer()
    {
        SYLAR_ASSERT(m_useSharedStack);
        SYLAR_ASSERT(m_sharedStack);

        if (m_savedStackLen == 0)
        {
            return;
        }

        void *buf = SaveBufferAllocator::Alloc(m_savedStackLen);
        SYLAR_ASSERT2(buf, "save buffer allocation failed");

        char *base = static_cast<char *>(m_sharedStack);
        std::memcpy(buf, base + m_savedStackOffset, m_savedStackLen);

        if (m_savedStackBuf)
        {
            SaveBufferAllocator::Dealloc(m_savedStackBuf, m_savedStackLen);
        }
        m_savedStackBuf = buf;
        ++s_shared_save_count;
    }

    void Fiber::restoreSharedStack()
    {
        if (!m_useSharedStack)
        {
            return;
        }

        if (m_boundThread != -1 && m_boundThread != sylar::GetThreadId())
        {
            SYLAR_LOG_WARN(SYLAR_LOG_ROOT())
                << "shared-stack fiber bound to thread " << m_boundThread
                << ", but resumed on thread " << sylar::GetThreadId();
            return;
        }

        void *stack = ThreadLocalSharedStack::GetInstance()->acquire();
        if (!stack)
        {
            return;
        }

        m_sharedStack = stack;
        if (m_savedStackBuf && m_savedStackLen > 0)
        {
            std::memcpy(m_sharedStack, m_savedStackBuf, m_savedStackLen);
            SaveBufferAllocator::Dealloc(m_savedStackBuf, m_savedStackLen);
            m_savedStackBuf = nullptr;
            m_savedStackLen = 0;
        }
    }

    void Fiber::restoreSharedStackFromBuffer()
    {
        if (!m_useSharedStack || !m_sharedStack || !m_savedStackBuf || m_savedStackLen == 0)
        {
            return;
        }

        char *base = static_cast<char *>(m_sharedStack);
        std::memcpy(base + m_savedStackOffset, m_savedStackBuf, m_savedStackLen);
        SaveBufferAllocator::Dealloc(m_savedStackBuf, m_savedStackLen);
        m_savedStackBuf = nullptr;
        m_savedStackLen = 0;
        m_savedStackOffset = 0;
        ++s_shared_restore_count;
    }

    void Fiber::releaseSharedStackToTls()
    {
        if (!m_sharedStack)
        {
            return;
        }
        ThreadLocalSharedStack::GetInstance()->release(m_sharedStack);
        m_sharedStack = nullptr;
    }

    void Fiber::finalizeSharedStackAfterYield()
    {
        SYLAR_ASSERT(m_useSharedStack);

        if (!m_sharedStack)
        {
            return;
        }

        if (m_state == TERM || m_state == EXCEPT)
        {
            m_needSharedStackFinalize = false;
            m_savedStackLen = 0;
            m_savedStackOffset = 0;
            releaseSharedStackToTls();
            return;
        }

        if (m_needSharedStackFinalize)
        {
            saveSharedStackToBuffer();
            m_needSharedStackFinalize = false;
        }

        releaseSharedStackToTls();
    }

    /**
     * @brief 切换到当前协程运行
     */
    void Fiber::resume()
    {
        SYLAR_ASSERT(m_state != EXEC && m_state != TERM && m_state != EXCEPT);

        SetThis(this);
        m_state = EXEC;

        Fiber *scheduler_fiber = nullptr;
        if (m_runInScheduler)
        {
            scheduler_fiber = Scheduler::GetMainFiber();
            if (!scheduler_fiber)
            {
                SYLAR_LOG_WARN(SYLAR_LOG_ROOT()) << "Fiber::resume fallback to thread fiber, id=" << m_id;
            }
        }

        // 如果参与调度，则与调度协程切换；否则与主协程切换
        if (m_runInScheduler && scheduler_fiber)
        {
            if (swapcontext(&scheduler_fiber->m_ctx, &m_ctx))
            {
                SYLAR_ASSERT2(false, "swapcontext");
            }
        }
        else
        {
            SYLAR_ASSERT2(t_thread_fiber, "resume without thread main fiber");
            if (swapcontext(&t_thread_fiber->m_ctx, &m_ctx))
            {
                SYLAR_ASSERT2(false, "swapcontext");
            }
        }
    }

    /**
     * @brief 让出执行权
     */
    void Fiber::yield()
    {
        SYLAR_ASSERT(m_state == EXEC || m_state == READY || m_state == HOLD ||
                     m_state == TERM || m_state == EXCEPT);

        Fiber *scheduler_fiber = nullptr;
        if (m_runInScheduler)
        {
            scheduler_fiber = Scheduler::GetMainFiber();
            if (!scheduler_fiber)
            {
                SYLAR_LOG_WARN(SYLAR_LOG_ROOT()) << "Fiber::yield fallback to thread fiber, id=" << m_id;
            }
        }

        // 如果参与调度，则切换回调度协程；否则切换回主协程
        if (m_runInScheduler && scheduler_fiber)
        {
            SetThis(scheduler_fiber);
        }
        else
        {
            SYLAR_ASSERT2(t_thread_fiber, "yield without thread main fiber");
            SetThis(t_thread_fiber.get());
        }

        if (m_state == EXEC)
        {
            m_state = HOLD;
        }

        if (m_useSharedStack)
        {
            char marker;
            const char *stack_base = static_cast<const char *>(m_sharedStack);
            const char *stack_top = stack_base + ThreadLocalSharedStack::GetStackSize();
            const char *current_sp = &marker;
            SYLAR_ASSERT2(current_sp >= stack_base && current_sp < stack_top,
                          "shared-stack current sp out of range before yield");

            size_t used = static_cast<size_t>(stack_top - current_sp);
            const size_t align = 4096;
            used = (used + align - 1) & ~(align - 1);
            if (used > ThreadLocalSharedStack::GetStackSize())
            {
                used = ThreadLocalSharedStack::GetStackSize();
            }

            m_savedStackLen = used;
            m_savedStackOffset = ThreadLocalSharedStack::GetStackSize() - used;
            m_needSharedStackFinalize = true;
        }

        if (m_runInScheduler && scheduler_fiber)
        {
            if (swapcontext(&m_ctx, &scheduler_fiber->m_ctx))
            {
                SYLAR_ASSERT2(false, "swapcontext");
            }
        }
        else
        {
            SYLAR_ASSERT2(t_thread_fiber, "yield swap without thread main fiber");
            if (swapcontext(&m_ctx, &t_thread_fiber->m_ctx))
            {
                SYLAR_ASSERT2(false, "swapcontext");
            }
        }
    }

    /**
     * @brief 切换到当前协程运行（用于 use_caller 线程）
     */
    void Fiber::call()
    {
        SetThis(this);
        m_state = EXEC;
        if (swapcontext(&t_thread_fiber->m_ctx, &m_ctx))
        {
            SYLAR_ASSERT2(false, "swapcontext");
        }
    }

    /**
     * @brief 让出执行权（用于 use_caller 线程）
     */
    void Fiber::back()
    {
        SetThis(t_thread_fiber.get());
        if (swapcontext(&m_ctx, &t_thread_fiber->m_ctx))
        {
            SYLAR_ASSERT2(false, "swapcontext");
        }
    }

    void Fiber::SetThis(Fiber *f)
    {
        t_fiber = f;
    }

    /**
     * @brief 获取当前执行流所在的协程对象
     * @details 如果当前线程还没有协程环境，会自动创建第一个“主协程”
     */
    Fiber::ptr Fiber::GetThis()
    {
        if (t_fiber)
        {
            return t_fiber->shared_from_this();
        }
        // 自动初始化主协程
        Fiber::ptr main_fiber(new Fiber);
        SYLAR_ASSERT(t_fiber == main_fiber.get());
        t_thread_fiber = main_fiber;
        return t_fiber->shared_from_this();
    }

    /**
     * @brief 挂起并设置为 READY 状态（等待后续再次被 resume）
     */
    void Fiber::YieldToReady()
    {
        Fiber::ptr cur = GetThis();
        SYLAR_ASSERT(cur->m_state == EXEC);
        cur->m_state = READY;
        cur->yield();
    }

    /**
     * @brief 挂起并设置为 HOLD 状态（通常用于等 IO 这种不确定时机的情况）
     */
    void Fiber::YieldToHold()
    {
        Fiber::ptr cur = GetThis();
        SYLAR_ASSERT(cur->m_state == EXEC);
        cur->yield();
    }

    uint64_t Fiber::TotalFibers()
    {
        return s_fiber_count;
    }

    /**
     * @brief 协程真正运行的入口函数（由 makecontext 调用）
     */
    void Fiber::MainFunc()
    {
        // 1. 获取当前正在跑的子协程
        Fiber::ptr cur = GetThis();
        SYLAR_ASSERT(cur);

        try
        {
            // 2. 执行真正的业务逻辑
            cur->m_cb();
            cur->m_cb = nullptr;
            cur->m_state = TERM; // 正常结束
        }
        catch (std::exception &ex)
        {
            cur->m_state = EXCEPT;
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Fiber Except: " << ex.what()
                                              << " fiber_id=" << cur->getId()
                                              << std::endl
                                              << sylar::BacktraceToString();
        }
        catch (...)
        {
            cur->m_state = EXCEPT;
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Fiber Except"
                                              << " fiber_id=" << cur->getId()
                                              << std::endl
                                              << sylar::BacktraceToString();
        }

        /**
         * @brief 非常重要的内存优化细节
         * @details
         * 协程执行完后，必须切回主协程。但 cur 是一个 shared_ptr，
         * 如果我们直接调用 yield()，由于当前栈上还攥着 cur 指针，
         * 协程对象的引用计数就不会降为 0，导致内存无法释放。
         * 所以：我们先拿到 raw 指针，手动把 shared_ptr 释放掉，再切回去。
         */
        auto raw_ptr = cur.get();
        cur.reset();

        // 如果是参与调度的协程，执行完毕后切回调度协程
        if (raw_ptr->m_runInScheduler)
        {
            raw_ptr->yield();
        }
        else
        {
            // 如果是不参与调度的协程（如 use_caller 的调度协程），执行完毕后切回线程主协程
            raw_ptr->back();
        }

        // 代码永远不应该运行到这里
        SYLAR_ASSERT2(false, "never reach fiber_id=" + std::to_string(raw_ptr->getId()));
    }

    uint64_t Fiber::GetFiberId()
    {
        if (t_fiber)
        {
            return t_fiber->getId();
        }
        return 0;
    }

    Fiber::SharedStackStats Fiber::GetSharedStackStats()
    {
        SharedStackStats stats;
        stats.bind_count = s_shared_bind_count.load();
        stats.prepare_count = s_shared_prepare_count.load();
        stats.finalize_count = s_shared_finalize_count.load();
        stats.save_count = s_shared_save_count.load();
        stats.restore_count = s_shared_restore_count.load();
        stats.acquire_fail_count = s_shared_acquire_fail_count.load();
        stats.unsupported_mode_fallback_count = s_shared_unsupported_mode_count.load();
        return stats;
    }

    std::string Fiber::GetSharedStackStatsString()
    {
        SharedStackStats stats = GetSharedStackStats();
        std::ostringstream oss;
        oss << "bind=" << stats.bind_count
            << ",prepare=" << stats.prepare_count
            << ",finalize=" << stats.finalize_count
            << ",save=" << stats.save_count
            << ",restore=" << stats.restore_count
            << ",acquire_fail=" << stats.acquire_fail_count
            << ",unsupported_fallback=" << stats.unsupported_mode_fallback_count;
        return oss.str();
    }

    void Fiber::ResetSharedStackStats()
    {
        s_shared_bind_count.store(0);
        s_shared_prepare_count.store(0);
        s_shared_finalize_count.store(0);
        s_shared_save_count.store(0);
        s_shared_restore_count.store(0);
        s_shared_acquire_fail_count.store(0);
        s_shared_unsupported_mode_count.store(0);
    }

    void Fiber::clearCallback()
    {
        m_cb = nullptr;
    }


}
