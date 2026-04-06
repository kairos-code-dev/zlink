#!/usr/bin/env python3
"""Single-pattern benchmark runner with policy-compliant reporting."""

from __future__ import annotations

import argparse
import datetime
import os
import platform
import re
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple

IS_WINDOWS = os.name == "nt"
IS_LINUX = (os.name != "nt") and platform.system().lower().startswith("linux")
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
    "SPOT",
]

PATTERN_TO_BINARY = {
    "PAIR": "perf_pair",
    "PUBSUB": "perf_pubsub",
    "DEALER_DEALER": "perf_dealer_dealer",
    "DEALER_ROUTER": "perf_dealer_router",
    "ROUTER_ROUTER": "perf_router_router",
    "SPOT": "perf_spot",
}

SUPPORTED_RECV_MODES = {
    "PAIR": ("callback",),
    "PUBSUB": ("callback",),
    "DEALER_DEALER": ("callback",),
    "DEALER_ROUTER": ("callback",),
    "ROUTER_ROUTER": ("callback",),
    "SPOT": ("callback",),
}

DEFAULT_MSG_SIZES_STANDARD = [64, 256, 1024, 65536, 131072, 262144]
DEFAULT_MSG_SIZES_STREAM = [64, 256, 1024, 65536]
DEFAULT_SOCKET_TRANSPORTS = ["tcp", "tls", "ws", "wss", "inproc"]
if not IS_WINDOWS:
    DEFAULT_SOCKET_TRANSPORTS.append("ipc")
DEFAULT_STREAM_TRANSPORTS = ["tcp", "tls", "ws", "wss"]
STREAM_TRANSPORT_PATTERNS = {
    "SPOT",
}
STREAM_SIZE_PATTERNS = set()

DEFAULT_RESULTS_DIR = os.path.join(PERF_DIR, "results")
DEFAULT_MAX_RESULT_FILES = 100
LATENCY_P95_METRIC = "latency_p95"
LATENCY_P99_METRIC = "latency_p99"
SND_PENDING_MAX_METRIC = "snd_pending_max"
RCV_PENDING_MAX_METRIC = "rcv_pending_max"
RCV_PENDING_END_METRIC = "rcv_pending_end"
REQUIRED_RESULT_METRICS = (
    "throughput",
    "bandwidth",
    "latency",
    LATENCY_P95_METRIC,
    LATENCY_P99_METRIC,
)
REQUIRED_RESULT_METRIC_COUNT = len(REQUIRED_RESULT_METRICS)
PATTERN_SEPARATOR = "==============================================================================="

@dataclass
class RunOutcome:
    status: str  # success | unsupported | skip | fail
    throughput: float = 0.0
    bandwidth: float = 0.0
    latency: float = 0.0
    latency_p95: float = 0.0
    latency_p99: float = 0.0
    cpu_pct: Optional[float] = None
    mem_mb: Optional[float] = None
    snd_pending_max: Optional[float] = None
    rcv_pending_max: Optional[float] = None
    rcv_pending_end: Optional[float] = None
    reason: str = ""
    warnings: Optional[List[str]] = None
    stderr: str = ""


@dataclass
class ComboRecord:
    status: str  # success | unsupported | skip | fail
    throughput: float = 0.0
    bandwidth: float = 0.0
    latency: float = 0.0
    latency_p95: float = 0.0
    latency_p99: float = 0.0
    cpu_pct: Optional[float] = None
    mem_mb: Optional[float] = None
    snd_pending_max: Optional[float] = None
    rcv_pending_max: Optional[float] = None
    rcv_pending_end: Optional[float] = None


class TeeStream:
    def __init__(self, *streams):
        self._streams = streams

    def write(self, data: str) -> int:
        for stream in self._streams:
            stream.write(data)
            stream.flush()
        return len(data)

    def flush(self) -> None:
        for stream in self._streams:
            stream.flush()


def emit_result_lines(combo_results: Dict[Tuple[str, str, int], ComboRecord]) -> None:
    for key in sorted(combo_results.keys()):
        pattern, transport, size = key
        record = combo_results[key]
        if record.status != "success":
            continue

        metrics = [
            ("throughput", record.throughput),
            ("bandwidth", record.bandwidth),
            ("latency", record.latency),
            (LATENCY_P95_METRIC, record.latency_p95),
            (LATENCY_P99_METRIC, record.latency_p99),
        ]

        if record.cpu_pct is not None:
            metrics.append(("cpu_pct", record.cpu_pct))
        if record.mem_mb is not None:
            metrics.append(("mem_mb", record.mem_mb))
        if record.snd_pending_max is not None:
            metrics.append((SND_PENDING_MAX_METRIC, record.snd_pending_max))
        if record.rcv_pending_max is not None:
            metrics.append((RCV_PENDING_MAX_METRIC, record.rcv_pending_max))
        if record.rcv_pending_end is not None:
            metrics.append((RCV_PENDING_END_METRIC, record.rcv_pending_end))

        for metric_name, value in metrics:
            print(
                f"RESULT,current,{pattern},{transport},{size},"
                f"{metric_name},{value:.3f}"
            )


def env_get(name: str) -> str:
    val = os.environ.get(name)
    if val:
        return val
    return ""


def env_flag_enabled(name: str) -> bool:
    return env_get(name) == "1"


def parse_env_list(name: str, cast_fn):
    val = env_get(name)
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


def parse_env_int(name: str, default: int) -> int:
    val = env_get(name)
    if not val:
        return default
    try:
        return int(val)
    except ValueError:
        return default


def resource_metrics_enabled() -> bool:
    return os.environ.get("PERF_DISABLE_RESOURCE_METRICS", "0") != "1"


def resolve_latency_triplet(
    latency: Optional[float],
    latency_p95: Optional[float],
    latency_p99: Optional[float],
) -> Tuple[Optional[float], Optional[float], Optional[float]]:
    mean = latency
    p95 = latency_p95 if latency_p95 is not None else mean
    if p95 is None:
        p95 = mean
    p99 = latency_p99 if latency_p99 is not None else p95
    if p99 is None:
        p99 = p95 if p95 is not None else mean
    return mean, p95, p99


def linux_proc_metrics_supported() -> bool:
    return IS_LINUX and hasattr(os, "sysconf") and os.path.exists("/proc")


def read_proc_cpu_ticks(pid: int) -> Optional[int]:
    stat_path = f"/proc/{pid}/stat"
    try:
        with open(stat_path, "r", encoding="utf-8", errors="ignore") as fh:
            data = fh.read().strip()
    except OSError:
        return None
    if not data:
        return None
    parts = data.split()
    if len(parts) < 15:
        return None
    try:
        utime = int(parts[13])
        stime = int(parts[14])
    except ValueError:
        return None
    return utime + stime


def read_proc_mem_mb(pid: int) -> Optional[float]:
    status_path = f"/proc/{pid}/status"
    try:
        with open(status_path, "r", encoding="utf-8", errors="ignore") as fh:
            for line in fh:
                if not line.startswith("VmRSS:"):
                    continue
                parts = line.split()
                if len(parts) < 2:
                    break
                return float(parts[1]) / 1024.0
    except OSError:
        return None
    return None


if IS_WINDOWS:
    import ctypes
    from ctypes import wintypes

    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    PROCESS_VM_READ = 0x0010

    class FILETIME(ctypes.Structure):
        _fields_ = [
            ("dwLowDateTime", wintypes.DWORD),
            ("dwHighDateTime", wintypes.DWORD),
        ]

    class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("PageFaultCount", wintypes.DWORD),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]

    _kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _psapi = ctypes.WinDLL("psapi", use_last_error=True)
    _kernel32.OpenProcess.argtypes = [
        wintypes.DWORD,
        wintypes.BOOL,
        wintypes.DWORD,
    ]
    _kernel32.OpenProcess.restype = wintypes.HANDLE
    _kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    _kernel32.CloseHandle.restype = wintypes.BOOL
    _kernel32.GetProcessTimes.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(FILETIME),
        ctypes.POINTER(FILETIME),
        ctypes.POINTER(FILETIME),
        ctypes.POINTER(FILETIME),
    ]
    _kernel32.GetProcessTimes.restype = wintypes.BOOL
    _psapi.GetProcessMemoryInfo.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(PROCESS_MEMORY_COUNTERS),
        wintypes.DWORD,
    ]
    _psapi.GetProcessMemoryInfo.restype = wintypes.BOOL


def windows_proc_metrics_supported() -> bool:
    return IS_WINDOWS


def open_windows_process_for_metrics(pid: int):
    if not windows_proc_metrics_supported():
        return None
    access = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ
    handle = _kernel32.OpenProcess(access, False, int(pid))
    if not handle:
        return None
    return handle


def close_windows_process_handle(handle) -> None:
    if handle:
        _kernel32.CloseHandle(handle)


def _filetime_to_uint64(filetime_obj) -> int:
    return (int(filetime_obj.dwHighDateTime) << 32) | int(
        filetime_obj.dwLowDateTime
    )


def read_windows_cpu_ticks_100ns(handle) -> Optional[int]:
    if not handle:
        return None
    creation = FILETIME()
    exit_ft = FILETIME()
    kernel = FILETIME()
    user = FILETIME()
    ok = _kernel32.GetProcessTimes(
        handle,
        ctypes.byref(creation),
        ctypes.byref(exit_ft),
        ctypes.byref(kernel),
        ctypes.byref(user),
    )
    if not ok:
        return None
    return _filetime_to_uint64(kernel) + _filetime_to_uint64(user)


def read_windows_mem_mb(handle) -> Optional[float]:
    if not handle:
        return None
    counters = PROCESS_MEMORY_COUNTERS()
    counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
    ok = _psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb)
    if not ok:
        return None
    return float(counters.WorkingSetSize) / (1024.0 * 1024.0)


def run_command_with_metrics(
    cmd: List[str], env: Dict[str, str], timeout_sec: int
) -> Dict[str, object]:
    started = time.monotonic()
    proc = subprocess.Popen(
        cmd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    sample_mode = ""
    if resource_metrics_enabled():
        if linux_proc_metrics_supported():
            sample_mode = "linux"
        elif windows_proc_metrics_supported():
            sample_mode = "windows"

    proc_handle = None
    start_ticks = None
    end_ticks = None
    end_mem_mb = None
    if sample_mode == "linux":
        start_ticks = read_proc_cpu_ticks(proc.pid)
        end_ticks = start_ticks
    elif sample_mode == "windows":
        proc_handle = open_windows_process_for_metrics(proc.pid)
        start_ticks = read_windows_cpu_ticks_100ns(proc_handle)
        end_ticks = start_ticks

    timed_out = False
    try:
        while True:
            rc = proc.poll()
            if sample_mode == "linux":
                ticks = read_proc_cpu_ticks(proc.pid)
                if ticks is not None:
                    end_ticks = ticks
                mem = read_proc_mem_mb(proc.pid)
                if mem is not None:
                    end_mem_mb = mem
            elif sample_mode == "windows":
                ticks = read_windows_cpu_ticks_100ns(proc_handle)
                if ticks is not None:
                    end_ticks = ticks
                mem = read_windows_mem_mb(proc_handle)
                if mem is not None:
                    end_mem_mb = mem

            if rc is not None:
                break
            if (time.monotonic() - started) > timeout_sec:
                timed_out = True
                proc.kill()
                break
            time.sleep(0.02)

        try:
            stdout, stderr = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            stdout, stderr = proc.communicate()
            timed_out = True

        if sample_mode == "linux":
            ticks = read_proc_cpu_ticks(proc.pid)
            if ticks is not None:
                end_ticks = ticks
            mem = read_proc_mem_mb(proc.pid)
            if mem is not None:
                end_mem_mb = mem
        elif sample_mode == "windows":
            ticks = read_windows_cpu_ticks_100ns(proc_handle)
            if ticks is not None:
                end_ticks = ticks
            mem = read_windows_mem_mb(proc_handle)
            if mem is not None:
                end_mem_mb = mem
    finally:
        close_windows_process_handle(proc_handle)

    elapsed = max(1e-6, time.monotonic() - started)
    cpu_pct = None
    if start_ticks is not None and end_ticks is not None:
        nproc = max(1.0, float(os.cpu_count() or 1))
        if sample_mode == "linux":
            try:
                hz = float(os.sysconf("SC_CLK_TCK"))
            except (OSError, ValueError):
                hz = 100.0
            cpu_delta = max(0.0, float(end_ticks - start_ticks)) / max(1.0, hz)
            cpu_pct = (cpu_delta / (elapsed * nproc)) * 100.0
        elif sample_mode == "windows":
            cpu_delta = max(0.0, float(end_ticks - start_ticks)) / 10000000.0
            cpu_pct = (cpu_delta / (elapsed * nproc)) * 100.0

    return {
        "returncode": proc.returncode,
        "stdout": stdout or "",
        "stderr": stderr or "",
        "timed_out": timed_out,
        "cpu_pct": cpu_pct,
        "mem_mb": end_mem_mb,
    }


FAIL_FAST = env_flag_enabled("PERF_FAIL_FAST")


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


def resolve_recv_mode(value: str) -> str:
    recv_mode = (value or "callback").strip().lower()
    if recv_mode not in ("recv", "callback"):
        raise ValueError(f"invalid recv mode: {value}")
    return recv_mode


def build_result_filename(recv_mode: str, tag: str = "") -> str:
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    name = f"perf_{platform_tag()}_{resolve_recv_mode(recv_mode)}_{stamp}"
    clean_tag = sanitize_suffix(tag)
    if clean_tag:
        name = f"{name}_{clean_tag}"
    return f"{name}.txt"


def single_result_dir(results_root: str) -> str:
    return os.path.join(results_root, "single", "report")


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
    build_root = os.path.join(ROOT_DIR, "core", "build")
    build_dir = os.path.join(build_root, "bin")
    if IS_WINDOWS:
        release_dir = os.path.join(build_dir, "Release")
        if os.path.isdir(release_dir):
            build_dir = release_dir
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
    env_transports = parse_env_list("PERF_TRANSPORTS", str)
    if not env_transports:
        return list(base)
    return [t for t in base if t in env_transports]


def default_msg_sizes_for_pattern(pattern: str) -> List[int]:
    if pattern in STREAM_SIZE_PATTERNS:
        return list(DEFAULT_MSG_SIZES_STREAM)
    return list(DEFAULT_MSG_SIZES_STANDARD)


def msg_sizes_for_pattern(pattern: str) -> List[int]:
    env_sizes = parse_env_list("PERF_MSG_SIZES", int)
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
    if metric not in (
        "throughput",
        "bandwidth",
        "latency",
        LATENCY_P95_METRIC,
        LATENCY_P99_METRIC,
        "cpu_pct",
        "mem_mb",
        SND_PENDING_MAX_METRIC,
        RCV_PENDING_MAX_METRIC,
        RCV_PENDING_END_METRIC,
    ):
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


def build_bench_cmd(binary_path: str, args: List[str], pin_cpu: bool) -> List[str]:
    if IS_WINDOWS:
        return [binary_path] + list(args)
    if (
        pin_cpu
        or env_flag_enabled("PERF_TASKSET")
        or os.environ.get("PERF_TASKSET") == "1"
    ):
        return ["taskset", "-c", "1", binary_path] + list(args)
    return [binary_path] + list(args)


def single_table_header_line() -> str:
    return (
        "| Size     |       Throughput |    Bandwidth |  Lat.Mean(ms) |"
        "   Lat.P95(ms) |   Lat.P99(ms) | CPU% | Mem MB |"
        " Q.Snd.Max | Q.Rcv.Max | Q.Rcv.End |"
    )


def single_table_separator_line() -> str:
    return (
        "|----------|------------------|--------------|--------------|--------------|"
        "--------------|------|--------|-----------|-----------|-----------|"
    )


def format_queue_pending(value: Optional[float]) -> str:
    if value is None:
        return "N/A"
    return f"{value:,.0f}"


def single_table_row_line(size: int, status: str, record: Optional[ComboRecord] = None) -> str:
    if status == "success" and record is not None:
        _, lat_p95, lat_p99 = resolve_latency_triplet(
            record.latency,
            record.latency_p95,
            record.latency_p99,
        )
        tp_s = format_throughput("", record.throughput)
        bw_s = format_bandwidth(record.bandwidth)
        lat_s = format_latency_ms(record.latency)
        lat95_s = format_latency_ms(lat_p95 if lat_p95 is not None else 0.0)
        lat99_s = format_latency_ms(lat_p99 if lat_p99 is not None else 0.0)
        cpu_s = f"{record.cpu_pct:4.1f}" if record.cpu_pct is not None else "N/A"
        mem_s = f"{record.mem_mb:6.1f}" if record.mem_mb is not None else "N/A"
        q_snd_s = format_queue_pending(record.snd_pending_max)
        q_rcv_max_s = format_queue_pending(record.rcv_pending_max)
        q_rcv_end_s = format_queue_pending(record.rcv_pending_end)
    elif status == "unsupported":
        tp_s = "UNSUPPORTED"
        bw_s = "UNSUPPORTED"
        lat_s = "UNSUPPORTED"
        lat95_s = "UNSUPPORTED"
        lat99_s = "UNSUPPORTED"
        cpu_s = "N/A"
        mem_s = "N/A"
        q_snd_s = "N/A"
        q_rcv_max_s = "N/A"
        q_rcv_end_s = "N/A"
    else:
        tp_s = "FAIL"
        bw_s = "FAIL"
        lat_s = "FAIL"
        lat95_s = "FAIL"
        lat99_s = "FAIL"
        if record is not None:
            cpu_s = f"{record.cpu_pct:4.1f}" if record.cpu_pct is not None else "N/A"
            mem_s = f"{record.mem_mb:6.1f}" if record.mem_mb is not None else "N/A"
            q_snd_s = format_queue_pending(record.snd_pending_max)
            q_rcv_max_s = format_queue_pending(record.rcv_pending_max)
            q_rcv_end_s = format_queue_pending(record.rcv_pending_end)
        else:
            cpu_s = "N/A"
            mem_s = "N/A"
            q_snd_s = "N/A"
            q_rcv_max_s = "N/A"
            q_rcv_end_s = "N/A"
    return (
        f"| {f'{size}B':<8} | {tp_s:>16} | {bw_s:>12} | {lat_s:>12} | "
        f"{lat95_s:>12} | {lat99_s:>12} | "
        f"{cpu_s:>4} | {mem_s:>6} | {q_snd_s:>9} | {q_rcv_max_s:>9} | "
        f"{q_rcv_end_s:>9} |"
    )


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
    # Single policy invariant:
    # one benchmark process executes exactly one pattern/transport/size/run
    # case. Repetition, aggregation, and table formatting stay in the runner.
    binary_path = os.path.join(build_dir, binary_name + EXE_SUFFIX)
    env = get_env_for_lib(current_lib_dir)
    cmd = build_bench_cmd(binary_path, [lib_name, transport, str(size)], pin_cpu)
    try:
        sampled = run_command_with_metrics(cmd, env, timeout_sec)
    except Exception as exc:  # pragma: no cover - defensive
        return RunOutcome(status="fail", reason=f"exception:{exc}")

    stdout = str(sampled.get("stdout") or "")
    stderr = str(sampled.get("stderr") or "")

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

    cpu_pct = metrics.get("cpu_pct")
    if cpu_pct is None:
        sampled_cpu = sampled.get("cpu_pct")
        if isinstance(sampled_cpu, (int, float)):
            cpu_pct = float(sampled_cpu)

    mem_mb = metrics.get("mem_mb")
    if mem_mb is None:
        sampled_mem = sampled.get("mem_mb")
        if isinstance(sampled_mem, (int, float)):
            mem_mb = float(sampled_mem)

    snd_pending_max = metrics.get(SND_PENDING_MAX_METRIC)
    rcv_pending_max = metrics.get(RCV_PENDING_MAX_METRIC)
    rcv_pending_end = metrics.get(RCV_PENDING_END_METRIC)

    if sampled.get("timed_out"):
        return RunOutcome(
            status="fail",
            reason="timeout",
            cpu_pct=cpu_pct,
            mem_mb=mem_mb,
            snd_pending_max=snd_pending_max,
            rcv_pending_max=rcv_pending_max,
            rcv_pending_end=rcv_pending_end,
            warnings=warnings or None,
            stderr=stderr,
        )

    special = detect_special_status(stdout, lib_name, pattern, transport)
    return_code = int(sampled.get("returncode") or 0)
    if return_code != 0:
        return RunOutcome(
            status="fail",
            reason=f"non_zero_exit_{return_code}",
            cpu_pct=cpu_pct,
            mem_mb=mem_mb,
            snd_pending_max=snd_pending_max,
            rcv_pending_max=rcv_pending_max,
            rcv_pending_end=rcv_pending_end,
            warnings=warnings or None,
            stderr=stderr,
        )

    if "throughput" in metrics and "bandwidth" in metrics and "latency" in metrics:
        latency_mean, latency_p95, latency_p99 = resolve_latency_triplet(
            metrics.get("latency"),
            metrics.get(LATENCY_P95_METRIC),
            metrics.get(LATENCY_P99_METRIC),
        )
        return RunOutcome(
            status="success",
            throughput=metrics["throughput"],
            bandwidth=metrics["bandwidth"],
            latency=latency_mean if latency_mean is not None else metrics["latency"],
            latency_p95=latency_p95 if latency_p95 is not None else metrics["latency"],
            latency_p99=latency_p99 if latency_p99 is not None else metrics["latency"],
            cpu_pct=cpu_pct,
            mem_mb=mem_mb,
            snd_pending_max=snd_pending_max,
            rcv_pending_max=rcv_pending_max,
            rcv_pending_end=rcv_pending_end,
            warnings=warnings or None,
            stderr=stderr,
        )

    if special == "unsupported" and not metrics:
        return RunOutcome(
            status="unsupported",
            reason="unsupported",
            cpu_pct=cpu_pct,
            mem_mb=mem_mb,
            snd_pending_max=snd_pending_max,
            rcv_pending_max=rcv_pending_max,
            rcv_pending_end=rcv_pending_end,
            warnings=warnings or None,
            stderr=stderr,
        )
    if special == "skip" and not metrics:
        return RunOutcome(
            status="skip",
            reason="skip",
            cpu_pct=cpu_pct,
            mem_mb=mem_mb,
            snd_pending_max=snd_pending_max,
            rcv_pending_max=rcv_pending_max,
            rcv_pending_end=rcv_pending_end,
            warnings=warnings or None,
            stderr=stderr,
        )

    return RunOutcome(
        status="fail",
        reason="no_data",
        cpu_pct=cpu_pct,
        mem_mb=mem_mb,
        snd_pending_max=snd_pending_max,
        rcv_pending_max=rcv_pending_max,
        rcv_pending_end=rcv_pending_end,
        warnings=warnings or None,
        stderr=stderr,
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


def resolve_binary_name(pattern: str, recv_mode: str) -> str:
    supported = SUPPORTED_RECV_MODES.get(pattern, ())
    if recv_mode not in supported:
        raise ValueError(
            f"pattern {pattern} does not support --recv {recv_mode}"
        )
    return PATTERN_TO_BINARY[pattern]


def collect_unsupported_patterns(
    patterns: Iterable[str], recv_mode: str
) -> List[str]:
    unsupported = []
    for pattern in patterns:
        if recv_mode not in SUPPORTED_RECV_MODES.get(pattern, ()):
            unsupported.append(pattern)
    return unsupported


def collect_missing_patterns(
    build_dir: str, patterns: Iterable[str], recv_mode: str
) -> List[str]:
    missing = []
    for pattern in patterns:
        binary_name = resolve_binary_name(pattern, recv_mode)
        binary_path = os.path.join(build_dir, binary_name + EXE_SUFFIX)
        if not os.path.exists(binary_path):
            missing.append(pattern)
    return sorted(set(missing))


def collect_missing_build_targets(
    build_dir: str, patterns: Iterable[str], recv_mode: str
) -> List[str]:
    targets = []
    for pattern in patterns:
        binary_name = resolve_binary_name(pattern, recv_mode)
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


def format_latency_ms(latency_us: float) -> str:
    return f"{latency_us / 1000.0:8.2f} ms"


def build_pattern_table_lines(
    pattern: str,
    transports: List[str],
    sizes: List[int],
    combo_results: Dict[Tuple[str, str, int], ComboRecord],
) -> List[str]:
    lines: List[str] = [f"## PATTERN: {pattern} ({pattern_direction_label(pattern)})", ""]
    for transport in transports:
        lines.append(f"### Transport: {transport}")
        lines.append(
            "| Size   |       Throughput |    Bandwidth | Lat.Mean(ms) |"
            "  Lat.P95(ms) |  Lat.P99(ms) | CPU% |  Mem MB |"
            " Q.Snd.Max | Q.Rcv.Max | Q.Rcv.End |"
        )
        lines.append(
            "|--------|------------------|--------------|-------------|-------------|-------------|"
            "------|---------|-----------|-----------|-----------|"
        )
        for size in sizes:
            record = combo_results.get((pattern, transport, size))
            if not record or record.status != "success":
                lines.append(
                    f"| {size}B  |              N/A |          N/A |         N/A |         N/A |"
                    f"         N/A |  N/A |     N/A |       N/A |       N/A |       N/A |"
                )
                continue
            tp_s = format_throughput(pattern, record.throughput)
            bw_s = format_bandwidth(record.bandwidth)
            lat_s = format_latency_ms(record.latency)
            _, lat95_value, lat99_value = resolve_latency_triplet(
                record.latency,
                record.latency_p95,
                record.latency_p99,
            )
            lat95_s = format_latency_ms(lat95_value if lat95_value is not None else 0.0)
            lat99_s = format_latency_ms(lat99_value if lat99_value is not None else 0.0)
            cpu_s = "N/A"
            mem_s = "N/A"
            if record.cpu_pct is not None:
                cpu_s = f"{record.cpu_pct:.1f}"
            if record.mem_mb is not None:
                mem_s = f"{record.mem_mb:.1f}"
            q_snd_s = format_queue_pending(record.snd_pending_max)
            q_rcv_max_s = format_queue_pending(record.rcv_pending_max)
            q_rcv_end_s = format_queue_pending(record.rcv_pending_end)
            lines.append(
                f"| {size}B  | {tp_s:>16} | {bw_s:>12} | {lat_s:>11} | {lat95_s:>11} | "
                f"{lat99_s:>11} | {cpu_s:>4} | {mem_s:>7} | {q_snd_s:>9} |"
                f" {q_rcv_max_s:>9} | {q_rcv_end_s:>9} |"
            )
        lines.append("")
    while lines and lines[-1] == "":
        lines.pop()
    return lines


def build_single_option_items(
    args: argparse.Namespace,
    timeout_sec: int,
    patterns: List[str],
    pattern_transports: Dict[str, List[str]],
    pattern_sizes: Dict[str, List[int]],
) -> List[Tuple[str, str]]:
    transports: List[str] = []
    sizes: List[int] = []
    for pattern in patterns:
        transports.extend(pattern_transports.get(pattern, []))
        sizes.extend(pattern_sizes.get(pattern, []))

    unique_transports = sorted(set(transports))
    unique_sizes = sorted(set(sizes))
    base_hwm = parse_env_int("PERF_SINGLE_HWM", 1000)
    sndhwm = parse_env_int("PERF_SINGLE_SNDHWM", base_hwm)
    rcvhwm = parse_env_int("PERF_SINGLE_RCVHWM", base_hwm)
    sndtimeo_ms = parse_env_int("PERF_SINGLE_SNDTIMEO_MS", 200)
    rcvtimeo_ms = parse_env_int("PERF_SINGLE_RCVTIMEO_MS", 200)
    io_threads = max(1, parse_env_int("PERF_IO_THREADS", 1))
    items: List[Tuple[str, str]] = [
        ("runs", str(args.runs)),
        ("recv_mode", args.recv),
        ("duration_seconds", str(parse_env_int("PERF_SINGLE_DURATION_SECONDS", 5))),
        ("timeout_seconds", str(timeout_sec)),
        ("io_threads", str(io_threads)),
        ("hwm", str(base_hwm)),
        ("sndhwm", str(sndhwm)),
        ("rcvhwm", str(rcvhwm)),
        ("sndtimeo_ms", str(sndtimeo_ms)),
        ("rcvtimeo_ms", str(rcvtimeo_ms)),
        ("patterns", ",".join(patterns)),
        ("transports", ",".join(unique_transports) if unique_transports else "none"),
        ("msg_sizes", ",".join(str(sz) for sz in unique_sizes) if unique_sizes else "none"),
    ]
    return items


def print_effective_options(label: str, items: List[Tuple[str, str]]) -> None:
    print(f"\n## Effective Options ({label})")
    for key, value in items:
        print(f"- {key}: {value}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Measure current zlink single-pattern benchmarks from core/build."
    )
    parser.add_argument("pattern", nargs="?", default="ALL")
    parser.add_argument("--runs", type=int, default=None)
    parser.add_argument("--duration", type=int, default=None)
    parser.add_argument("--build-dir", default="")
    parser.add_argument("--pin-cpu", action="store_true")
    parser.add_argument("--results-dir", default=DEFAULT_RESULTS_DIR)
    parser.add_argument("--results-tag", default="")
    parser.add_argument("--result-file", default="")
    parser.add_argument("--recv", default=env_get("PERF_RECV_MODE") or "callback")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if not args.results_tag:
        args.results_tag = env_get("PERF_RESULTS_TAG").strip()

    if args.runs is None:
        args.runs = 1
    if args.runs < 1:
        print("Error: --runs must be >= 1.", file=sys.stderr)
        return 1
    if args.duration is not None and args.duration < 1:
        print("Error: --duration must be >= 1.", file=sys.stderr)
        return 1
    try:
        args.recv = resolve_recv_mode(args.recv)
    except ValueError:
        print("Error: --recv must be 'recv' or 'callback'.", file=sys.stderr)
        return 1
    os.environ["PERF_RECV_MODE"] = args.recv
    if args.duration is not None:
        os.environ["PERF_SINGLE_DURATION_SECONDS"] = str(args.duration)

    try:
        patterns = parse_pattern_arg(args.pattern)
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    unsupported_patterns = collect_unsupported_patterns(patterns, args.recv)
    if unsupported_patterns:
        print(
            "Error: unsupported --recv mode for single patterns: "
            + ", ".join(sorted(set(unsupported_patterns))),
            file=sys.stderr,
        )
        return 1

    if IS_WINDOWS:
        build_dir = normalize_build_dir(
            args.build_dir
            or os.path.join(ROOT_DIR, "core", "build")
        )
    else:
        auto_build_dir, _ = resolve_linux_paths()
        build_dir = normalize_build_dir(args.build_dir or auto_build_dir)

    current_lib_dir = derive_current_lib_dir(build_dir)
    default_timeout_sec = max(
        30,
        parse_env_int("PERF_SINGLE_DURATION_SECONDS", 5) * 6 + 15,
    )
    timeout_sec = max(
        1,
        parse_env_int(
            "PERF_SINGLE_TIMEOUT_SECONDS", default_timeout_sec
        ),
    )

    no_autobuild = (
        env_flag_enabled("PERF_NO_AUTOBUILD")
        or env_flag_enabled("PERF_SKIP_AUTO_BUILD")
    )

    missing_patterns = collect_missing_patterns(build_dir, patterns, args.recv)
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

        targets = collect_missing_build_targets(build_dir, patterns, args.recv)
        build_rc = run_cmake_build(cmake_build_dir, targets)
        if build_rc != 0:
            print(f"Error: auto-build failed with exit code {build_rc}", file=sys.stderr)
            return build_rc

    missing_patterns = collect_missing_patterns(build_dir, patterns, args.recv)
    if missing_patterns:
        print(
            "Error: current benchmark binaries are missing for patterns: "
            + ", ".join(missing_patterns),
            file=sys.stderr,
        )
        return 1

    results_root = os.path.abspath(args.results_dir)
    result_dir = single_result_dir(results_root)
    result_file = args.result_file
    if not result_file:
        result_file = os.path.join(
            result_dir, build_result_filename(args.recv, args.results_tag)
        )
    result_parent = os.path.dirname(result_file)
    if result_parent:
        os.makedirs(result_parent, exist_ok=True)
    result_log_fh = open(result_file, "w", encoding="utf-8", buffering=1)
    orig_stdout = sys.stdout
    orig_stderr = sys.stderr
    sys.stdout = TeeStream(orig_stdout, result_log_fh)
    sys.stderr = TeeStream(orig_stderr, result_log_fh)

    pattern_transports: Dict[str, List[str]] = {}
    pattern_sizes: Dict[str, List[int]] = {}
    requested_combo_count = 0
    for pattern in patterns:
        transports = select_transports(pattern)
        sizes = msg_sizes_for_pattern(pattern)
        pattern_transports[pattern] = transports
        pattern_sizes[pattern] = sizes
        requested_combo_count += len(transports) * len(sizes)

    effective_options = build_single_option_items(
        args,
        timeout_sec,
        patterns,
        pattern_transports,
        pattern_sizes,
    )
    print_effective_options("start", effective_options)

    all_failures: List[Tuple[str, str, int, str]] = []
    combo_results: Dict[Tuple[str, str, int], ComboRecord] = {}
    run_warnings: List[str] = []
    table_lines: List[str] = []

    for pattern in patterns:
        binary_name = resolve_binary_name(pattern, args.recv)
        transports = pattern_transports.get(pattern, [])
        sizes = pattern_sizes.get(pattern, [])

        if table_lines:
            table_lines.extend(["", PATTERN_SEPARATOR, ""])
            print("")
            print(PATTERN_SEPARATOR)
            print("")

        pattern_header = f"## PATTERN: {pattern} ({pattern_direction_label(pattern)})"
        print(pattern_header)
        table_lines.append(pattern_header)

        if not transports:
            line = f"  Skipping {pattern}: no matching transports."
            print(line)
            table_lines.append(line)
            continue
        if not sizes:
            line = f"  Skipping {pattern}: no message sizes configured."
            print(line)
            table_lines.append(line)
            continue

        bench_line = f"  > Benchmarking current for {pattern}..."
        print(bench_line)
        table_lines.append(bench_line)

        for transport in transports:
            testing_line = f"    Testing {transport}:"
            print(testing_line)
            table_lines.append(testing_line)

            show_run_labels = args.runs > 1
            if not show_run_labels:
                header = f"      {single_table_header_line()}"
                separator = f"      {single_table_separator_line()}"
                print(header)
                print(separator)
                table_lines.append(header)
                table_lines.append(separator)

            t_samples: Dict[int, List[float]] = {size: [] for size in sizes}
            b_samples: Dict[int, List[float]] = {size: [] for size in sizes}
            l_samples: Dict[int, List[float]] = {size: [] for size in sizes}
            l95_samples: Dict[int, List[float]] = {size: [] for size in sizes}
            l99_samples: Dict[int, List[float]] = {size: [] for size in sizes}
            cpu_samples: Dict[int, List[float]] = {size: [] for size in sizes}
            mem_samples: Dict[int, List[float]] = {size: [] for size in sizes}
            snd_pending_samples: Dict[int, List[float]] = {size: [] for size in sizes}
            rcv_pending_max_samples: Dict[int, List[float]] = {
                size: [] for size in sizes
            }
            rcv_pending_end_samples: Dict[int, List[float]] = {
                size: [] for size in sizes
            }
            failed_records: Dict[int, ComboRecord] = {}
            failed_sizes: Dict[int, str] = {}
            transport_unsupported = False
            transport_skip = False

            abort_pattern = False
            for run_idx in range(args.runs):
                if transport_unsupported or transport_skip:
                    break
                run_no = run_idx + 1
                if show_run_labels:
                    run_line = f"      run {run_no}/{args.runs}:"
                    print(run_line)
                    table_lines.append(run_line)
                    header = f"        {single_table_header_line()}"
                    separator = f"        {single_table_separator_line()}"
                    print(header)
                    print(separator)
                    table_lines.append(header)
                    table_lines.append(separator)
                    row_indent = "        "
                else:
                    row_indent = "      "

                for size_idx, size in enumerate(sizes):
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
                                f"{pattern} {transport} {size}B run#{run_no}: {warning}"
                            )

                    if outcome.status == "success":
                        t_samples[size].append(outcome.throughput)
                        b_samples[size].append(outcome.bandwidth)
                        l_samples[size].append(outcome.latency)
                        l95_samples[size].append(outcome.latency_p95)
                        l99_samples[size].append(outcome.latency_p99)
                        if outcome.cpu_pct is not None:
                            cpu_samples[size].append(outcome.cpu_pct)
                        if outcome.mem_mb is not None:
                            mem_samples[size].append(outcome.mem_mb)
                        if outcome.snd_pending_max is not None:
                            snd_pending_samples[size].append(outcome.snd_pending_max)
                        if outcome.rcv_pending_max is not None:
                            rcv_pending_max_samples[size].append(outcome.rcv_pending_max)
                        if outcome.rcv_pending_end is not None:
                            rcv_pending_end_samples[size].append(outcome.rcv_pending_end)
                        row = single_table_row_line(
                            size,
                            "success",
                            ComboRecord(
                                status="success",
                                throughput=outcome.throughput,
                                bandwidth=outcome.bandwidth,
                                latency=outcome.latency,
                                latency_p95=outcome.latency_p95,
                                latency_p99=outcome.latency_p99,
                                cpu_pct=outcome.cpu_pct,
                                mem_mb=outcome.mem_mb,
                                snd_pending_max=outcome.snd_pending_max,
                                rcv_pending_max=outcome.rcv_pending_max,
                                rcv_pending_end=outcome.rcv_pending_end,
                            ),
                        )
                        line = f"{row_indent}{row}"
                        print(line)
                        table_lines.append(line)
                        continue

                    if outcome.status == "unsupported":
                        transport_unsupported = True
                        for remain_size in sizes[size_idx:]:
                            row = single_table_row_line(remain_size, "unsupported", None)
                            line = f"{row_indent}{row}"
                            print(line)
                            table_lines.append(line)
                        break

                    if outcome.status == "skip":
                        transport_skip = True
                        reason = outcome.reason or "skip"
                        skip_record = ComboRecord(
                            status="fail",
                            cpu_pct=outcome.cpu_pct,
                            mem_mb=outcome.mem_mb,
                            snd_pending_max=outcome.snd_pending_max,
                            rcv_pending_max=outcome.rcv_pending_max,
                            rcv_pending_end=outcome.rcv_pending_end,
                        )
                        for remain_size in sizes[size_idx:]:
                            failed_sizes[remain_size] = reason
                            row_record = skip_record if remain_size == size else None
                            if row_record is not None:
                                failed_records[remain_size] = row_record
                            row = single_table_row_line(remain_size, "fail", row_record)
                            line = f"{row_indent}{row}"
                            print(line)
                            table_lines.append(line)
                        break

                    reason = outcome.reason or "fail"
                    failed_sizes[size] = reason
                    all_failures.append((pattern, transport, size, reason))
                    failed_record = ComboRecord(
                        status="fail",
                        cpu_pct=outcome.cpu_pct,
                        mem_mb=outcome.mem_mb,
                        snd_pending_max=outcome.snd_pending_max,
                        rcv_pending_max=outcome.rcv_pending_max,
                        rcv_pending_end=outcome.rcv_pending_end,
                    )
                    failed_records[size] = failed_record
                    row = single_table_row_line(size, "fail", failed_record)
                    line = f"{row_indent}{row}"
                    print(line)
                    table_lines.append(line)
                    if FAIL_FAST:
                        abort_pattern = True
                        break

                if abort_pattern:
                    break

            if transport_unsupported:
                for size in sizes:
                    combo_results[(pattern, transport, size)] = ComboRecord(status="unsupported")
            elif transport_skip:
                for size in sizes:
                    combo_results[(pattern, transport, size)] = ComboRecord(status="skip")
            else:
                for size in sizes:
                    if (
                        size in failed_sizes
                        or not t_samples[size]
                        or not b_samples[size]
                        or not l_samples[size]
                        or not l95_samples[size]
                        or not l99_samples[size]
                    ):
                        combo_results[(pattern, transport, size)] = failed_records.get(
                            size, ComboRecord(status="fail")
                        )
                        if size not in failed_sizes:
                            all_failures.append((pattern, transport, size, "no_data"))
                        continue

                    throughput = statistics.median(t_samples[size])
                    bandwidth = statistics.median(b_samples[size])
                    latency = statistics.median(l_samples[size])
                    latency_p95 = statistics.median(l95_samples[size])
                    latency_p99 = statistics.median(l99_samples[size])
                    cpu_pct = statistics.median(cpu_samples[size]) if cpu_samples[size] else None
                    mem_mb = statistics.median(mem_samples[size]) if mem_samples[size] else None
                    snd_pending_max = (
                        statistics.median(snd_pending_samples[size])
                        if snd_pending_samples[size]
                        else None
                    )
                    rcv_pending_max = (
                        statistics.median(rcv_pending_max_samples[size])
                        if rcv_pending_max_samples[size]
                        else None
                    )
                    rcv_pending_end = (
                        statistics.median(rcv_pending_end_samples[size])
                        if rcv_pending_end_samples[size]
                        else None
                    )

                    combo_results[(pattern, transport, size)] = ComboRecord(
                        status="success",
                        throughput=throughput,
                        bandwidth=bandwidth,
                        latency=latency,
                        latency_p95=latency_p95,
                        latency_p99=latency_p99,
                        cpu_pct=cpu_pct,
                        mem_mb=mem_mb,
                        snd_pending_max=snd_pending_max,
                        rcv_pending_max=rcv_pending_max,
                        rcv_pending_end=rcv_pending_end,
                    )

            if show_run_labels:
                median_line = "      median:"
                print(median_line)
                table_lines.append(median_line)
                header = f"        {single_table_header_line()}"
                separator = f"        {single_table_separator_line()}"
                print(header)
                print(separator)
                table_lines.append(header)
                table_lines.append(separator)

                for size in sizes:
                    record = combo_results.get((pattern, transport, size))
                    if record and record.status == "success":
                        row = single_table_row_line(size, "success", record)
                    elif record and record.status == "unsupported":
                        row = single_table_row_line(size, "unsupported", None)
                    else:
                        row = single_table_row_line(size, "fail", record)
                    line = f"        {row}"
                    print(line)
                    table_lines.append(line)

            done_line = f"    Testing {transport}: Done"
            print(done_line)
            table_lines.append(done_line)

            if FAIL_FAST and all_failures:
                break

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
        0,
        (requested_combo_count - unsupported_combo_count - skip_combo_count)
        * REQUIRED_RESULT_METRIC_COUNT,
    )
    actual_result_lines = sum(
        REQUIRED_RESULT_METRIC_COUNT
        for record in combo_results.values()
        if record.status == "success"
    )
    completion_status = (
        "complete" if expected_result_lines == actual_result_lines else "partial"
    )

    print_effective_options("result", effective_options)
    if any(record.status == "success" for record in combo_results.values()):
        print("\n## Result Data")
        emit_result_lines(combo_results)
    print("\n## Completion")
    print(f"- status: {completion_status}")
    print(f"- expected_result_lines: {expected_result_lines}")
    print(f"- actual_result_lines: {actual_result_lines}")

    if run_warnings:
        print("\n## Warnings")
        for item in run_warnings:
            print(f"- {item}")

    enforce_file_retention(os.path.dirname(result_file))
    print(f"\nSaved result file: {result_file} (status={completion_status})")

    sys.stdout = orig_stdout
    sys.stderr = orig_stderr
    result_log_fh.close()
    if completion_status != "complete":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
