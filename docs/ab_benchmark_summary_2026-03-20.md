# A/B 方案压测结论（2026-03-20）

## 1. 目的与范围

本次对比的两组方案：

- 方案 A：线程绑定调度 + thread_local 本地池（`pinned_tls`）
- 方案 B：保持跨线程调度（`baseline`）

数据来源：

- `result_baseline.csv`
- `result_pinned.csv`

测试维度：`create_destroy` / `yield_switch` / `timer_dense`，线程数 `1/2/4/8`，每组 3 次取均值。

## 2. 核心结果（A 相对 B）

说明：

- 吞吐比 = `A_ops_per_sec / B_ops_per_sec`，越大越好
- 延迟比 = `A_p95 / B_p95`，越小越好

| 场景-线程 | 吞吐比 | p95 延迟比 | 结论 |
| --- | ---: | ---: | --- |
| create_destroy-1 | 0.834 | 1.211 | A 略差 |
| create_destroy-2 | 0.930 | 1.079 | A 略差 |
| create_destroy-4 | 1.055 | 0.831 | A 小幅领先 |
| create_destroy-8 | 0.006 | 0.735 | A 吞吐严重退化 |
| yield_switch-1 | 0.709 | 1.452 | A 明显变差 |
| yield_switch-2 | 0.034 | 37.908 | A 严重退化 |
| yield_switch-4 | ~0.000 | 325.866 | A 严重退化 |
| yield_switch-8 | 0.001 | 202.077 | A 严重退化 |
| timer_dense-1 | 0.635 | 1.398 | A 明显变差 |
| timer_dense-2 | 0.095 | 14.214 | A 严重退化 |
| timer_dense-4 | 0.002 | 129.130 | A 严重退化 |
| timer_dense-8 | 0.001 | 553.443 | A 严重退化 |

**结论**：除个别点（`create_destroy-4`）外，方案 A 在多线程与调度密集场景整体显著劣化，尤其 `yield/timer` 类场景出现数量级退化。

## 3. 为什么不直接用“协程绑定线程 + 本地池”

不是本地池本身无效，而是“用绑定线程换本地池”代价过高：

1. 绑定策略破坏了跨线程负载均衡
- ready 协程无法被空闲线程接管，热点线程会排队。
- 线程数升高后，队列不均衡带来的调度等待超过了 malloc/free 节省。

2. `accept_worker/io_worker` 共用 IOManager 时，绑定会放大抖动
- 新连接、定时器回调、唤醒任务更容易堆在局部线程。
- 调度公平性下降，尾延迟（p95/p99）显著拉长。

3. 本地分配收益小于调度损失
- glibc 线程 arena 已有一定线程局部优化。
- 在“跨线程协作”的真实负载下，调度灵活性通常比局部分配快路径更关键。

## 4. 当前代码状态（已执行）

已按结论回滚方案 A 相关改动，保留 baseline 调度能力：

- 移除线程绑定字段/分支（Scheduler/Fiber/IO 相关）
- 移除 TLS 栈缓存分支
- CMake 移除 `ENABLE_THREAD_PINNING` / `ENABLE_TLS_STACK_CACHE` 开关
- Benchmark 统一标记为 `baseline`

同时保留了对两方案都适用的修复：

- `Scheduler::start()` 启动幂等修复
- `Fiber::yield()` READY 状态修复
- `TimerManager::listExpiredCb()` 去掉临时堆分配

## 5. 第 2 档执行方案（保留跨线程调度前提下优化分配）

目标：不牺牲跨线程调度能力，做“TLS 快路径 + 跨线程远端回收”。

### 阶段 1：只做安全的 TLS 快路径（低风险）

- 保持方案 B 调度不变。
- 仅优化“明确同线程分配/释放”的小对象路径（例如临时节点、局部高频对象）。
- 为每类对象加统计：本地分配命中率、跨线程释放次数、回退 `new/delete` 次数。

### 阶段 2：引入跨线程远端回收（中风险）

- 每个线程持有 owner-local 池（可复用现有 `MemoryPool` 作为本地池核心）。
- 对象头记录 `owner_tid`。
- 若释放线程 != owner 线程：
  - 不直接 `deallocate` 到本线程池；
  - 投递到 owner 的 remote-free 队列（MPSC）。
- owner 线程在调度循环/空闲点批量 drain，再归还本地池。

### 阶段 3：灰度替换（可回滚）

- 先在 `Timer` 等小对象试点。
- 通过宏或运行时开关控制，异常时可立即回退到系统分配器。

## 6. 现有 MemoryPool 能不能用

**可以部分复用，但不能直接拿来做跨线程 free。**

- 可以复用：
  - 作为每线程 owner-local 的无锁池核心（`allocate/deallocate` 仅 owner 线程调用）。
- 不能直接复用：
  - 当前 `deallocate` 不是并发安全；跨线程直接归还会破坏链表。
- 需要补充：
  - remote-free 队列 + owner 线程 drain 机制
  - owner 标识与 debug 校验（防止误用）

最终建议：

- 调度层保持方案 B（跨线程自由调度）
- 内存层走“TLS 快路径 + 远端回收”的第 2 档路线
