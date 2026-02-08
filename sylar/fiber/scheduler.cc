/**
 * @file scheduler.cc
 * @brief 协程调度器实现
 * @author sylar.yin (Modified by Gemini Agent)
 * @date 2026-02-08
 */
#include "sylar/fiber/scheduler.h"
#include "sylar/log/logger.h"
#include "sylar/base/macro.h"

namespace sylar
{

    static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

    /// 当前线程的调度器指针（TLS）
    static thread_local Scheduler *t_scheduler = nullptr;
    /// 当前线程的调度协程（TLS），每个线程跑 run 循环的那个协程
    static thread_local Fiber *t_scheduler_fiber = nullptr;

    Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name)
        : m_name(name)
    {
        SYLAR_ASSERT(threads > 0);

        if (use_caller)
        {
            // 1. 初始化当前线程的主协程环境
            sylar::Fiber::GetThis();
            --threads; // 总线程数减1，因为当前线程也要占一个名额

            // 2. 一个线程只能有一个调度器
            SYLAR_ASSERT(GetThis() == nullptr);
            t_scheduler = this;

            /**
             * 3. 在当前线程创建一个“调度控制协程”
             * 这里直接绑定 Scheduler::run 成员函数。
             * 注意：这个协程不会立即执行，直到 stop() 时被显式拉起来。
             * 
             * 【微调】run_in_scheduler 设为 false。
             * 因为调度协程本身不应该被自己调度，它退出时应该直接回退到 main 函数。
             */
            m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));
            sylar::Thread::SetName(m_name);

            t_scheduler_fiber = m_rootFiber.get();
            m_rootThread = sylar::GetThreadId();
            m_threadIds.push_back(m_rootThread);
        }
        else
        {
            m_rootThread = -1;
        }
        m_threadCount = threads;
    }

    Scheduler::~Scheduler()
    {
        SYLAR_ASSERT(m_stopping);
        if (GetThis() == this)
        {
            t_scheduler = nullptr;
        }
    }

    Scheduler *Scheduler::GetThis()
    {
        return t_scheduler;
    }

    Fiber *Scheduler::GetMainFiber()
    {
        return t_scheduler_fiber;
    }

    void Scheduler::setThis()
    {
        t_scheduler = this;
    }

    void Scheduler::start()
    {
        MutexType::Lock lock(m_mutex);
        if (!m_stopping)
        {
            return;
        }
        m_stopping = false;
        SYLAR_ASSERT(m_threads.empty());

        // 创建线程池
        m_threads.resize(m_threadCount);
        for (size_t i = 0; i < m_threadCount; ++i)
        {
            // 每个线程诞生后，入口函数都是 Scheduler::run
            m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this), m_name + "_" + std::to_string(i)));
            m_threadIds.push_back(m_threads[i]->getId());
        }
        lock.unlock();
    }

    void Scheduler::stop()
    {
        m_autoStop = true;
        // 如果是 use_caller 模式，且只有主线程在工作
        if (m_rootFiber && m_threadCount == 0 && (m_rootFiber->getState() == Fiber::TERM || m_rootFiber->getState() == Fiber::INIT))
        {
            SYLAR_LOG_INFO(g_logger) << this << " stopped";
            m_stopping = true;

            if (stopping())
            {
                return;
            }
        }

        // 检查是否有资格调 stop
        if (m_rootThread != -1)
        {
            SYLAR_ASSERT(GetThis() == this);
        }
        else
        {
            SYLAR_ASSERT(GetThis() != this);
        }

        m_stopping = true;
        // 叫醒所有线程去处理剩余任务并准备退出
        for (size_t i = 0; i < m_threadCount; ++i)
        {
            tickle();
        }

        if (m_rootFiber)
        {
            tickle();
        }

        // 如果开启了 use_caller，主线程在这里加入战斗，跑一遍 run
        if (m_rootFiber)
        {
            if (!stopping())
            {
                m_rootFiber->call();
            }
        }

        // 等待子线程全部退出
        std::vector<Thread::ptr> thrs;
        {
            MutexType::Lock lock(m_mutex);
            thrs.swap(m_threads);
        }

        for (auto &i : thrs)
        {
            i->join();
        }
    }

    void Scheduler::tickle()
    {
        SYLAR_LOG_INFO(g_logger) << "tickle";
    }

    bool Scheduler::stopping()
    {
        MutexType::Lock lock(m_mutex);
        // 只有当主动调了 stop，且任务队列空了，且没有活跃线程时，才真正停止
        return m_autoStop && m_stopping && m_fibers.empty() && m_activeThreadCount == 0;
    }

    void Scheduler::idle()
    {
        SYLAR_LOG_INFO(g_logger) << "idle";
        while (!stopping())
        {
            // 没活干时就在这里让出 CPU，回到 run 的下一轮循环
            sylar::Fiber::YieldToHold();
        }
    }

    void Scheduler::run()
    {
        SYLAR_LOG_DEBUG(g_logger) << m_name << " run";
        setThis();

        // 如果当前线程不是主线程（即线程池创建的子线程）
        if (sylar::GetThreadId() != m_rootThread)
        {
            // 设置当前线程的调度协程为线程启动时的原始协程
            t_scheduler_fiber = Fiber::GetThis().get();
        }

        // 创建空闲协程：当没活干时跑这个
        Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
        Fiber::ptr cb_fiber; // 用于包装 callback 函数的临时协程

        FiberAndThread ft;
        while (true)
        {
            ft.reset();
            bool tickle_me = false; // 是否需要通知其他线程
            bool is_active = false; // 是否真的抓到了活干

            // 1. 【抢活阶段】从任务队列里挑一个能干的活
            {
                MutexType::Lock lock(m_mutex);
                auto it = m_fibers.begin();
                while (it != m_fibers.end())
                {
                    // 如果任务指定了线程 ID，但不是我这个线程，就跳过
                    if (it->thread != -1 && it->thread != sylar::GetThreadId())
                    {
                        ++it;
                        tickle_me = true; // 顺便通知一下别的线程来领
                        continue;
                    }

                    SYLAR_ASSERT(it->fiber || it->cb);
                    // 如果协程已经在跑了，跳过
                    if (it->fiber && it->fiber->getState() == Fiber::EXEC)
                    {
                        ++it;
                        continue;
                    }

                    // 抓到了！从队列里取出来
                    ft = *it;
                    m_fibers.erase(it++);
                    ++m_activeThreadCount; // 活跃线程数加1
                    is_active = true;
                    break;
                }
                // 如果队列里还有活，通知一下别的伙伴也来抢
                tickle_me |= it != m_fibers.end();
            }

            if (tickle_me)
            {
                tickle();
            }

            // 2. 【干活阶段 - 处理协程任务】
            if (ft.fiber && (ft.fiber->getState() != Fiber::TERM && ft.fiber->getState() != Fiber::EXCEPT))
            {
                ft.fiber->resume();    // 切入业务协程！
                --m_activeThreadCount; // 业务跑完了（或挂起了），活跃数减1

                if (ft.fiber->getState() == Fiber::READY)
                {
                    // 如果协程是主动让出的（Ready 状态），塞回队列下次接着跑
                    schedule(ft.fiber);
                }
                else if (ft.fiber->getState() != Fiber::TERM && ft.fiber->getState() != Fiber::EXCEPT)
                {
                    // 否则设为挂起状态
                    ft.fiber->setState(Fiber::HOLD);
                }
                ft.reset();
            }
                    // 3. 【干活阶段 - 处理函数任务】
                    else if(ft.cb) {
                        if(cb_fiber) {
                            // 增加状态检查：只有结束了的协程才能复用
                            if(cb_fiber->getState() == Fiber::TERM 
                               || cb_fiber->getState() == Fiber::EXCEPT) {
                                cb_fiber->reset(ft.cb);
                            } else {
                                // 还没结束（说明可能在 HOLD），只能另起炉灶
                                cb_fiber.reset(new Fiber(ft.cb));
                            }
                        } else {
                            cb_fiber.reset(new Fiber(ft.cb));
                        }
                        ft.reset();
                        cb_fiber->resume(); // 切入包装后的协程跑函数！
                        --m_activeThreadCount;
            
                        if(cb_fiber->getState() == Fiber::READY) {
                            schedule(cb_fiber);
                            cb_fiber.reset();
                        } else if(cb_fiber->getState() == Fiber::EXCEPT
                                || cb_fiber->getState() == Fiber::TERM) {
                            // 函数跑完了，不重置 cb_fiber，留在下轮复用
                        } else {
                            cb_fiber->setState(Fiber::HOLD);
                            cb_fiber.reset();
                        }
                    } 
            
            // 4. 【空闲阶段】真的没活干了
            else
            {
                if (is_active)
                {
                    --m_activeThreadCount;
                    continue;
                }
                // 如果调度器已经关门了，且 idle 协程也跑完了，就彻底退出循环
                if (idle_fiber->getState() == Fiber::TERM)
                {
                    SYLAR_LOG_INFO(g_logger) << "idle fiber term";
                    break;
                }

                ++m_idleThreadCount;
                idle_fiber->resume(); // 进 idle 协程待命
                --m_idleThreadCount;
                if (idle_fiber->getState() != Fiber::TERM && idle_fiber->getState() != Fiber::EXCEPT)
                {
                    idle_fiber->setState(Fiber::HOLD);
                }
            }
        }
    }

}