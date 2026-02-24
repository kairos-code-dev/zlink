#!/usr/bin/env python3
"""Single-pattern benchmark runner with policy-compliant reporting."""

from __future__ import annotations

import argparse
import datetime
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple

IS_WINDOWS = os.name == "nt"
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""

SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
PERF_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
ROOT_DIR = os.path.abspath(os.path.join(PERF_DIR, "..", ".."))
BUILD_CONFIG_DIRS = ("Release", "Debug", "RelWithDebInfo", "MinSizeRel")

DEFAULT_PATTERNS = [
    "PAIR",
    "PUBSUB",
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "ROUTER_ROUTER_POLL",
    "STREAM",
    "STREAM_CALLBACK",
    "STREAM_LEN32BE",
    "GATEWAY",
    "SPOT",
]

PATTERN_TO_BINARY = {
    "PAIR": "perf_pair",
    "PUBSUB": "perf_pubsub",
    "DEALER_DEALER": "perf_dealer_dealer",
    "DEALER_ROUTER": "perf_dealer_router",
    "ROUTER_ROUTER": "perf_router_router",
    "ROUTER_ROUTER_POLL": "perf_router_router_poll",
    "STREAM": "perf_stream",
    "STREAM_CALLBACK": "perf_stream_callback",
    "STREAM_LEN32BE": "perf_stream_len32be",
    "GATEWAY": "perf_gateway",
    "SPOT": "perf_spot",
}

DEFAULT_MSG_SIZES_STANDARD = [64, 256, 1024, 65536, 131072, 262144]
DEFAULT_MSG_SIZES_STREAM = [64, 256, 1024, 65536]
DEFAULT_SOCKET_TRANSPORTS = ["tcp", "tls", "ws", "wss", "inproc"]
if not IS_WINDOWS:
    DEFAULT_SOCKET_TRANSPORTS.append("ipc")
DEFAULT_STREAM_TRANSPORTS = ["tcp", "tls", "ws", "wss"]
STREAM_TRANSPORT_PATTERNS = {
    "STREAM",
    "STREAM_CALLBACK",
    "STREAM_LEN32BE",
    "GATEWAY",
    "SPOT",
}
STREAM_SIZE_PATTERNS = {
    "STREAM",
    "STREAM_CALLBACK",
    "STREAM_LEN32BE",
}

DEFAULT_RESULTS_DIR = os.path.join(PERF_DIR, "results")
DEFAULT_MAX_RESULT_FILES = 100
DEFAULT_THRESHOLD_FILE = os.path.join(PERF_DIR, "thresholds.json")

THROUGHPUT_WARN_PCT = -10.0
THROUGHPUT_FAIL_PCT = -15.0
LATENCY_WARN_PCT = 10.0
LATENCY_FAIL_PCT = 15.0
PATTERN_SEPARATOR = "==============================================================================="

MetricKey = Tuple[str, str, str, int, str]  # lib, pattern, transport, size, metric


@dataclass
class RunOutcome:
    status: str  # success | unsupported | skip | fail
    throughput: float = 0.0
    bandwidth: float = 0.0
    latency: float = 0.0
    cpu_pct: Optional[float] = None
    mem_mb: Optional[float] = None
    reason: str = ""
    warnings: Optional[List[str]] = None


@dataclass
class ComboRecord:
    status: str  # success | unsupported | skip | fail
    throughput: float = 0.0
    bandwidth: float = 0.0
    latency: float = 0.0
    cpu_pct: Optional[float] = None
    mem_mb: Optional[float] = None


@dataclass
class ThresholdRule:
    pattern: str
    transport: str
    metric: str
    warning: Optional[float]
    fail: Optional[float]
    rank: int
    order: int


def env_get(primary_name: str, legacy_name: Optional[str] = None) -> str:
    val = os.environ.get(primary_name)
    if val:
        return val
    if legacy_name:
        legacy_val = os.environ.get(legacy_name)
        if legacy_val:
            return legacy_val
    return ""


def env_flag_enabled(primary_name: str, legacy_name: Optional[str] = None) -> bool:
    return env_get(primary_name, legacy_name) == "1"


def parse_env_list(name: str, cast_fn, legacy_name: Optional[str] = None):
    val = env_get(name, legacy_name)
    if not val:
        return None
    items = []
    for part in val.split(","):
        part = part.strip()
        if not part:
            continue
        try:
            items.append(cast_fn(part))
        except ValueError:
            continue
    return items or None


def parse_env_int(name: str, default: int, legacy_name: Optional[str] = None) -> int:
    val = env_get(name, legacy_name)
    if not val:
        return default
    try:
        return int(val)
    except ValueError:
        return default


FAIL_FAST = env_flag_enabled("PERF_FAIL_FAST", "BENCH_FAIL_FAST")


def platform_tag() -> str:
    if IS_WINDOWS:
        return "windows"
    if "darwin" in platform.system().lower():
        return "macos"
    return "linux"


def sanitize_suffix(value: str) -> str:
    if not value:
        return ""
    return re.sub(r"[^a-zA-Z0-9._-]+", "_", value.strip())


def build_result_filename(tag: str = "") -> str:
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    name = f"perf_{platform_tag()}_{stamp}"
    clean_tag = sanitize_suffix(tag)
    if clean_tag:
        name = f"{name}_{clean_tag}"
    return f"{name}.txt"


def single_tmp_dir(results_root: str) -> str:
    return os.path.join(results_root, "single", "tmp")


def single_report_dir(results_root: str) -> str:
    return os.path.join(results_root, "single", "report")


def single_baseline_dir(results_root: str) -> str:
    return os.path.join(results_root, "single", "baseline")


def enforce_file_retention(
    directory: str,
    max_files: int = DEFAULT_MAX_RESULT_FILES,
    exclude_names: Optional[Iterable[str]] = None,
) -> None:
    if max_files <= 0 or not os.path.isdir(directory):
        return

    excluded = set(exclude_names or [])
    files: List[Tuple[str, str]] = []
    for name in os.listdir(directory):
        if name in excluded:
            continue
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            continue
        files.append((name, path))

    if len(files) <= max_files:
        return

    files.sort(key=lambda item: item[0])
    excess = len(files) - max_files
    for _, path in files[:excess]:
        try:
            os.remove(path)
        except OSError:
            pass


def resolve_linux_paths() -> Tuple[str, str]:
    sys_name = platform.system().lower()
    platform_tag = "macos" if "darwin" in sys_name else "linux"
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        arch_tag = "x64"
    elif machine in ("aarch64", "arm64"):
        arch_tag = "arm64"
    else:
        arch_tag = machine

    possible_paths = [
        os.path.join(ROOT_DIR, "core", "build", f"{platform_tag}-{arch_tag}", "bin"),
        os.path.join(
            ROOT_DIR, "core", "build", f"{platform_tag}-{arch_tag}", "bin", "Release"
        ),
        os.path.join(ROOT_DIR, "core", "build", "bin"),
    ]

    build_dir = next((p for p in possible_paths if os.path.exists(p)), possible_paths[0])
    build_root = build_dir
    base = os.path.basename(build_root)
    if base in BUILD_CONFIG_DIRS:
        bin_root = os.path.dirname(build_root)
        if os.path.basename(bin_root) == "bin":
            build_root = os.path.dirname(bin_root)
    elif base == "bin":
        build_root = os.path.dirname(build_root)

    lib_dir = os.path.abspath(os.path.join(build_root, "lib"))
    return build_dir, lib_dir


def normalize_build_dir(path: str) -> str:
    if not path:
        return path
    abs_path = os.path.abspath(path)
    if not os.path.isdir(abs_path):
        return abs_path

    base = os.path.basename(abs_path)
    if base in BUILD_CONFIG_DIRS:
        return abs_path

    if base == "bin":
        if IS_WINDOWS:
            release_dir = os.path.join(abs_path, "Release")
            if os.path.isdir(release_dir):
                return release_dir
        return abs_path

    bin_dir = os.path.join(abs_path, "bin")
    if os.path.isdir(bin_dir):
        if IS_WINDOWS:
            release_dir = os.path.join(bin_dir, "Release")
            if os.path.isdir(release_dir):
                return release_dir
        return bin_dir

    return abs_path


def derive_current_lib_dir(build_dir: str) -> str:
    build_root = build_dir
    base = os.path.basename(build_root)
    if base in BUILD_CONFIG_DIRS:
        bin_root = os.path.dirname(build_root)
        if os.path.basename(bin_root) == "bin":
            build_root = os.path.dirname(bin_root)
    elif base == "bin":
        build_root = os.path.dirname(build_root)
    return os.path.abspath(os.path.join(build_root, "lib"))


def has_cmake_cache(path: str) -> bool:
    return os.path.isfile(os.path.join(path, "CMakeCache.txt"))


def derive_cmake_build_dir(runtime_build_dir: str) -> str:
    if not runtime_build_dir:
        return ""

    abs_path = os.path.abspath(runtime_build_dir)
    if not os.path.isdir(abs_path):
        return ""
    if has_cmake_cache(abs_path):
        return abs_path

    base = os.path.basename(abs_path)
    if base in BUILD_CONFIG_DIRS:
        bin_root = os.path.dirname(abs_path)
        if os.path.basename(bin_root) == "bin":
            candidate = os.path.dirname(bin_root)
            if has_cmake_cache(candidate):
                return candidate
    elif base == "bin":
        candidate = os.path.dirname(abs_path)
        if has_cmake_cache(candidate):
            return candidate

    return ""


def run_cmake_build(cmake_build_dir: str, targets: List[str]) -> int:
    cmd = ["cmake", "--build", cmake_build_dir]
    if IS_WINDOWS:
        cmd.extend(["--config", "Release"])
    if targets:
        cmd.extend(["--target", *targets])

    print(f"  > Auto-building missing benchmark binaries in {cmake_build_dir}", flush=True)
    print(f"  > Build command: {' '.join(cmd)}", flush=True)

    try:
        return subprocess.run(cmd).returncode
    except FileNotFoundError:
        print("Error: cmake not found in PATH.", file=sys.stderr)
        return 127


def select_transports(pattern: str) -> List[str]:
    base = (
        DEFAULT_STREAM_TRANSPORTS
        if pattern in STREAM_TRANSPORT_PATTERNS
        else DEFAULT_SOCKET_TRANSPORTS
    )
    env_transports = parse_env_list("PERF_TRANSPORTS", str, "BENCH_TRANSPORTS")
    if not env_transports:
        return list(base)
    return [t for t in base if t in env_transports]


def default_msg_sizes_for_pattern(pattern: str) -> List[int]:
    if pattern in STREAM_SIZE_PATTERNS:
        return list(DEFAULT_MSG_SIZES_STREAM)
    return list(DEFAULT_MSG_SIZES_STANDARD)


def msg_sizes_for_pattern(pattern: str) -> List[int]:
    env_sizes = parse_env_list("PERF_MSG_SIZES", int, "BENCH_MSG_SIZES")
    if env_sizes:
        return env_sizes
    return default_msg_sizes_for_pattern(pattern)


def get_env_for_lib(current_lib_dir: str) -> Dict[str, str]:
    env = os.environ.copy()
    if IS_WINDOWS:
        env["PATH"] = f"{current_lib_dir};{env.get('PATH', '')}"
    else:
        env["LD_LIBRARY_PATH"] = f"{current_lib_dir}:{env.get('LD_LIBRARY_PATH', '')}"
    return env


def parse_metric_from_result_line(
    line: str,
    expected_lib: str,
    expected_pattern: str,
    expected_transport: str,
    expected_size: int,
) -> Tuple[Optional[Tuple[str, float]], Optional[str]]:
    if not line.startswith("RESULT,"):
        return None, None
    parts = line.split(",")
    if len(parts) != 7:
        return None, f"ignored malformed RESULT line (field_count={len(parts)}): {line}"
    try:
        lib = parts[1].strip()
        pattern = parts[2].strip().upper()
        transport = parts[3].strip()
        size = int(parts[4].strip())
        metric = parts[5].strip().lower()
        value = float(parts[6].strip())
    except ValueError:
        return None, f"ignored malformed RESULT numeric field: {line}"
    if lib != expected_lib or pattern != expected_pattern.upper():
        return None, None
    if transport != expected_transport or size != expected_size:
        return None, None
    if metric not in ("throughput", "bandwidth", "latency", "cpu_pct", "mem_mb"):
        return None, f"ignored unknown RESULT metric '{metric}': {line}"
    return (metric, value), None


def detect_special_status(
    stdout: str,
    expected_lib: str,
    expected_pattern: str,
    expected_transport: str,
) -> Optional[str]:
    expected_pattern = expected_pattern.upper()
    expected_transport = expected_transport.lower()
    for raw in stdout.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("UNSUPPORTED,"):
            parts = line.split(",")
            if len(parts) >= 4:
                lib = parts[1].strip()
                pattern = parts[2].strip().upper()
                transport = parts[3].strip().lower()
                if (
                    lib == expected_lib
                    and pattern == expected_pattern
                    and transport == expected_transport
                ):
                    return "unsupported"
        elif line.startswith("SKIP,"):
            parts = line.split(",")
            if len(parts) >= 5:
                lib = parts[1].strip()
                pattern = parts[2].strip().upper()
                transport = parts[3].strip().lower()
                if (
                    lib == expected_lib
                    and pattern == expected_pattern
                    and transport == expected_transport
                ):
                    return "skip"
    return None


def run_single_test(
    build_dir: str,
    current_lib_dir: str,
    binary_name: str,
    lib_name: str,
    pattern: str,
    transport: str,
    size: int,
    timeout_sec: int,
    pin_cpu: bool,
) -> RunOutcome:
    binary_path = os.path.join(build_dir, binary_name + EXE_SUFFIX)
    env = get_env_for_lib(current_lib_dir)

    if IS_WINDOWS:
        cmd = [binary_path, lib_name, transport, str(size)]
    elif pin_cpu or env_flag_enabled("PERF_TASKSET", "BENCH_TASKSET"):
        cmd = ["taskset", "-c", "1", binary_path, lib_name, transport, str(size)]
    else:
        cmd = [binary_path, lib_name, transport, str(size)]

    try:
        proc = subprocess.run(
            cmd,
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout_sec,
        )
    except subprocess.TimeoutExpired:
        return RunOutcome(status="fail", reason="timeout")
    except Exception as exc:  # pragma: no cover - defensive
        return RunOutcome(status="fail", reason=f"exception:{exc}")

    stdout = proc.stdout or ""
    stderr = proc.stderr or ""

    metrics: Dict[str, float] = {}
    warnings: List[str] = []
    for line in stdout.splitlines():
        parsed, warning = parse_metric_from_result_line(
            line, lib_name, pattern, transport, size
        )
        if warning:
            warnings.append(warning)
        if not parsed:
            continue
        metric, value = parsed
        if metric in metrics:
            warnings.append(
                "duplicate RESULT metric detected; keeping last value: "
                f"{pattern} {transport} {size}B {metric}"
            )
        metrics[metric] = value

    special = detect_special_status(stdout, lib_name, pattern, transport)
    if proc.returncode != 0:
        return RunOutcome(
            status="fail",
            reason=f"non_zero_exit_{proc.returncode}",
            warnings=warnings or None,
        )

    if "throughput" in metrics and "bandwidth" in metrics and "latency" in metrics:
        return RunOutcome(
            status="success",
            throughput=metrics["throughput"],
            bandwidth=metrics["bandwidth"],
            latency=metrics["latency"],
            cpu_pct=metrics.get("cpu_pct"),
            mem_mb=metrics.get("mem_mb"),
            warnings=warnings or None,
        )

    if special == "unsupported" and not metrics:
        return RunOutcome(
            status="unsupported", reason="unsupported", warnings=warnings or None
        )
    if special == "skip" and not metrics:
        return RunOutcome(status="skip", reason="skip", warnings=warnings or None)

    return RunOutcome(
        status="fail",
        reason="no_data",
        warnings=warnings or None,
    )


def parse_pattern_arg(pattern_arg: str) -> List[str]:
    if pattern_arg.upper() == "ALL":
        return list(DEFAULT_PATTERNS)

    requested = []
    for part in pattern_arg.split(","):
        p = part.strip().upper()
        if not p:
            continue
        requested.append(p)

    if not requested:
        raise ValueError("--pattern requires at least one value")

    unknown = [p for p in requested if p not in PATTERN_TO_BINARY]
    if unknown:
        raise ValueError("unsupported patterns: " + ", ".join(sorted(set(unknown))))

    ordered = [p for p in DEFAULT_PATTERNS if p in requested]
    return ordered


def collect_missing_patterns(build_dir: str, patterns: Iterable[str]) -> List[str]:
    missing = []
    for pattern in patterns:
        binary_name = PATTERN_TO_BINARY[pattern]
        binary_path = os.path.join(build_dir, binary_name + EXE_SUFFIX)
        if not os.path.exists(binary_path):
            missing.append(pattern)
    return sorted(set(missing))


def collect_missing_build_targets(build_dir: str, patterns: Iterable[str]) -> List[str]:
    targets = []
    for pattern in patterns:
        binary_name = PATTERN_TO_BINARY[pattern]
        binary_path = os.path.join(build_dir, binary_name + EXE_SUFFIX)
        if not os.path.exists(binary_path):
            targets.append(binary_name)
    return targets


def pattern_direction_label(_pattern: str) -> str:
    return "one-way"


def format_throughput(_pattern: str, throughput_per_sec: float) -> str:
    return f"{throughput_per_sec/1e3:6.2f} Kmsg/s"


def format_bandwidth(bandwidth_mb_s: float) -> str:
    return f"{bandwidth_mb_s:8.2f} MB/s"


def format_latency_us(latency: float) -> str:
    return f"{latency:8.2f} us"


def build_pattern_table_lines(
    pattern: str,
    transports: List[str],
    sizes: List[int],
    combo_results: Dict[Tuple[str, str, int], ComboRecord],
) -> List[str]:
    lines: List[str] = [f"## PATTERN: {pattern} ({pattern_direction_label(pattern)})", ""]
    for transport in transports:
        lines.append(f"### Transport: {transport}")
        lines.append("| Size   |       Throughput |    Bandwidth |     Latency | CPU% |  Mem MB |")
        lines.append("|--------|------------------|--------------|-------------|------|---------|")
        for size in sizes:
            record = combo_results.get((pattern, transport, size))
            if not record or record.status != "success":
                lines.append(
                    f"| {size}B  |              N/A |          N/A |         N/A |  N/A |     N/A |"
                )
                continue
            tp_s = format_throughput(pattern, record.throughput)
            bw_s = format_bandwidth(record.bandwidth)
            lat_s = format_latency_us(record.latency)
            cpu_s = "N/A"
            mem_s = "N/A"
            if record.cpu_pct is not None:
                cpu_s = f"{record.cpu_pct:.1f}"
            if record.mem_mb is not None:
                mem_s = f"{record.mem_mb:.1f}"
            lines.append(
                f"| {size}B  | {tp_s:>16} | {bw_s:>12} | {lat_s:>11} | {cpu_s:>4} | {mem_s:>7} |"
            )
        lines.append("")
    while lines and lines[-1] == "":
        lines.pop()
    return lines


def parse_result_line_generic(line: str) -> Optional[Tuple[MetricKey, float]]:
    if not line.startswith("RESULT,"):
        return None
    parts = line.strip().split(",")
    if len(parts) != 7:
        return None
    lib = parts[1].strip()
    pattern = parts[2].strip().upper()
    transport = parts[3].strip().lower()
    try:
        size = int(parts[4].strip())
        metric = parts[5].strip().lower()
        value = float(parts[6].strip())
    except ValueError:
        return None

    if metric not in ("throughput", "bandwidth", "latency", "cpu_pct", "mem_mb"):
        return None

    return (lib, pattern, transport, size, metric), value


def parse_result_file(path: str) -> Optional[Tuple[Dict[str, str], Dict[MetricKey, float]]]:
    try:
        with open(path, "r", encoding="utf-8") as fh:
            lines = fh.readlines()
    except OSError:
        return None

    meta: Dict[str, str] = {}
    results: Dict[MetricKey, float] = {}
    saw_meta = False

    for raw in lines:
        line = raw.strip()
        if not line:
            continue

        if line.startswith("META,"):
            parts = line.split(",", 2)
            if len(parts) == 3:
                saw_meta = True
                meta[parts[1].strip()] = parts[2].strip()
            continue

        parsed = parse_result_line_generic(line)
        if parsed:
            key, value = parsed
            results[key] = value
    if not saw_meta or not results:
        return None

    return meta, results


def sorted_result_files(report_dir: str) -> List[str]:
    if not os.path.isdir(report_dir):
        return []

    files = [
        os.path.join(report_dir, name)
        for name in os.listdir(report_dir)
        if (
            name.endswith(".txt")
            and (name.startswith("perf_") or name.startswith("bench_"))
        )
    ]

    files.sort(key=lambda p: os.path.basename(p), reverse=True)
    return files


def load_rolling_baseline(
    results_dir: str,
    rolling_n: int,
    keys_of_interest: Iterable[MetricKey],
) -> Tuple[Dict[MetricKey, float], Dict[MetricKey, int]]:
    files = sorted_result_files(results_dir)
    wanted = list(keys_of_interest)
    samples: Dict[MetricKey, List[float]] = {key: [] for key in wanted}

    if rolling_n <= 0:
        rolling_n = 1

    complete_files = 0
    for path in files:
        parsed = parse_result_file(path)
        if not parsed:
            continue
        file_meta, file_results = parsed
        if file_meta.get("status", "").strip().lower() != "complete":
            continue

        for key in wanted:
            if key in file_results:
                samples[key].append(file_results[key])
        complete_files += 1
        if complete_files >= rolling_n:
            break

    baseline: Dict[MetricKey, float] = {}
    counts: Dict[MetricKey, int] = {}
    for key, values in samples.items():
        if values:
            baseline[key] = statistics.median(values)
        if values:
            counts[key] = len(values)

    return baseline, counts


def load_fixed_baseline(path: str) -> Dict[MetricKey, float]:
    parsed = parse_result_file(path)
    if not parsed:
        return {}
    _, results = parsed
    return results


def threshold_defaults(metric: str) -> Tuple[float, float]:
    if metric == "throughput":
        return THROUGHPUT_WARN_PCT, THROUGHPUT_FAIL_PCT
    return LATENCY_WARN_PCT, LATENCY_FAIL_PCT


def threshold_rank(pattern: str, transport: str) -> int:
    if pattern != "*" and transport != "*":
        return 1
    if pattern != "*" and transport == "*":
        return 2
    if pattern == "*" and transport != "*":
        return 3
    return 4


def normalize_threshold_value(metric: str, field: str, value: float) -> Tuple[float, Optional[str]]:
    if metric == "throughput" and value > 0:
        return -abs(value), (
            f"{metric} threshold should be <= 0; "
            f"auto-negated key field '{field}' to {-abs(value):.2f}"
        )
    if metric == "latency" and value < 0:
        return abs(value), (
            "latency threshold should be >= 0; "
            f"auto-abs key field '{field}' to {abs(value):.2f}"
        )
    return value, None


def load_threshold_rules() -> Tuple[List[ThresholdRule], List[str]]:
    path = (
        env_get("PERF_THRESHOLDS_FILE", "BENCH_THRESHOLDS_FILE").strip()
        or DEFAULT_THRESHOLD_FILE
    )
    warnings: List[str] = []

    if not os.path.isfile(path):
        return [], warnings

    try:
        with open(path, "r", encoding="utf-8") as fh:
            raw = json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        warnings.append(f"threshold file parse failed ({path}): {exc}; using defaults")
        return [], warnings

    if not isinstance(raw, dict):
        warnings.append(f"threshold file must be a JSON object: {path}; using defaults")
        return [], warnings

    rules: List[ThresholdRule] = []
    for order, (raw_key, raw_value) in enumerate(raw.items()):
        if not isinstance(raw_key, str):
            warnings.append(f"ignored invalid threshold key (non-string): {raw_key!r}")
            continue
        parts = raw_key.split("/")
        if len(parts) != 3:
            warnings.append(f"ignored invalid threshold key format: {raw_key}")
            continue
        pattern, transport, metric = parts[0].strip(), parts[1].strip(), parts[2].strip().lower()
        if not pattern or not transport or metric not in ("throughput", "latency"):
            warnings.append(f"ignored invalid threshold key: {raw_key}")
            continue
        if pattern != "*" and not pattern.replace("_", "").isalnum():
            warnings.append(f"ignored invalid threshold pattern segment: {raw_key}")
            continue
        if transport != "*" and not transport.isalpha():
            warnings.append(f"ignored invalid threshold transport segment: {raw_key}")
            continue
        if not isinstance(raw_value, dict):
            warnings.append(f"ignored threshold value (must be object): {raw_key}")
            continue

        warn_val: Optional[float] = None
        fail_val: Optional[float] = None
        if "warning" in raw_value:
            try:
                warn_val = float(raw_value["warning"])
            except (TypeError, ValueError):
                warnings.append(f"ignored threshold key due to non-numeric warning: {raw_key}")
                continue
            warn_val, sign_warn = normalize_threshold_value(metric, "warning", warn_val)
            if sign_warn:
                warnings.append(f"{raw_key}: {sign_warn}")
        if "fail" in raw_value:
            try:
                fail_val = float(raw_value["fail"])
            except (TypeError, ValueError):
                warnings.append(f"ignored threshold key due to non-numeric fail: {raw_key}")
                continue
            fail_val, sign_warn = normalize_threshold_value(metric, "fail", fail_val)
            if sign_warn:
                warnings.append(f"{raw_key}: {sign_warn}")
        if warn_val is None and fail_val is None:
            continue

        rules.append(
            ThresholdRule(
                pattern=pattern.upper(),
                transport=transport.lower(),
                metric=metric,
                warning=warn_val,
                fail=fail_val,
                rank=threshold_rank(pattern, transport),
                order=order,
            )
        )

    return rules, warnings


def resolve_threshold(
    rules: List[ThresholdRule], pattern: str, transport: str, metric: str
) -> Tuple[float, float]:
    warning, fail = threshold_defaults(metric)
    pattern = pattern.upper()
    transport = transport.lower()
    for rank in (1, 2, 3, 4):
        for rule in rules:
            if rule.rank != rank or rule.metric != metric:
                continue
            if rule.pattern not in (pattern, "*"):
                continue
            if rule.transport not in (transport, "*"):
                continue
            if rule.warning is not None:
                warning = rule.warning
            if rule.fail is not None:
                fail = rule.fail
            return warning, fail
    return warning, fail


def compare_against_baseline(
    mode: str,
    current: Dict[MetricKey, float],
    baseline: Dict[MetricKey, float],
    threshold_rules: List[ThresholdRule],
) -> Tuple[List[str], List[str], int]:
    warnings: List[str] = []
    fails: List[str] = []
    skipped = 0

    for key in sorted(current.keys()):
        _, pattern, transport, size, metric = key
        if metric not in ("throughput", "latency"):
            continue

        cur = current[key]
        base = baseline.get(key)
        if base is None:
            warnings.append(
                f"{pattern} {transport} {size}B {metric}: baseline missing; comparison skipped"
            )
            skipped += 1
            continue
        if base <= 0:
            warnings.append(
                f"{pattern} {transport} {size}B {metric}: baseline <= 0; comparison skipped"
            )
            skipped += 1
            continue

        delta_pct = ((cur - base) / base) * 100.0

        warn_limit, fail_limit = resolve_threshold(threshold_rules, pattern, transport, metric)
        if metric == "throughput":
            warn = delta_pct <= warn_limit
            fail = delta_pct <= fail_limit
        else:
            warn = delta_pct >= warn_limit
            fail = delta_pct >= fail_limit

        msg = (
            f"{pattern} {transport} {size}B {metric}: "
            f"current={cur:.2f}, baseline={base:.2f}, delta={delta_pct:+.2f}%"
        )

        if warn:
            warnings.append(msg)
        if mode == "gate" and fail:
            fails.append(msg)

    return warnings, fails, skipped


def detect_cpu_model() -> str:
    if not IS_WINDOWS and os.path.isfile("/proc/cpuinfo"):
        try:
            with open("/proc/cpuinfo", "r", encoding="utf-8", errors="ignore") as fh:
                for line in fh:
                    if line.lower().startswith("model name"):
                        return line.split(":", 1)[1].strip()
        except OSError:
            pass

    cpu = platform.processor().strip()
    if cpu:
        return cpu
    return "unknown"


def read_build_type(build_dir: str) -> str:
    cmake_build_dir = derive_cmake_build_dir(build_dir)
    if not cmake_build_dir:
        return "Release"

    cache_path = os.path.join(cmake_build_dir, "CMakeCache.txt")
    if not os.path.isfile(cache_path):
        return "Release"

    try:
        with open(cache_path, "r", encoding="utf-8", errors="ignore") as fh:
            for line in fh:
                if line.startswith("CMAKE_BUILD_TYPE:STRING="):
                    build_type = line.split("=", 1)[1].strip()
                    return build_type or "Release"
    except OSError:
        pass

    return "Release"


def git_commit_short() -> str:
    try:
        proc = subprocess.run(
            ["git", "-C", ROOT_DIR, "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            check=False,
        )
    except Exception:
        return "unknown"

    if proc.returncode != 0:
        return "unknown"
    out = (proc.stdout or "").strip()
    return out or "unknown"


def gather_meta(mode: str, runs: int, build_dir: str) -> Dict[str, str]:
    local_now = datetime.datetime.now().astimezone()
    meta = {
        "os": platform.platform(),
        "cpu": detect_cpu_model(),
        "cores": str(os.cpu_count() or 0),
        "build": read_build_type(build_dir),
        "commit": git_commit_short(),
        "timestamp": local_now.replace(microsecond=0).isoformat(),
        "mode": mode,
        "runs": str(runs),
    }

    if hasattr(os, "getloadavg"):
        try:
            la = os.getloadavg()
            meta["load_avg"] = f"{la[0]:.2f} {la[1]:.2f} {la[2]:.2f}"
        except OSError:
            pass

    return meta


def result_sort_key(key: MetricKey):
    lib, pattern, transport, size, metric = key
    pattern_order = {name: idx for idx, name in enumerate(DEFAULT_PATTERNS)}
    transport_order = {
        "tcp": 0,
        "inproc": 1,
        "ipc": 2,
        "tls": 3,
        "ws": 4,
        "wss": 5,
    }
    metric_order = {
        "throughput": 0,
        "bandwidth": 1,
        "latency": 2,
        "cpu_pct": 3,
        "mem_mb": 4,
    }
    return (
        pattern_order.get(pattern, 999),
        transport_order.get(transport, 999),
        size,
        metric_order.get(metric, 999),
        lib,
    )


def write_result_file(
    path: str,
    meta: Dict[str, str],
    metrics: Dict[MetricKey, float],
    table_text: str = "",
    table_only: bool = False,
) -> None:
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)

    cleaned_table = table_text.rstrip("\n")

    ordered_meta_keys = [
        "os",
        "cpu",
        "cores",
        "build",
        "commit",
        "timestamp",
        "load_avg",
        "mode",
        "runs",
        "status",
        "expected",
        "actual",
    ]

    with open(path, "w", encoding="utf-8") as fh:
        if table_only:
            if cleaned_table:
                fh.write(cleaned_table + "\n")
            return
        for key in ordered_meta_keys:
            if key in meta:
                fh.write(f"META,{key},{meta[key]}\n")
        for metric_key in sorted(metrics.keys(), key=result_sort_key):
            lib, pattern, transport, size, metric = metric_key
            value = metrics[metric_key]
            fh.write(
                f"RESULT,{lib},{pattern},{transport},{size},{metric},{value:.2f}\n"
            )
        if cleaned_table:
            fh.write("TABLE\n")
            fh.write(cleaned_table + "\n")


def save_baseline(
    results_dir: str,
    version: str,
    meta: Dict[str, str],
    metrics: Dict[MetricKey, float],
    table_text: str,
) -> str:
    clean_version = sanitize_suffix(version)
    if not clean_version:
        raise ValueError("baseline version is empty")

    baseline_dir = single_baseline_dir(results_dir)
    os.makedirs(baseline_dir, exist_ok=True)

    baseline_path = os.path.join(baseline_dir, f"{clean_version}.txt")
    write_result_file(baseline_path, meta, metrics, table_text=table_text)

    latest_path = os.path.join(baseline_dir, "latest.txt")
    if os.path.lexists(latest_path):
        os.remove(latest_path)

    try:
        os.symlink(os.path.basename(baseline_path), latest_path)
    except OSError:
        shutil.copy2(baseline_path, latest_path)

    enforce_file_retention(
        baseline_dir, DEFAULT_MAX_RESULT_FILES, exclude_names=("latest.txt",)
    )
    return baseline_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Measure current zlink single-pattern benchmarks."
    )
    parser.add_argument("pattern", nargs="?", default="ALL")
    parser.add_argument("--runs", type=int, default=None)
    parser.add_argument("--build-dir", default="")
    parser.add_argument("--pin-cpu", action="store_true")
    parser.add_argument("--mode", choices=["observe", "trend", "gate"], default="observe")
    parser.add_argument("--baseline-file", default="")
    parser.add_argument("--result", action="store_true", dest="save_report")
    parser.add_argument("--save", nargs="?", const="", default=None)
    parser.add_argument("--save-report", action="store_true", dest="save_report_legacy")
    parser.add_argument("--save-baseline", default=None)
    parser.add_argument("--save-file", default="")
    parser.add_argument("--rolling-n", type=int, default=None)
    parser.add_argument("--results-dir", default=DEFAULT_RESULTS_DIR)
    parser.add_argument("--results-tag", default="")
    parser.add_argument("--result-file", default="")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if not args.results_tag:
        args.results_tag = env_get("PERF_RESULTS_TAG", "BENCH_RESULTS_TAG").strip()

    if args.runs is None:
        mode_default_runs = {"observe": 1, "trend": 3, "gate": 5}
        args.runs = mode_default_runs.get(args.mode, 1)
    if args.runs < 1:
        print("Error: --runs must be >= 1.", file=sys.stderr)
        return 1
    if args.save_baseline is not None:
        print(
            "Error: --save-baseline is removed. Use --save [VERSION].",
            file=sys.stderr,
        )
        return 1
    if args.rolling_n is None:
        args.rolling_n = parse_env_int("PERF_ROLLING_N", 10, "BENCH_ROLLING_N")
    if args.rolling_n < 1:
        print("Error: --rolling-n must be >= 1.", file=sys.stderr)
        return 1

    try:
        patterns = parse_pattern_arg(args.pattern)
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    if IS_WINDOWS:
        build_dir = normalize_build_dir(
            args.build_dir
            or os.path.join(ROOT_DIR, "core", "build", "windows-x64", "bin", "Release")
        )
    else:
        auto_build_dir, _ = resolve_linux_paths()
        build_dir = normalize_build_dir(args.build_dir or auto_build_dir)

    current_lib_dir = derive_current_lib_dir(build_dir)
    timeout_sec = max(
        1,
        parse_env_int(
            "PERF_SINGLE_TIMEOUT_SECONDS", 120, "BENCH_SINGLE_TIMEOUT_SECONDS"
        ),
    )

    no_autobuild = (
        env_flag_enabled("PERF_NO_AUTOBUILD", "BENCH_NO_AUTOBUILD")
        or env_flag_enabled("PERF_SKIP_AUTO_BUILD", "BENCH_SKIP_AUTO_BUILD")
    )

    missing_patterns = collect_missing_patterns(build_dir, patterns)
    if missing_patterns:
        if no_autobuild:
            print(
                "Error: current benchmark binaries are missing for patterns: "
                + ", ".join(missing_patterns),
                file=sys.stderr,
            )
            return 1
        cmake_build_dir = derive_cmake_build_dir(build_dir)
        if not cmake_build_dir:
            print(
                "Error: missing benchmark binaries and failed to derive CMake build dir.",
                file=sys.stderr,
            )
            print(f"  build dir: {build_dir}", file=sys.stderr)
            print("  missing: " + ", ".join(missing_patterns), file=sys.stderr)
            return 1

        targets = collect_missing_build_targets(build_dir, patterns)
        build_rc = run_cmake_build(cmake_build_dir, targets)
        if build_rc != 0:
            print(f"Error: auto-build failed with exit code {build_rc}", file=sys.stderr)
            return build_rc

    missing_patterns = collect_missing_patterns(build_dir, patterns)
    if missing_patterns:
        print(
            "Error: current benchmark binaries are missing for patterns: "
            + ", ".join(missing_patterns),
            file=sys.stderr,
        )
        return 1

    results_root = os.path.abspath(args.results_dir)
    tmp_dir = single_tmp_dir(results_root)
    report_dir = single_report_dir(results_root)

    pattern_transports: Dict[str, List[str]] = {}
    pattern_sizes: Dict[str, List[int]] = {}
    requested_combo_count = 0
    for pattern in patterns:
        transports = select_transports(pattern)
        sizes = msg_sizes_for_pattern(pattern)
        pattern_transports[pattern] = transports
        pattern_sizes[pattern] = sizes
        requested_combo_count += len(transports) * len(sizes)

    all_failures: List[Tuple[str, str, int, str]] = []
    combo_results: Dict[Tuple[str, str, int], ComboRecord] = {}
    result_metrics: Dict[MetricKey, float] = {}
    compare_metrics: Dict[MetricKey, float] = {}
    run_warnings: List[str] = []
    table_lines: List[str] = []

    threshold_rules, threshold_load_warnings = load_threshold_rules()
    run_warnings.extend(threshold_load_warnings)

    for pattern in patterns:
        binary_name = PATTERN_TO_BINARY[pattern]
        transports = pattern_transports.get(pattern, [])
        sizes = pattern_sizes.get(pattern, [])

        print(f"\n## RUN: {pattern} ({pattern_direction_label(pattern)})")
        if not transports:
            print(f"  Skipping {pattern}: no matching transports.")
            continue
        if not sizes:
            print(f"  Skipping {pattern}: no message sizes configured.")
            continue

        print(f"  > Benchmarking current for {pattern}...")

        for transport in transports:
            for size in sizes:
                print(f"    Testing {transport} | {size}B: ", end="", flush=True)

                t_vals: List[float] = []
                b_vals: List[float] = []
                l_vals: List[float] = []
                cpu_vals: List[float] = []
                mem_vals: List[float] = []
                combo_status = "success"
                combo_reason = ""

                for run_idx in range(args.runs):
                    print(f"{run_idx + 1} ", end="", flush=True)
                    outcome = run_single_test(
                        build_dir,
                        current_lib_dir,
                        binary_name,
                        "current",
                        pattern,
                        transport,
                        size,
                        timeout_sec,
                        args.pin_cpu,
                    )
                    if outcome.warnings:
                        for warning in outcome.warnings:
                            run_warnings.append(
                                f"{pattern} {transport} {size}B run#{run_idx + 1}: {warning}"
                            )

                    if outcome.status == "success":
                        t_vals.append(outcome.throughput)
                        b_vals.append(outcome.bandwidth)
                        l_vals.append(outcome.latency)
                        if outcome.cpu_pct is not None:
                            cpu_vals.append(outcome.cpu_pct)
                        if outcome.mem_mb is not None:
                            mem_vals.append(outcome.mem_mb)
                        continue

                    combo_status = outcome.status
                    combo_reason = outcome.reason or outcome.status
                    if combo_status == "fail":
                        all_failures.append((pattern, transport, size, combo_reason))
                    break

                if combo_status == "unsupported":
                    combo_results[(pattern, transport, size)] = ComboRecord(status="unsupported")
                    print("unsupported Done", flush=True)
                    continue
                if combo_status == "skip":
                    combo_results[(pattern, transport, size)] = ComboRecord(status="skip")
                    print("skip Done", flush=True)
                    continue
                if combo_status == "fail" or not t_vals or not b_vals or not l_vals:
                    combo_results[(pattern, transport, size)] = ComboRecord(status="fail")
                    if combo_status != "fail":
                        all_failures.append((pattern, transport, size, combo_reason or "no_data"))
                    print("fail Done", flush=True)
                    if FAIL_FAST:
                        break
                    continue

                throughput = statistics.median(t_vals) if len(t_vals) > 1 else t_vals[0]
                bandwidth = statistics.median(b_vals) if len(b_vals) > 1 else b_vals[0]
                latency = statistics.median(l_vals) if len(l_vals) > 1 else l_vals[0]
                cpu_pct = statistics.median(cpu_vals) if cpu_vals else None
                mem_mb = statistics.median(mem_vals) if mem_vals else None

                combo_results[(pattern, transport, size)] = ComboRecord(
                    status="success",
                    throughput=throughput,
                    bandwidth=bandwidth,
                    latency=latency,
                    cpu_pct=cpu_pct,
                    mem_mb=mem_mb,
                )
                tp_key = ("current", pattern, transport, size, "throughput")
                bw_key = ("current", pattern, transport, size, "bandwidth")
                lat_key = ("current", pattern, transport, size, "latency")
                result_metrics[tp_key] = throughput
                result_metrics[bw_key] = bandwidth
                result_metrics[lat_key] = latency
                compare_metrics[tp_key] = throughput
                compare_metrics[lat_key] = latency
                if cpu_pct is not None:
                    result_metrics[("current", pattern, transport, size, "cpu_pct")] = cpu_pct
                if mem_mb is not None:
                    result_metrics[("current", pattern, transport, size, "mem_mb")] = mem_mb
                print("Done", flush=True)

            if FAIL_FAST and all_failures:
                break

        pattern_table_lines = build_pattern_table_lines(
            pattern, transports, sizes, combo_results
        )
        if table_lines:
            table_lines.extend(["", PATTERN_SEPARATOR, ""])
            print("")
            print(PATTERN_SEPARATOR)
            print("")
        else:
            print("")
        table_lines.extend(pattern_table_lines)
        for line in pattern_table_lines:
            print(line)

        if FAIL_FAST and all_failures:
            break

    if all_failures:
        print("\n## Failures")
        for pattern, transport, size, reason in all_failures:
            print(f"- {pattern} current {transport} {size}B: {reason}")

    unsupported_combo_count = sum(
        1 for record in combo_results.values() if record.status == "unsupported"
    )
    skip_combo_count = sum(1 for record in combo_results.values() if record.status == "skip")
    expected_result_lines = max(
        0, (requested_combo_count - unsupported_combo_count - skip_combo_count) * 3
    )
    actual_result_lines = sum(
        3 for record in combo_results.values() if record.status == "success"
    )
    completion_status = (
        "complete" if expected_result_lines == actual_result_lines else "partial"
    )

    print("\n## Completion")
    print(f"- status: {completion_status}")
    print(f"- expected_result_lines: {expected_result_lines}")
    print(f"- actual_result_lines: {actual_result_lines}")

    baseline: Dict[MetricKey, float] = {}
    baseline_info = ""
    exec_errors: List[str] = []

    if args.mode == "trend":
        baseline, sample_counts = load_rolling_baseline(
            tmp_dir, args.rolling_n, compare_metrics.keys()
        )
        if baseline:
            baseline_info = (
                f"rolling median from recent up to {args.rolling_n} runs "
                f"(matched keys: {len(baseline)})"
            )
        else:
            observed = sum(1 for count in sample_counts.values() if count > 0)
            if observed > 0:
                baseline_info = (
                    "rolling baseline unavailable (insufficient history); "
                    "comparison skipped"
                )
            else:
                baseline_info = "rolling baseline unavailable; comparison skipped"

    elif args.mode == "gate":
        baseline_file = args.baseline_file
        if not baseline_file:
            baseline_file = os.path.join(single_baseline_dir(results_root), "latest.txt")
        if os.path.isfile(baseline_file):
            baseline = load_fixed_baseline(baseline_file)
            if baseline:
                baseline_info = f"fixed baseline: {baseline_file}"
            else:
                baseline_info = f"baseline parse failed: {baseline_file}"
                exec_errors.append(f"baseline parse failed: {baseline_file}")
        else:
            baseline_info = f"baseline missing: {baseline_file}"
            exec_errors.append(f"baseline missing: {baseline_file}")

    warnings: List[str] = list(run_warnings)
    gate_fails: List[str] = []
    skipped_compares = 0
    if args.mode in ("trend", "gate") and baseline:
        warnings, gate_fails, skipped_compares = compare_against_baseline(
            args.mode, compare_metrics, baseline, threshold_rules
        )
        warnings = list(run_warnings) + warnings

    if baseline_info:
        print(f"\n## Baseline\n- {baseline_info}")
        if skipped_compares > 0:
            print(f"- skipped comparisons (missing/zero baseline): {skipped_compares}")

    if warnings:
        print("\n## Warnings")
        for item in warnings:
            print(f"- {item}")

    if gate_fails:
        print("\n## Gate Failures")
        for item in gate_fails:
            print(f"- {item}")

    meta = gather_meta(args.mode, args.runs, build_dir)
    meta["status"] = completion_status
    meta["expected"] = str(expected_result_lines)
    meta["actual"] = str(actual_result_lines)
    table_text = ""
    if table_lines:
        table_text = "\n".join(table_lines).rstrip() + "\n"

    save_report_requested = bool(args.save_report or args.save_report_legacy)
    save_baseline_requested = args.save is not None
    save_baseline_version = (args.save or "").strip() if save_baseline_requested else ""

    tmp_result_file = args.result_file
    if not tmp_result_file:
        tmp_result_file = os.path.join(tmp_dir, build_result_filename(args.results_tag))
    write_result_file(tmp_result_file, meta, result_metrics, table_text=table_text)
    enforce_file_retention(os.path.dirname(tmp_result_file))
    print(f"\nSaved tmp result file: {tmp_result_file}")

    save_report_path = args.save_file
    if save_report_requested and not save_report_path:
        save_report_path = os.path.join(report_dir, build_result_filename(args.results_tag))
    if save_report_requested and save_report_path:
        if completion_status == "complete":
            write_result_file(
                save_report_path, meta, result_metrics, table_text=table_text, table_only=True
            )
            enforce_file_retention(report_dir)
            print(f"Saved report file: {save_report_path}")
        else:
            print("Skipped report save: run status is partial")

    if save_baseline_requested:
        if completion_status != "complete":
            print(
                "Error: --save requires complete run "
                f"(expected={expected_result_lines}, actual={actual_result_lines})",
                file=sys.stderr,
            )
            exec_errors.append("partial run cannot be saved as baseline")
        else:
            if not save_baseline_version:
                save_baseline_version = build_result_filename(args.results_tag)
                if save_baseline_version.endswith(".txt"):
                    save_baseline_version = save_baseline_version[:-4]
            baseline_path = save_baseline(
                results_root, save_baseline_version, meta, result_metrics, table_text
            )
            print(f"Saved baseline: {baseline_path}")
    exit_code = 0
    if exec_errors:
        exit_code = max(exit_code, 1)
    if completion_status == "complete" and gate_fails:
        exit_code = max(exit_code, 2)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
