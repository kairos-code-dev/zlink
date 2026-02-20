#!/usr/bin/env python3

import statistics


def parse_results(stdout, pattern, transport):
    out = {}
    for line in stdout.splitlines():
        if not line.startswith("RESULT,"):
            continue
        parts = line.split(",")
        if len(parts) < 7:
            continue
        if parts[2].strip() != pattern:
            continue
        if parts[3].strip() != transport:
            continue
        try:
            size = int(parts[4])
        except ValueError:
            continue
        metric = parts[5].strip().lower()
        if metric not in ("throughput", "latency"):
            continue
        try:
            value = float(parts[6])
        except ValueError:
            continue
        slot = out.setdefault(size, {})
        slot[metric] = value
    return out


def parse_result(stdout, pattern, transport, msg_size):
    parsed = parse_results(stdout, pattern, transport).get(int(msg_size), {})
    return parsed if isinstance(parsed, dict) else {}


def update_stats_with_median(stats, key_prefix, values):
    if not values:
        return
    stats[f"{key_prefix}|throughput"] = statistics.median(
        v["throughput"] for v in values
    )
    stats[f"{key_prefix}|latency"] = statistics.median(
        v["latency"] for v in values
    )


def load_cached_stats(stats, cached_pattern, key_prefix):
    if not cached_pattern:
        return
    ck = cached_pattern.get(f"{key_prefix}|throughput")
    cl = cached_pattern.get(f"{key_prefix}|latency")
    if ck is None or cl is None:
        return
    stats[f"{key_prefix}|throughput"] = float(ck)
    stats[f"{key_prefix}|latency"] = float(cl)


def format_throughput(value):
    return f"{value/1e3:6.2f} Kmsg/s"


def format_latency(value):
    return f"{value:8.2f} us"


def throughput_diff_str(base_throughput, current_throughput):
    if (
        base_throughput is None
        or current_throughput is None
        or base_throughput <= 0
    ):
        return "N/A"
    diff = (current_throughput - base_throughput) / base_throughput * 100.0
    return f"{diff:+.2f}%"


def latency_diff_str(base_latency, current_latency):
    if base_latency is None or current_latency is None or base_latency <= 0:
        return "N/A"
    diff = (base_latency - current_latency) / base_latency * 100.0
    return f"{diff:+.2f}%"
