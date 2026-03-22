# Allocator + A/B 拓扑压测说明（更新于 2026-03-22）

## 1. 对比目标

统一对比三种分配器在两种 Reactor 拓扑下的表现：

- `baseline`（系统默认分配器）
- `jemalloc`（`LD_PRELOAD`）
- `tcmalloc`（`LD_PRELOAD`）

拓扑定义：

- `mode=a`：主线程运行 `accept_worker`（主线程 `runCaller`）
- `mode=b`：主线程不运行 worker（`accept/io` 都在后台线程）

> 当前代码约束：`accept_worker` 和 `io_worker` 必须是不同 `IOManager` 实例。

## 2. 基准程序与参数

- 可执行文件：`build/bin/test_benchmark_tcp_allocator`
- 源码：`tests/test_benchmark_tcp_allocator.cc`

核心参数：

- `--mode=a|b|all`（兼容 `on/off`，内部会映射到 `a/b`）
- `--workload=persistent|short|all`
- `--io_threads=2,4,8`（兼容 `--threads`）
- `--repeat=3`
- `--run_id=1`

负载定义：

- `persistent`：长连接复用
- `short`：短连接风暴（每请求新建连接）

## 3. CSV 输出口径

单次运行输出字段：

`allocator,mode,workload,io_threads,run_id,total_requests,total_ms,req_per_sec,p50_us,p95_us,p99_us,max_us,errors,status,exit_code`

关键字段：

- `status=ok`：该次 run 正常完成
- `status=start_fail|exception|crash|timeout|...`：失败类型
- `exit_code`：进程退出码（`ok` 时应为 `0`）

## 4. 一键压测脚本

```bash
tests/run_allocator_bench.sh
```

脚本行为：

- 维度：`allocator × mode(a/b) × workload × io_threads × run_id`
- 每个组合单独进程执行（隔离崩溃影响）
- 单 run 失败会记录并继续，不中断整轮压测
- 支持超时保护（`RUN_TIMEOUT_SEC`）

可选环境变量：

- `IO_THREADS=2,4,8`
- `MODES=a,b`
- `WORKLOADS=persistent,short`
- `REPEAT=3`
- `RUN_TIMEOUT_SEC=120`
- `CONNECTIONS=64`
- `REQUESTS_PER_CONN=200`
- `SHORT_TOTAL_REQUESTS=10000`
- `PAYLOAD_BYTES=256`
- `STARTUP_DELAY_MS=80`
- `JEMALLOC_LIB=/abs/path/libjemalloc.so`
- `TCMALLOC_LIB=/abs/path/libtcmalloc.so`

## 5. 输出文件

- 原始结果：`result_allocator_matrix_raw.csv`
- 成功 run 聚合均值：`result_allocator_matrix_avg.csv`
- 失败明细：`result_allocator_matrix_failures.csv`

说明：

- `avg` 只聚合 `status=ok` 的 run；
- 失败 run 进入 `failures`，同时在 `raw` 中以失败状态占位，便于后续统计稳定性。
