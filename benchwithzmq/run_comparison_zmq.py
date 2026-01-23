#!/usr/bin/env python3
import os
import sys

import run_comparison as rc

def format_throughput(size, msgs_per_sec):
    return f"{msgs_per_sec/1e3:6.2f} Kmsg/s"

def format_throughput_delta(size, delta_msgs_per_sec):
    return f"{delta_msgs_per_sec/1e3:7.2f} Kmsg/s"

def parse_args():
    usage = (
        "Usage: run_comparison_zmq.py [PATTERN] [options]\n\n"
        "Options:\n"
        "  --runs N                Iterations per configuration (default: 3)\n"
        "  --build-dir PATH        Build directory (default: build/bench)\n"
        "  -h, --help              Show this help\n"
        "\n"
        "Env:\n"
        "  BENCH_NO_TASKSET=1      Disable taskset CPU pinning on Linux\n"
    )
    p_req = "ALL"
    num_runs = rc.DEFAULT_NUM_RUNS
    build_dir = ""

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
    return p_req, num_runs, build_dir

def main():
    p_req, num_runs, build_dir = parse_args()
    if build_dir:
        build_dir = rc.normalize_build_dir(build_dir)
        rc.BUILD_DIR = build_dir

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
        a_stats, failures = rc.collect_data(std_bin, "libzmq", p_name, num_runs)
        all_failures.extend(failures)
        b_stats, failures = rc.collect_data(std_bin, "libzmq", p_name, num_runs)
        all_failures.extend(failures)

        size_w = 6
        metric_w = 10
        val_w = 16
        diff_w = 9
        for tr in rc.TRANSPORTS:
            print(f"\n### Transport: {tr}")
            print(
                f"| {'Size':<{size_w}} | {'Metric':<{metric_w}} | {'libzmq A':>{val_w}} | {'libzmq B':>{val_w}} | {'Diff (%)':>{diff_w}} | {'Delta':>{val_w}} |"
            )
            print(
                f"|{'-' * (size_w + 2)}|{'-' * (metric_w + 2)}|{'-' * (val_w + 2)}|{'-' * (val_w + 2)}|{'-' * (diff_w + 2)}|{'-' * (val_w + 2)}|"
            )
            for sz in rc.MSG_SIZES:
                at = a_stats.get(f"{tr}|{sz}|throughput", 0)
                bt = b_stats.get(f"{tr}|{sz}|throughput", 0)
                td = ((bt - at) / at * 100) if at > 0 else 0
                dt = bt - at
                al = a_stats.get(f"{tr}|{sz}|latency", 0)
                bl = b_stats.get(f"{tr}|{sz}|latency", 0)
                ld = ((al - bl) / al * 100) if al > 0 else 0
                at_s = format_throughput(sz, at)
                bt_s = format_throughput(sz, bt)
                dt_s = format_throughput_delta(sz, dt)
                al_s = f"{al:8.2f} us"
                bl_s = f"{bl:8.2f} us"
                print(
                    f"| {f'{sz}B':<{size_w}} | {'Throughput':<{metric_w}} | {at_s:>{val_w}} | {bt_s:>{val_w}} | {td:>+7.2f}% | {dt_s:>{val_w}} |"
                )
                print(
                    f"| {f'{sz}B':<{size_w}} | {'Latency':<{metric_w}} | {al_s:>{val_w}} | {bl_s:>{val_w}} | {ld:>+7.2f}% | {'':>{val_w}} |"
                )

    if all_failures:
        print("\n## Failures")
        for pattern, lib_name, tr, sz, reason in all_failures:
            print(f"- {pattern} {lib_name} {tr} {sz}B: {reason}")

if __name__ == "__main__":
    main()
