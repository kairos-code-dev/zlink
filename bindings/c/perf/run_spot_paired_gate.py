#!/usr/bin/env python3
"""Run and evaluate the Core Spot performance paired gate.

Each Spot cell is paired with the direction-matched ROUTER cell.  The order is
reversed on every repetition so machine drift does not consistently favor one
side.  This tool intentionally drives the existing public benchmark runner
instead of adding a perf-only Core path.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import pathlib
import shutil
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from typing import Iterable, Mapping, Sequence


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
DEFAULT_RUNNER = REPO_ROOT / "bindings/c/perf/run_benchmarks_multi.sh"
DEFAULT_BUILD_DIR = REPO_ROOT / "bindings/c/build"
DEFAULT_CORE_BUILD_DIR = REPO_ROOT / "core/build"
DEFAULT_TRANSPORTS = ("tcp", "tls", "ws", "wss")
DEFAULT_SIZES = (64, 256, 1024, 4096, 65536, 131072)
REQUIRED_METRICS = (
    "throughput",
    "bandwidth",
    "latency",
    "latency_p95",
    "latency_p99",
)
PAIR_BASELINES = {
    "SPOT_PUBSUB": "ROUTER_ROUTER_ONEWAY",
    "SPOT_REQREP": "ROUTER_ROUTER_REQREP",
    "SPOT_SENDSEND": "ROUTER_ROUTER_SENDSEND",
}
BUILD_TARGETS = {
    "SPOT_PUBSUB": (
        "comp_src_spot_pubsub_server",
        "comp_src_spot_pubsub_client",
    ),
    "SPOT_REQREP": (
        "comp_src_spot_reqrep_server",
        "comp_src_spot_reqrep_client",
    ),
    "SPOT_SENDSEND": (
        "comp_src_spot_sendsend_server",
        "comp_src_spot_sendsend_client",
    ),
    "ROUTER_ROUTER_ONEWAY": (
        "comp_src_router_router_oneway_server",
        "comp_src_router_router_oneway_client",
    ),
    "ROUTER_ROUTER_REQREP": (
        "comp_src_router_router_reqrep_server",
        "comp_src_router_router_reqrep_matched_client",
    ),
    "ROUTER_ROUTER_SENDSEND": (
        "comp_src_router_router_sendsend_server",
        "comp_src_router_router_sendsend_matched_client",
    ),
}


@dataclass(frozen=True)
class Sample:
    pattern: str
    transport: str
    size: int
    metrics: Mapping[str, float]
    multicast_dropped_targets: int = 0


def parse_csv_choice(
    raw: str, allowed: Iterable[str], value_name: str
) -> tuple[str, ...]:
    allowed_set = set(allowed)
    values = tuple(value.strip().upper() for value in raw.split(",") if value.strip())
    invalid = [value for value in values if value not in allowed_set]
    if not values or invalid:
        raise ValueError(
            f"invalid {value_name}: {','.join(invalid) if invalid else raw}"
        )
    return values


def parse_sizes(raw: str) -> tuple[int, ...]:
    values = tuple(int(value.strip()) for value in raw.split(",") if value.strip())
    if not values or any(value <= 0 for value in values):
        raise ValueError("message sizes must be positive integers")
    return values


def public_pattern(pattern: str) -> str:
    return pattern.removeprefix("MULTI_")


def result_pattern(pattern: str) -> str:
    return pattern if pattern.startswith("MULTI_") else f"MULTI_{pattern}"


def paired_order(spot_pattern: str, repetition: int) -> tuple[str, str]:
    baseline = PAIR_BASELINES[spot_pattern]
    if repetition % 2 == 0:
        return spot_pattern, baseline
    return baseline, spot_pattern


def parse_report(text: str, expected_pattern: str, transport: str, size: int) -> Sample:
    metrics: dict[str, float] = {}
    expected_result_pattern = result_pattern(expected_pattern)
    status = ""
    diagnostic_count = 0
    multicast_dropped_targets = 0
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if line.startswith("- status:"):
            status = line.split(":", 1)[1].strip()
            continue
        if line.startswith("SPOT_DIAG,"):
            row = next(csv.reader([line]))
            if (
                len(row) >= 7
                and row[1] == "current"
                and row[2] == expected_result_pattern
                and row[3] == transport
                and int(row[4]) == size
            ):
                diagnostic_count += 1
                fields = dict(
                    token.split("=", 1)
                    for token in row[5:]
                    if "=" in token
                )
                multicast_dropped_targets += int(
                    fields.get("multicast_dropped_targets", "0")
                )
            continue
        if not line.startswith("RESULT,"):
            continue
        row = next(csv.reader([line]))
        if len(row) != 7:
            continue
        _, library, pattern, row_transport, row_size, metric, value = row
        if (
            library != "current"
            or pattern != expected_result_pattern
            or row_transport != transport
            or int(row_size) != size
        ):
            continue
        metrics[metric] = float(value)
    missing = [metric for metric in REQUIRED_METRICS if metric not in metrics]
    if status != "complete" or missing:
        raise ValueError(
            f"incomplete report pattern={expected_result_pattern} "
            f"transport={transport} size={size} status={status or 'missing'} "
            f"missing={','.join(missing) if missing else 'none'}"
        )
    if expected_pattern.startswith("SPOT_") and diagnostic_count == 0:
        raise ValueError(
            f"missing Spot diagnostics pattern={expected_result_pattern} "
            f"transport={transport} size={size}"
        )
    return Sample(
        expected_pattern,
        transport,
        size,
        metrics,
        multicast_dropped_targets,
    )


def median_metrics(samples: Sequence[Sample]) -> dict[str, float]:
    if not samples:
        raise ValueError("cannot aggregate an empty sample set")
    return {
        metric: statistics.median(sample.metrics[metric] for sample in samples)
        for metric in REQUIRED_METRICS
    }


def evaluate_cell(
    spot_pattern: str,
    spot_metrics: Mapping[str, float],
    baseline_metrics: Mapping[str, float],
    throughput_floor: float,
    latency_ceiling: float,
    multicast_dropped_targets: int = 0,
) -> dict[str, object]:
    baseline_throughput = baseline_metrics["throughput"]
    throughput_ratio = (
        spot_metrics["throughput"] / baseline_throughput
        if baseline_throughput > 0
        else 0.0
    )
    latency_ratios = {}
    for metric in ("latency", "latency_p95", "latency_p99"):
        baseline_value = baseline_metrics[metric]
        latency_ratios[metric] = (
            spot_metrics[metric] / baseline_value if baseline_value > 0 else float("inf")
        )
    failures = []
    if multicast_dropped_targets > 0:
        failures.append(
            f"multicast_dropped_targets={multicast_dropped_targets}>0"
        )
    if throughput_ratio < throughput_floor:
        failures.append(
            f"throughput_ratio={throughput_ratio:.6f}<{throughput_floor:.6f}"
        )
    for metric, ratio in latency_ratios.items():
        if ratio > latency_ceiling:
            failures.append(f"{metric}_ratio={ratio:.6f}>{latency_ceiling:.6f}")
    return {
        "spot_pattern": spot_pattern,
        "baseline_pattern": PAIR_BASELINES[spot_pattern],
        "spot": dict(spot_metrics),
        "baseline": dict(baseline_metrics),
        "throughput_ratio": throughput_ratio,
        "latency_ratios": latency_ratios,
        "multicast_dropped_targets": multicast_dropped_targets,
        "status": "pass" if not failures else "fail",
        "failures": failures,
    }


def find_core_runtime(core_build_dir: pathlib.Path) -> pathlib.Path:
    candidates = (
        core_build_dir / "lib/libzlink.so",
        core_build_dir / "bin/libzlink.so",
        core_build_dir / "lib/libzlink.dylib",
        core_build_dir / "bin/libzlink.dylib",
        core_build_dir / "bin/zlink.dll",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise RuntimeError(f"Core runtime not found under {core_build_dir}")


def newest_source_after(runtime: pathlib.Path) -> pathlib.Path | None:
    runtime_mtime = runtime.stat().st_mtime_ns
    for source_root in (REPO_ROOT / "core/src", REPO_ROOT / "core/include"):
        for path in source_root.rglob("*"):
            if path.is_file() and path.stat().st_mtime_ns > runtime_mtime:
                return path
    return None


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def assert_runtime_identity(runtime: pathlib.Path, expected_sha256: str) -> None:
    stale_source = newest_source_after(runtime)
    if stale_source is not None:
        raise RuntimeError(
            f"Core source changed after paired gate preflight: {stale_source}"
        )
    actual_sha256 = sha256_file(runtime)
    if actual_sha256 != expected_sha256:
        raise RuntimeError(
            "Core runtime changed during paired gate: "
            f"expected={expected_sha256} actual={actual_sha256}"
        )


def source_tree_sha256(paths: Sequence[pathlib.Path]) -> str:
    digest = hashlib.sha256()
    for root in paths:
        candidates = [root] if root.is_file() else list(root.rglob("*"))
        for path in sorted(candidate for candidate in candidates if candidate.is_file()):
            relative = path.relative_to(REPO_ROOT).as_posix().encode()
            digest.update(len(relative).to_bytes(4, "big"))
            digest.update(relative)
            digest.update(path.read_bytes())
    return digest.hexdigest()


def git_commit() -> str:
    return subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, text=True
    ).strip()


def build_targets(patterns: Sequence[str], jobs: int) -> None:
    targets = sorted(
        {
            target
            for spot_pattern in patterns
            for pattern in (spot_pattern, PAIR_BASELINES[spot_pattern])
            for target in BUILD_TARGETS[pattern]
        }
    )
    subprocess.run(
        [
            "cmake",
            "--build",
            str(DEFAULT_BUILD_DIR),
            "--target",
            *targets,
            f"-j{jobs}",
        ],
        cwd=REPO_ROOT,
        check=True,
    )


def build_case_command(
    runner: pathlib.Path,
    pattern: str,
    transport: str,
    size: int,
    clients: int,
    duration: int,
    result_root: pathlib.Path,
    tag: str,
) -> list[str]:
    return [
        str(runner),
        "--reuse-build",
        "--pattern",
        public_pattern(pattern),
        "--transports",
        transport,
        "--msg-sizes",
        str(size),
        "--duration",
        str(duration),
        "--clients",
        str(clients),
        "--runs",
        "1",
        "--server-io-threads",
        "1",
        "--client-io-threads",
        "1",
        "--results-dir",
        str(result_root),
        "--results-tag",
        tag,
    ]


def run_case(
    command: Sequence[str],
    output_path: pathlib.Path,
    perf_output: pathlib.Path | None,
    time_output: pathlib.Path | None,
) -> str:
    final_command = list(command)
    if time_output is not None:
        final_command = [
            "/usr/bin/time",
            "-v",
            "-o",
            str(time_output),
            *final_command,
        ]
    if perf_output is not None:
        final_command = [
            "perf",
            "record",
            "--quiet",
            "--call-graph",
            "dwarf",
            "--output",
            str(perf_output),
            "--",
            *final_command,
        ]
    completed = subprocess.run(
        final_command,
        cwd=REPO_ROOT,
        env={**os.environ, "PERF_MULTI_MATCHED_BASELINE": "1"},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    output_path.write_text(completed.stdout, encoding="utf-8")
    sys.stdout.write(completed.stdout)
    if completed.returncode != 0:
        raise RuntimeError(
            f"benchmark failed rc={completed.returncode}: {' '.join(final_command)}"
        )
    return completed.stdout


def render_markdown(report: Mapping[str, object]) -> str:
    lines = [
        "# Spot paired performance gate",
        "",
        f"- status: {report['status']}",
        f"- source commit: `{report['source_commit']}`",
        f"- source tree SHA-256: `{report['source_tree_sha256']}`",
        f"- runtime: `{report['runtime']}`",
        f"- runtime SHA-256: `{report['runtime_sha256']}`",
        f"- clients: {report['clients']}",
        f"- active duration: {report['duration_seconds']} seconds",
        f"- paired repetitions: {report['runs']}",
        "- I/O threads: server=1, client=1",
        "",
        "| Spot | Transport | Size | Throughput ratio | Mean ratio | P95 ratio | P99 ratio | Status |",
        "|---|---|---:|---:|---:|---:|---:|---|",
    ]
    for cell in report["cells"]:
        ratios = cell["latency_ratios"]
        lines.append(
            "| {spot} | {transport} | {size} | {throughput:.4f} | "
            "{mean:.4f} | {p95:.4f} | {p99:.4f} | {status} |".format(
                spot=cell["spot_pattern"],
                transport=cell["transport"],
                size=cell["size"],
                throughput=cell["throughput_ratio"],
                mean=ratios["latency"],
                p95=ratios["latency_p95"],
                p99=ratios["latency_p99"],
                status=cell["status"],
            )
        )
    lines.append("")
    return "\n".join(lines)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--patterns",
        default=",".join(PAIR_BASELINES),
        help="Spot patterns to compare",
    )
    parser.add_argument(
        "--transports", default=",".join(DEFAULT_TRANSPORTS)
    )
    parser.add_argument(
        "--msg-sizes", default=",".join(str(size) for size in DEFAULT_SIZES)
    )
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--clients", type=int, default=100)
    parser.add_argument("--duration", type=int, default=5)
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--runner", type=pathlib.Path, default=DEFAULT_RUNNER)
    parser.add_argument("--results-dir", type=pathlib.Path)
    parser.add_argument("--tag", default="s9-p03-spot-paired")
    parser.add_argument("--perf-record", action="store_true")
    parser.add_argument("--time-verbose", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--build-cooldown-ms", type=int, default=5000)
    parser.add_argument("--case-cooldown-ms", type=int, default=3000)
    parser.add_argument("--throughput-floor", type=float, default=0.90)
    parser.add_argument("--latency-ceiling", type=float, default=1.25)
    args = parser.parse_args(argv)

    try:
        patterns = parse_csv_choice(
            args.patterns, PAIR_BASELINES, "Spot patterns"
        )
        transports = tuple(
            value.strip().lower()
            for value in args.transports.split(",")
            if value.strip()
        )
        if not transports or any(value not in DEFAULT_TRANSPORTS for value in transports):
            raise ValueError(f"invalid transports: {args.transports}")
        sizes = parse_sizes(args.msg_sizes)
    except ValueError as error:
        parser.error(str(error))
    if args.runs < 1 or args.clients < 1 or args.duration < 1:
        parser.error("runs, clients, and duration must be positive")
    if args.build_cooldown_ms < 0 or args.case_cooldown_ms < 0:
        parser.error("cooldowns must be non-negative")

    runtime = find_core_runtime(DEFAULT_CORE_BUILD_DIR)
    stale_source = newest_source_after(runtime)
    if stale_source is not None:
        raise RuntimeError(
            f"stale Core runtime: {stale_source} is newer than {runtime}; "
            "run cmake --build core/build first"
        )
    if args.perf_record and shutil.which("perf") is None:
        raise RuntimeError("--perf-record requires the perf executable")
    runtime_sha256 = sha256_file(runtime)
    source_roots = (
        REPO_ROOT / "core/src",
        REPO_ROOT / "core/include",
        REPO_ROOT / "bindings/c/include",
        REPO_ROOT / "bindings/c/src",
        REPO_ROOT / "bindings/c/CMakeLists.txt",
        REPO_ROOT / "bindings/c/perf/common",
        REPO_ROOT / "bindings/c/perf/multi/common",
        REPO_ROOT / "bindings/c/perf/multi/src",
        REPO_ROOT / "bindings/c/perf/CMakeLists.txt",
        REPO_ROOT / "bindings/c/perf/run_comparison.py",
        REPO_ROOT / "bindings/c/perf/run_benchmarks_multi.sh",
        REPO_ROOT / "bindings/c/perf/run_spot_paired_gate.py",
    )
    initial_source_tree_sha256 = source_tree_sha256(source_roots)

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    results_dir = (
        args.results_dir
        if args.results_dir is not None
        else REPO_ROOT / "bindings/c/perf/results/multi/paired" / f"{timestamp}-{args.tag}"
    )
    raw_dir = results_dir / "raw"
    runner_results = results_dir / "runner-results"

    commands = []
    for spot_pattern in patterns:
        for transport in transports:
            for size in sizes:
                for repetition in range(args.runs):
                    for pattern in paired_order(spot_pattern, repetition):
                        run_tag = (
                            f"{args.tag}-{spot_pattern.lower()}-{transport}-{size}-"
                            f"r{repetition + 1}-{pattern.lower()}"
                        )
                        output_path = raw_dir / f"{run_tag}.txt"
                        commands.append(
                            (
                                spot_pattern,
                                transport,
                                size,
                                repetition,
                                pattern,
                                output_path,
                                build_case_command(
                                    args.runner,
                                    pattern,
                                    transport,
                                    size,
                                    args.clients,
                                    args.duration,
                                    runner_results,
                                    run_tag,
                                ),
                            )
                        )

    if args.dry_run:
        print(f"runtime={runtime}")
        print(f"runtime_sha256={runtime_sha256}")
        print(f"source_tree_sha256={initial_source_tree_sha256}")
        for command in commands:
            print(" ".join(command[-1]))
        return 0

    results_dir.mkdir(parents=True, exist_ok=False)
    raw_dir.mkdir(parents=True)
    runner_results.mkdir(parents=True)
    build_targets(patterns, args.jobs)
    if args.build_cooldown_ms:
        time.sleep(args.build_cooldown_ms / 1000.0)

    samples: dict[tuple[str, str, int, str], list[Sample]] = {}
    for spot_pattern, transport, size, repetition, pattern, output_path, command in commands:
        assert_runtime_identity(runtime, runtime_sha256)
        perf_output = None
        if args.perf_record:
            perf_output = output_path.with_suffix(".perf.data")
        time_output = (
            output_path.with_suffix(".time.txt") if args.time_verbose else None
        )
        print(
            f"=== pair {spot_pattern} {transport} {size}B "
            f"repetition {repetition + 1}/{args.runs}: {pattern} ==="
        )
        text = run_case(command, output_path, perf_output, time_output)
        expected_runtime_line = f"Perf runtime libzlink: {runtime}"
        if expected_runtime_line not in text:
            raise RuntimeError(
                f"runner did not confirm expected Core runtime: {expected_runtime_line}"
            )
        sample = parse_report(text, pattern, transport, size)
        samples.setdefault((spot_pattern, transport, size, pattern), []).append(sample)
        if args.case_cooldown_ms:
            time.sleep(args.case_cooldown_ms / 1000.0)

    cells = []
    for spot_pattern in patterns:
        baseline_pattern = PAIR_BASELINES[spot_pattern]
        for transport in transports:
            for size in sizes:
                spot = median_metrics(
                    samples[(spot_pattern, transport, size, spot_pattern)]
                )
                spot_samples = samples[
                    (spot_pattern, transport, size, spot_pattern)
                ]
                baseline = median_metrics(
                    samples[(spot_pattern, transport, size, baseline_pattern)]
                )
                cell = evaluate_cell(
                    spot_pattern,
                    spot,
                    baseline,
                    args.throughput_floor,
                    args.latency_ceiling,
                    sum(
                        sample.multicast_dropped_targets
                        for sample in spot_samples
                    ),
                )
                cell["transport"] = transport
                cell["size"] = size
                cells.append(cell)

    final_source_tree_sha256 = source_tree_sha256(source_roots)
    if final_source_tree_sha256 != initial_source_tree_sha256:
        raise RuntimeError(
            "Core or C perf source changed during paired gate: "
            f"start={initial_source_tree_sha256} final={final_source_tree_sha256}"
        )
    assert_runtime_identity(runtime, runtime_sha256)

    report = {
        "status": "pass" if all(cell["status"] == "pass" for cell in cells) else "fail",
        "source_commit": git_commit(),
        "source_tree_sha256": initial_source_tree_sha256,
        "runtime": str(runtime),
        "runtime_sha256": runtime_sha256,
        "clients": args.clients,
        "duration_seconds": args.duration,
        "runs": args.runs,
        "patterns": patterns,
        "transports": transports,
        "msg_sizes": sizes,
        "throughput_floor": args.throughput_floor,
        "latency_ceiling": args.latency_ceiling,
        "cells": cells,
    }
    (results_dir / "paired-gate.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (results_dir / "paired-gate.md").write_text(
        render_markdown(report), encoding="utf-8"
    )
    print(render_markdown(report))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
