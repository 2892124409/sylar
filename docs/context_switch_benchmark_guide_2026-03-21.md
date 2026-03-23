# Fiber 上下文切换基准说明（2026-03-21）

## 1. 测试目标

对比两种协程上下文切换后端的纯切换成本：

- `ucontext`
- `libco_asm`

固定输出指标：

- `total time (ms)`
- `ns per switch`
- `speedup`

并补充：

- `baseline loop`（空循环开销）
- `ns per switch(net)`（扣除 baseline 后净开销）

## 2. 基准模型与实现

模型固定为单线程双 Fiber ping-pong：

`main -> A <-> B`

- Fiber A：循环 `N` 次切到 B
- Fiber B：循环 `N` 次切回 A
- 总切换数：`2 * N`

实现位置：

- 基准程序：`tests/test_context_switch_pingpong.cc`
- 运行脚本：`tests/run_context_switch_bench.sh`

关键约束：

- 编译期切换后端（两次独立构建），不做运行时分支切换
- 默认 `taskset -c 0` 绑核运行
- 每个后端 `warmup=1`，正式测量 `repeat=5`

## 3. 本次实测环境

测试日期：`2026-03-21`

- OS：`Linux 6.6.87.2-microsoft-standard-WSL2 x86_64`
- CPU：`AMD Ryzen 7 8845H`（16 vCPU）
- 编译器：`g++ 13.3.0`
- CMake：`3.28.3`
- 构建类型：`Release`
- 代码基线：`git rev-parse --short HEAD = 742fb1b`

## 4. 执行命令

一键命令：

```bash
tests/run_context_switch_bench.sh
```

本次实际参数（脚本默认值）：

- `ITERATIONS=10000000`
- `REPEAT=5`
- `WARMUP=1`
- `STACK_SIZE=131072`
- `CPU_CORE=0`
- `BUILD_TYPE=Release`

脚本内部执行两次独立配置：

1. `SYLAR_FIBER_CONTEXT_BACKEND=ucontext`
2. `SYLAR_FIBER_CONTEXT_BACKEND=libco_asm`

## 5. 原始结果（5 次）

### 5.1 ucontext

总切换数：`20,000,000`

| run | total time (ms) | baseline (ms) | ns per switch | ns per switch(net) |
| --- | ---: | ---: | ---: | ---: |
| 1 | 5324.931 | 2.130 | 266.247 | 266.140 |
| 2 | 5255.751 | 2.039 | 262.788 | 262.686 |
| 3 | 5314.375 | 2.025 | 265.719 | 265.618 |
| 4 | 5315.912 | 2.061 | 265.796 | 265.693 |
| 5 | 5280.246 | 2.012 | 264.012 | 263.912 |

均值：

- `total time = 5298.243022 ms`
- `ns per switch = 264.912151`
- `baseline loop = 2.053256 ms`
- `ns per switch(net) = 264.809488`

### 5.2 libco_asm

总切换数：`20,000,000`

| run | total time (ms) | baseline (ms) | ns per switch | ns per switch(net) |
| --- | ---: | ---: | ---: | ---: |
| 1 | 147.667 | 2.055 | 7.383 | 7.281 |
| 2 | 142.673 | 2.027 | 7.134 | 7.032 |
| 3 | 140.221 | 2.022 | 7.011 | 6.910 |
| 4 | 140.959 | 2.019 | 7.048 | 6.947 |
| 5 | 142.001 | 2.041 | 7.100 | 6.998 |

均值：

- `total time = 142.704029 ms`
- `ns per switch = 7.135201`
- `baseline loop = 2.032648 ms`
- `ns per switch(net) = 7.033569`

## 6. 汇总指标

| 指标 | ucontext | libco_asm | 对比 |
| --- | ---: | ---: | ---: |
| total time (ms) | 5298.243022 | 142.704029 | `37.128x`（ucontext/asm） |
| ns per switch | 264.912151 | 7.135201 | `37.127x` |
| ns per switch(net) | 264.809488 | 7.033569 | `37.649x` |

脚本最终输出：

```text
speedup(raw): 37.127x
speedup(net): 37.649x
speedup: 37.649x
```

## 7. 结论

1. 在本次单线程双 Fiber ping-pong 微基准下，`libco_asm` 相比 `ucontext` 的净切换开销提升约 `37.65x`。  
2. 单次切换净开销从 `264.81 ns` 降至 `7.03 ns`，降幅约 `97.34%`。  
3. baseline 空循环开销很小：`ucontext` 约占总时间 `0.04%`，`libco_asm` 约占 `1.42%`，不影响“数量级差距”的判断。  
4. 结果稳定性可接受：`ns per switch(net)` 的 5 次波动系数约为 `0.49% (ucontext)` 与 `1.86% (libco_asm)`。  

可直接用于论文/汇报描述：

> 基于双 Fiber ping-pong 模型构建 microbenchmark，在单线程、CPU 绑定、无 IO 干扰条件下，对比 ucontext 与手写汇编上下文切换实现，测得汇编版本将单次切换净开销降低约 97.34%，整体加速约 37.65 倍。

## 8. 复现与扩展

产物文件：

- `result_context_switch_ucontext.txt`
- `result_context_switch_libco_asm.txt`

扩展建议：

- 可在同机多时段复测（冷机/热机），观察频率波动影响
- 可追加 `K` 个 Fiber 环形切换（`K=2/10/100`）验证趋势是否一致
