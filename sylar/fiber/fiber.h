/**
 * @file fiber.h
 * @brief 协程封装
 * @author sylar.yin
 * @date 2026-02-07
 */
#ifndef __SYLAR_FIBER_H__
#define __SYLAR_FIBER_H__

#include <memory>
#include <functional>
#include <ucontext.h>

namespace sylar
{

    /**
     * @brief 协程类
     */
    class Fiber : public std::enable_shared_from_this<Fiber>
    {
    public:
        typedef std::shared_ptr<Fiber> ptr;

        /**
         * @brief 协程状态
         */
        enum State
        {
            /// 初始化状态
            INIT,
            /// 可运行状态
            READY,
            /// 运行中状态
            EXEC,
            /// 挂起状态
            HOLD,
            /// 结束状态
            TERM,
            /// 异常状态
            EXCEPT
        };

    private:
        /**
         * @brief 无参构造函数
         * @details 每个线程第一个协程的构造（主协程）
         */
        Fiber();

    public:
        /**
         * @brief 构造函数
         * @param[in] cb 协程执行函数
         * @param[in] stacksize 栈大小
         * @param[in] run_in_scheduler 本协程是否参与调度器调度
         */
        Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_scheduler = true);

        /**
         * @brief 析构函数
         */
        ~Fiber();

        /**
         * @brief 重置协程执行函数，并重置状态
         * @pre 状态为 TERM, EXCEPT 或 INIT
         * @post 状态为 READY
         */
        void reset(std::function<void()> cb);

        /**
         * @brief 将当前协程切换到运行状态
         * @pre 当前协程状态不是 EXEC
         * @post 当前协程状态为 EXEC
         */
        void resume();

        /**
         * @brief 将当前协程切换到后台
         * @details 当前协程切换到后台，并唤醒主协程
         */
        void yield();

        /**
         * @brief 从当前线程的主协程切换到当前的协程
         * @pre 当前协程状态不是 EXEC
         * @post 当前协程状态为 EXEC
         */
        void call();

        /**
         * @brief 将当前协程切换到后台，唤醒主协程
         */
        void back();

        /**
         * @brief 返回协程id
         */
        uint64_t getId() const { return m_id; }

        /**
         * @brief 返回协程状态
         */
        State getState() const { return m_state; }

        /**
         * @brief 设置协程状态
         * @param[in] s 状态
         */
        void setState(State s) { m_state = s; }

    public:
        /**
         * @brief 设置当前线程的运行协程
         * @param[in] f 运行协程
         */
        static void SetThis(Fiber *f);

        /**
         * @brief 返回当前所在协程
         */
        static Fiber::ptr GetThis();

        /**
         * @brief 将当前协程切换到 READY 状态，并让出执行权
         */
        static void YieldToReady();

        /**
         * @brief 将当前协程切换到 HOLD 状态，并让出执行权
         */
        static void YieldToHold();

        /**
         * @brief 返回当前协程的总数量
         */
        static uint64_t TotalFibers();

        /**
         * @brief 协程执行函数
         * @post 执行完成之后返回到主协程
         */
        static void MainFunc();

        /**
         * @brief 获取当前协程的id
         */
        static uint64_t GetFiberId();

    private:
        /// 协程id
        uint64_t m_id = 0;
        /// 协程栈大小
        uint32_t m_stacksize = 0;
        /// 协程状态
        State m_state = INIT;
        /// 协程上下文
        ucontext_t m_ctx;
        /// 协程栈地址
        void *m_stack = nullptr;
        /// 协程运行函数
        std::function<void()> m_cb;
        /// 本协程是否参与调度器调度
        bool m_runInScheduler;
    };

}

#endif
