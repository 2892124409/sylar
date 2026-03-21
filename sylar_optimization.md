# Sylar Optimization Notes

## 0. 文档说明

这份文档不是单纯的开发日志，而是按“源码理解 + 性能优化复盘 + 面试表达”三个目标来整理的。

我目前把项目里的优化拆成两个阶段：

| 阶段 | 核心改动 | 主要解决的问题 |
| --- | --- | --- |
| 第一阶段 | 协程上下文切换从 `ucontext` 改成手写汇编 | 切换路径太重，单次切换开销高 |
| 第二阶段 | 调度器、IOManager、Timer、hook、`TcpServer` 拓扑重构 | 全局锁竞争、共享 `epoll` 争用、全局 timer 热点、`accept` 线程模型不清晰 |

### 0.1 阅读建议

如果是为了准备面试，建议按这个顺序读：

1. 第一部分的 `1.1` 和 `1.5`，先拿到“上下文切换为什么快”的主线。
2. 第二部分的 `2.1`、`2.2`、`2.3`、`2.4`，先拿到“网络层为什么重构”的主线。
3. 第二部分的 `2.6`、`2.7`，看这轮最容易被面试官追问的语义细节。
4. 最后再回来看时序图、汇编伪代码和下一阶段优化方向。

### 0.2 关键代码落点

- 协程切换：`sylar/fiber/context.*`、`sylar/fiber/coctx_swap_x86_64.S`
- 调度器：`sylar/fiber/scheduler.*`
- IO 事件：`sylar/fiber/iomanager.*`
- 定时器：`sylar/fiber/timer.*`
- hook / 等待语义：`sylar/fiber/hook.cc`、`sylar/fiber/fiber.*`
- 服务端样例：`sylar/net/tcp_server.cc`、`tests/test_tcp_server.cc`
- benchmark：`tests/test_benchmark_tcp_allocator.cc`、`tests/test_benchmark_fiber.cc`

---

## 第一部分：协程上下文切换优化专题 (2026-03-21)

### 1.1 优化背景与结论

第一阶段做的事情很明确：把协程上下文切换从 `ucontext` 路径改成了手写汇编路径。

目标不是“功能变多”，而是把协程切换这条热路径瘦身：

- 少保存不必要的上下文
- 少走库层的通用逻辑
- 让 `resume/yield` 更接近“最小必要切换”

最终结论是：在协作式协程场景里，手写汇编只保存最小必要寄存器集合就够了，而且性能收益非常明显。

### 1.2 协程为什么能“暂停再继续”

协程本质上是“用户态可暂停函数”。  
它不是被内核抢占，而是在明确的切换点主动让出执行权，比如：

- `Fiber::resume()`
- `Fiber::yield()`
- `Fiber::YieldToHold()`
- `Fiber::YieldToReady()`

之所以能“下次从上次的位置继续”，核心靠两类信息：

1. CPU 现场
   - 下一条指令在哪里执行
   - 当前栈顶在哪里
   - 哪些寄存器值要恢复
2. 协程自己的栈
   - 局部变量还在
   - 函数调用链还在
   - 返回地址还在

所以恢复协程时，本质上是“把 CPU 看向原来那块栈，并从原来的指令地址继续跑”。

### 1.3 CPU 现场与 `ucontext`

“CPU 现场”可以先粗暴理解成一份“继续执行所需的快照”。

最低限度包括：

- 指令位置
- 栈顶位置
- 需要跨函数保存的寄存器

`ucontext_t` 的语义更完整，通常包含：

1. 机器上下文
2. 栈描述
3. 后继上下文
4. 信号屏蔽字

这也是为什么 `ucontext` 适合做“通用用户态上下文机制”，但不适合在高频协程切换里做极致性能方案。  
它保存得更全，抽象更重，通用性更强，但热路径更长。

### 1.4 当前汇编后端到底保存了什么

当前 `x86_64` 汇编后端保存的是最小必要集合：

1. `rsp`
2. `rip`
3. `rbx`
4. `rbp`
5. `r12`
6. `r13`
7. `r14`
8. `r15`

这套选择直接对应 `x86_64 SysV ABI` 的“被调用者保存寄存器”集合，再加上控制执行流所必须的 `rsp/rip`。

首次切入协程时，`InitChildContext` 会把：

- `rip` 设成协程入口函数
- `rsp` 设到协程栈顶
- 再压一个兜底返回地址

这样第一次切进去时就能直接开始执行。

### 1.5 为什么只保存这些也能正确工作

这个问题是面试里最常见的追问。

核心原因有三个：

#### 1.5.1 这是协作式切换，不是抢占式中断

切换发生在明确的函数调用边界，不是 CPU 在任意一条指令中间被打断。  
因此不需要像内核抢占那样托管一整套“全量上下文”。

#### 1.5.2 ABI 已经帮我们定义了寄存器责任边界

在 `x86_64 SysV ABI` 下：

- 被调用者保存寄存器要在函数前后保持不变
- 调用者保存寄存器由调用方自己负责

协程切换函数本质上也是一种“受 ABI 约束的切换边界”，所以只保存被调用者保存集是成立的。

#### 1.5.3 真正的大量业务状态本来就躺在协程栈里

很多人会误以为“协程切换需要把整个函数状态拷来拷去”。其实不用。  
局部变量、临时值、返回地址、调用链已经天然在协程栈里。

协程切换真正需要做的是：

- 记住从哪条指令继续
- 记住回到哪块栈
- 把少量必须恢复的寄存器还原

### 1.6 为什么手写汇编切换更快

性能优势主要来自三点：

#### 1.6.1 保存和恢复的数据更少

`ucontext` 的语义更全，处理的上下文也更多。  
手写汇编只处理最小必要集合，内存读写明显减少。

#### 1.6.2 调用路径更短

手写汇编几乎就是：

- 固定偏移写寄存器
- 固定偏移读寄存器
- `jmp` 到目标地址

没有额外的通用库层语义负担。

#### 1.6.3 场景更专用

`ucontext` 是通用 API，汇编后端是“只服务本项目协程模型”的专用实现。  
通用性和极致性能往往是互相拉扯的。

#### 1.6.4 当前版本的 microbenchmark 结论

在双 Fiber ping-pong、单线程、绑核条件下，实测数据是：

- `ucontext`：`~264.81 ns/switch(net)`
- 汇编后端：`~7.03 ns/switch(net)`
- 加速比：`~37.65x`

这个数量级是符合预期的。

### 1.7 一次真实调用链时序（以 `test_fiber` 为例）

一个典型时序是这样的：

1. 主协程执行 `Fiber::GetThis()`，建立线程主协程。
2. 创建子协程，分配独立 `m_stack`。
3. 第一次 `fiber->resume()`，切到子协程栈。
4. 子协程进入 `Fiber::MainFunc()`，开始执行业务函数。
5. 业务函数里调用 `YieldToHold()`。
6. 切回主协程，主协程从 `resume()` 之后继续跑。
7. 第二次 `resume()`，恢复上次子协程保存的 `rsp/rip`，从上次暂停点继续执行。
8. 子协程执行结束，状态变成 `TERM`，再切回主协程。

可以把它想成两个独立栈之间来回切换：

```text
主协程栈:
  test_fiber()
    -> fiber->resume()
    -> resume 返回后的下一行

子协程栈:
  Fiber::MainFunc()
    -> user_cb()
    -> YieldToHold() 之后的位置
```

### 1.8 `extern "C"` 与 `.S` 文件的关系

像下面这行：

```cpp
extern "C" void sylar_coctx_swap(Context* from, const Context* to);
```

它只是声明，不是实现。

它的作用是告诉 C++ 编译器：

- 这个函数符号叫 `sylar_coctx_swap`
- 参数长这样
- 按 C 链接方式导出，不做 C++ 名字改编

真正的实现是在汇编文件 `.S` 里。  
链接阶段，C++ 调用点会和汇编里的全局符号名对上。

### 1.9 看懂 `coctx_swap_x86_64.S` 的最低门槛

只要记住三个前置知识就够了：

1. 第 1 个参数在 `rdi`，第 2 个参数在 `rsi`
2. 栈向低地址增长
3. `rsp` 是当前栈顶，`rip` 是下一条指令地址

把 `sylar_coctx_swap` 当成伪代码看，其实就是：

```cpp
void sylar_coctx_swap(Context* from, const Context* to) {
    from->rsp = rsp + 8;
    from->rip = *(void**)rsp;
    from->rbx = rbx; from->rbp = rbp;
    from->r12 = r12; from->r13 = r13;
    from->r14 = r14; from->r15 = r15;

    rbx = to->rbx; rbp = to->rbp;
    r12 = to->r12; r13 = to->r13;
    r14 = to->r14; r15 = to->r15;
    rsp = to->rsp;
    goto *to->rip;
}
```

### 1.10 面试怎么讲

第一部分适合讲成“我先优化协程 runtime 的最热路径”。

一个 30 秒版本：

> 我第一轮优化先不碰业务逻辑，专门处理协程上下文切换这条热路径，把通用 `ucontext` 切换换成了手写汇编实现。原因是这里是协作式切换，不需要保存完整通用上下文，只要保存 `rsp/rip` 和被调用者保存寄存器就够了，所以状态更少、路径更短，实测切换开销有明显下降。

一个 2 分钟版本最好覆盖四点：

1. 为什么 `ucontext` 慢
   - 通用语义重，保存的信息更多，路径更长。
2. 为什么汇编方案安全
   - 这里是受控的协作式切换，不是异步抢占；再加上 ABI 保证，只保存最小集合就成立。
3. 为什么不是“汇编天然快”
   - 真正快的原因是 runtime 模型被收窄了，不是因为“手写汇编”四个字本身。
4. 你真的看过底层吗
   - 可以把 `coctx_swap` 直接翻成伪代码，说清楚它就是“保存当前上下文、恢复目标上下文、跳到目标 rip”。

这一部分最常见的追问关键词有三个：

- “协作式切换”
- “callee-saved / caller-saved”
- “最小保存集合”

---

## 第二部分：协程网络层与调度优化专题 (2026-03-21)

### 2.1 这一轮到底在解决什么问题

第二阶段不是单点优化，而是把整个协程网络层里几个互相耦合的热点一起拆开：

1. `Scheduler` 用的是全局任务链表 + 全局锁
2. `IOManager` 用的是共享 `epoll` + 共享唤醒管道
3. `TimerManager` 用的是全局定时器集合
4. `use_caller` 更像“停机时顺便进 run loop”，不适合当主线程 Reactor

这些问题叠在一起，高并发下会出现几个典型现象：

- 大量线程竞争同一把任务队列锁
- 所有 IO 事件增删改查都打到一个 `epoll`
- 所有唤醒都写同一组 wakeup fd
- 所有 timer 都走同一个全局容器
- `accept` 线程模型不够清晰，主线程不是真正意义上的 Reactor

### 2.2 优化后的整体架构

优化后的推荐拓扑是：

```text
main thread
  accept_iom(1, true)
    runCaller()
    - accept fibers
    - epoll_0
    - eventfd_0
    - timer_heap_0

io worker threads
  io_iom(N, false)
    worker_i:
      - localQueue
      - remoteQueue
      - work stealing
      - epoll_i
      - eventfd_i
      - timer_heap_i
      - handleClient/read/write/timer callbacks
```

也就是说：

- 主线程主要负责 `accept`
- 连接建立后，把 client 投递到 IO worker 池处理
- 每个 worker 自己维护任务、IO 等待和 timer

这套结构已经很接近“主 Reactor + 子 Reactor”。

### 2.3 Scheduler 重构：从全局任务链表到线程局部队列

#### 2.3.1 旧版本的问题

旧版本的 `Scheduler` 核心是：

- 一个全局 `m_fibers`
- 一个全局互斥锁
- 所有线程 schedule / 抢任务 都围绕这个全局容器转

这在功能上没问题，但它天然是一个争用点。

#### 2.3.2 新版本的数据结构

现在 `Scheduler` 里每个 worker 都有：

- `localQueue`
- `remoteQueue`
- `sleeping` 标志

此外调度器层面还有：

- `m_pendingTaskCount`
- `m_activeThreadCount`
- `m_idleThreadCount`
- `m_workerCursor`
- `m_callerActive`

核心变化是：**全局任务链表已经没有了。**

#### 2.3.3 任务现在怎么流动

现在的任务流动大致是这样：

1. 如果任务投递给当前 worker 自己，直接进 `localQueue`
2. 如果是跨线程投递，进目标 worker 的 `remoteQueue`
3. worker 执行前先把 `remoteQueue` 批量倒入自己的 `localQueue`
4. 如果自己没活，再去别人的 `localQueue` 里偷任务

也就是说：

- 别的线程**可以投递**任务给某个 worker
- 但不是直接抢它的 `localQueue` 锁去塞任务，而是写它的 `remoteQueue`
- 别的线程**也可以偷任务**
- 但偷的是对方 `localQueue` 里没有线程亲和性的任务

这正好对应你前面问过的两个点：

1. 现在没有全局任务链表了吗？
   - 对，已经没有全局任务链表了。
2. 每个线程本地队列，别的线程还能投递和偷取吗？
   - 能。投递走 `remoteQueue`，偷取走 `localQueue`。

#### 2.3.4 为什么要分成 `localQueue` 和 `remoteQueue`

因为这两种写入场景的冲突模型不一样：

- `localQueue` 主要是 worker 自己频繁 push/pop
- `remoteQueue` 主要是别的线程偶发跨线程投递

把它们分开以后，worker 自己处理本地任务时不必总和远端投递者抢同一把锁。

#### 2.3.5 work stealing 的边界

work stealing 不是“什么都偷”，而是有边界的：

- 只偷没有显式线程亲和性的任务
- 优先自己本地执行
- 只有本地空了才偷

所以它的目标不是平均分配一切，而是：

- 尽量局部化
- 但在局部化失败时提供负载兜底

#### 2.3.6 `use_caller` 语义这次也顺手补正了

新版本新增了 `Scheduler::runCaller()`，caller 线程不再需要靠 `stop()` 阶段“顺便进 run loop”。  
这让主线程可以真正承担一个 Reactor。

同时又补了一条很重要的语义：

- 当 `use_caller=true`
- 且 caller 还没有真正进入 run loop
- 如果还有其他 worker 可用
- 那么未绑定线程的任务默认不会先投到 caller worker

否则任务会提前堆在 caller worker 上，caller 线程还没开始跑，就会形成“任务被卡在入口处”的问题。

### 2.4 IOManager 重构：从共享 `epoll` 到每线程独立 `epoll`

#### 2.4.1 旧版本的问题

旧版本的 `IOManager` 是一个共享 `epoll`：

- 全部 fd 都注册到同一个 `m_epfd`
- 全部唤醒都走同一组 `m_tickleFds`
- 事件上下文也都集中在同一套调度路径上

这意味着：

- `epoll_ctl` 热点集中
- wakeup 热点集中
- 所有 IO 事件调度天然更容易互相打架

#### 2.4.2 新版本的基本结构

现在每个 worker 都有自己的：

- `epfd`
- `wakeFd`

也就是每线程独立：

- 一套 `epoll`
- 一套 `eventfd`

对应数据结构是 `WorkerContext`。

#### 2.4.3 fd owner 绑定

新版本引入了一个很关键的概念：`ownerWorker`。

某个 fd 第一次注册事件时，会绑定一个 owner worker：

- 如果当前就在某个 worker 线程里，就优先绑定当前 worker
- 否则用 round-robin 分配一个 worker

绑定之后，这个 fd 后续的：

- `addEvent`
- `delEvent`
- `cancelEvent`
- timeout 处理

都会围绕这个 owner worker 走。

这相当于给 fd 建了线程归属。

#### 2.4.4 事件是怎么被唤醒并恢复协程的

新版本的路径是：

1. `addEventInternal` 把事件注册到 owner 的 `epoll`
2. 同时在 `FdContext::EventContext` 里记住：
   - scheduler
   - fiber / cb
   - timeoutTimer
   - thread
   - waitToken
3. owner worker 的 `idle()` 里跑自己的 `epoll_wait`
4. 事件到了以后，`triggerEvent()`：
   - 取消 timeoutTimer
   - 设置 wait 结果
   - 把 fiber 或回调重新 schedule 回去

因为每个 worker 有自己的 `epoll`，所以事件分发天然更局部化。

#### 2.4.5 为什么 `eventfd` 比共享 pipe 更合适

新版本用的是每 worker 一个 `eventfd`，而不是共享管道。

这样做的好处是：

- 唤醒目标更明确
- 写入模型更简单
- 少掉共享 wakeup 点上的争用

它本质上就是“把 tickle 也做线程局部化”。

### 2.5 Timer 重构：从全局 `set` 到每线程最小堆

#### 2.5.1 旧版本的问题

旧版本 `TimerManager` 的核心是：

- 一个全局 `std::set<Timer::ptr>`
- 一把全局读写锁

这有两个问题：

1. 所有 timer 插入、删除、取最近超时都走同一处
2. `set` 的数据结构和缓存局部性对高频 timer 热路径并不友好

#### 2.5.2 新版本的结构

新版本把 timer 改成了按 worker 分桶：

- `m_buckets`
- 每个 bucket 一个最小堆 `heap`
- 每个 timer 记录自己属于哪个 worker

timer 的关键字段现在有：

- `m_worker`
- `m_heapIndex`
- `m_next`
- `m_sequence`

这意味着 timer 不是全局漂浮的，而是天然带 worker 归属。

#### 2.5.3 为什么改成最小堆

最小堆在这里很适合“只关心最近一个到期 timer”的场景：

- 取最近 timer：看堆顶
- 插入/删除：`O(logN)`
- 数据局部性比树结构更好

同时 `m_sequence` 作为打平局的次序号，保证相同过期时间下的稳定顺序。

#### 2.5.4 为什么改用 `CLOCK_MONOTONIC`

旧版本更偏 wall clock 语义，还要考虑“系统时间回拨”。

新版本用单调时钟：

- 不受手工校时和 NTP 调整影响
- 对 timeout/timer 语义更稳定
- 更适合 runtime 内部的相对时间管理

#### 2.5.5 Timer 和 IO worker 为什么要绑定

这次 timer 的真正价值，不只是“容器更快”，而是和 IO worker 局部化一起形成闭环：

- 某个 worker 上注册的 IO 等待
- 其 timeout timer 也尽量落在同一个 worker
- `idle()` 在 `epoll_wait` 之后，顺手取本 worker 的过期 timer 回调

这样 timer 不再是“全局旁路系统”，而是直接融进每个 worker 的事件循环里。

### 2.6 Hook / wait 路径修正：从“隐式改 `errno`”到显式等待结果

#### 2.6.1 旧版本的问题

旧版本 `do_io()` / `connect_with_timeout()` 的 timeout 路径，本质上是：

- 定时器回调触发
- 回调里取消 IO 事件
- 再通过副作用去影响等待方的错误语义

这种模型能工作，但语义有点“绕”：

- timeout 结果不是协程显式拿到的
- 协程恢复后要靠外部副作用判断是不是超时
- 当 timer、cancel、resume 交织时，理解和维护成本偏高

#### 2.6.2 新版本怎么做

新版本在 `Fiber` 里引入了：

- `WaitResult`
- `waitToken`

等待流程变成：

1. `waitEvent()` 给当前 fiber 生成一个 `waitToken`
2. `addEventInternal()` 把这个 token 记到 `EventContext`
3. 如果发生：
   - 正常 IO ready -> `WAIT_READY`
   - timeout -> `WAIT_TIMEOUT`
   - cancel -> `WAIT_CANCELLED`
4. `triggerEvent()` 在恢复 fiber 前先把结果写回 fiber
5. `waitEvent()` 恢复后显式 `consumeWaitResult(token)`

这样等待结果不再是“猜出来的”，而是“明确传回来的”。

#### 2.6.3 为什么这比直接改 `errno` 更干净

因为 `errno` 是线程局部错误码，不适合拿来承载完整的等待状态机。  
新版本把“等待结果”从 `errno` 里拆出来以后，语义边界更清楚：

- `WAIT_TIMEOUT` 是协程等待结果
- `errno = ETIMEDOUT` 只是对外保留的 POSIX 风格兼容表现

也就是说：

- 运行时内部看 `WaitResult`
- 对用户接口仍然保持熟悉的 `errno`

#### 2.6.4 这轮还顺手补了一个主线程 Reactor 相关 bugfix

主线程 `accept` 模式下，监听 socket 往往在进入 caller run loop 之前就已经创建好了。  
如果 hook 路径第一次访问这个 fd 时，`FdManager` 里还没有对应 `FdCtx`，`accept()` 可能会退化成阻塞系统调用。

所以新版本在 hook 入口做了“懒注册 `FdCtx`”：

- 只有当当前线程真正进入 hook I/O 路径时，才补建 `FdCtx`
- 这样既能保证主线程 `accept` 进入非阻塞 + epoll 路径
- 又不会把所有非 hook 线程里的普通 socket 语义都提前改掉

这个点很小，但非常工程化。

### 2.7 主线程 `accept` Reactor 化

#### 2.7.1 为什么 `accept` 适合单线程

`accept` 本身不是重 CPU 逻辑，它更像一个轻量分发入口。  
默认把它放在一个线程里，通常更稳妥：

- 模型简单
- 监听 fd 归属清晰
- 少掉多线程 `accept` 之间的协调和抢占

尤其在当前这个协程网络层里，真正重的工作在后面的：

- 读写
- 协议处理
- 业务回调

这些都应该交给 IO worker 池。

#### 2.7.2 当前推荐拓扑

这一轮之后，推荐写法是：

- `accept_iom(1, true)`：主线程 reactor
- `io_iom(N, false)`：真正的 IO worker 池
- `TcpServer(io_worker=&io_iom, accept_worker=&accept_iom)`
- 主线程调用 `accept_iom.runCaller()`

这就对应你前面说的那种 Reactor 结构：

- 主线程负责监听和接入
- 子线程池负责连接处理

#### 2.7.3 `use_caller` 现在能开吗

现在能开，但要分场景理解：

- 对 `accept_iom(1, true)` 这种主线程 Reactor，**推荐开**
- 对纯 worker 池，如果你不打算让主线程真正进入 run loop，就**不要为了开而开**

换句话说，`use_caller` 现在已经不再是“不能碰”的状态了，但它应该对应一个明确角色：

- caller 线程就是一个真实 worker
- 而不是“最后 stop 时顺便跑一圈”

#### 2.7.4 `runCaller()` 为什么重要

没有 `runCaller()` 之前，caller 线程更像“预留席位”。  
有了 `runCaller()` 之后，主线程可以显式进入调度循环。

这带来两个好处：

1. 架构表达更自然
   - main thread 真的是 accept reactor
2. benchmark / test 不需要再靠 guard timer 或 stop hack 去把 caller 拉起来

#### 2.7.5 `TcpServer` 停机路径现在怎么工作

现在主线程 accept 拓扑下，`TcpServer::stop()` 的逻辑是：

1. 先把 `m_isStop` 置成 `true`
2. 再把清理动作 schedule 到 `acceptWorker`
3. 清理动作里：
   - `cancelAll()`
   - `close()` 监听 socket
4. 监听 fd 被关闭后，`startAccept()` 的等待会被唤醒并退出循环

这保证了：

- accept loop 不会永远卡在监听 fd 上
- stop 路径是可控退出，而不是粗暴打断

### 2.8 benchmark 与测试拓扑也跟着改了

这轮不只是改库代码，也把验证方式改成了和新架构一致的拓扑：

- `tests/test_tcp_server.cc`
  - 主线程跑 `accept_iom.runCaller()`
  - `io_iom` 负责客户端连接处理
- `tests/test_benchmark_tcp_allocator.cc`
  - on-mode/off-mode 都不再依赖旧的 guard timer hack
  - caller 线程通过 `runCaller()` 正式进入事件循环

这样 benchmark 测到的，不再是“旧拓扑 + 新局部实现”的混合物，而是完整新架构。

### 2.9 这一轮实际验证了什么

这一轮至少验证了下面几件事：

1. `test_tcp_server`
   - 主线程 accept
   - 多监听端口
   - echo 正常
   - stop 能优雅退出
2. `test_benchmark_tcp_allocator`
   - off-mode 下主线程 accept 拓扑能正常跑通
3. `test_benchmark_fiber`
   - 第一阶段的 fiber 基础能力没有被第二阶段回归破坏

### 2.10 本轮收益与边界

#### 收益

这一轮最核心的收益有四个：

1. 全局任务链表大锁被拆掉了
2. 共享 `epoll/wakeup` 热点被拆成 per-worker 资源
3. timer 从全局资源变成 worker 局部资源
4. `accept` 拓扑明确成了主线程 Reactor

#### 代价和边界

这并不意味着“以后完全没有同步开销”。

当前还存在的代价包括：

- `remoteQueue` 跨线程投递仍然有同步和内存分配成本
- work stealing 天生会带来跨核缓存干扰
- fd 绑定 owner 后，事件局部性更好，但线程归属也更固定

所以第二阶段更准确的描述不是“消灭锁”，而是：

> 把高频共享热点拆散，把大部分热路径局部化。

### 2.11 面试怎么讲

第二部分适合讲成“我把网络层运行时从共享热点架构改成了局部化架构”。

一个 30 秒版本：

> 第二轮我主要处理的是高并发下的共享热点问题。旧版本里任务调度、IO 事件、timer 和 accept 拓扑都偏全局化，所以我把它们统一改成了 per-worker 局部化模型：调度器改成每线程本地队列加远程投递和 stealing，IOManager 改成每线程独立 `epoll + eventfd`，timer 改成每线程最小堆，accept 固定放到主线程 Reactor 上。

一个 2 分钟版本最好按“问题 -> 改法 -> 价值”来讲：

1. 调度器
   - 旧版是全局任务链表大锁。
   - 新版是 `localQueue + remoteQueue + work stealing`。
   - 价值是把大部分任务流动局部化，只在真正跨线程时同步。
2. IOManager
   - 旧版是共享 `epoll + pipe`。
   - 新版是 per-worker `epoll + eventfd`，并给 fd 绑定 owner。
   - 价值是把事件注册、等待、唤醒、恢复尽量放回同一个 worker。
3. Timer
   - 旧版是全局 `set`。
   - 新版是每线程最小堆，且和 worker 事件循环绑定。
   - 价值是 timer 不再是全局旁路系统。
4. Hook / wait
   - 旧版 timeout 更依赖外部副作用。
   - 新版用 `WaitResult + token` 显式表达等待结果。
   - 价值是语义更清楚，也更容易排查复杂时序问题。
5. accept 拓扑
   - 现在主线程通过 `runCaller()` 作为真正的 accept reactor。
   - 连接建立后交给 IO worker 池处理。

如果被追问“你这一轮最本质的设计思想是什么”，最稳的回答是：

> 不是追求绝对无锁，而是把全局共享热点拆成线程局部资源，把高频热路径局部化。

如果被追问“为什么 accept 只放一个线程”，可以直接答：

> `accept` 本身是轻量入口，默认一个线程模型更简单、更稳，真正该扩的是后面的读写和业务处理线程。

---

## 第三部分：下一阶段优化方向

第三阶段我不准备再优先去拆成“一个线程一个 `IOManager` 对象”，而是先把当前这套 per-worker 局部化模型继续收紧成更明确的 ownership 模型。

当前第二阶段虽然已经没有全局任务链表，也把 `epoll/timer/wakeup` 做到了 per-worker，但 worker 之间依然保留了：

- 普通任务互相投递
- work stealing

这说明当前模型本质上还是“局部化线程池”，而不是“强 ownership 的分片模型”。  
第三阶段要解决的，就是把这条边界继续收紧。

### 3.1 第三阶段的目标

第三阶段的目标可以概括成四条：

1. 保留 `accept -> worker` 分发
2. 去掉 worker 之间普通任务互投
3. 去掉 work stealing
4. 把跨线程行为收缩成 mailbox / command queue 语义

也就是说，下一阶段不是“完全没有跨线程通信”，而是：

- 保留入口线程到 worker 的连接分发
- 保留主控线程到 worker 的控制命令
- 取消 worker 和 worker 之间任意扔普通业务任务的能力

### 3.2 为什么第三阶段不先做“一线程一个 IOM 对象”

这个问题要先讲清楚。

当前实现虽然对象层面还是一个 `io_iom(N, false)`，但逻辑层面其实已经是：

- 每个 worker 一套 `epoll`
- 每个 worker 一套 `eventfd`
- 每个 worker 一套 timer bucket
- 每个 worker 一套本地任务队列

所以性能上的“大共享热点”其实已经拆掉了。  
如果第三阶段直接改成“一个线程一个 `IOManager` 实例”，会先遇到一批工程复杂度问题：

- 生命周期管理变复杂
- `TcpServer` 上层接口会更碎
- fd owner / timer owner / stop 路径要重新拆分
- 很多本来在一个调度器内部完成的协作，要变成多个对象之间的协作

但这些额外复杂度，并不会天然带来和第二阶段同量级的性能收益。

所以更合理的顺序是：

1. 先把运行语义收紧
2. 再决定以后要不要真的拆成多个 `IOManager` 实例

### 3.3 为什么要去掉 worker 间普通任务互投

第二阶段里，worker 之间普通任务互投还能工作，但它会破坏几个第三阶段想保住的东西：

- 线程归属稳定性
- cache 局部性
- fd / timer / callback 的 ownership 纯度

如果 worker A 可以随时给 worker B 丢一个普通业务任务，那么即使：

- fd owner 已经绑定了
- timer 已经局部化了
- epoll 已经按 worker 分片了

业务执行路径仍然可能被跨线程打散。

所以第三阶段的方向是：

> worker 默认只处理自己本地拥有的任务，不再接受其他 worker 随手丢过来的普通业务任务。

这会让“谁拥有这个连接 / 这个 timer / 这个回调”变得更稳定。

### 3.4 为什么要去掉 work stealing

work stealing 在第二阶段是有价值的，因为它给局部化模型补了一个负载均衡兜底。  
但如果第三阶段更强调 ownership，那么 stealing 反而会开始和目标冲突：

- 它会破坏线程局部性
- 它会增加跨核缓存干扰
- 它会削弱“任务归属”和“连接归属”的一致性

所以第三阶段的默认方向应该是：

- 不再启用 work stealing
- worker 只跑自己的本地任务
- 负载均衡只发生在入口分发阶段

也就是：

- 新连接刚接入时做一次 worker 选择
- 一旦绑定，就尽量不再迁移

这样模型会更接近“分片化 reactor / actor”，而不是共享线程池。

### 3.5 mailbox / command queue 语义到底是什么意思

这不是说“彻底禁止跨线程通信”，而是说：

> 跨线程传递的不再是任意普通任务，而是少数几种明确的命令。

可以把第二阶段的 `remoteQueue` 理解成“远程任务队列”，  
第三阶段则更想把它收敛成“收件箱”。

也就是说，跨线程只允许传这些东西：

- `accept -> worker`：新连接移交
- 管理线程 -> worker：停止、取消、关闭 fd
- 必要时：唤醒 worker 处理 mailbox

而不再允许这种事情：

- worker A 直接给 worker B 塞一个普通业务 callback
- worker A 指望 worker B 帮它跑一段无归属的普通任务

如果把它写成抽象语义，可以理解成：

```cpp
enum class WorkerCmdType {
    NewConnection,
    CancelFd,
    StopWorker,
};
```

```cpp
struct WorkerCmd {
    WorkerCmdType type;
    int fd;
    Socket::ptr sock;
};
```

重点不是这个结构体本身，而是设计边界：

- 远程入口里放的是命令
- 不是任意 fiber / 任意 callback

这会让系统更容易继续往 actor / shard 方向演化。

### 3.6 第三阶段的推荐拓扑

第三阶段的推荐拓扑可以写成：

```text
main thread
  accept reactor
    - accept
    - choose target worker
    - send NewConnection cmd

worker_i
  - local task queue
  - mailbox / command queue
  - epoll_i
  - eventfd_i
  - timer_heap_i
  - no work stealing
  - no ordinary task push from other workers
```

这里最重要的语义变化有三条：

1. 负载均衡主要发生在连接接入那一刻
2. 连接绑定 worker 后，后续读写、timer、callback 尽量留在该 worker
3. 跨线程通信只做“移交”和“控制”，不做普通业务任务外包

### 3.7 第三阶段的实现优先级

如果真按这条路线继续做，我会这样排优先级：

1. 先限制跨线程 `schedule` 的语义
   - 只保留 `accept -> worker`
   - 只保留管理命令 -> worker
2. 去掉 work stealing
3. 把 `remoteQueue` 从“远程任务队列”收敛成 mailbox / command queue
4. 再补更系统的 benchmark / profiling
5. 最后再评估是否需要进一步拆成多个 `IOManager` 实例

这个顺序的好处是：

- 先收紧运行语义
- 再决定对象边界要不要继续拆

也就是先把“系统应该怎么工作”想清楚，再决定“对象要怎么分”。

### 3.8 面试怎么讲

第三阶段最适合讲成“从局部化线程池继续演进到 ownership 更强的分片模型”。

一个简洁版本：

> 下一阶段我不会立刻把系统拆成一个线程一个 `IOManager` 对象，因为当前每个 worker 已经有独立的 `epoll/eventfd/timer` 了，主要热点已经拆掉。更值得优先做的是把运行语义继续收紧：保留 `accept -> worker` 分发，去掉 worker 间普通任务互投和 work stealing，把跨线程通信收缩成 mailbox 里的少数命令，比如新连接移交和控制命令。这样连接、timer、回调的 ownership 会更稳定，系统也会更接近分片化 reactor/actor。

这段回答最好让面试官听到三个关键词：

- `ownership`
- `mailbox / command queue`
- `load balance at ingress`

如果被追问“为什么不直接一个线程一个 IOM”，可以直接答：

> 因为当前已经做到了 per-worker IO 上下文，下一步更关键的是先把跨线程执行语义收紧；否则对象拆得更碎，但运行语义还是松的，收益不会成比例增长。
