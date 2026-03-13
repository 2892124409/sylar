#include "sylar/fiber/fiber_framework_config.h"

#include "config/config.h"

namespace sylar
{

    namespace
    {
        // 重载形式：启动参数型（创建 Scheduler 时读取）
        static ConfigVar<bool>::ptr g_scheduler_use_caller =
            Config::Lookup<bool>("scheduler.use_caller", true, "scheduler use caller at startup");

        // 重载形式：启动参数型（创建 IOManager 时读取）
        static ConfigVar<bool>::ptr g_iomanager_use_caller =
            Config::Lookup<bool>("iomanager.use_caller", true, "iomanager use caller at startup");

        // 重载形式：运行时动态读取型（每次 connect hook 调用读取）
        static ConfigVar<int>::ptr g_tcp_connect_timeout =
            Config::Lookup<int>("tcp.connect.timeout", 5000, "tcp connect timeout");

        // 重载形式：新对象生效型（新建 Fiber/FiberPool 时读取）
        static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
            Config::Lookup<uint32_t>("fiber.stack_size", 1024 * 1024, "fiber stack size");

        // 重载形式：新对象生效型（新建 Fiber/FiberPool 时读取）
        static ConfigVar<bool>::ptr g_fiber_use_shared_stack =
            Config::Lookup<bool>("fiber.use_shared_stack", false, "fiber use thread-bound shared stack");

        // 重载形式：启动参数型（共享栈大小建议启动前确定）
        static ConfigVar<uint32_t>::ptr g_fiber_shared_stack_size =
            Config::Lookup<uint32_t>("fiber.shared_stack_size", 1024 * 1024, "fiber shared stack size");

        // 重载形式：运行时动态读取型（每次 acquire/release 时读取）
        static ConfigVar<bool>::ptr g_fiber_pool_enabled =
            Config::Lookup<bool>("fiber.pool.enabled", true, "enable fiber pool");

        // 重载形式：运行时动态读取型（每次 release/shrink 时读取）
        static ConfigVar<uint32_t>::ptr g_fiber_pool_max_size =
            Config::Lookup<uint32_t>("fiber.pool.max_size", 1000, "max pool size per thread");

        // 重载形式：运行时动态读取型（每次 shrink 时读取）
        static ConfigVar<uint32_t>::ptr g_fiber_pool_min_keep =
            Config::Lookup<uint32_t>("fiber.pool.min_keep", 10, "min keep size when shrinking");

        // 重载形式：运行时动态读取型（每次 shrink 时读取）
        static ConfigVar<uint32_t>::ptr g_fiber_pool_idle_timeout =
            Config::Lookup<uint32_t>("fiber.pool.idle_timeout", 60000, "idle timeout ms");
    }

    bool FiberFrameworkConfig::GetSchedulerUseCaller()
    {
        return g_scheduler_use_caller->getValue();
    }

    void FiberFrameworkConfig::SetSchedulerUseCaller(bool value)
    {
        g_scheduler_use_caller->setValue(value);
    }

    bool FiberFrameworkConfig::GetIOManagerUseCaller()
    {
        return g_iomanager_use_caller->getValue();
    }

    void FiberFrameworkConfig::SetIOManagerUseCaller(bool value)
    {
        g_iomanager_use_caller->setValue(value);
    }

    uint64_t FiberFrameworkConfig::GetTcpConnectTimeoutMs()
    {
        int timeout = g_tcp_connect_timeout->getValue();
        return timeout <= 0 ? 0 : static_cast<uint64_t>(timeout);
    }

    void FiberFrameworkConfig::SetTcpConnectTimeoutMs(uint64_t value)
    {
        g_tcp_connect_timeout->setValue(static_cast<int>(value));
    }

    uint32_t FiberFrameworkConfig::GetFiberStackSize()
    {
        return g_fiber_stack_size->getValue();
    }

    void FiberFrameworkConfig::SetFiberStackSize(uint32_t value)
    {
        g_fiber_stack_size->setValue(value);
    }

    bool FiberFrameworkConfig::GetFiberUseSharedStack()
    {
        return g_fiber_use_shared_stack->getValue();
    }

    void FiberFrameworkConfig::SetFiberUseSharedStack(bool value)
    {
        g_fiber_use_shared_stack->setValue(value);
    }

    uint32_t FiberFrameworkConfig::GetFiberSharedStackSize()
    {
        return g_fiber_shared_stack_size->getValue();
    }

    void FiberFrameworkConfig::SetFiberSharedStackSize(uint32_t value)
    {
        g_fiber_shared_stack_size->setValue(value);
    }

    bool FiberFrameworkConfig::GetFiberPoolEnabled()
    {
        return g_fiber_pool_enabled->getValue();
    }

    void FiberFrameworkConfig::SetFiberPoolEnabled(bool value)
    {
        g_fiber_pool_enabled->setValue(value);
    }

    uint32_t FiberFrameworkConfig::GetFiberPoolMaxSize()
    {
        return g_fiber_pool_max_size->getValue();
    }

    void FiberFrameworkConfig::SetFiberPoolMaxSize(uint32_t value)
    {
        g_fiber_pool_max_size->setValue(value);
    }

    uint32_t FiberFrameworkConfig::GetFiberPoolMinKeep()
    {
        return g_fiber_pool_min_keep->getValue();
    }

    void FiberFrameworkConfig::SetFiberPoolMinKeep(uint32_t value)
    {
        g_fiber_pool_min_keep->setValue(value);
    }

    uint32_t FiberFrameworkConfig::GetFiberPoolIdleTimeoutMs()
    {
        return g_fiber_pool_idle_timeout->getValue();
    }

    void FiberFrameworkConfig::SetFiberPoolIdleTimeoutMs(uint32_t value)
    {
        g_fiber_pool_idle_timeout->setValue(value);
    }

} // namespace sylar
