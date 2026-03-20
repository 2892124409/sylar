# Allocator 对比基准说明（2026-03-20）

## 1. 对比目标

- A：系统默认分配器（baseline）
- B1：jemalloc（LD_PRELOAD）
- B2：tcmalloc（LD_PRELOAD）

统一前提：

- 协程保持跨线程自由调度（不做线程绑定）
- 统一测试 `use_caller=on/off` 两种模式
- `threads_total` 使用同一组：`2,4,8`

## 2. 基准程序

- 可执行文件：`build/bin/test_benchmark_tcp_allocator`
- 源码：`tests/test_benchmark_tcp_allocator.cc`

输出 CSV 字段：

`allocator,mode,workload,threads_total,run_id,total_requests,total_ms,req_per_sec,p50_us,p95_us,p99_us,max_us,errors`

## 3. 调度拓扑定义

### mode=on

- `accept_worker` 与 `io_worker` 共用一个 `IOManager(threads=T, use_caller=true)`

### mode=off

- 主线程：`accept_iom(threads=1, use_caller=true)`，承载 `accept_worker`
- 子线程：创建并运行 `io_iom(threads=T-1, use_caller=true)`，独占 `io_worker`

## 4. 负载类型

- `persistent`：持久连接，多连接多次收发
- `short`：短连接风暴，每请求新建连接

## 5. 一键运行

```bash
tests/run_allocator_bench.sh
```

可选环境变量：

- `JEMALLOC_LIB=/abs/path/libjemalloc.so`
- `TCMALLOC_LIB=/abs/path/libtcmalloc*.so`
- `THREADS=2,4,8`
- `CONNECTIONS=64`
- `REQUESTS_PER_CONN=200`
- `SHORT_TOTAL_REQUESTS=10000`
- `PAYLOAD_BYTES=256`

## 6. 输出文件

- 快速筛查（repeat=1）：`result_allocator_matrix_quick.csv`
- 全量原始（repeat=3）：`result_allocator_matrix_raw.csv`
- 全量聚合均值：`result_allocator_matrix_avg.csv`

## 7. 本次实测结果总结（2026-03-20）

本次实际执行参数（为控制总时长）：

- `THREADS=2,4,8`
- `CONNECTIONS=32`
- `REQUESTS_PER_CONN=50`
- `SHORT_TOTAL_REQUESTS=300`
- `PAYLOAD_BYTES=256`
- quick=1 次，full=3 次（脚本默认流程）

数据来源：

- `result_allocator_matrix_avg.csv`

统计口径：

- 吞吐优化(%) = `(B.req_per_sec / A.req_per_sec - 1) * 100%`（越大越好）
- p95变化(%) = `(B.p95 / A.p95 - 1) * 100%`（越小越好，负值表示 p95 降低）

### 7.1 全局平均（12 个组合：2 mode × 2 workload × 3 threads）

| allocator | 吞吐优化(%) | p95变化(%) |
| --- | ---: | ---: |
| jemalloc | -0.92% | -0.84% |
| tcmalloc | +3.78% | -6.47% |

结论：

- `tcmalloc` 在本次矩阵里整体最优（平均吞吐提升 +3.78%，平均 p95 下降 6.47%）。
- `jemalloc` 整体接近 baseline（吞吐略降，p95 略好）。

### 7.2 分场景平均（按 mode/workload 聚合 3 个线程点）

| allocator | mode | workload | 吞吐优化(%) | p95变化(%) |
| --- | --- | --- | ---: | ---: |
| jemalloc | off | persistent | +6.92% | -10.54% |
| jemalloc | off | short | -3.59% | +4.70% |
| jemalloc | on | persistent | -1.85% | -0.79% |
| jemalloc | on | short | -5.18% | +3.28% |
| tcmalloc | off | persistent | +10.64% | -14.30% |
| tcmalloc | off | short | +1.54% | -3.39% |
| tcmalloc | on | persistent | +5.05% | -7.06% |
| tcmalloc | on | short | -2.09% | -1.14% |

结论：

- `persistent` 场景中，`tcmalloc` 与 `jemalloc` 均有收益，`tcmalloc` 更稳定。
- `short` 场景中，分配器收益不稳定，网络层瞬时建连压力更主导结果。

### 7.3 线程维度关键点（相对 baseline）

- `off + persistent + 8`：
  - `jemalloc`：吞吐 `+25.13%`，p95 `-44.19%`
  - `tcmalloc`：吞吐 `+40.57%`，p95 `-50.53%`
- `on + persistent + 4`：
  - `jemalloc`：吞吐 `+2.65%`，p95 `-8.80%`
  - `tcmalloc`：吞吐 `+8.12%`，p95 `-11.79%`

### 7.4 错误率说明（short 连接风暴）

- `short` 在 4/8 线程下存在非 0 `avg_errors`（三组 allocator 都出现）。
- 该现象主要由短连接风暴下的连接队列/超时压力导致，不是 allocator 单点问题。
