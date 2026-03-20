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

        extern "C" void sylar_coctx_swap(Context *from, const Context *to);

        namespace
        {
            [[noreturn]] void FiberEntryReturnAbort()
            {
                SYLAR_ASSERT2(false, "fiber entry returned unexpectedly");
                std::abort();
            }
        } // namespace

        void InitMainContext(Context &ctx)
        {
            ctx = Context();
        }

        void InitChildContext(Context &ctx, void *stack, size_t stack_size, EntryFunc entry)
        {
            SYLAR_ASSERT(stack);
            SYLAR_ASSERT(stack_size >= 16);
            SYLAR_ASSERT(entry);

            ctx = Context();

            uintptr_t stack_top = reinterpret_cast<uintptr_t>(static_cast<char *>(stack) + stack_size);
            stack_top &= ~static_cast<uintptr_t>(0x0F);

            uintptr_t rsp = stack_top - sizeof(void *);
            *reinterpret_cast<void **>(rsp) = reinterpret_cast<void *>(&FiberEntryReturnAbort);

            ctx.rsp = reinterpret_cast<void *>(rsp);
            ctx.rip = reinterpret_cast<void *>(entry);
        }

        void SwapContext(Context &from, Context &to)
        {
            sylar_coctx_swap(&from, &to);
        }

        const char *BackendName()
        {
            return "libco_asm";
        }

#elif defined(SYLAR_FIBER_CONTEXT_BACKEND_UCONTEXT)

        void InitMainContext(Context &ctx)
        {
            if (getcontext(&ctx.uc))
            {
                SYLAR_ASSERT2(false, "getcontext");
            }
        }

        void InitChildContext(Context &ctx, void *stack, size_t stack_size, EntryFunc entry)
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

        void SwapContext(Context &from, Context &to)
        {
            if (swapcontext(&from.uc, &to.uc))
            {
                SYLAR_ASSERT2(false, "swapcontext");
            }
        }

        const char *BackendName()
        {
            return "ucontext";
        }

#else
#error "No fiber context backend selected"
#endif

    } // namespace fiber_context
} // namespace sylar
