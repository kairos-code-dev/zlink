#!/usr/bin/env python3
import argparse
import csv
import json
import os
import statistics
from collections import defaultdict


def parse_list(text, cast):
    out = []
    for token in str(text).split(","):
        token = token.strip()
        if not token:
            continue
        out.append(cast(token))
    return out


def fnum(row, key):
    try:
        return float(row.get(key, ""))
    except Exception:
        return None

def tps_or_none(row, size):
    direct = fnum(row, "throughput_tps")
    if direct is not None:
        return direct
    bps = fnum(row, "throughput_bps")
    if bps is None or size <= 0:
        return None
    return bps / float(size)


def median_or_none(values):
    if not values:
        return None
    return statistics.median(values)


def load_skips(path):
    skips = []
    if not path or not os.path.exists(path):
        return skips
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            stack = row.get("stack", "").strip()
            reason = row.get("reason", "").strip()
            if stack:
                skips.append({"stack": stack, "reason": reason})
    return skips


def main():
    parser = argparse.ArgumentParser(description="Summarize stream compare metrics")
    parser.add_argument("--metrics", required=True)
    parser.add_argument("--summary", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--stacks", default="")
    parser.add_argument("--sizes", default="")
    parser.add_argument("--skip-file", default="")
    args = parser.parse_args()

    selected_stacks = parse_list(args.stacks, str)
    selected_sizes = parse_list(args.sizes, int)
    skips = load_skips(args.skip_file)

    rows = []
    with open(args.metrics, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)

    grouped = defaultdict(list)
    for row in rows:
        stack = row.get("stack", "")
        try:
            size = int(row.get("size", "0"))
        except Exception:
            continue
        if selected_stacks and stack not in selected_stacks:
            continue
        if selected_sizes and size not in selected_sizes:
            continue
        grouped[(stack, size)].append(row)

    summary = {
        "config": {
            "runs": args.runs,
            "stacks": selected_stacks,
            "sizes": selected_sizes,
        },
        "skipped_stacks": skips,
        "metrics": {},
        "official_ranking": {},
        "reference_ranking": {},
    }

    for stack in selected_stacks:
        for size in selected_sizes:
            key = (stack, size)
            items = grouped.get(key, [])
            total = len(items)
            pass_rows = [
                r
                for r in items
                if r.get("pass_fail") == "PASS" and r.get("client_rc") == "0"
            ]

            throughputs = []
            tps_values = []
            p95s = []
            server_avg_cpu = []
            server_peak_rss_kb = []
            client_avg_cpu = []
            client_peak_rss_kb = []
            system_avg_cpu = []
            system_peak_mem_used_pct = []
            for row in pass_rows:
                tp = fnum(row, "throughput_bps")
                p95 = fnum(row, "p95_us")
                if tp is None or p95 is None:
                    continue
                throughputs.append(tp)
                p95s.append(p95)
                tps = tps_or_none(row, size)
                if tps is not None:
                    tps_values.append(tps)

                v = fnum(row, "server_avg_cpu_pct")
                if v is not None:
                    server_avg_cpu.append(v)
                v = fnum(row, "server_peak_rss_kb")
                if v is not None:
                    server_peak_rss_kb.append(v)
                v = fnum(row, "client_avg_cpu_pct")
                if v is not None:
                    client_avg_cpu.append(v)
                v = fnum(row, "client_peak_rss_kb")
                if v is not None:
                    client_peak_rss_kb.append(v)
                v = fnum(row, "system_avg_cpu_pct")
                if v is not None:
                    system_avg_cpu.append(v)
                v = fnum(row, "system_peak_mem_used_pct")
                if v is not None:
                    system_peak_mem_used_pct.append(v)

            peak = max(throughputs) if throughputs else None
            median = statistics.median(throughputs) if throughputs else None
            peak_tps = max(tps_values) if tps_values else None
            median_tps = statistics.median(tps_values) if tps_values else None

            cv = None
            if len(throughputs) >= 2:
                mean = statistics.mean(throughputs)
                if mean > 0:
                    cv = statistics.pstdev(throughputs) / mean * 100.0

            pass_rate = (len(pass_rows) / total) if total > 0 else 0.0

            if args.runs <= 1:
                gate_pass = pass_rate == 1.0 and peak is not None
                confidence = "LOW_SINGLE_RUN"
            else:
                gate_pass = pass_rate == 1.0 and peak is not None and cv is not None and cv <= 5.0
                confidence = "STABLE_CHECKED" if gate_pass else "UNSTABLE"

            summary["metrics"][f"{stack}:{size}"] = {
                "total_runs": total,
                "pass_runs": len(pass_rows),
                "pass_rate": pass_rate,
                "peak_bps": peak,
                "median_bps": median,
                "peak_tps": peak_tps,
                "median_tps": median_tps,
                "cv_pct": cv,
                "median_p95_us": statistics.median(p95s) if p95s else None,
                "median_server_avg_cpu_pct": median_or_none(server_avg_cpu),
                "median_server_peak_rss_kb": median_or_none(server_peak_rss_kb),
                "median_client_avg_cpu_pct": median_or_none(client_avg_cpu),
                "median_client_peak_rss_kb": median_or_none(client_peak_rss_kb),
                "median_system_avg_cpu_pct": median_or_none(system_avg_cpu),
                "median_system_peak_mem_used_pct": median_or_none(system_peak_mem_used_pct),
                "gate_pass": gate_pass,
                "confidence": confidence,
            }

    for size in selected_sizes:
        official = []
        ref = []
        for stack in selected_stacks:
            item = summary["metrics"].get(f"{stack}:{size}")
            if not item or item.get("peak_bps") is None:
                continue
            entry = {
                "stack": stack,
                "peak_bps": item.get("peak_bps"),
                "peak_tps": item.get("peak_tps"),
                "median_p95_us": item.get("median_p95_us"),
                "gate_pass": item.get("gate_pass"),
            }
            ref.append(entry)
            if item.get("gate_pass"):
                official.append(entry)

        official.sort(
            key=lambda x: (
                -(x.get("peak_bps") or 0.0),
                x.get("median_p95_us") if x.get("median_p95_us") is not None else 1e30,
            )
        )
        ref.sort(
            key=lambda x: (
                -(x.get("peak_bps") or 0.0),
                x.get("median_p95_us") if x.get("median_p95_us") is not None else 1e30,
            )
        )

        summary["official_ranking"][str(size)] = official
        summary["reference_ranking"][str(size)] = ref

    os.makedirs(os.path.dirname(args.summary), exist_ok=True)
    with open(args.summary, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    lines = []
    lines.append("# Stream Compare Report")
    lines.append("")
    lines.append(f"- runs: {args.runs}")
    if args.runs <= 1:
        lines.append("- warning: single-run mode is noise-sensitive")
    lines.append("")

    if skips:
        lines.append("## Skipped Stacks")
        lines.append("")
        for skip in skips:
            lines.append(f"- {skip['stack']}: {skip['reason']}")
        lines.append("")

    lines.append("## Metrics")
    lines.append("")
    lines.append("| stack | size | pass/total | peak bps | peak tps | median bps | median tps | cv% | p95(us) | srv cpu% | srv rss(MiB) | cli cpu% | cli rss(MiB) | sys cpu% | sys mem%(peak) | gate | confidence |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|")
    for size in selected_sizes:
        for stack in selected_stacks:
            item = summary["metrics"].get(f"{stack}:{size}")
            if not item:
                lines.append(f"| {stack} | {size} | 0/0 | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | FAIL | n/a |")
                continue
            cv = item.get("cv_pct")
            srv_cpu = item.get("median_server_avg_cpu_pct")
            srv_rss_kb = item.get("median_server_peak_rss_kb")
            cli_cpu = item.get("median_client_avg_cpu_pct")
            cli_rss_kb = item.get("median_client_peak_rss_kb")
            sys_cpu = item.get("median_system_avg_cpu_pct")
            sys_mem_peak = item.get("median_system_peak_mem_used_pct")
            lines.append(
                "| {stack} | {size} | {pass_runs}/{total_runs} | {peak} | {peak_tps} | {median} | {median_tps} | {cv} | {p95} | {srv_cpu} | {srv_rss} | {cli_cpu} | {cli_rss} | {sys_cpu} | {sys_mem_peak} | {gate} | {confidence} |".format(
                    stack=stack,
                    size=size,
                    pass_runs=item.get("pass_runs", 0),
                    total_runs=item.get("total_runs", 0),
                    peak=(f"{item['peak_bps']:.2f}" if item.get("peak_bps") is not None else "n/a"),
                    peak_tps=(f"{item['peak_tps']:.2f}" if item.get("peak_tps") is not None else "n/a"),
                    median=(f"{item['median_bps']:.2f}" if item.get("median_bps") is not None else "n/a"),
                    median_tps=(f"{item['median_tps']:.2f}" if item.get("median_tps") is not None else "n/a"),
                    cv=(f"{cv:.2f}" if cv is not None else "n/a"),
                    p95=(f"{item['median_p95_us']:.2f}" if item.get("median_p95_us") is not None else "n/a"),
                    srv_cpu=(f"{srv_cpu:.2f}" if srv_cpu is not None else "n/a"),
                    srv_rss=(f"{(srv_rss_kb / 1024.0):.2f}" if srv_rss_kb is not None else "n/a"),
                    cli_cpu=(f"{cli_cpu:.2f}" if cli_cpu is not None else "n/a"),
                    cli_rss=(f"{(cli_rss_kb / 1024.0):.2f}" if cli_rss_kb is not None else "n/a"),
                    sys_cpu=(f"{sys_cpu:.2f}" if sys_cpu is not None else "n/a"),
                    sys_mem_peak=(f"{sys_mem_peak:.2f}" if sys_mem_peak is not None else "n/a"),
                    gate=("PASS" if item.get("gate_pass") else "FAIL"),
                    confidence=item.get("confidence", "n/a"),
                )
            )

    for size in selected_sizes:
        lines.append("")
        lines.append(f"## Official Ranking (size={size})")
        lines.append("")
        lines.append("| rank | stack | peak bps | peak tps | p95(us) |")
        lines.append("|---:|---|---:|---:|---:|")
        official = summary["official_ranking"].get(str(size), [])
        if not official:
            lines.append("| 1 | n/a | n/a | n/a | n/a |")
        else:
            for idx, entry in enumerate(official, 1):
                peak_tps = entry.get("peak_tps")
                peak_tps_text = f"{peak_tps:.2f}" if peak_tps is not None else "n/a"
                p95 = entry.get("median_p95_us")
                p95_text = f"{p95:.2f}" if p95 is not None else "n/a"
                lines.append(
                    f"| {idx} | {entry['stack']} | {entry['peak_bps']:.2f} | "
                    f"{peak_tps_text} | {p95_text} |"
                )

    with open(args.report, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
