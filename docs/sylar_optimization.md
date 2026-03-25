# Sylar Optimization Notes

## 0. 文档说明

这份文档不是单纯的开发日志，而是按“源码理解 + 性能优化复盘 + 面试表达”三个目标来整理的。

我目前把项目里的优化整理成两个阶段（中间过渡方案不展开）：

| 阶段 | 核心改动 | 主要解决的问题 |
| --- | --- | --- |
| 第一阶段 | 协程上下文切换从 `ucontext` 改成手写汇编 | 切换路径太重，单次切换开销高 |
| 第二阶段（当前终态） | 调度器、IOManager、Timer、hook、`TcpServer` 一体化重构 | 全局锁竞争、共享 `epoll`/wakeup 热点、timer 热点、跨线程语义过松、`accept` 拓扑不清晰 |

### 0.1 阅读建议

如果是为了准备面试，建议按这个顺序读：

1. 第一部分的 `1.1` 和 `1.5`，先拿到“上下文切换为什么快”的主线。
2. 第二部分的 `2.1`、`2.2`、`2.3`，先拿到“网络层终态怎么设计”的主线。
3. 第二部分的 `2.4`、`2.6`、`2.8`，看最容易被追问的工程细节和稳定性修复。

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

## 第二部分：网络层终态与 A/B 压测复盘 (2026-03-25)

这一部分只记录当前分支已经落地并验证过的实现，不再描述已废弃的中间方案。

### 2.1 终态目标与约束

当前网络层的目标是：

1. `accept` 与业务 IO 明确分离（主从 Reactor）
2. 单连接生命周期只在一个 IO worker 线程内执行
3. 保持高吞吐（RPS）前提下尽量压制高并发尾延迟（p99）
4. TLS 路径纳入统一压测口径（HTTP/HTTPS 都测）

当前代码约束（硬规则）：

- `TcpServer::start()` 要求 `accept_worker != io_worker`（不同 `IOManager` 实例）
- IO 事件 API（`addEvent/delEvent/cancelEvent/cancelAll/waitEvent`）必须在当前 `IOManager` worker 线程调用；否则返回 `EXDEV`
- `FdCtx` 通过 `bindAffinityIfUnset()` 固化线程亲和，线程不一致时拒绝执行

### 2.2 终态拓扑

```text
accept_iom(1)                    io_iom(N)
  - accept loop                    - worker_0 ... worker_N-1
  - 只负责接入                      - 每 worker 独立 epoll/eventfd/timer bucket
  - 将新连接投递到目标 io 线程         - handleClient + socket IO + timer callback

连接路径：
accept -> select target io thread -> io_worker.schedule(..., thread_id) -> handleClient
```

关键点：

- `accept_worker` 可以向 `io_worker` 投递任务
- IO worker 之间普通业务任务不做任意互投（跨 worker 普通 `schedule` 会降级为本地队列）

### 2.3 Scheduler 终态（P2C + 分层收件箱）

`Scheduler` 关键结构：

- 每 worker 本地 `localQueue`
- 跨线程收件箱两层：
  - `mailboxRing`（有界无锁环）
  - `mailboxFallback`（无界链表兜底）
- `queuedTasks` 原子计数（负载近似值）

关键语义：

1. `selectWorker` 从 RR 升级为 P2C（比较两个候选 `queuedTasks`）
2. `schedule(...)` 默认 `allow_cross_worker=false`
   - 当 worker 内部投递到其他 worker 时，降级回本地队列
3. `scheduleCommand(...)` 保留跨线程命令语义（内部控制路径使用）
4. 仅在目标 worker `sleeping=true` 时 `tickle(worker)`，降低 wakeup 风暴
5. 新增 `getWorkerStatsSnapshot()` 给 `TcpServer` 做接入负载均衡

### 2.4 IOManager 终态（严格线程域 + 显式等待结果）

IO 层当前不是“跨 worker 转发执行”模型，而是“严格线程域”模型：

- 每 worker 独立 `epoll + eventfd`
- 事件操作必须由当前 worker 自己执行
- 不再依赖旧的 `ownerWorker + Semaphore` 跨线程同步路径

等待语义：

1. `waitEvent()` 生成 `waitToken`
2. 事件触发时写入 `WAIT_READY/WAIT_TIMEOUT/WAIT_CANCELLED`
3. fiber 恢复后 `consumeWaitResult(token)` 判定结果
4. 对外 errno 仍保持 POSIX 习惯（如超时映射 `ETIMEDOUT`）

### 2.5 TcpServer 终态（连接感知接入均衡）

`TcpServer` 的接入分配从“简单 RR”收敛到“连接感知打分”：

- 采样指标来自 `io_worker.getWorkerStatsSnapshot()`
- 每线程维护 `inflightClientsByThread`
- 评分：`score = inflight * 8 + queuedTasks`
- 同分用轮转打散

调度流程：

1. `accept` 到新连接
2. 选择目标 `target_thread`
3. 记录 inflight++（`onClientScheduled`）
4. `m_ioWorker->schedule(..., target_thread)` 执行 `handleClient`
5. 连接结束 inflight--（`onClientFinished`）

并在进入 `handleClient` 前再次执行 `FdCtx::bindAffinityIfUnset(current_tid)`，保证单 fd 生命周期线程一致。

### 2.6 TLS 路径状态（`waitEvent` 实验已回滚）

这一轮曾做过一个 TLS 实验：

- 方案：把 `SSL_ERROR_WANT_READ/WANT_WRITE` 从自旋重试改为“等待事件后再重试”
- 结果：在当前高并发短压（5s）口径下，本地实现出现“RPS 下降且部分场景 p99 更高”
- 处理：该实验已回滚，当前代码恢复为原始重试策略

落点文件仍是：`http/ssl/ssl_socket.cc`（本次文档记录的是“实验过程 + 回滚结论”）

### 2.7 压测口径与最新结果

统一口径：`wrk`、高并发、keepalive、`/ping + /echo`、HTTP+HTTPS。

已完成的关键结果：

| 场景 | 结果结论 |
| --- | --- |
| HTTP 高并发（3 轮中位数） | 本地实现 RPS `+24.29% ~ +48.22%`；p99 `-7.71% ~ +5.67%`（基本持平） |
| HTTPS（最终版本，已回滚） | 本地实现 RPS `+20.99% ~ +52.31%`；p99 `+21.89% ~ +191.37%`（明显更差） |
| HTTPS（`waitEvent` 实验，未保留） | 本地实现 RPS `+14.92% ~ +43.24%`；p99 `+48.77% ~ +245.42%`（更差） |

补充（仅看本地 HTTPS，`waitEvent` 实验对比）：

- `echo_raw@1024`：p99 `186.01 -> 172.85 ms`（下降）
- `ping@1024`：p99 `219.43 -> 215.05 ms`（小幅下降）
- `ping@2048`：p99 `491.88 -> 496.11 ms`（基本持平）
- `echo_raw@2048`：p99 `370.54 -> 485.21 ms`（上升）

结论：`waitEvent` 版本没有带来整体收益，已回滚。高并发下 HTTPS p99 的主矛盾仍是排队与调度抖动，不是单一重试策略问题。

### 2.8 工程改动落点（当前终态）

- `sylar/fiber/scheduler.*`
  - P2C 选 worker
  - `schedule` 跨 worker 降级本地
  - `mailboxRing + mailboxFallback`
  - `getWorkerStatsSnapshot()`
- `sylar/fiber/iomanager.*`
  - per-worker `epoll/eventfd`
  - 严格线程域校验（非法线程返回 `EXDEV`）
  - `WaitResult + waitToken`
- `sylar/fiber/fd_manager.*`
  - `FdCtx` 线程亲和绑定与校验
- `sylar/net/tcp_server.*`
  - accept/io 分离硬约束
  - 连接感知接入均衡（`inflight + queuedTasks`）
- `http/ssl/ssl_socket.cc`
  - TLS `WANT_READ/WANT_WRITE` 曾做协程让出实验，已回滚到原策略
- `tests/run_http_ab_suite.sh`、`tests/http_bench_server.cc`
  - HTTP/HTTPS A/B 压测入口

### 2.9 当前可复述的技术结论

1. 本地网络层目前是“吞吐优先”形态，RPS 领先稳定
2. HTTP 尾延迟已基本打平参考实现
3. HTTPS 尾延迟仍落后，尤其在 2048 并发以上
4. TLS `waitEvent` 实验在当前口径下无净收益，已回滚

### 2.10 面试怎么讲（当前版本）

30 秒版本：

> 第二阶段我把网络层收敛成 accept/io 分离的主从 Reactor。调度器从 RR 升级到 P2C，普通跨 worker 投递降级到本地队列，IOManager 切到每 worker 独立 epoll 并做严格线程域约束，等待语义显式化成 waitToken + WaitResult。TcpServer 接入时按 inflight 和队列长度做连接感知分配，并保证单 fd 生命周期线程亲和。压测结果是 RPS 明显领先参考实现，HTTP p99 基本打平，但 HTTPS p99 仍偏高；我们做过 TLS `WANT_READ/WANT_WRITE` 的协程让出实验，结果在当前口径下回退，最终选择回滚。 

2 分钟关键词：

- `master-sub reactor`
- `P2C over queuedTasks`
- `strict thread-domain IO ops`
- `fd affinity`
- `explicit wait semantics`
- `RPS-leading but HTTPS tail-latency gap`

---

## 第三部分：无锁队列模型与代码模板（理论）

这一部分不绑定具体项目，只讲并发队列模型本身。

### 3.1 六种组合总览

- 环形队列（有界）：
  - MPSC：多生产者，单消费者
  - SPMC：单生产者，多消费者
  - MPMC：多生产者，多消费者
- 无界链表（无界）：
  - MPSC：多生产者，单消费者
  - SPMC：单生产者，多消费者
  - MPMC：多生产者，多消费者

这六种里，最难的是多消费者侧（`SPMC/MPMC`）的并发出队和安全回收。

### 3.2 MPMC 有界环形队列（代码模板）

```cpp
template <typename T, size_t CapacityPow2>
class MpmcBoundedRing {
    static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0, "pow2");
    struct Cell {
        std::atomic<size_t> seq;
        T data;
    };

    static constexpr size_t kMask = CapacityPow2 - 1;
    alignas(64) Cell buffer_[CapacityPow2];
    alignas(64) std::atomic<size_t> enqueuePos_{0};
    alignas(64) std::atomic<size_t> dequeuePos_{0};

public:
    MpmcBoundedRing() {
        for (size_t i = 0; i < CapacityPow2; ++i) {
            buffer_[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    bool try_enqueue(T v) {
        size_t pos = enqueuePos_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = buffer_[pos & kMask];
            size_t seq = cell.seq.load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)pos;
            if (diff == 0) {
                if (enqueuePos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    cell.data = std::move(v);
                    cell.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // full
            } else {
                pos = enqueuePos_.load(std::memory_order_relaxed);
            }
        }
    }

    bool try_dequeue(T& out) {
        size_t pos = dequeuePos_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = buffer_[pos & kMask];
            size_t seq = cell.seq.load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
            if (diff == 0) {
                if (dequeuePos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    out = std::move(cell.data);
                    cell.seq.store(pos + CapacityPow2, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // empty
            } else {
                pos = dequeuePos_.load(std::memory_order_relaxed);
            }
        }
    }
};
```

### 3.3 MPMC 无界链表队列（代码模板）

```cpp
template <typename T, typename RetireFn>
class MpmcUnboundedLinked {
    struct Node {
        std::atomic<Node*> next;
        T value;
        bool hasValue;
        Node() : next(nullptr), value(), hasValue(false) {} // dummy
        explicit Node(T v) : next(nullptr), value(std::move(v)), hasValue(true) {}
    };

    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    RetireFn retire_;

public:
    explicit MpmcUnboundedLinked(RetireFn retire) : retire_(retire) {
        Node* dummy = new Node();
        head_.store(dummy, std::memory_order_relaxed);
        tail_.store(dummy, std::memory_order_relaxed);
    }

    bool enqueue(T v) {
        Node* n = new Node(std::move(v));
        for (;;) {
            Node* tail = tail_.load(std::memory_order_acquire);
            Node* next = tail->next.load(std::memory_order_acquire);
            if (tail != tail_.load(std::memory_order_acquire)) continue;
            if (next == nullptr) {
                if (tail->next.compare_exchange_weak(
                        next, n, std::memory_order_release, std::memory_order_relaxed)) {
                    tail_.compare_exchange_strong(
                        tail, n, std::memory_order_release, std::memory_order_relaxed);
                    return true;
                }
            } else {
                tail_.compare_exchange_weak(
                    tail, next, std::memory_order_release, std::memory_order_relaxed);
            }
        }
    }

    bool try_dequeue(T& out) {
        for (;;) {
            Node* head = head_.load(std::memory_order_acquire);
            Node* tail = tail_.load(std::memory_order_acquire);
            Node* next = head->next.load(std::memory_order_acquire);
            if (head != head_.load(std::memory_order_acquire)) continue;
            if (next == nullptr) return false; // empty
            if (head == tail) {
                tail_.compare_exchange_weak(
                    tail, next, std::memory_order_release, std::memory_order_relaxed);
                continue;
            }
            T tmp = next->value;
            if (head_.compare_exchange_weak(
                    head, next, std::memory_order_acq_rel, std::memory_order_acquire)) {
                out = std::move(tmp);
                retire_(head); // 必须是安全回收（Hazard Pointer / Epoch）
                return true;
            }
        }
    }
};
```

### 3.4 MPSC 的出队代码（因为入队可复用 MPMC 入队侧）

环形队列（MPSC）出队：消费者独占 `dequeuePos`，不再需要 CAS 抢占出队位。

```cpp
bool try_dequeue_mpsc(T& out) {
    size_t pos = dequeuePos_.load(std::memory_order_relaxed);
    Cell& cell = buffer_[pos & kMask];
    size_t seq = cell.seq.load(std::memory_order_acquire);
    intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
    if (diff < 0) return false; // empty
    out = std::move(cell.data);
    dequeuePos_.store(pos + 1, std::memory_order_relaxed);
    cell.seq.store(pos + CapacityPow2, std::memory_order_release);
    return true;
}
```

无界链表（MPSC）出队：单消费者可直接推进 `head`，不需要多消费者 CAS 竞争。

```cpp
bool try_dequeue_mpsc(T& out) {
    Node* head = head_.load(std::memory_order_relaxed);
    Node* next = head->next.load(std::memory_order_acquire);
    if (!next) return false;
    out = std::move(next->value);
    head_.store(next, std::memory_order_relaxed);
    retire_(head); // 仍需安全回收策略
    return true;
}
```

### 3.5 SPMC 的入队代码（因为出队可复用 MPMC 出队侧）

环形队列（SPMC）入队：生产者独占 `enqueuePos`，不再需要 CAS 抢占入队位。

```cpp
bool try_enqueue_spmc(T v) {
    size_t pos = enqueuePos_.load(std::memory_order_relaxed);
    Cell& cell = buffer_[pos & kMask];
    size_t seq = cell.seq.load(std::memory_order_acquire);
    intptr_t diff = (intptr_t)seq - (intptr_t)pos;
    if (diff < 0) return false; // full
    cell.data = std::move(v);
    enqueuePos_.store(pos + 1, std::memory_order_relaxed);
    cell.seq.store(pos + 1, std::memory_order_release);
    return true;
}
```

无界链表（SPMC）入队：生产者独占尾指针，可顺序 append。

```cpp
bool enqueue_spmc(T v) {
    Node* n = new Node(std::move(v));
    Node* tail = tail_.load(std::memory_order_relaxed);
    tail->next.store(n, std::memory_order_release);
    tail_.store(n, std::memory_order_relaxed);
    return true;
}
```

### 3.6 ABA 问题：是什么、在哪出现、怎么解

ABA 的本质是：一个原子值从 `A -> B -> A`，CAS 只看到“还是 A”，却看不到中间语义已经变了。

1. 在有界 ring（sequence-slot）里
   - 典型实现通常不直接 CAS 指针，而是比较 `seq` 与位置，ABA 风险天然较低。
   - 真正风险是“序号回绕”导致旧值被误认成新轮次。
   - 工程做法：`seq` 用足够宽的无符号整型（常见 `uint64_t/size_t`），并保证容量与更新规则正确。
2. 在无界链表（MS queue）里
   - 风险集中在 `head/tail/next` 指针 CAS，节点被回收并复用后最容易出现 ABA。
   - 工程做法：不要裸 `delete`，必须配合安全回收机制。
3. 常见治理策略
   - Tagged Pointer（指针+版本号）避免“同地址同版本”误判。
   - Hazard Pointer（读前发布 hazard，回收前扫描）避免节点被提前释放。
   - Epoch/RCU（批次延迟回收）把回收延后到所有线程离开旧 epoch。

一句话面试回答：
“ABA 不是 CAS 指令本身的问题，是对象生命周期与地址复用问题；链表结构必须把 CAS 和内存回收一起设计。”

### 3.7 无锁链表的安全回收（最容易被追问）

如果只讲“CAS 成功就 pop”，但没讲回收，通常会被继续追问。

1. Hazard Pointer
   - 优点：精确、可实时回收。
   - 代价：每次读路径需要发布/检查 hazard，代码复杂度较高。
2. Epoch Based Reclamation
   - 优点：实现相对简单，吞吐通常更好。
   - 代价：长尾线程可能拖慢回收，峰值内存上升。
3. 引用计数
   - 优点：语义直观。
   - 代价：原子增减开销大，在高并发队列里往往不是最优。

工程上常见结论是：
`MPMC` 无界链表通常优先 `Hazard Pointer` 或 `Epoch`，不建议“无保护直接 delete”。

### 3.8 内存序怎么讲（简版）

面试里不需要背标准条文，但要说清“发布-可见”关系：

1. 生产者写数据后用 `release` 发布“可消费标记”（如 ring 的 `seq` 或链表的 `next`）。
2. 消费者读取可消费标记时用 `acquire`，确保读到的数据体是完整可见的。
3. 计数器、游标抢位等纯竞争变量可用 `relaxed`，但不能破坏上面两条数据可见性链。

一句话面试回答：
“我把内存序拆成两类：数据发布链路必须 `release/acquire`，纯竞争元数据尽量 `relaxed`。”

### 3.9 伪共享与缓存抖动

无锁不等于高性能，缓存行为经常是第一瓶颈。

1. `head/tail` 或 `enqueuePos/dequeuePos` 尽量 cache line 对齐分离（常见 `alignas(64)`）。
2. 高频写热点分散，避免多个线程反复写同一 cache line。
3. 环形队列优点是连续内存、预取友好；链表缺点是指针跳转与分配器压力。

### 3.10 满队列/空队列语义（有界结构必问）

有界 ring 必须先定义清楚“满了怎么办”，这属于功能语义，不只是性能策略。

1. `try_enqueue` 直接失败（上层重试或降级）。
2. 阻塞/自旋等待（延迟更不可控，容易放大尾延迟）。
3. 降级到兜底通道（工程里常见“ring 热路径 + 无界 fallback”）。

面试时要明确说：
“我优先保证语义正确（不丢任务/可退化），再谈吞吐最优。”

### 3.11 高频追问清单（答题模板）

1. Q: 为什么 `MPSC/SPMC` 不能说成“MPMC 删一半 CAS”？
   - A: 并发参与者变了，不变量也变了；单侧独占后应重写该侧协议，而不是机械删 CAS。
2. Q: 线性化点在哪里？
   - A: ring 通常是发布 `seq` 的那一刻；链表通常是成功链接 `next` 或成功推进 `head` 的 CAS 点。
3. Q: 无锁是不是一定比加锁快？
   - A: 不一定。低并发或临界区很短时锁可能更稳，复杂无锁可能输在缓存和回收成本。
4. Q: 怎么验证你的无锁实现正确？
   - A: 压测只证明“好像能跑”，还需要模型测试/线性化检查/TSAN/故障注入做并发语义验证。
5. Q: 怎么选 ring 还是链表？
   - A: 有界且追求 cache 友好选 ring；需要弹性容量选链表，但必须接受回收复杂度与分配开销。
