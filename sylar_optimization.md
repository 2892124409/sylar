# Sylar Optimization Notes

## 协程上下文切换优化专题 (2026-03-21)
- 目标: 理解协程切换的底层原理，以及 `ucontext` 与手写汇编切换的差异。
- 范围: 当前项目中的 `fiber_context` 抽象层与 `x86_64` 汇编后端。

---

## 1. 协程的基本原理

### 1.1 协程是什么
协程（Fiber/Coroutine）可以理解为“用户态可暂停函数”。  
它不是由内核抢占调度，而是由程序在明确的切换点（如 `resume/yield`）主动切换。

### 1.2 协程为什么能“暂停再继续”
关键是保存执行现场并在未来恢复：

1. 保存“下一条要执行的指令位置”（程序计数器）
2. 保存“当前栈位置”（栈指针）
3. 保存必要寄存器
4. 协程自己的栈内存继续保留（局部变量、调用链都还在）

因此，恢复时就像“读档”，会从上次 `yield` 后的位置继续执行。

### 1.3 在本项目中的切换路径
上层统一调用 `fiber_context::SwapContext(from, to)`，不关心后端实现是 `ucontext` 还是 `asm`：

- `Fiber::resume()`：从调度协程或线程主协程切到目标协程
- `Fiber::yield()`：从当前协程切回调度协程或线程主协程

---

## 2. CPU 现场到底是什么

### 2.1 直观理解
“CPU 现场”就是让代码能无损继续执行的一组状态快照。  
它至少包含：

- 指令地址（下一条要执行哪里）
- 栈顶地址（当前函数栈在哪里）
- 需要跨函数保留的寄存器值

### 2.2 扩展理解
完整机器上下文在更广义上还可能包含：

- 通用寄存器组
- 标志寄存器（如算术标志位）
- 浮点/SIMD 状态
- 信号屏蔽字等进程/线程上下文信息

不同切换方案保存范围不同：  
保存越全，兼容性和通用性越好；保存越少，切换通常越快。

---

## 3. ucontext 保存了哪些内容

### 3.1 `ucontext_t` 的核心字段（语义层）
在 Linux/glibc 语义里，`ucontext_t` 通常包含：

1. `uc_mcontext`：机器上下文（寄存器等）
2. `uc_stack`：上下文使用的栈描述
3. `uc_link`：该上下文返回后的后继上下文
4. `uc_sigmask`：信号屏蔽字

### 3.2 这意味着什么
`ucontext` 是“通用、完整语义”的用户态上下文 API。  
它要处理的不只是最小协程切换寄存器，还包括信号相关语义和更完整的上下文状态，所以抽象层更重、处理路径更长。

---

## 4. 当前版本的汇编保存了哪些内容

当前项目 `libco_asm` 后端的 `Context` 仅保存 8 个字段（x86_64）：

1. `rsp`：栈指针。决定“当前调用栈顶部在哪里”。
2. `rip`：指令指针。决定“下一条从哪里开始执行”。
3. `rbx`：被调用者保存寄存器，常用于长期临时值/基址保存。
4. `rbp`：传统帧指针（函数栈帧链路常用）。
5. `r12`：被调用者保存寄存器，常用于长期临时值。
6. `r13`：被调用者保存寄存器，常用于长期临时值。
7. `r14`：被调用者保存寄存器，常用于长期临时值。
8. `r15`：被调用者保存寄存器，常用于长期临时值。

### 4.1 为什么只存这些
这是按 x86_64 SysV ABI 的“被调用者保存寄存器”集合做的最小切换实现：  
调用者保存寄存器（如 `rax/rcx/rdx/rsi/rdi/r8-r11`）由编译器在调用边界按规则处理，不需要由协程切换函数额外完整托管。

### 4.2 首次进入协程如何启动
`InitChildContext` 里会：

- 设置 `ctx.rip = entry`（协程入口函数）
- 设置 `ctx.rsp` 到协程栈顶并压入一个兜底返回地址（意外 `return` 时触发断言）

这样第一次切入该协程时，会直接跳到入口函数执行。

---

## 5. 为什么汇编少保存很多内容也能正常切换

### 5.1 核心原因：协作式、受控切换点
协程切换发生在明确的函数调用边界（`SwapContext`），不是异步中断抢占。  
在这种受控场景里，“最小可恢复集合”就足够保证执行正确性。

### 5.2 ABI 保证在这里很关键
编译器和 ABI 共同保证了：

- 被调用者保存寄存器在函数调用前后需保持
- 调用者保存寄存器由调用方自行处理

因此，只要切换函数正确保存/恢复被调用者保存集 + `rsp/rip`，就能在语义上恢复到正确执行点。

### 5.3 协程栈本身已经保留大量状态
函数局部变量、调用链返回地址在协程栈里本来就还在。  
汇编切换只需把“CPU 该看哪块栈、下一步去哪条指令”恢复出来即可继续跑。

---

## 6. 为什么汇编切换比 ucontext 快

### 6.1 保存/恢复的数据更少
`ucontext` 的语义覆盖面更大，状态更全；  
手写汇编只处理最小必要集合，内存读写明显更少。

### 6.2 路径更短、抽象层更薄
手写汇编切换是固定偏移读写寄存器 + `jmp`，路径极短；  
`ucontext` 需要经过更通用的库实现路径，通用性带来额外开销。

### 6.3 无额外通用语义负担
`ucontext` 设计目标是“通用用户上下文机制”；  
手写汇编目标是“仅服务本协程运行时模型”，因此能够针对场景极致裁剪。

### 6.4 本项目实测结论（microbenchmark）
在双 Fiber ping-pong、单线程、绑核条件下：

- `ucontext`：`~264.81 ns/switch(net)`
- `libco_asm`：`~7.03 ns/switch(net)`
- 加速比：`~37.65x`

这与“最小保存集合 + 超短切换路径”的预期一致。

---

## 学习建议（阅读顺序）

1. `sylar/fiber/fiber.cc`：先看谁在调用 `SwapContext`
2. `sylar/fiber/context.h`：看 `Context` 数据结构差异
3. `sylar/fiber/context.cc`：看 `ucontext` 与 `asm` 双实现入口
4. `sylar/fiber/coctx_swap_x86_64.S`：逐行对应偏移理解保存/恢复

---

## 7. 一次真实调用链时序图（`tests/test_fiber.cc`）

### 7.1 参与者

- 主协程（线程原生栈）
- 子协程 Fiber（`m_stack` 独立栈）
- `SwapContext`（`ucontext` 或 `asm` 后端）

### 7.2 时序（按 `test_fiber` 实际路径）

1. `T0`：主协程执行 `test_fiber()`，当前使用线程主栈。  
   代码点：`sylar::Fiber::GetThis();`
2. `T1`：创建子协程，分配 `m_stack`。  
   代码点：`Fiber::Fiber(cb)` -> `m_stack = malloc(...)`。
3. `T2`：第一次 `fiber->resume()`。  
   `SwapContext(main_ctx, child_ctx)`，CPU `rsp/rip` 切到子协程。
4. `T3`：子协程开始跑 `Fiber::MainFunc -> m_cb(run_in_fiber)`。  
   在子协程栈上形成新的函数栈帧（返回地址、局部变量、临时值）。
5. `T4`：`run_in_fiber()` 调 `YieldToHold()`。  
   `SwapContext(child_ctx, main_ctx)`，保存子协程 `rsp/rip`，回到主协程 `resume()` 下一行。
6. `T5`：第二次 `fiber->resume()`。  
   恢复 `child_ctx`，从上次 `yield` 后继续执行（不是重头开始）。
7. `T6`：子协程执行完毕，`MainFunc` 收尾切回主协程，状态变 `TERM`。  
   后续 `reset()` 会复用同一块 `m_stack`，重新初始化入口再跑。

### 7.3 双栈快照（抽象）

```text
时刻 A：刚创建子协程，尚未执行

主协程栈（线程栈）:
  [ ... ]
  [ test_fiber 栈帧 ]
  [ fiber->resume 调用点 ]

子协程栈（m_stack）:
  [ 空白可用区 ... ]
  [ 栈顶对齐 ]
  [ 兜底返回地址 FiberEntryReturnAbort ]   <- InitChildContext 预置
```

```text
时刻 B：子协程运行到 YieldToHold 后暂停

主协程栈（线程栈）:
  [ ... ]
  [ test_fiber 栈帧 ]                     <- 已恢复执行
  [ fiber->resume 返回后的下一行 ]

子协程栈（m_stack）:
  [ Fiber::MainFunc 栈帧 ]
  [ run_in_fiber 栈帧 ]                   <- 停在 YieldToHold 之后
  [ 其局部变量/临时值/返回地址 ]
```

### 7.4 “协程栈内容从哪里来”

不是手工把业务数据 memcpy 到栈里。  
来源是 CPU 在该栈上真实执行函数时自动形成的：

1. 函数调用时，`call` 指令自动压入返回地址
2. 函数序言调整 `rsp`，为局部变量留空间
3. 编译器把需要落栈的临时值、溢出寄存器、参数写入这块栈

`yield` 时只保存“怎么回到这里”（`rsp/rip` + 必要寄存器），  
栈内存本身不丢，下次 `resume` 恢复 `rsp` 后继续用原栈帧。

---

## 8. `extern "C"` 与 `.S` 文件的关系（开发笔记补充）

### 8.1 这行代码到底是什么

```cpp
extern "C" void sylar_coctx_swap(Context* from, const Context* to);
```

这行是**函数声明**，不是函数实现。  
它告诉 C++ 编译器：

1. 有一个外部函数名叫 `sylar_coctx_swap`
2. 参数是 `Context*` 和 `const Context*`
3. 使用 C 链接方式（不做 C++ 名字改编）

### 8.2 真正实现在哪里

实现在汇编文件 `sylar/fiber/coctx_swap_x86_64.S`：

- `.globl sylar_coctx_swap`
- `sylar_coctx_swap:` 标签下的指令序列

链接阶段会把 C++ 调用点和这个汇编符号名对上。

### 8.3 常见误区

Q: 这是不是“用 C++ 函数代替汇编”？  
A: 不是。  
你是在 **C++ 里调用汇编实现**。`extern "C"` 只是桥接声明。

Q: 如果 `.S` 没被编进来会怎样？  
A: 链接报错：`undefined reference to sylar_coctx_swap`。

---

## 9. 看懂 `coctx_swap_x86_64.S` 的汇编基础

### 9.1 先记住 3 个前置知识

1. 平台约定：x86_64 SysV ABI（Linux 常见）  
2. 函数参数寄存器：第 1 个参数在 `rdi`，第 2 个在 `rsi`  
3. 栈向低地址增长，`rsp` 指向当前栈顶

在本函数里：

- `rdi = from`
- `rsi = to`

### 9.2 寄存器速查（本文件用到的）

- `rsp`：栈指针（当前栈顶）
- `rip`：指令位置（通过返回地址或跳转目标体现）
- `rbx/rbp/r12/r13/r14/r15`：被调用者保存寄存器
- `rax`：临时寄存器（本函数中拿来中转）

### 9.3 这几个指令怎么读

- `movq A, B`：把 A 的 8 字节值写到 B
- `leaq X, rax`：计算地址 X，写入 `rax`（不读内存）
- `jmp *%rax`：无条件跳到 `rax` 指向的地址执行

### 9.4 内存寻址怎么理解

形如 `16(%rdi)` 的意思是：  
以 `rdi` 为基址，加偏移 16 的内存位置。  
对应到 `Context` 就是 `from + 16` 字节处字段（`rbx`）。

### 9.5 逐段读 `sylar_coctx_swap`

第一段：保存当前上下文到 `from`

1. `leaq 8(%rsp), %rax`  
   算出“如果当前函数返回，返回后栈顶会在哪”（`rsp + 8`）。
2. `movq %rax, 0(%rdi)`  
   存到 `from->rsp`。
3. `movq (%rsp), %rax`  
   取当前栈顶上的返回地址（可视作当前 `rip`）。
4. `movq %rax, 8(%rdi)`  
   存到 `from->rip`。
5. 后续 `movq %rbx/%rbp/%r12.., offset(%rdi)`  
   依次保存被调用者寄存器。

第二段：从 `to` 恢复目标上下文

1. `movq offset(%rsi), reg`  
   依次恢复 `rbx/rbp/r12/r13/r14/r15`。
2. `movq 0(%rsi), %rsp`  
   切换到目标协程栈。
3. `movq 8(%rsi), %rax`  
   取目标执行地址 `to->rip`。
4. `jmp *%rax`  
   直接跳过去执行（不再返回到本函数下一行）。

### 9.6 为什么这里用 `jmp` 而不是 `ret`

因为我们要跳去“另一个上下文”的 `rip`，  
它不是当前函数正常调用链的返回地址。  
`jmp *%rax` 是最直接的“切换执行流”。

### 9.7 把这段汇编当成伪代码

```cpp
void sylar_coctx_swap(Context* from, const Context* to) {
    from->rsp = rsp + 8;
    from->rip = *(void**)rsp;
    from->rbx = rbx; from->rbp = rbp;
    from->r12 = r12; from->r13 = r13; from->r14 = r14; from->r15 = r15;

    rbx = to->rbx; rbp = to->rbp;
    r12 = to->r12; r13 = to->r13; r14 = to->r14; r15 = to->r15;
    rsp = to->rsp;
    goto *to->rip;
}
```

---
