#include "fiber.h"
#include "scheduler.h"
#include "sylar/base/config.h"
#include "sylar/base/macro.h"
#include <atomic>

namespace sylar
{

    // 全局静态变量，用于生成协程 ID 和统计协程总数，因为没加thread_local所以统计的是当前进程中的协程数和 ID
    static std::atomic<uint64_t> s_fiber_id{0};
    static std::atomic<uint64_t> s_fiber_count{0};

    // 线程局部变量：当前线程正在运行的协程
    static thread_local Fiber *t_fiber = nullptr;
    // 线程局部变量：当前线程的主协程（调度协程）
    static thread_local Fiber::ptr t_thread_fiber = nullptr;

    // 配置项：默认协程栈大小 (1MB)
    static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
        Config::Lookup<uint32_t>("fiber.stack_size", 1024 * 1024, "fiber stack size");

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
        m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();

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
        if (m_stack)
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
        SYLAR_ASSERT(m_stack); // 只有子协程能 reset
        SYLAR_ASSERT(m_state == TERM || m_state == EXCEPT || m_state == INIT);
        m_cb = cb;

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

    /**
     * @brief 切换到当前协程运行
     */
    void Fiber::resume()
    {
        SYLAR_ASSERT(m_state != EXEC && m_state != TERM && m_state != EXCEPT);
        SetThis(this);
        m_state = EXEC;

        // 如果参与调度，则与调度协程切换；否则与主协程切换
        if (m_runInScheduler)
        {
            if (swapcontext(&Scheduler::GetMainFiber()->m_ctx, &m_ctx))
            {
                SYLAR_ASSERT2(false, "swapcontext");
            }
        }
        else
        {
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
        SYLAR_ASSERT(m_state == EXEC || m_state == TERM || m_state == EXCEPT);

        // 如果参与调度，则切换回调度协程；否则切换回主协程
        if (m_runInScheduler)
        {
            SetThis(Scheduler::GetMainFiber());
        }
        else
        {
            SetThis(t_thread_fiber.get());
        }

        if (m_state != TERM && m_state != EXCEPT)
        {
            m_state = HOLD;
        }

        if (m_runInScheduler)
        {
            if (swapcontext(&m_ctx, &Scheduler::GetMainFiber()->m_ctx))
            {
                SYLAR_ASSERT2(false, "swapcontext");
            }
        }
        else
        {
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
        if (raw_ptr->m_runInScheduler) {
            raw_ptr->yield();
        } else {
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

}