#!/usr/bin/env python3
import os
import statistics
import sys

import run_comparison as rc

def format_throughput(size, msgs_per_sec):
    return f"{msgs_per_sec/1e3:6.2f} Kmsg/s"


def parse_args():
    usage = (
        "Usage: run_comparison_zmq_ab.py [PATTERN] [options]\n\n"
        "Options:\n"
        "  --runs N                Iterations per configuration (default: 3)\n"
        "  --build-dir PATH        Build directory (default: core/build/bench)\n"
        "  --pin-cpu               Pin CPU core during benchmarks (Linux taskset)\n"
        "  -h, --help              Show this help\n"
        "\n"
        "Env:\n"
        "  BENCH_TASKSET=1         Enable taskset CPU pinning on Linux\n"
    )
    p_req = "ALL"
    num_runs = rc.DEFAULT_NUM_RUNS
    build_dir = ""
    pin_cpu = False

    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg in ("-h", "--help"):
            print(usage)
            sys.exit(0)
        if arg == "--runs":
            if i + 1 >= len(sys.argv):
                print("Error: --runs requires a value.", file=sys.stderr)
                sys.exit(1)
            try:
                num_runs = int(sys.argv[i + 1])
            except ValueError:
                print("Error: --runs must be an integer.", file=sys.stderr)
                sys.exit(1)
            i += 1
        elif arg.startswith("--runs="):
            try:
                num_runs = int(arg.split("=", 1)[1])
            except ValueError:
                print("Error: --runs must be an integer.", file=sys.stderr)
                sys.exit(1)
        elif arg == "--build-dir":
            if i + 1 >= len(sys.argv):
                print("Error: --build-dir requires a value.", file=sys.stderr)
                sys.exit(1)
            build_dir = sys.argv[i + 1]
            i += 1
        elif arg == "--pin-cpu":
            pin_cpu = True
        elif arg.startswith("--build-dir="):
            build_dir = arg.split("=", 1)[1]
        elif arg.startswith("--"):
            print(f"Unknown option: {arg}", file=sys.stderr)
            print(usage, file=sys.stderr)
            sys.exit(1)
        else:
            p_req = arg.upper()
        i += 1

    if num_runs < 1:
        print("Error: --runs must be >= 1.", file=sys.stderr)
        sys.exit(1)
    return p_req, num_runs, build_dir, pin_cpu

def to_metric_map(result):
    out = {}
    for item in result or []:
        metric = item.get("metric")
        value = item.get("value")
        if metric and value is not None:
            out[metric] = value
    return out

def median_or_zero(values):
    return statistics.median(values) if values else 0.0

def cv_percent(values):
    if not values or len(values) < 2:
        return None
    mean = statistics.mean(values)
    if mean <= 0:
        return None
    return statistics.pstdev(values) / mean * 100.0

def format_cv(value):
    if value is None:
        return "  n/a"
    return f"{value:5.2f}%"

def main():
    p_req, num_runs, build_dir, pin_cpu = parse_args()
    if build_dir:
        build_dir = rc.normalize_build_dir(build_dir)
        rc.BUILD_DIR = build_dir
    if pin_cpu:
        rc.base_env["BENCH_TASKSET"] = "1"

    check_bin = os.path.join(rc.BUILD_DIR, "comp_std_zmq_pair" + rc.EXE_SUFFIX)
    if not os.path.exists(check_bin):
        print(f"Error: Binaries not found at {rc.BUILD_DIR}.")
        print("Please build the project first or pass --build-dir.")
        return

    comparisons = [
        ("comp_std_zmq_pair", "PAIR"),
        ("comp_std_zmq_pubsub", "PUBSUB"),
        ("comp_std_zmq_dealer_dealer", "DEALER_DEALER"),
        ("comp_std_zmq_dealer_router", "DEALER_ROUTER"),
        ("comp_std_zmq_router_router", "ROUTER_ROUTER"),
        ("comp_std_zmq_router_router_poll", "ROUTER_ROUTER_POLL"),
    ]

    all_failures = []
    for std_bin, p_name in comparisons:
        if p_req != "ALL" and p_name != p_req:
            continue

        print(f"\n## PATTERN: {p_name}")

        for tr in rc.TRANSPORTS:
            print(f"\n### Transport: {tr}")
            size_w = 6
            metric_w = 10
            val_w = 14
            diff_w = 9
            print(
                f"| {'Size':<{size_w}} | {'Metric':<{metric_w}} | {'libzmq A':>{val_w}} | {'libzmq B':>{val_w}} | {'Diff (%)':>{diff_w}} |"
            )
            print(
                f"|{'-' * (size_w + 2)}|{'-' * (metric_w + 2)}|{'-' * (val_w + 2)}|{'-' * (val_w + 2)}|{'-' * (diff_w + 2)}|"
            )

            for sz in rc.MSG_SIZES:
                a_tp = []
                b_tp = []
                a_lat = []
                b_lat = []

                for _ in range(num_runs):
                    a_res = rc.run_single_test(std_bin, "libzmq", tr, sz)
                    if a_res is None:
                        all_failures.append((p_name, "libzmq", tr, sz, "timeout"))
                        continue
                    if not a_res:
                        all_failures.append((p_name, "libzmq", tr, sz, "no_data"))
                        continue
                    a_map = to_metric_map(a_res)
                    if "throughput" in a_map:
                        a_tp.append(a_map["throughput"])
                    if "latency" in a_map:
                        a_lat.append(a_map["latency"])

                    b_res = rc.run_single_test(std_bin, "libzmq", tr, sz)
                    if b_res is None:
                        all_failures.append((p_name, "libzmq", tr, sz, "timeout"))
                        continue
                    if not b_res:
                        all_failures.append((p_name, "libzmq", tr, sz, "no_data"))
                        continue
                    b_map = to_metric_map(b_res)
                    if "throughput" in b_map:
                        b_tp.append(b_map["throughput"])
                    if "latency" in b_map:
                        b_lat.append(b_map["latency"])

                at = median_or_zero(a_tp)
                bt = median_or_zero(b_tp)
                td = ((bt - at) / at * 100) if at > 0 else 0
                a_cv = cv_percent(a_tp)
                b_cv = cv_percent(b_tp)
                al = median_or_zero(a_lat)
                bl = median_or_zero(b_lat)
                ld = ((al - bl) / al * 100) if al > 0 else 0

                a_tp = format_throughput(sz, at)
                b_tp = format_throughput(sz, bt)
                a_lat = f"{al:8.2f} us"
                b_lat = f"{bl:8.2f} us"
                print(
                    f"| {f'{sz}B':<{size_w}} | {'Throughput':<{metric_w}} | {a_tp:>{val_w}} | {b_tp:>{val_w}} | {td:>+7.2f}% |"
                )
                print(
                    f"| {f'{sz}B':<{size_w}} | {'Latency':<{metric_w}} | {a_lat:>{val_w}} | {b_lat:>{val_w}} | {ld:>+7.2f}% |"
                )

    if all_failures:
        print("\n## Failures")
        for pattern, lib_name, tr, sz, reason in all_failures:
            print(f"- {pattern} {lib_name} {tr} {sz}B: {reason}")

if __name__ == "__main__":
    main()
