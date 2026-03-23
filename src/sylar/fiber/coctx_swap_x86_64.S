    .text
    .globl sylar_coctx_swap
    .type sylar_coctx_swap, @function
sylar_coctx_swap:
    # Save current context (from)
    leaq 8(%rsp), %rax
    movq %rax, 0(%rdi)
    movq (%rsp), %rax
    movq %rax, 8(%rdi)
    movq %rbx, 16(%rdi)
    movq %rbp, 24(%rdi)
    movq %r12, 32(%rdi)
    movq %r13, 40(%rdi)
    movq %r14, 48(%rdi)
    movq %r15, 56(%rdi)

    # Restore target context (to)
    movq 16(%rsi), %rbx
    movq 24(%rsi), %rbp
    movq 32(%rsi), %r12
    movq 40(%rsi), %r13
    movq 48(%rsi), %r14
    movq 56(%rsi), %r15
    movq 0(%rsi), %rsp
    movq 8(%rsi), %rax
    jmp *%rax

    .size sylar_coctx_swap, .-sylar_coctx_swap

    .section .note.GNU-stack,"",@progbits
