/**
 * @file context.cc
 * @brief 协程上下文后端抽象实现
 */
#include "sylar/fiber/context.h"
#include "sylar/base/macro.h"

#include <cstdint>
#include <cstdlib>

namespace sylar
{
namespace fiber_context
{
#if defined(SYLAR_FIBER_CONTEXT_BACKEND_LIBCO_ASM)

/**
 * @brief 汇编实现的上下文切换函数
 * @param from 当前上下文（保存目标）
 * @param to 目标上下文（恢复来源）
 *
 * @details
 * 定义在 `coctx_swap_x86_64.S`：
 * - 保存 from 的 `rsp/rip/rbx/rbp/r12-r15`
 * - 恢复 to 的同名字段
 * - 直接跳转到 to->rip
 */
extern "C" void sylar_coctx_swap(Context* from, const Context* to);

namespace
{
/**
 * @brief 协程入口意外 return 的保护函数
 *
 * @details
 * 子协程入口应由 `Fiber::MainFunc` 管理生命周期并显式切回；
 * 若业务路径直接 return 到栈顶哨兵，则触发断言并中止进程。
 */
[[noreturn]] void FiberEntryReturnAbort()
{
    SYLAR_ASSERT2(false, "fiber entry returned unexpectedly");
    std::abort();
}
} // namespace

/**
 * @brief 初始化主协程上下文（汇编后端）
 *
 * @details
 * 主协程上下文在首次切换时由 `sylar_coctx_swap` 真实保存，
 * 这里先置零即可。
 */
void InitMainContext(Context& ctx)
{
    ctx = Context();
}

/**
 * @brief 初始化子协程上下文（汇编后端）
 *
 * @details
 * 初始化要点：
 * 1. 计算并 16 字节对齐栈顶。
 * 2. 在栈顶预留一个返回地址槽位，写入 `FiberEntryReturnAbort`。
 * 3. 将 `ctx.rsp` 指向该槽位，将 `ctx.rip` 指向协程入口 `entry`。
 *
 * 首次切入时会直接从 `entry` 开始执行，并使用该独立栈。
 */
void InitChildContext(Context& ctx, void* stack, size_t stack_size, EntryFunc entry)
{
    SYLAR_ASSERT(stack);
    SYLAR_ASSERT(stack_size >= 16);
    SYLAR_ASSERT(entry);

    ctx = Context();

    uintptr_t stack_top = reinterpret_cast<uintptr_t>(static_cast<char*>(stack) + stack_size);
    stack_top &= ~static_cast<uintptr_t>(0x0F);

    uintptr_t rsp = stack_top - sizeof(void*);
    *reinterpret_cast<void**>(rsp) = reinterpret_cast<void*>(&FiberEntryReturnAbort);

    ctx.rsp = reinterpret_cast<void*>(rsp);
    ctx.rip = reinterpret_cast<void*>(entry);
}

/**
 * @brief 上下文切换（汇编后端）
 *
 * @details
 * 直接转发到汇编实现，避免额外分支与包装开销。
 */
void SwapContext(Context& from, Context& to)
{
    sylar_coctx_swap(&from, &to);
}

/**
 * @brief 返回当前后端名称
 */
const char* BackendName()
{
    return "libco_asm";
}

#elif defined(SYLAR_FIBER_CONTEXT_BACKEND_UCONTEXT)

/**
 * @brief 初始化主协程上下文（ucontext 后端）
 *
 * @details
 * 使用 `getcontext` 抓取当前线程执行流上下文，
 * 作为后续切回主协程时的恢复点。
 */
void InitMainContext(Context& ctx)
{
    if (getcontext(&ctx.uc))
    {
        SYLAR_ASSERT2(false, "getcontext");
    }
}

/**
 * @brief 初始化子协程上下文（ucontext 后端）
 *
 * @details
 * 标准三步：
 * 1. `getcontext` 先拿一份可修改模板；
 * 2. 设置 `uc_stack/uc_link`；
 * 3. `makecontext` 绑定入口函数。
 */
void InitChildContext(Context& ctx, void* stack, size_t stack_size, EntryFunc entry)
{
    SYLAR_ASSERT(stack);
    SYLAR_ASSERT(stack_size > 0);
    SYLAR_ASSERT(entry);

    if (getcontext(&ctx.uc))
    {
        SYLAR_ASSERT2(false, "getcontext");
    }

    ctx.uc.uc_link = nullptr;
    ctx.uc.uc_stack.ss_sp = stack;
    ctx.uc.uc_stack.ss_size = stack_size;
    makecontext(&ctx.uc, entry, 0);
}

/**
 * @brief 上下文切换（ucontext 后端）
 *
 * @details
 * `swapcontext(&from, &to)` 会先保存当前现场到 `from`，
 * 再恢复 `to` 并跳转执行。
 */
void SwapContext(Context& from, Context& to)
{
    if (swapcontext(&from.uc, &to.uc))
    {
        SYLAR_ASSERT2(false, "swapcontext");
    }
}

/**
 * @brief 返回当前后端名称
 */
const char* BackendName()
{
    return "ucontext";
}

#else
#error "No fiber context backend selected"
#endif

} // namespace fiber_context
} // namespace sylar
