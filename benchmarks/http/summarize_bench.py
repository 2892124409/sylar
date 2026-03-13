#!/usr/bin/env python3
import json
import re
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path


STATUS_KEYS = ["1xx", "2xx", "3xx", "4xx", "5xx", "other"]


def read_text(path):
    return path.read_text(encoding="utf-8", errors="replace")


def parse_latency_to_ms(text):
    match = re.fullmatch(r"([0-9]+(?:\.[0-9]+)?)(ns|us|ms|s)", text.strip())
    if not match:
        return None

    value = float(match.group(1))
    unit = match.group(2)
    if unit == "ns":
        return value / 1_000_000.0
    if unit == "us":
        return value / 1_000.0
    if unit == "ms":
        return value
    if unit == "s":
        return value * 1_000.0
    return None


def extract_first_float(pattern, text):
    match = re.search(pattern, text, re.MULTILINE)
    if not match:
        return None
    return float(match.group(1))


def extract_first_int(pattern, text):
    match = re.search(pattern, text, re.MULTILINE)
    if not match:
        return None
    return int(match.group(1))


def parse_status_counts(text):
    counts = {key: 0 for key in STATUS_KEYS}
    for key, value in re.findall(r"status_([0-9]xx|other)=([0-9]+)", text):
        counts[key] = int(value)
    return counts


def parse_wrk_metrics(text):
    metrics = {
        "requests_per_sec": extract_first_float(r"Requests/sec:\s+([0-9.]+)", text),
        "transfer_per_sec": None,
        "total_requests": None,
        "duration_seconds": None,
        "p50_ms": None,
        "p75_ms": None,
        "p90_ms": None,
        "p99_ms": None,
        "non_2xx_3xx": extract_first_int(r"Non-2xx or 3xx responses:\s+([0-9]+)", text),
        "socket_errors": {"connect": 0, "read": 0, "write": 0, "timeout": 0},
        "status_counts": parse_status_counts(text),
        "crashed": bool(re.search(r"Segmentation fault|core dumped", text)),
    }

    transfer_match = re.search(r"Transfer/sec:\s+([0-9.]+)([KMG]?B)", text)
    if transfer_match:
        metrics["transfer_per_sec"] = {
            "value": float(transfer_match.group(1)),
            "unit": transfer_match.group(2),
        }

    requests_match = re.search(r"([0-9]+)\s+requests in\s+([0-9.]+)s", text)
    if requests_match:
        metrics["total_requests"] = int(requests_match.group(1))
        metrics["duration_seconds"] = float(requests_match.group(2))

    socket_match = re.search(
        r"Socket errors:\s+connect\s+([0-9]+),\s+read\s+([0-9]+),\s+write\s+([0-9]+),\s+timeout\s+([0-9]+)",
        text,
    )
    if socket_match:
        metrics["socket_errors"] = {
            "connect": int(socket_match.group(1)),
            "read": int(socket_match.group(2)),
            "write": int(socket_match.group(3)),
            "timeout": int(socket_match.group(4)),
        }

    latency_percentiles = {}
    for percentile, value in re.findall(r"^\s*(50|75|90|99)%\s+([0-9.]+(?:ns|us|ms|s))", text, re.MULTILINE):
        latency_percentiles[percentile] = parse_latency_to_ms(value)

    metrics["p50_ms"] = latency_percentiles.get("50")
    metrics["p75_ms"] = latency_percentiles.get("75")
    metrics["p90_ms"] = latency_percentiles.get("90")
    metrics["p99_ms"] = latency_percentiles.get("99")
    return metrics


def normalize_base_scenario(label):
    label = label.strip()
    mapping = {
        "throughput": "throughput",
        "mixed": "mixed",
        "edge: blocked": "edge_blocked",
        "edge: conn-limit": "edge_conn_limit",
    }
    return mapping.get(label, label.replace(" ", "_").replace(":", ""))


def normalize_matrix_case(label):
    if label.startswith("Baseline: /ping"):
        return "ping"
    if label.startswith("Timer Wait: /api/user/profile"):
        return "timer_wait"
    if label.startswith("POST & JSON Parse: /api/data/upload"):
        return "post_json"
    if label.startswith("Large Payload: /api/file/download"):
        return "large_payload"
    return label.lower().replace(" ", "_")


def parse_base_log(path):
    text = read_text(path)
    scenarios = {}
    current_label = None
    current_lines = []

    def flush():
        nonlocal current_label, current_lines
        if not current_label:
            return
        section_text = "\n".join(current_lines)
        scenarios[normalize_base_scenario(current_label)] = {
            "label": current_label,
            "metrics": parse_wrk_metrics(section_text),
        }
        current_label = None
        current_lines = []

    for line in text.splitlines():
        if line.startswith("== ") and line.endswith(" =="):
            flush()
            current_label = line.strip("= ").strip()
        elif current_label:
            current_lines.append(line)
    flush()
    return {"path": str(path), "scenarios": scenarios, "crashed": bool(re.search(r"Segmentation fault|core dumped", text))}


def parse_matrix_log(path):
    text = read_text(path)
    records = []
    current_config = None
    current_label = None
    current_lines = []

    def flush():
        nonlocal current_label, current_lines
        if not current_label:
            return
        record_text = "\n".join(current_lines)
        record = {
            "label": current_label,
            "case": normalize_matrix_case(current_label),
            "config": dict(current_config or {}),
            "metrics": parse_wrk_metrics(record_text),
        }
        records.append(record)
        current_label = None
        current_lines = []

    config_pattern = re.compile(r"shared_stack=(\d+)\s+\|\s+fiber_pool=(\d+)\s+\|\s+use_caller=(\d+)")
    for line in text.splitlines():
        if line.startswith("[CONFIG] "):
            flush()
            match = config_pattern.search(line)
            if match:
                current_config = {
                    "shared_stack": int(match.group(1)),
                    "fiber_pool": int(match.group(2)),
                    "use_caller": int(match.group(3)),
                }
            else:
                current_config = {}
        elif line.startswith(">>> "):
            flush()
            current_label = line[4:].strip()
        elif current_label:
            current_lines.append(line)
    flush()
    return {"path": str(path), "records": records, "crashed": bool(re.search(r"Segmentation fault|core dumped", text))}


def parse_sweep_log(path):
    text = read_text(path)
    meta = {
        "endpoint": path.stem.split("_c", 1)[0],
        "connections": None,
        "path": None,
        "threads": None,
        "duration": None,
        "config": {"shared_stack": None, "fiber_pool": None, "use_caller": None},
        "metrics": parse_wrk_metrics(text),
    }

    header = re.search(
        r">>> SWEEP endpoint=([a-z_]+) path=([^ ]+) connections=([0-9]+) threads=([0-9]+) duration=([0-9a-zA-Z.]+)",
        text,
    )
    if header:
        meta["endpoint"] = header.group(1)
        meta["path"] = header.group(2)
        meta["connections"] = int(header.group(3))
        meta["threads"] = int(header.group(4))
        meta["duration"] = header.group(5)

    config = re.search(r"shared_stack=(\d+)\s+\|\s+fiber_pool=(\d+)\s+\|\s+use_caller=(\d+)", text)
    if config:
        meta["config"] = {
            "shared_stack": int(config.group(1)),
            "fiber_pool": int(config.group(2)),
            "use_caller": int(config.group(3)),
        }
    return meta


def parse_stability_log(path):
    parsed = parse_base_log(path)
    parsed["file_name"] = path.name
    return parsed


def discover_inputs(run_dir):
    phase1_dir = run_dir / "phase1"
    phase3_dir = run_dir / "phase3"

    if phase1_dir.exists():
        edge_logs = sorted(phase1_dir.glob("edge_run_*.log"))
        throughput_logs = sorted(phase1_dir.glob("throughput_run_*.log"))
    else:
        edge_logs = sorted(run_dir.glob("run_edge_*.log"))
        throughput_logs = sorted(run_dir.glob("run_throughput_*.log"))

    baseline_log = run_dir / "phase2_baseline.log"
    if not baseline_log.exists():
        candidates = sorted(run_dir.glob("base*.log"))
        baseline_log = candidates[0] if candidates else None

    matrix_log = run_dir / "phase2_matrix.log"
    if not matrix_log.exists():
        candidates = sorted(run_dir.glob("matrix*.log"))
        matrix_log = candidates[0] if candidates else None

    if phase3_dir.exists():
        sweep_logs = sorted(path for path in phase3_dir.glob("*.log") if path.name != "phase3_sweep.log")
    else:
        sweep_logs = []

    return {
        "edge_logs": edge_logs,
        "throughput_logs": throughput_logs,
        "baseline_log": baseline_log,
        "matrix_log": matrix_log,
        "sweep_logs": sweep_logs,
    }


def phase1_summary(edge_runs, throughput_runs):
    summary = {
        "edge_runs": edge_runs,
        "throughput_runs": throughput_runs,
        "edge_all_passed": True,
        "throughput_all_passed": True,
        "edge_pass_count": 0,
        "throughput_pass_count": 0,
    }

    for run in edge_runs:
        blocked = run["scenarios"].get("edge_blocked", {}).get("metrics", {})
        conn_limit = run["scenarios"].get("edge_conn_limit", {}).get("metrics", {})
        blocked_status = blocked.get("status_counts", {})
        conn_status = conn_limit.get("status_counts", {})
        passed = (
            not run["crashed"]
            and blocked_status.get("4xx", 0) > 0
            and blocked_status.get("5xx", 0) == 0
            and conn_status.get("5xx", 0) > 0
            and conn_status.get("4xx", 0) == 0
        )
        run["passed"] = passed
        summary["edge_all_passed"] = summary["edge_all_passed"] and passed
        summary["edge_pass_count"] += 1 if passed else 0

    for run in throughput_runs:
        metrics = run["scenarios"].get("throughput", {}).get("metrics", {})
        passed = not run["crashed"] and (metrics.get("requests_per_sec") or 0) > 0
        run["passed"] = passed
        summary["throughput_all_passed"] = summary["throughput_all_passed"] and passed
        summary["throughput_pass_count"] += 1 if passed else 0

    return summary


def phase3_summary(records):
    grouped = defaultdict(list)
    for record in records:
        grouped[record["endpoint"]].append(record)

    summary = {}
    for endpoint, items in grouped.items():
        items.sort(key=lambda item: item.get("connections") or 0)
        best = max(items, key=lambda item: item["metrics"].get("requests_per_sec") or 0)
        p99_values = [item["metrics"].get("p99_ms") for item in items if item["metrics"].get("p99_ms") is not None]
        min_p99 = min(p99_values) if p99_values else None

        first_timeout = None
        for item in items:
            if item["metrics"]["socket_errors"].get("timeout", 0) > 0:
                first_timeout = item["connections"]
                break

        long_tail_warning = None
        if min_p99 is not None:
            for item in items:
                p99 = item["metrics"].get("p99_ms")
                if p99 is not None and p99 >= min_p99 * 2:
                    long_tail_warning = item["connections"]
                    break

        summary[endpoint] = {
            "cases": items,
            "best_case": best,
            "first_timeout_connections": first_timeout,
            "long_tail_warning_connections": long_tail_warning,
        }
    return summary


def find_best_ping_config(matrix_records):
    ping_records = [record for record in matrix_records if record["case"] == "ping"]
    if not ping_records:
        return None
    return max(ping_records, key=lambda record: record["metrics"].get("requests_per_sec") or 0)


def build_summary(run_dir):
    inputs = discover_inputs(run_dir)

    edge_runs = [parse_stability_log(path) for path in inputs["edge_logs"]]
    throughput_runs = [parse_stability_log(path) for path in inputs["throughput_logs"]]
    baseline = parse_base_log(inputs["baseline_log"]) if inputs["baseline_log"] else None
    matrix = parse_matrix_log(inputs["matrix_log"]) if inputs["matrix_log"] else None
    sweep = [parse_sweep_log(path) for path in inputs["sweep_logs"]]

    phase1 = phase1_summary(edge_runs, throughput_runs)
    phase3 = phase3_summary(sweep)

    matrix_best_ping = find_best_ping_config(matrix["records"]) if matrix else None

    return {
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "run_dir": str(run_dir),
        "phase1": phase1,
        "phase2": {
            "baseline": baseline,
            "matrix": matrix,
            "best_ping_config": matrix_best_ping,
        },
        "phase3": phase3,
    }


def render_status_summary(status_counts):
    return " ".join(f"{key}={status_counts.get(key, 0)}" for key in STATUS_KEYS)


def render_markdown_table(headers, rows):
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(str(cell) for cell in row) + " |")
    return "\n".join(lines)


def create_summary_markdown(summary):
    lines = ["# HTTP 全流程压测摘要", ""]
    lines.append(f"- 生成时间：`{summary['generated_at']}`")
    lines.append(f"- 结果目录：`{summary['run_dir']}`")
    lines.append("")

    phase1 = summary["phase1"]
    lines.append("## Phase 1 稳定性回归")
    lines.append(
        f"- edge 通过：`{phase1['edge_pass_count']}/{len(phase1['edge_runs'])}`，整体结果：`{'PASS' if phase1['edge_all_passed'] else 'FAIL'}`"
    )
    lines.append(
        f"- throughput 通过：`{phase1['throughput_pass_count']}/{len(phase1['throughput_runs'])}`，整体结果：`{'PASS' if phase1['throughput_all_passed'] else 'FAIL'}`"
    )
    lines.append("")

    if phase1["edge_runs"]:
        edge_rows = []
        for index, run in enumerate(phase1["edge_runs"], start=1):
            blocked = run["scenarios"].get("edge_blocked", {}).get("metrics", {})
            conn_limit = run["scenarios"].get("edge_conn_limit", {}).get("metrics", {})
            edge_rows.append([
                index,
                "PASS" if run["passed"] else "FAIL",
                "yes" if run["crashed"] else "no",
                blocked.get("status_counts", {}).get("4xx", 0),
                conn_limit.get("status_counts", {}).get("5xx", 0),
            ])
        lines.append(render_markdown_table(["Run", "Result", "Crash", "Blocked 4xx", "Conn-limit 5xx"], edge_rows))
        lines.append("")

    if phase1["throughput_runs"]:
        tp_rows = []
        for index, run in enumerate(phase1["throughput_runs"], start=1):
            metrics = run["scenarios"].get("throughput", {}).get("metrics", {})
            tp_rows.append([
                index,
                "PASS" if run["passed"] else "FAIL",
                f"{(metrics.get('requests_per_sec') or 0):.2f}",
                metrics.get("socket_errors", {}).get("timeout", 0),
            ])
        lines.append(render_markdown_table(["Run", "Result", "Requests/sec", "Timeouts"], tp_rows))
        lines.append("")

    baseline = summary["phase2"]["baseline"]
    if baseline:
        lines.append("## Phase 2 基线场景")
        rows = []
        for key, item in baseline["scenarios"].items():
            metrics = item["metrics"]
            rows.append([
                item["label"],
                f"{(metrics.get('requests_per_sec') or 0):.2f}",
                f"{metrics['p99_ms']:.2f}" if metrics.get("p99_ms") is not None else "-",
                render_status_summary(metrics["status_counts"]),
            ])
        lines.append(render_markdown_table(["Scenario", "Requests/sec", "p99(ms)", "Statuses"], rows))
        lines.append("")

    matrix = summary["phase2"]["matrix"]
    if matrix:
        lines.append("## Phase 2 调度矩阵")
        rows = []
        for record in matrix["records"]:
            config = record["config"]
            metrics = record["metrics"]
            rows.append([
                record["case"],
                config.get("shared_stack"),
                config.get("fiber_pool"),
                config.get("use_caller"),
                f"{(metrics.get('requests_per_sec') or 0):.2f}",
                f"{metrics['p99_ms']:.2f}" if metrics.get("p99_ms") is not None else "-",
            ])
        lines.append(render_markdown_table(["Case", "SS", "FP", "UC", "Requests/sec", "p99(ms)"], rows))
        lines.append("")

        best_ping = summary["phase2"].get("best_ping_config")
        if best_ping:
            lines.append(
                "- `/ping` 最佳组合："
                f"`shared_stack={best_ping['config']['shared_stack']}, "
                f"fiber_pool={best_ping['config']['fiber_pool']}, "
                f"use_caller={best_ping['config']['use_caller']}`，"
                f"`Requests/sec={best_ping['metrics']['requests_per_sec']:.2f}`"
            )
            lines.append("")

    if summary["phase3"]:
        lines.append("## Phase 3 上限探索")
        rows = []
        for endpoint, item in sorted(summary["phase3"].items()):
            best = item["best_case"]
            rows.append([
                endpoint,
                best.get("connections"),
                f"{(best['metrics'].get('requests_per_sec') or 0):.2f}",
                item.get("first_timeout_connections") or "-",
                item.get("long_tail_warning_connections") or "-",
            ])
        lines.append(render_markdown_table(["Endpoint", "Best Conn", "Best Requests/sec", "First Timeout", "Long-tail Warning"], rows))
        lines.append("")

    return "\n".join(lines).strip() + "\n"


def create_interview_notes(summary):
    lines = ["# 面试口述提纲", ""]

    phase1 = summary["phase1"]
    baseline = summary["phase2"]["baseline"]
    best_ping = summary["phase2"].get("best_ping_config")
    phase3 = summary["phase3"]

    throughput = None
    mixed = None
    if baseline:
        throughput = baseline["scenarios"].get("throughput")
        mixed = baseline["scenarios"].get("mixed")

    lines.append("## 1 分钟结论")
    lines.append(
        f"- 我把 HTTP 压测做成了完整流水线，先做稳定性回归，再做基线、矩阵和上限探索。"
    )
    lines.append(
        f"- 修复后 `edge` 稳定性回归通过 `"
        f"{phase1['edge_pass_count']}/{len(phase1['edge_runs'])}` 轮，"
        f"`throughput` 回归通过 `"
        f"{phase1['throughput_pass_count']}/{len(phase1['throughput_runs'])}` 轮。"
    )
    if throughput:
        lines.append(f"- 当前基线 `/ping` 的 `Requests/sec` 是 `{throughput['metrics']['requests_per_sec']:.2f}`。")
    if mixed:
        lines.append(f"- `mixed` 场景的 `Requests/sec` 是 `{mixed['metrics']['requests_per_sec']:.2f}`。")
    if best_ping:
        lines.append(
            f"- 调度矩阵里 `/ping` 最优组合是 "
            f"`shared_stack={best_ping['config']['shared_stack']}, "
            f"fiber_pool={best_ping['config']['fiber_pool']}, "
            f"use_caller={best_ping['config']['use_caller']}`，"
            f"吞吐 `{best_ping['metrics']['requests_per_sec']:.2f}`。"
        )
    lines.append("")

    lines.append("## 3 分钟案例")
    lines.append("- 压测最先打出来的不是性能瓶颈，而是协议正确性问题：`curl` 正常，但 `wrk` 会出现 `0 requests`。")
    lines.append("- 第一类问题是普通 HTTP 响应边界不稳定，根因是 header/body 分开发送且 `Content-Length` 不稳定，修法是改成一次性发送完整报文。")
    lines.append("- 第二类问题是连接上限拒绝路径不稳定，根因是服务端没有先消费请求就直接 `send + close`，修法是先快速读请求，再返回完整 `503`。")
    lines.append("- 第三类问题只在 `Release` 下暴露：`edge` 场景会随机崩溃。最后定位到 `ucontext` fiber 在 worker 线程间迁移后被跨线程恢复，修法是给 fiber 补调度线程亲和性；另外 benchmark 场景切换时不再走不稳定的优雅停服路径，改成强制回收进程，修复后稳定性回归通过。")
    if phase3:
        lines.append("- 我还补了一轮性能上限探索，把 `/ping`、定时等待、上传、下载四类路径分别扫连接数，输出最佳吞吐点和首个超时/长尾恶化档位。")
    lines.append("")

    if phase3:
        lines.append("## 上限探索摘录")
        for endpoint, item in sorted(phase3.items()):
            best = item["best_case"]
            lines.append(
                f"- `{endpoint}`：最佳吞吐 `"
                f"{best['metrics']['requests_per_sec']:.2f}` @ `c={best.get('connections')}`，"
                f"首个 timeout 档位 `{item.get('first_timeout_connections') or '-'}`，"
                f"长尾告警档位 `{item.get('long_tail_warning_connections') or '-'}`。"
            )
    lines.append("")
    return "\n".join(lines).strip() + "\n"


def main(argv):
    if len(argv) != 2:
        print("Usage: summarize_bench.py <run_dir>", file=sys.stderr)
        return 1

    run_dir = Path(argv[1]).resolve()
    if not run_dir.is_dir():
        print(f"Run directory not found: {run_dir}", file=sys.stderr)
        return 1

    summary = build_summary(run_dir)
    summary_json = run_dir / "summary.json"
    summary_md = run_dir / "summary.md"
    interview_md = run_dir / "interview_notes.md"

    summary_json.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    summary_md.write_text(create_summary_markdown(summary), encoding="utf-8")
    interview_md.write_text(create_interview_notes(summary), encoding="utf-8")

    print(f"Wrote {summary_json}")
    print(f"Wrote {summary_md}")
    print(f"Wrote {interview_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
