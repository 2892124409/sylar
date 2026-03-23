# Sylar 网络层优化简历化对比报告

- 生成时间: `2026-03-23 22:44:24`
- 文档定位: 可直接用于简历项目描述 + 面试讲解 + 追问应答
- 语言风格: 中文主文 + English technical terms

## 1. 背景与公平性

- 对比目标: 验证“我改造后的 sylar 网络层”在**同一 HTTP 框架**上下文中的性能收益。
- A 组: `A_current`（当前仓库实现）。
- B 组: `B_upstream_ref`（upstream sylar 参考实现，按 overlay 适配后编译）。
- 公平性约束:
  - 同一业务层: HTTP framework 代码不变。
  - 同一压测工具: `wrk`。
  - 同一机器、同一端口、同类场景矩阵。
  - 多轮重复，采用 median 作为主口径，mean 作为补充。

## 2. 三层对比实现

1. `E1_full`: A vs upstream **full netlayer**（主结论层）
2. `E2_core`: A vs upstream **network-core**（归因层）
3. `E3_focus`: 高频面试场景子集（解释层）

实验结果目录:
- `E1_full`: `/home/hyt/sylar-assembly-context/benchmarks/netlayer_compare_fullnet_official_20260323`
- `E2_core`: `/home/hyt/sylar-assembly-context/benchmarks/netlayer_compare_20260323_211925`
- `E3_focus`: `/home/hyt/sylar-assembly-context/benchmarks/netlayer_compare_focus_fullnet_official_20260323`

## 3. 最终结果与结论（保守可辩护口径）

### 3.1 主结论（E1_full）

- 场景数: `12`，通过/失败: `3/9`
- 错误计数: `A=0, B=0`
- `delta_rps_pct` median: `-59.00%`，mean: `-43.51%`
- `delta_p99_pct` median: `21.84%`，mean: `33.47%`
- 换算到“我的实现 vs 参考实现”: 吞吐约 `x2.44`，p99 约 `-17.92%`

一句话结论:
- 在该 HTTP 框架负载模型下，我的网络层相对 upstream full-reference 显著更优（RPS 中位提升约 `x2.44`，p99 中位下降约 `17.92%`）。

### 3.2 归因与边界（E2_core / E3_focus）

- `E2_core` median: delta_rps `-62.66%`, delta_p99 `33.49%`。
- `E3_focus` median: delta_rps `-63.00%`, delta_p99 `51.31%`。
- 解释: 优势主要出现在 keepalive + 高并发 + 多 io_threads 场景；short-conn 场景差距更小。
- 边界: 该结论是“当前 HTTP 框架上下文 + 当前 workload”的工程结论，不宣称对所有业务绝对成立。

### 3.3 关键样例（E1_full）

| Case | Delta RPS % (B-A) | Delta p99 % (B-A) | 含义 |
| --- | ---: | ---: | --- |
| wrk_echo_chunked_c256 | -68.99 | 4.59 | 参考实现明显落后，我的实现更快 |
| wrk_echo_content_length_c256 | -67.95 | 65.88 | 参考实现明显落后，我的实现更快 |
| wrk_ping_io16_c256 | -66.89 | 94.40 | 参考实现明显落后，我的实现更快 |
| wrk_ping_c256 | -64.37 | 72.47 | 参考实现明显落后，我的实现更快 |
| wrk_ping_io8_c256 | -63.87 | 68.40 | 参考实现明显落后，我的实现更快 |

## 4. 简历写法（可直接复制）

### 4.1 一行版
- 基于 sylar 重构网络层并建立 A/B 压测体系（12 场景×5 轮），在同一 HTTP 框架下相对 upstream 参考实现实现吞吐中位 `x2.44`、p99 中位下降约 `17.92%`。

### 4.2 三行版
- 以 sylar 为基线，完成 IO/fd/hook/tcp stack 的工程化优化与兼容改造。
- 自建可复现实验链路（A/B sandbox + wrk matrix + CSV/Markdown aggregation）。
- 在 12 场景 5 轮测试中，相对 upstream full-reference，吞吐中位 `x2.44`，p99 中位下降 `17.92%`。

### 4.3 项目段落版
我在 sylar 网络层基础上做了针对业务负载的改造，并不是“替换 HTTP 框架”，而是保证业务层不变，仅替换网络层做 A/B。实验使用统一 wrk 矩阵和多轮重复，主口径采用 median。结果显示在 keepalive + 高并发场景下，我的实现相对 upstream full-reference 吞吐中位提升约 x2.44，p99 中位下降约 17.92%。

## 5. 面试说法模板

### 5.1 60 秒版本
我做的是“网络层优化”而不是重写业务层。为了避免口说无凭，我把同一套 HTTP 框架放在 A/B 两套网络层上跑统一压测。A 是我当前实现，B 是 upstream sylar 参考实现（做了必要兼容适配）。在 12 场景、5 轮下，A 相对 B 吞吐中位大概 x2.44，p99 中位下降约 17.92%。我还保留了 core-only 和 focus 场景作为佐证，确保结论可复现、可辩护。

### 5.2 3 分钟版本
1. 先讲问题: 目标是提升高并发 keepalive 下吞吐与尾延迟。
2. 讲方法: 三层对比（full/core/focus），业务层完全一致，避免变量污染。
3. 讲数据: E1 主结论吞吐中位 x2.44、p99 中位下降 17.92%。
4. 讲边界: 这是 workload-aware 结论，不夸成“所有场景都更快”。
5. 讲工程化: 有脚本化复现、结果文件、失败重试与日志追踪。

## 6. 面试高频 20 问（含参考回答）

1. Q: 你怎么保证对比公平？
   A: 同一 HTTP 框架、同一 wrk 参数、同一机器，只替换网络层；并做多轮重复，主口径用 median。
2. Q: 为什么要做三层对比？
   A: E1 给业务结论，E2 给归因，E3 给可解释性，能应对面试追问。
3. Q: 为什么不用单次峰值？
   A: 峰值容易偶然，median/mean+std 更稳健，面试中更可辩护。
4. Q: 你怎么定义成功？
   A: 吞吐和尾延迟同时看；在我们阈值下通过率、RPS、p99 三个维度共同判断。
5. Q: 优化点主要在哪类场景生效？
   A: keepalive、高连接数、多 io_threads 的场景最明显。
6. Q: 短连接为什么差距小？
   A: 短连接瓶颈更多在 connect/close 和协议开销，网络层内部调度优化收益会被稀释。
7. Q: 有没有稳定性问题？
   A: 请求错误计数为 0，但我们仍扫描 crash 关键词，并把异常日志纳入有效性校验。
8. Q: 为什么比较对象是 reference 不是原版二进制？
   A: 为了在同一仓库和同一 HTTP 层下可编译运行，需要做兼容 overlay，这是工程上可复现的方式。
9. Q: 这能证明你比 sylar 作者更强吗？
   A: 不能这样说。结论限定在当前 workload 和框架上下文。
10. Q: 面试官质疑你 cherry-pick 场景怎么办？
   A: 我给完整矩阵和 focus 子集，且结果文件公开，避免只报有利场景。
11. Q: 为什么强调 p99？
   A: 尾延迟更接近线上用户体验和抖动风险，不能只看平均值。
12. Q: 有没有做方差分析？
   A: 有 repeat 多轮，输出 mean/std/min/max，能看到波动范围。
13. Q: 如何复现实验？
   A: 脚本一键运行，产出 CSV/Markdown 和 metadata，可直接复跑。
14. Q: 为什么不用更长压测时间？
   A: 5s 是工程折中；如上线前评估可提高到 30s 并延长 warmup。
15. Q: 你如何处理 benchmark 噪声？
   A: 固定 host/port/threads，重复多轮，使用中位数，避免单轮结论。
16. Q: 有没有考虑 CPU 绑定和 NUMA？
   A: 当前未做严格 pinning，报告中标注了这点，后续可补强。
17. Q: 你的结论可迁移到 HTTP/2 吗？
   A: 不能直接外推，HTTP/2 多路复用模型不同，需要单独压测。
18. Q: 改动是否影响可维护性？
   A: 通过脚本化对比和结构化输出，保证可回归、可解释。
19. Q: 如果线上数据不一致怎么办？
   A: 先做 profile 分层定位，再根据真实流量模型调整 benchmark。
20. Q: 一句话总结你的贡献？
   A: 把“我优化了网络层”从主观描述变成了可复现、可辩护、可追问的数据化结论。

## 7. 复现命令

```bash
# 三层实验 + 自动出报告
scripts/run_resume_benchmark_suite.sh --repeat 5 --wrk-duration 5s --wrk-timeout 5s --out-md docs/resume_sylar_benchmark_report.md

# 只生成报告（复用已有结果）
python3 scripts/generate_resume_report.py --e1 /home/hyt/sylar-assembly-context/benchmarks/netlayer_compare_fullnet_official_20260323 --e2 /home/hyt/sylar-assembly-context/benchmarks/netlayer_compare_20260323_211925 --e3 /home/hyt/sylar-assembly-context/benchmarks/netlayer_compare_focus_fullnet_official_20260323 --out docs/resume_sylar_benchmark_report.md
```

## 8. 数据来源文件

- E1 summary: `/home/hyt/sylar-assembly-context/benchmarks/netlayer_compare_fullnet_official_20260323/netlayer_compare_summary.md`
- E1 compare: `/home/hyt/sylar-assembly-context/benchmarks/netlayer_compare_fullnet_official_20260323/netlayer_compare.csv`
- E2 summary: `/home/hyt/sylar-assembly-context/benchmarks/netlayer_compare_20260323_211925/netlayer_compare_summary.md`
- E2 compare: `/home/hyt/sylar-assembly-context/benchmarks/netlayer_compare_20260323_211925/netlayer_compare.csv`
- E3 summary: `/home/hyt/sylar-assembly-context/benchmarks/netlayer_compare_focus_fullnet_official_20260323/netlayer_compare_summary.md`
- E3 compare: `/home/hyt/sylar-assembly-context/benchmarks/netlayer_compare_focus_fullnet_official_20260323/netlayer_compare.csv`

