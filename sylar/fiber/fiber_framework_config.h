#ifndef __SYLAR_FIBER_FRAMEWORK_CONFIG_H__
#define __SYLAR_FIBER_FRAMEWORK_CONFIG_H__

#include <stdint.h>

namespace sylar
{

    class FiberFrameworkConfig
    {
    public:
        /**
         * @brief 获取 Scheduler 默认 use_caller 开关
         * @details
         * 影响新建 Scheduler 时是否让调用线程参与调度。
         * 重载形式：启动参数型（已运行的 Scheduler 不受影响）。
         */
        static bool GetSchedulerUseCaller();

        /**
         * @brief 设置 Scheduler 默认 use_caller 开关
         * @details
         * 仅影响后续新建的 Scheduler。
         */
        static void SetSchedulerUseCaller(bool value);

        /**
         * @brief 获取 IOManager 默认 use_caller 开关
         * @details
         * 影响新建 IOManager 时是否让调用线程参与调度。
         * 重载形式：启动参数型（已运行的 IOManager 不受影响）。
         */
        static bool GetIOManagerUseCaller();

        /**
         * @brief 设置 IOManager 默认 use_caller 开关
         * @details
         * 仅影响后续新建的 IOManager。
         */
        static void SetIOManagerUseCaller(bool value);

        /**
         * @brief 获取 TCP connect 超时（毫秒）
         * @details
         * 用于 hook 中 connect_with_timeout。
         * 重载形式：运行时动态读取型（后续 connect 立即生效）。
         */
        static uint64_t GetTcpConnectTimeoutMs();

        /**
         * @brief 设置 TCP connect 超时（毫秒）
         * @details
         * 后续新的 connect 调用立即使用新值。
         */
        static void SetTcpConnectTimeoutMs(uint64_t value);

        /**
         * @brief 获取 Fiber 默认栈大小（字节）
         * @details
         * 用于新建 Fiber 时选择默认栈大小。
         * 重载形式：新对象生效型（已存在 Fiber 不变）。
         */
        static uint32_t GetFiberStackSize();

        /**
         * @brief 设置 Fiber 默认栈大小（字节）
         * @details
         * 仅影响后续新建 Fiber。
         */
        static void SetFiberStackSize(uint32_t value);

        /**
         * @brief 获取是否启用共享栈模式
         * @details
         * 表示是否“尝试”共享栈，最终仍受调度器运行时条件约束。
         * 重载形式：新对象生效型（已运行协程不切换模式）。
         */
        static bool GetFiberUseSharedStack();

        /**
         * @brief 设置是否启用共享栈模式
         * @details
         * 仅影响后续新建/获取的协程路径。
         */
        static void SetFiberUseSharedStack(bool value);

        /**
         * @brief 获取共享栈大小（字节）
         * @details
         * 重载形式：启动参数型（建议启动前确定，运行中修改风险高）。
         */
        static uint32_t GetFiberSharedStackSize();

        /**
         * @brief 设置共享栈大小（字节）
         * @details
         * 建议仅在启动阶段设置，避免运行中共享栈尺寸不一致。
         */
        static void SetFiberSharedStackSize(uint32_t value);

        /**
         * @brief 获取协程池开关
         * @details
         * 重载形式：运行时动态读取型（acquire/release 路径实时读取）。
         */
        static bool GetFiberPoolEnabled();

        /**
         * @brief 设置协程池开关
         * @details
         * 设置后后续 acquire/release 行为立即按新值执行。
         */
        static void SetFiberPoolEnabled(bool value);

        /**
         * @brief 获取协程池最大容量（每线程）
         * @details
         * 重载形式：运行时动态读取型（release 路径实时读取）。
         */
        static uint32_t GetFiberPoolMaxSize();

        /**
         * @brief 设置协程池最大容量（每线程）
         * @details
         * 后续归还协程时将按新上限判断是否入池。
         */
        static void SetFiberPoolMaxSize(uint32_t value);

        /**
         * @brief 获取协程池最小保留数量
         * @details
         * 重载形式：运行时动态读取型（shrink 路径实时读取）。
         */
        static uint32_t GetFiberPoolMinKeep();

        /**
         * @brief 设置协程池最小保留数量
         * @details
         * 后续 shrink 时按新保留值执行。
         */
        static void SetFiberPoolMinKeep(uint32_t value);

        /**
         * @brief 获取协程池空闲淘汰超时（毫秒）
         * @details
         * 重载形式：运行时动态读取型（shrink 路径实时读取）。
         */
        static uint32_t GetFiberPoolIdleTimeoutMs();

        /**
         * @brief 设置协程池空闲淘汰超时（毫秒）
         * @details
         * 后续 shrink 时按新超时执行淘汰。
         */
        static void SetFiberPoolIdleTimeoutMs(uint32_t value);
    };

} // namespace sylar

#endif
