/**
 * @file context.h
 * @brief 协程上下文后端抽象
 */
#ifndef __SYLAR_FIBER_CONTEXT_H__
#define __SYLAR_FIBER_CONTEXT_H__

#include <cstddef>

#if defined(SYLAR_FIBER_CONTEXT_BACKEND_UCONTEXT)
#include <ucontext.h>
#endif

namespace sylar
{
namespace fiber_context
{
/**
 * @brief 协程入口函数类型
 * @details 子协程首次切入时从该入口开始执行。
 */
typedef void (*EntryFunc)();

#if defined(SYLAR_FIBER_CONTEXT_BACKEND_LIBCO_ASM)
/**
 * @brief x86_64 最小上下文（汇编后端）
 * @details
 * 该结构体字段顺序与汇编文件 `coctx_swap_x86_64.S` 中的偏移严格对应：
 * - 0:  rsp
 * - 8:  rip
 * - 16: rbx
 * - 24: rbp
 * - 32: r12
 * - 40: r13
 * - 48: r14
 * - 56: r15
 *
 * 这里只保存协作式切换所需的最小寄存器集合。
 */
struct Context
{
    /// @brief 目标栈顶（恢复后写入 CPU RSP）
    void* rsp = nullptr;
    /// @brief 目标指令地址（恢复后跳转到该地址）
    void* rip = nullptr;
    /// @brief 被调用者保存寄存器 RBX
    void* rbx = nullptr;
    /// @brief 被调用者保存寄存器 RBP
    void* rbp = nullptr;
    /// @brief 被调用者保存寄存器 R12
    void* r12 = nullptr;
    /// @brief 被调用者保存寄存器 R13
    void* r13 = nullptr;
    /// @brief 被调用者保存寄存器 R14
    void* r14 = nullptr;
    /// @brief 被调用者保存寄存器 R15
    void* r15 = nullptr;
};
#elif defined(SYLAR_FIBER_CONTEXT_BACKEND_UCONTEXT)
/**
 * @brief ucontext 后端上下文封装
 */
struct Context
{
    /// @brief glibc 提供的用户态上下文结构
    ucontext_t uc;
};
#else
#error                                                                                                                 \
    "No fiber context backend selected. Define SYLAR_FIBER_CONTEXT_BACKEND_LIBCO_ASM or SYLAR_FIBER_CONTEXT_BACKEND_UCONTEXT."
#endif

/**
 * @brief 初始化主协程上下文
 * @param[out] ctx 主协程上下文对象
 *
 * @details
 * - 汇编后端：清零并等待第一次 `SwapContext` 保存真实现场
 * - ucontext 后端：直接抓取当前线程执行流上下文
 */
void InitMainContext(Context& ctx);

/**
 * @brief 初始化子协程上下文
 * @param[out] ctx 子协程上下文对象
 * @param[in] stack 子协程栈起始地址
 * @param[in] stack_size 子协程栈大小（字节）
 * @param[in] entry 子协程入口函数
 *
 * @pre stack != nullptr
 * @pre stack_size > 0
 * @pre entry != nullptr
 *
 * @details
 * 初始化后可通过 `SwapContext` 切入该协程。
 */
void InitChildContext(Context& ctx, void* stack, size_t stack_size, EntryFunc entry);

/**
 * @brief 切换上下文（保存 from，恢复 to）
 * @param[in,out] from 当前执行上下文（切出时写回）
 * @param[in,out] to   目标执行上下文（切入时读取）
 *
 * @details
 * 该函数返回时表示未来某次又切回了 `from`。
 */
void SwapContext(Context& from, Context& to);

/**
 * @brief 返回当前上下文后端名称
 * @return `"libco_asm"` 或 `"ucontext"`
 */
const char* BackendName();
} // namespace fiber_context
} // namespace sylar

#endif
