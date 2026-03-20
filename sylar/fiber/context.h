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
        typedef void (*EntryFunc)();

#if defined(SYLAR_FIBER_CONTEXT_BACKEND_LIBCO_ASM)
        /**
         * @brief x86_64 最小上下文
         * @details 保存恢复协作式切换所需的核心寄存器
         */
        struct Context
        {
            void *rsp = nullptr;
            void *rip = nullptr;
            void *rbx = nullptr;
            void *rbp = nullptr;
            void *r12 = nullptr;
            void *r13 = nullptr;
            void *r14 = nullptr;
            void *r15 = nullptr;
        };
#elif defined(SYLAR_FIBER_CONTEXT_BACKEND_UCONTEXT)
        struct Context
        {
            ucontext_t uc;
        };
#else
#error "No fiber context backend selected. Define SYLAR_FIBER_CONTEXT_BACKEND_LIBCO_ASM or SYLAR_FIBER_CONTEXT_BACKEND_UCONTEXT."
#endif

        void InitMainContext(Context &ctx);
        void InitChildContext(Context &ctx, void *stack, size_t stack_size, EntryFunc entry);
        void SwapContext(Context &from, Context &to);
        const char *BackendName();
    } // namespace fiber_context
} // namespace sylar

#endif

