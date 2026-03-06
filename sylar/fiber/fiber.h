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

        /**
         * @brief 是否启用共享栈模式
         * @note 当前仅预留 V1 线程绑定共享栈骨架，默认关闭
         */
        bool isSharedStackEnabled() const { return m_useSharedStack; }

        /**
         * @brief 共享栈上下文是否已初始化
         */
        bool isSharedStackContextInited() const { return m_ctxInited; }

        /**
         * @brief 获取共享栈 Fiber 绑定线程
         * @return -1 表示尚未绑定线程
         */
        int getBoundThread() const { return m_boundThread; }

        /**
         * @brief 供 Scheduler 调用的共享栈 resume 前准备钩子（当前仅骨架）
         */
        void onSchedulerBeforeResume();

        /**
         * @brief 供 Scheduler 调用的共享栈 yield 返回后后处理钩子（当前仅骨架）
         */
        void onSchedulerAfterResume();

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
        /**
         * @brief 确保共享栈 Fiber 绑定到当前线程（Scheduler 后处理版骨架）
         */
        void ensureSharedStackBinding();

        /**
         * @brief 共享栈模式下 resume 前准备（Scheduler 后处理版骨架）
         */
        void prepareSharedStackBeforeResume();

        /**
         * @brief 共享栈模式下 yield 返回后，由 Scheduler 调用的后处理（骨架）
         */
        void finalizeSharedStackAfterYield();

        /**
         * @brief 初始化共享栈模式上下文（V1 线程绑定路径）
         */
        void initSharedStackContext();

        /**
         * @brief 计算共享栈实际使用量（V1 骨架，暂未接入运行路径）
         */
        size_t calculateStackUsage() const;

        /**
         * @brief 挂起前保存共享栈内容（V1 骨架，暂未接入运行路径）
         */
        void saveSharedStack();

        /**
         * @brief 恢复前加载共享栈内容（V1 骨架，暂未接入运行路径）
         */
        void restoreSharedStack();

        /**
         * @brief 将共享栈内容保存到缓冲区（Scheduler 后处理版骨架）
         */
        void saveSharedStackToBuffer();

        /**
         * @brief 从缓冲区恢复共享栈内容（Scheduler 后处理版骨架）
         */
        void restoreSharedStackFromBuffer();

        /**
         * @brief 归还当前共享栈到线程本地管理器（Scheduler 后处理版骨架）
         */
        void releaseSharedStackToTls();

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
        /// V1: 是否启用线程绑定共享栈（当前默认关闭）
        bool m_useSharedStack = false;
        /// V1: 共享栈上下文是否已初始化
        bool m_ctxInited = false;
        /// V1: 共享栈 Fiber 绑定的线程 id，-1 表示未绑定
        int m_boundThread = -1;
        /// V1: 当前占用的共享栈指针
        void *m_sharedStack = nullptr;
        /// V1: 挂起时保存栈内容的缓冲区
        void *m_savedStackBuf = nullptr;
        /// V1: 保存的栈内容长度
        size_t m_savedStackLen = 0;
        /// V1: 保存区域相对共享栈基址的偏移
        size_t m_savedStackOffset = 0;
        /// V1: 当前 yield 返回后是否需要在 Scheduler 侧做保存后处理
        bool m_needSharedStackFinalize = false;
        /// 协程运行函数
        std::function<void()> m_cb;
        /// 本协程是否参与调度器调度
        bool m_runInScheduler;
    };

}

#endif
