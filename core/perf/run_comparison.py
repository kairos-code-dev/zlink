#!/usr/bin/env python3
"""
core/perf - zlink benchmark runner
"""
import datetime
import json
import os
import platform
import queue
import re
import shutil
import statistics
import subprocess
import sys
import threading
import time

# Environment helpers
IS_WINDOWS = os.name == "nt"
IS_LINUX = platform.system() == "Linux"
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""
SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
BUILD_CONFIG_DIRS = ("Release", "Debug", "RelWithDebInfo", "MinSizeRel")
RUN_STARTED_AT = time.time()

MODE_OBSERVE = "observe"
MODE_TREND = "trend"
MODE_GATE = "gate"
VALID_MODES = {MODE_OBSERVE, MODE_TREND, MODE_GATE}

DEFAULT_WARN_THROUGHPUT_PCT = 10.0
DEFAULT_FAIL_THROUGHPUT_PCT = 15.0
DEFAULT_WARN_LATENCY_PCT = 10.0
DEFAULT_FAIL_LATENCY_PCT = 15.0
PATTERN_SEPARATOR = "==============================================================================="
DEFAULT_THRESHOLDS_FILE = os.path.join(SCRIPT_DIR, "thresholds.json")
STREAM_VARIANT_PATTERNS = (
    "MULTI_STREAM",
    "MULTI_STREAM_CALLBACK",
    "MULTI_STREAM_LEN32BE",
)
MULTI_PATTERN_SUFFIX = {
    "MULTI_DEALER_DEALER": "dealer_dealer",
    "MULTI_DEALER_ROUTER": "dealer_router",
    "MULTI_ROUTER_ROUTER": "router_router",
    "MULTI_PUBSUB": "pubsub",
    "MULTI_GATEWAY": "gateway",
    "MULTI_SPOT": "spot",
    "MULTI_STREAM": "stream",
    "MULTI_STREAM_CALLBACK": "stream_callback",
    "MULTI_STREAM_LEN32BE": "stream_len32be",
}
MULTI_STREAM_MONO_BINARY = {
    "MULTI_STREAM": "comp_current_multi_stream_mono",
    "MULTI_STREAM_CALLBACK": "comp_current_multi_stream_callback_mono",
    "MULTI_STREAM_LEN32BE": "comp_current_multi_stream_len32be_mono",
}
MULTI_ECHO_PATTERNS = {
    "MULTI_DEALER_DEALER",
    "MULTI_DEALER_ROUTER",
    "MULTI_ROUTER_ROUTER",
}
SINGLE_ECHO_PATTERNS = {
    "PAIR",
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "ROUTER_ROUTER_POLL",
}


def is_echo_pattern(pattern_name):
    pattern = (pattern_name or "").strip().upper()
    if pattern.startswith("MULTI_"):
        return pattern in MULTI_ECHO_PATTERNS
    return pattern in SINGLE_ECHO_PATTERNS


def resolve_multi_required_binaries(pattern_name):
    pattern = (pattern_name or "").strip().upper()
    if pattern in MULTI_STREAM_MONO_BINARY:
        return [MULTI_STREAM_MONO_BINARY[pattern]]

    suffix = MULTI_PATTERN_SUFFIX.get(pattern)
    if not suffix:
        return []
    return [
        f"comp_current_multi_{suffix}_server",
        f"comp_current_multi_{suffix}_client",
    ]


def pattern_direction_label(pattern_name):
    return "echo" if is_echo_pattern(pattern_name) else "one-way"


def compute_bandwidth_mb_s(pattern_name, size, throughput):
    direction_factor = 2.0 if is_echo_pattern(pattern_name) else 1.0
    return (float(throughput) * float(size) * direction_factor) / 1000000.0


def normalize_threshold_value(metric, field, value):
    if metric in ("throughput", "bandwidth") and value > 0:
        adjusted = -abs(value)
        warning = (
            f"{metric} threshold should be <= 0; "
            f"auto-negated {field}={value} -> {adjusted}"
        )
        return adjusted, warning
    if metric == "latency" and value < 0:
        adjusted = abs(value)
        warning = (
            f"{metric} threshold should be >= 0; "
            f"auto-abs {field}={value} -> {adjusted}"
        )
        return adjusted, warning
    return value, ""


def threshold_rank(pattern, transport):
    if pattern != "*" and transport != "*":
        return 1
    if pattern != "*" and transport == "*":
        return 2
    if pattern == "*" and transport != "*":
        return 3
    return 4


def resolve_linux_paths():
    """Return build/library paths for Linux/macOS environments."""
    sys_name = platform.system().lower()
    if "darwin" in sys_name:
        platform_tag = "macos"
    else:
        platform_tag = "linux"
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
            ROOT_DIR,
            "core",
            "build",
            f"{platform_tag}-{arch_tag}",
            "bin",
            "Release",
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
    current_lib_dir = os.path.abspath(os.path.join(build_root, "lib"))
    return build_dir, current_lib_dir


def normalize_build_dir(path):
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


def derive_current_lib_dir(build_dir):
    build_root = build_dir
    base = os.path.basename(build_root)
    if base in BUILD_CONFIG_DIRS:
        bin_root = os.path.dirname(build_root)
        if os.path.basename(bin_root) == "bin":
            build_root = os.path.dirname(bin_root)
    elif base == "bin":
        build_root = os.path.dirname(build_root)
    return os.path.abspath(os.path.join(build_root, "lib"))


def has_cmake_cache(path):
    return os.path.isfile(os.path.join(path, "CMakeCache.txt"))


def derive_cmake_build_dir(runtime_build_dir):
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


if IS_WINDOWS:
    BUILD_DIR = os.path.join(ROOT_DIR, "core", "build", "windows-x64", "bin", "Release")
    CURRENT_LIB_DIR = os.path.join(
        ROOT_DIR, "core", "build", "windows-x64", "bin", "Release"
    )
else:
    BUILD_DIR, CURRENT_LIB_DIR = resolve_linux_paths()

DEFAULT_NUM_RUNS = 1


def _read_env_value(name, *fallback_names):
    for key in (name,) + fallback_names:
        val = os.environ.get(key)
        if val:
            return val
    return None


def parse_env_list(name, cast_fn, *fallback_names):
    val = _read_env_value(name, *fallback_names)
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


def parse_env_int(name, default, *fallback_names):
    val = _read_env_value(name, *fallback_names)
    if not val:
        return default
    try:
        return int(val)
    except ValueError:
        return default


def parse_env_float(name, default, *fallback_names):
    val = _read_env_value(name, *fallback_names)
    if not val:
        return default
    try:
        return float(val)
    except ValueError:
        return default


def linux_proc_metrics_supported():
    return IS_LINUX and hasattr(os, "sysconf") and os.path.exists("/proc")


def read_proc_cpu_ticks(pid):
    stat_path = f"/proc/{pid}/stat"
    try:
        with open(stat_path, "r", encoding="utf-8", errors="ignore") as f:
            data = f.read().strip()
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


def read_proc_mem_mb(pid):
    status_path = f"/proc/{pid}/status"
    try:
        with open(status_path, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
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
        _fields_ = [("dwLowDateTime", wintypes.DWORD), ("dwHighDateTime", wintypes.DWORD)]

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


def windows_proc_metrics_supported():
    return IS_WINDOWS


def open_windows_process_for_metrics(pid):
    if not windows_proc_metrics_supported():
        return None
    access = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ
    handle = _kernel32.OpenProcess(access, False, int(pid))
    if not handle:
        return None
    return handle


def close_windows_process_handle(handle):
    if handle:
        _kernel32.CloseHandle(handle)


def _filetime_to_uint64(filetime_obj):
    return (int(filetime_obj.dwHighDateTime) << 32) | int(filetime_obj.dwLowDateTime)


def read_windows_cpu_ticks_100ns(handle):
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


def read_windows_mem_mb(handle):
    if not handle:
        return None
    counters = PROCESS_MEMORY_COUNTERS()
    counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
    ok = _psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb)
    if not ok:
        return None
    return float(counters.WorkingSetSize) / (1024.0 * 1024.0)


def run_command_with_metrics(cmd, env, timeout_sec):
    started = time.monotonic()
    proc = subprocess.Popen(
        cmd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    sample_mode = ""
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


# Settings for loop
_env_transports = parse_env_list("PERF_TRANSPORTS", str, "BENCH_TRANSPORTS")

# Default transports for ZMP sockets (non-STREAM)
TRANSPORTS = ["tcp", "tls", "ws", "wss", "inproc"]
if not IS_WINDOWS:
    TRANSPORTS.append("ipc")

# STREAM socket uses different transports (raw TCP/TLS/WS/WSS)
STREAM_TRANSPORTS = ["tcp", "tls", "ws", "wss"]
FAIL_FAST = os.environ.get("BENCH_FAIL_FAST", "0") == "1"


def is_multi_pattern(pattern_name):
    return pattern_name.startswith("MULTI_")


def select_transports(pattern_name):
    base = (
        STREAM_TRANSPORTS
        if pattern_name
        in (
            "STREAM",
            "MULTI_STREAM",
            "MULTI_STREAM_CALLBACK",
            "MULTI_STREAM_LEN32BE",
            "GATEWAY",
            "SPOT",
            "MULTI_GATEWAY",
            "MULTI_SPOT",
        )
        else TRANSPORTS
    )
    if not _env_transports:
        return list(base)
    return [t for t in base if t in _env_transports]


_env_sizes = parse_env_list("PERF_MSG_SIZES", int, "BENCH_MSG_SIZES")
if _env_sizes:
    MSG_SIZES = _env_sizes
else:
    MSG_SIZES = [64, 256, 1024, 65536, 131072, 262144]

_env_multi_stream_sizes = parse_env_list(
    "PERF_MULTI_STREAM_MSG_SIZES",
    int,
    "BENCH_MULTI_STREAM_MSG_SIZES",
)
if _env_multi_stream_sizes:
    MULTI_STREAM_MSG_SIZES = _env_multi_stream_sizes
elif _env_sizes:
    MULTI_STREAM_MSG_SIZES = _env_sizes
else:
    MULTI_STREAM_MSG_SIZES = [64, 256, 1024, 65536]

DEFAULT_RUN_COOLDOWN_MS = max(
    0,
    parse_env_int(
        "PERF_MULTI_RUN_COOLDOWN_MS",
        3000,
        "BENCH_MULTI_RUN_COOLDOWN_MS",
    ),
)

base_env = os.environ.copy()

ENV_ALIAS_PAIRS = (
    ("PERF_TRANSPORTS", "BENCH_TRANSPORTS"),
    ("PERF_MSG_SIZES", "BENCH_MSG_SIZES"),
    ("PERF_MULTI_STREAM_MSG_SIZES", "BENCH_MULTI_STREAM_MSG_SIZES"),
    ("PERF_MULTI_PATTERN", "BENCH_MULTI_PATTERN"),
    ("PERF_MULTI_CLIENTS", "BENCH_MULTI_CLIENTS"),
    ("PERF_MULTI_INFLIGHT", "BENCH_MULTI_INFLIGHT"),
    ("PERF_MULTI_HWM", "BENCH_MULTI_HWM"),
    ("PERF_MULTI_WARMUP_SECONDS", "BENCH_MULTI_WARMUP_SECONDS"),
    ("PERF_MULTI_DURATION_SECONDS", "BENCH_MULTI_DURATION_SECONDS"),
    ("PERF_MULTI_SNDTIMEO_MS", "BENCH_MULTI_SNDTIMEO_MS"),
    ("PERF_MULTI_RCVTIMEO_MS", "BENCH_MULTI_RCVTIMEO_MS"),
    ("PERF_MULTI_CONNECT_CONCURRENCY", "BENCH_MULTI_CONNECT_CONCURRENCY"),
    ("PERF_MULTI_DRAIN_MS", "BENCH_MULTI_DRAIN_MS"),
    (
        "PERF_MULTI_SIZE_TRANSITION_DRAIN_MS",
        "BENCH_MULTI_SIZE_TRANSITION_DRAIN_MS",
    ),
    ("PERF_MULTI_RECV_BATCH", "BENCH_MULTI_RECV_BATCH"),
    ("PERF_MULTI_SEND_WORKERS", "BENCH_MULTI_SEND_WORKERS"),
    ("PERF_MULTI_SEND_BACKOFF_US", "BENCH_MULTI_SEND_BACKOFF_US"),
    ("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", "BENCH_MULTI_CONNECT_READY_TIMEOUT_MS"),
    ("PERF_MULTI_MONITOR_HWM", "BENCH_MULTI_MONITOR_HWM"),
    ("PERF_MULTI_SERVER_READY_TIMEOUT_MS", "BENCH_MULTI_SERVER_READY_TIMEOUT_MS"),
    (
        "PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS",
        "BENCH_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS",
    ),
    ("PERF_MULTI_SERVER_BIND_PORT", "BENCH_MULTI_SERVER_BIND_PORT"),
    ("PERF_SERVER_RECV_THREADS", "BENCH_SERVER_RECV_THREADS"),
    ("PERF_IO_THREADS", "BENCH_IO_THREADS"),
    ("PERF_MAX_SOCKETS", "BENCH_MAX_SOCKETS"),
    ("PERF_LAT_COUNT", "BENCH_LAT_COUNT"),
    ("PERF_MULTI_LAT_TIMEOUT_MS", "BENCH_MULTI_LAT_TIMEOUT_MS"),
    ("PERF_MULTI_LAT_RETRIES", "BENCH_MULTI_LAT_RETRIES"),
    ("PERF_MULTI_LAT_RETRY_DRAIN_MS", "BENCH_MULTI_LAT_RETRY_DRAIN_MS"),
    (
        "PERF_MULTI_STREAM_NON_TCP_CLIENTS_MAX",
        "BENCH_MULTI_STREAM_NON_TCP_CLIENTS_MAX",
    ),
    ("PERF_MULTI_STREAM_SEND_BATCH", "BENCH_MULTI_STREAM_SEND_BATCH"),
    ("PERF_MULTI_STREAM_SEND_WORKERS", "BENCH_MULTI_STREAM_SEND_WORKERS"),
    (
        "PERF_MULTI_STREAM_MAX_INFLIGHT_MSGS",
        "BENCH_MULTI_STREAM_MAX_INFLIGHT_MSGS",
    ),
    (
        "PERF_MULTI_STREAM_MAX_INFLIGHT_BYTES",
        "BENCH_MULTI_STREAM_MAX_INFLIGHT_BYTES",
    ),
    ("PERF_MULTI_STREAM_SEND_RETRIES", "BENCH_MULTI_STREAM_SEND_RETRIES"),
    (
        "PERF_MULTI_STREAM_SEND_RETRY_BACKOFF_US",
        "BENCH_MULTI_STREAM_SEND_RETRY_BACKOFF_US",
    ),
    ("PERF_MULTI_STREAM_DRAIN_IDLE_MS", "BENCH_MULTI_STREAM_DRAIN_IDLE_MS"),
    ("PERF_MULTI_STREAM_DRAIN_MAX_MS", "BENCH_MULTI_STREAM_DRAIN_MAX_MS"),
    (
        "PERF_MULTI_STREAM_DRAIN_RELAY_BUDGET",
        "BENCH_MULTI_STREAM_DRAIN_RELAY_BUDGET",
    ),
)


def sync_perf_bench_aliases(env):
    for perf_key, bench_key in ENV_ALIAS_PAIRS:
        perf_value = env.get(perf_key, "").strip()
        bench_value = env.get(bench_key, "").strip()
        if perf_value and not bench_value:
            env[bench_key] = perf_value
        elif bench_value and not perf_value:
            env[perf_key] = bench_value


def env_pair_value(env, perf_key, bench_key):
    value = env.get(perf_key, "").strip()
    if value:
        return value
    return env.get(bench_key, "").strip()


def set_env_pair(env, perf_key, bench_key, value):
    string_value = str(value)
    env[perf_key] = string_value
    env[bench_key] = string_value


def get_env_for_lib(_lib_name):
    env = base_env.copy()
    sync_perf_bench_aliases(env)
    if IS_WINDOWS:
        env["PATH"] = f"{CURRENT_LIB_DIR};{env.get('PATH', '')}"
    else:
        env["LD_LIBRARY_PATH"] = f"{CURRENT_LIB_DIR}:{env.get('LD_LIBRARY_PATH', '')}"
    return env


def collect_missing_patterns(comparisons, requested):
    missing_current = []
    for current_bin, p_name in comparisons:
        if requested is not None and p_name not in requested:
            continue

        required_bins = [current_bin]
        if is_multi_pattern(p_name):
            bins = resolve_multi_required_binaries(p_name)
            if bins:
                required_bins = bins

        missing = False
        for bin_name in required_bins:
            current_path = os.path.join(BUILD_DIR, bin_name + EXE_SUFFIX)
            if not os.path.exists(current_path):
                missing = True
                break
        if missing:
            missing_current.append(p_name)

    return sorted(set(missing_current))


def collect_missing_build_targets(comparisons, requested):
    targets = []
    for current_bin, p_name in comparisons:
        if requested is not None and p_name not in requested:
            continue

        required_bins = [current_bin]
        if is_multi_pattern(p_name):
            bins = resolve_multi_required_binaries(p_name)
            if bins:
                required_bins = bins

        for bin_name in required_bins:
            current_path = os.path.join(BUILD_DIR, bin_name + EXE_SUFFIX)
            if not os.path.exists(current_path) and bin_name not in targets:
                targets.append(bin_name)

    return targets


def run_cmake_build(cmake_build_dir, targets):
    cmd = ["cmake", "--build", cmake_build_dir]
    if IS_WINDOWS:
        cmd.extend(["--config", "Release"])
    if targets:
        cmd.append("--target")
        cmd.extend(targets)

    print(f"  > Auto-building missing benchmark binaries in {cmake_build_dir}", flush=True)
    print(f"  > Build command: {' '.join(cmd)}", flush=True)

    try:
        return subprocess.run(cmd).returncode
    except FileNotFoundError:
        print("Error: cmake not found in PATH.", file=sys.stderr)
        return 127


def parse_result_line(line, transport, expected_sizes):
    if not line.startswith("RESULT,"):
        return None, None
    parts = line.strip().split(",")
    if len(parts) != 7:
        return None, f"ignored malformed RESULT line (field_count={len(parts)}): {line.strip()}"
    try:
        line_transport = parts[3].strip().lower()
        line_size = int(parts[4].strip())
        metric = parts[5].strip().lower()
        value = float(parts[6].strip())
    except ValueError:
        return None, f"ignored malformed RESULT numeric field: {line.strip()}"

    if line_transport != transport.lower() or line_size not in expected_sizes:
        return None, None

    if metric not in (
        "throughput",
        "bandwidth",
        "latency",
        "cpu_pct",
        "mem_mb",
        "client_cpu_pct",
        "client_mem_mb",
        "server_cpu_pct",
        "server_mem_mb",
    ):
        return None, f"ignored unknown RESULT metric '{metric}': {line.strip()}"

    return (line_transport, line_size, metric, value), None


def parse_special_token(line):
    stripped = line.strip()
    if stripped.startswith("UNSUPPORTED,"):
        parts = stripped.split(",", 4)
        if len(parts) >= 4:
            return ("unsupported", parts[1].strip(), parts[2].strip().upper(), parts[3].strip().lower())
    if stripped.startswith("SKIP,"):
        parts = stripped.split(",", 5)
        if len(parts) >= 5:
            reason = parts[4].strip() if len(parts) >= 5 else ""
            return (
                "skip",
                parts[1].strip(),
                parts[2].strip().upper(),
                parts[3].strip().lower(),
                reason,
            )
    return None


def detect_special_status(stdout, expected_lib, expected_pattern, expected_transport):
    expected_pattern = expected_pattern.upper()
    expected_transport = expected_transport.lower()
    for raw in stdout.splitlines():
        token = parse_special_token(raw)
        if not token:
            continue
        token_status = token[0]
        token_lib = token[1]
        token_pattern = token[2]
        token_transport = token[3]
        if (
            token_lib == expected_lib
            and token_pattern == expected_pattern
            and token_transport == expected_transport
        ):
            if token_status == "skip":
                reason = token[4] if len(token) > 4 else ""
                return token_status, reason
            return token_status, ""
    return "", ""


def multi_pattern_default_clients(pattern_name, transport=None):
    if pattern_name in STREAM_VARIANT_PATTERNS:
        base = max(1, parse_env_int("BENCH_MULTI_DEFAULT_STREAM_CLIENTS", 10000))
        tr = (transport or "").strip().lower()
        if tr in ("tls", "ws", "wss"):
            non_tcp_cap = max(
                1, parse_env_int("BENCH_MULTI_STREAM_NON_TCP_CLIENTS_MAX", 1000)
            )
            return min(base, non_tcp_cap)
        return base
    return max(1, parse_env_int("BENCH_MULTI_DEFAULT_CLIENTS", 100))


def multi_pattern_default_drain_ms(pattern_name):
    if pattern_name in (
        "MULTI_DEALER_DEALER",
        "MULTI_DEALER_ROUTER",
        "MULTI_ROUTER_ROUTER",
        "MULTI_PUBSUB",
        "MULTI_STREAM",
        "MULTI_STREAM_CALLBACK",
        "MULTI_STREAM_LEN32BE",
    ):
        return 300
    return 0


def resolve_pattern_connect_concurrency(clients):
    if clients >= 10000:
        return 1024
    return 128


def normalize_multi_metric_name(metric):
    if metric == "cpu_pct":
        return "client_cpu_pct"
    if metric == "mem_mb":
        return "client_mem_mb"
    return metric


def resolve_multi_binary_names(pattern_name):
    suffix = MULTI_PATTERN_SUFFIX.get(pattern_name)
    if not suffix:
        return None
    return {
        "server": f"comp_current_multi_{suffix}_server",
        "client": f"comp_current_multi_{suffix}_client",
    }


def resolve_multi_stream_mono_binary(pattern_name):
    return MULTI_STREAM_MONO_BINARY.get((pattern_name or "").strip().upper())


def parse_ready_endpoint(line):
    stripped = line.strip()
    if not stripped.startswith("READY,"):
        return ""
    parts = stripped.split(",", 1)
    if len(parts) != 2:
        return ""
    return parts[1].strip()


def _pipe_reader(pipe, stream_name, out_queue):
    try:
        for line in iter(pipe.readline, ""):
            out_queue.put((stream_name, line))
    except Exception:
        pass
    finally:
        out_queue.put((stream_name, None))


def run_multi_sizes_test_split(server_binary_name, client_binary_name, lib_name, transport, sizes, pattern_name):
    server_binary_path = os.path.join(BUILD_DIR, server_binary_name + EXE_SUFFIX)
    client_binary_path = os.path.join(BUILD_DIR, client_binary_name + EXE_SUFFIX)
    fallback_size = sizes[0] if sizes else 64
    duration_seconds = max(1, parse_env_int("BENCH_MULTI_DURATION_SECONDS", 5))
    timeout_sec = max(120, duration_seconds * max(1, len(sizes)) * 8 + 60)
    if pattern_name in STREAM_VARIANT_PATTERNS:
        timeout_sec = max(180, timeout_sec)

    ready_timeout_ms = max(0, parse_env_int("BENCH_MULTI_SERVER_READY_TIMEOUT_MS", 10000))
    shutdown_timeout_ms = max(0, parse_env_int("BENCH_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS", 5000))
    bind_port = max(0, parse_env_int("BENCH_MULTI_SERVER_BIND_PORT", 0))
    expected_sizes = set(sizes)

    for _ in range(1):
        env = get_env_for_lib(lib_name)
        size_csv = ",".join(str(sz) for sz in sizes)
        set_env_pair(env, "PERF_MSG_SIZES", "BENCH_MSG_SIZES", size_csv)
        set_env_pair(env, "PERF_MULTI_PATTERN", "BENCH_MULTI_PATTERN", pattern_name)
        if pattern_name in STREAM_VARIANT_PATTERNS:
            set_env_pair(
                env,
                "PERF_MULTI_STREAM_MSG_SIZES",
                "BENCH_MULTI_STREAM_MSG_SIZES",
                size_csv,
            )

        clients_value = env_pair_value(env, "PERF_MULTI_CLIENTS", "BENCH_MULTI_CLIENTS")
        if not clients_value:
            clients_value = str(multi_pattern_default_clients(pattern_name, transport))
        set_env_pair(env, "PERF_MULTI_CLIENTS", "BENCH_MULTI_CLIENTS", clients_value)

        connect_value = env_pair_value(
            env,
            "PERF_MULTI_CONNECT_CONCURRENCY",
            "BENCH_MULTI_CONNECT_CONCURRENCY",
        )
        if not connect_value:
            try:
                clients = int(clients_value)
            except ValueError:
                clients = multi_pattern_default_clients(pattern_name, transport)
            connect_value = str(resolve_pattern_connect_concurrency(max(1, clients)))
        set_env_pair(
            env,
            "PERF_MULTI_CONNECT_CONCURRENCY",
            "BENCH_MULTI_CONNECT_CONCURRENCY",
            connect_value,
        )

        drain_value = env_pair_value(env, "PERF_MULTI_DRAIN_MS", "BENCH_MULTI_DRAIN_MS")
        if not drain_value:
            drain_value = str(multi_pattern_default_drain_ms(pattern_name))
        set_env_pair(env, "PERF_MULTI_DRAIN_MS", "BENCH_MULTI_DRAIN_MS", drain_value)
        set_env_pair(
            env,
            "PERF_MULTI_SERVER_READY_TIMEOUT_MS",
            "BENCH_MULTI_SERVER_READY_TIMEOUT_MS",
            ready_timeout_ms,
        )
        set_env_pair(
            env,
            "PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS",
            "BENCH_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS",
            shutdown_timeout_ms,
        )
        set_env_pair(
            env,
            "PERF_MULTI_SERVER_BIND_PORT",
            "BENCH_MULTI_SERVER_BIND_PORT",
            bind_port,
        )

        if IS_WINDOWS:
            server_cmd = [server_binary_path, lib_name, transport]
            client_cmd = [
                client_binary_path,
                lib_name,
                transport,
                str(fallback_size),
            ]
        elif os.environ.get("BENCH_TASKSET") == "1":
            server_cmd = ["taskset", "-c", "1", server_binary_path, lib_name, transport]
            client_cmd = [
                "taskset",
                "-c",
                "1",
                client_binary_path,
                lib_name,
                transport,
                str(fallback_size),
            ]
        else:
            server_cmd = [server_binary_path, lib_name, transport]
            client_cmd = [client_binary_path, lib_name, transport, str(fallback_size)]

        server_proc = None
        server_stdout_lines = []
        server_stderr_lines = []
        out_queue = queue.Queue()
        reader_threads = []

        def stop_server():
            nonlocal server_proc
            if not server_proc:
                return
            if server_proc.poll() is None:
                try:
                    if server_proc.stdin:
                        try:
                            server_proc.stdin.write("STOP\n")
                            server_proc.stdin.flush()
                        except Exception:
                            pass
                        try:
                            server_proc.stdin.close()
                        except Exception:
                            pass
                except Exception:
                    pass
                wait_sec = shutdown_timeout_ms / 1000.0
                if wait_sec <= 0:
                    wait_sec = 0.1
                try:
                    server_proc.wait(timeout=wait_sec)
                except subprocess.TimeoutExpired:
                    try:
                        server_proc.terminate()
                    except Exception:
                        pass
                    try:
                        server_proc.wait(timeout=max(1.0, wait_sec))
                    except Exception:
                        try:
                            server_proc.kill()
                        except Exception:
                            pass
                        try:
                            server_proc.wait(timeout=2)
                        except Exception:
                            pass

        def drain_server_output():
            for t in reader_threads:
                try:
                    t.join(timeout=1.0)
                except Exception:
                    pass

            while True:
                try:
                    stream_name, line = out_queue.get_nowait()
                except queue.Empty:
                    break
                if line is None:
                    continue
                if stream_name == "stdout":
                    server_stdout_lines.append(line)
                else:
                    server_stderr_lines.append(line)

        try:
            server_proc = subprocess.Popen(
                server_cmd,
                env=env,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
            )
            reader_threads = [
                threading.Thread(
                    target=_pipe_reader,
                    args=(server_proc.stdout, "stdout", out_queue),
                    daemon=True,
                ),
                threading.Thread(
                    target=_pipe_reader,
                    args=(server_proc.stderr, "stderr", out_queue),
                    daemon=True,
                ),
            ]
            for t in reader_threads:
                t.start()

            endpoint = ""
            early_status = ""
            early_reason = ""
            ready_deadline = time.monotonic() + (ready_timeout_ms / 1000.0)
            while not endpoint and time.monotonic() <= ready_deadline:
                try:
                    stream_name, line = out_queue.get(timeout=0.05)
                except queue.Empty:
                    if server_proc.poll() is not None:
                        break
                    continue
                if line is None:
                    continue
                if stream_name == "stdout":
                    server_stdout_lines.append(line)
                    token = parse_special_token(line)
                    if token:
                        token_status = token[0]
                        token_lib = token[1]
                        token_pattern = token[2]
                        token_transport = token[3]
                        if (
                            token_lib == lib_name
                            and token_pattern == pattern_name
                            and token_transport == transport
                        ):
                            early_status = token_status
                            early_reason = token[4] if len(token) > 4 else token_status
                            break
                    parsed_ep = parse_ready_endpoint(line)
                    if parsed_ep:
                        endpoint = parsed_ep
                else:
                    server_stderr_lines.append(line)

            if early_status in ("unsupported", "skip"):
                stop_server()
                drain_server_output()
                return {
                    "status": early_status,
                    "parsed": {},
                    "timed_out": False,
                    "returncode": 0,
                    "cpu_pct": None,
                    "mem_mb": None,
                    "reason": early_reason or early_status,
                    "warnings": [],
                }

            if not endpoint:
                stop_server()
                drain_server_output()
                reason = "server_ready_timeout"
                if server_proc and server_proc.poll() is not None:
                    reason = f"server_exit_before_ready_{server_proc.returncode}"
                return {
                    "status": "fail",
                    "parsed": {},
                    "timed_out": False,
                    "returncode": server_proc.returncode if server_proc else -1,
                    "cpu_pct": None,
                    "mem_mb": None,
                    "reason": reason,
                    "warnings": [],
                }

            client_cmd = client_cmd + ["--endpoint", endpoint]
            sampled = run_command_with_metrics(client_cmd, env, timeout_sec)
            client_stdout = sampled.get("stdout", "") or ""
            client_stderr = sampled.get("stderr", "") or ""

            stop_server()
            drain_server_output()
            server_rc = server_proc.returncode if server_proc else 0

            combined_stdout = "".join(server_stdout_lines) + client_stdout
            combined_stderr = "".join(server_stderr_lines) + client_stderr
            stderr_lower = combined_stderr.lower()

            if sampled.get("timed_out", False):
                return {
                    "status": "fail",
                    "parsed": {},
                    "timed_out": True,
                    "returncode": sampled.get("returncode", -1),
                    "cpu_pct": sampled.get("cpu_pct"),
                    "mem_mb": sampled.get("mem_mb"),
                    "reason": "timeout",
                    "warnings": [],
                }
            if sampled.get("returncode", 0) != 0:
                return {
                    "status": "fail",
                    "parsed": {},
                    "timed_out": False,
                    "returncode": sampled.get("returncode", -1),
                    "cpu_pct": sampled.get("cpu_pct"),
                    "mem_mb": sampled.get("mem_mb"),
                    "reason": f"non_zero_exit_{sampled.get('returncode', -1)}",
                    "warnings": [],
                }
            if server_rc not in (0, None):
                return {
                    "status": "fail",
                    "parsed": {},
                    "timed_out": False,
                    "returncode": server_rc,
                    "cpu_pct": sampled.get("cpu_pct"),
                    "mem_mb": sampled.get("mem_mb"),
                    "reason": f"server_non_zero_exit_{server_rc}",
                    "warnings": [],
                }

            parsed = {}
            warnings = []
            for line in combined_stdout.splitlines():
                parsed_line, warning = parse_result_line(line, transport, expected_sizes)
                if warning:
                    warnings.append(warning)
                if not parsed_line:
                    continue
                line_transport, line_size, metric, value = parsed_line
                metric_name = normalize_multi_metric_name(metric)
                key = f"{line_transport}|{line_size}|{metric_name}"
                if key in parsed:
                    warnings.append(
                        "duplicate RESULT metric detected; keeping last value: "
                        f"{pattern_name} {transport} {line_size}B {metric_name}"
                    )
                parsed[key] = value

            client_cpu = sampled.get("cpu_pct")
            client_mem = sampled.get("mem_mb")
            if client_cpu is not None:
                for sz in sizes:
                    key = f"{transport}|{sz}|client_cpu_pct"
                    parsed.setdefault(key, float(client_cpu))
            if client_mem is not None:
                for sz in sizes:
                    key = f"{transport}|{sz}|client_mem_mb"
                    parsed.setdefault(key, float(client_mem))

            token_status, token_reason = detect_special_status(
                combined_stdout, lib_name, pattern_name, transport
            )

            if parsed:
                return {
                    "status": "success",
                    "parsed": parsed,
                    "timed_out": False,
                    "returncode": 0,
                    "cpu_pct": sampled.get("cpu_pct"),
                    "mem_mb": sampled.get("mem_mb"),
                    "reason": "",
                    "warnings": warnings,
                }
            if token_status in ("unsupported", "skip"):
                return {
                    "status": token_status,
                    "parsed": {},
                    "timed_out": False,
                    "returncode": 0,
                    "cpu_pct": sampled.get("cpu_pct"),
                    "mem_mb": sampled.get("mem_mb"),
                    "reason": token_reason if token_reason else token_status,
                    "warnings": warnings,
                }
            if "protocol not supported" in stderr_lower:
                return {
                    "status": "unsupported",
                    "parsed": {},
                    "timed_out": False,
                    "returncode": 0,
                    "cpu_pct": sampled.get("cpu_pct"),
                    "mem_mb": sampled.get("mem_mb"),
                    "reason": "unsupported",
                    "warnings": warnings,
                }
            return {
                "status": "fail",
                "parsed": {},
                "timed_out": False,
                "returncode": sampled.get("returncode", -1),
                "cpu_pct": sampled.get("cpu_pct"),
                "mem_mb": sampled.get("mem_mb"),
                "reason": "no_data",
                "warnings": warnings,
            }
        except Exception:
            stop_server()
            return {
                "status": "fail",
                "parsed": {},
                "timed_out": False,
                "returncode": -1,
                "cpu_pct": None,
                "mem_mb": None,
                "reason": "exception",
                "warnings": [],
            }

    return {
        "status": "fail",
        "parsed": {},
        "timed_out": False,
        "returncode": -1,
        "cpu_pct": None,
        "mem_mb": None,
        "reason": "no_data",
        "warnings": [],
    }


def run_multi_sizes_test_stream_mono(binary_name, lib_name, transport, sizes, pattern_name):
    binary_path = os.path.join(BUILD_DIR, binary_name + EXE_SUFFIX)
    fallback_size = sizes[0] if sizes else 64
    duration_seconds = max(
        1,
        parse_env_int(
            "PERF_MULTI_DURATION_SECONDS",
            parse_env_int("BENCH_MULTI_DURATION_SECONDS", 5),
        ),
    )
    timeout_sec = max(180, duration_seconds * max(1, len(sizes)) * 10 + 120)
    expected_sizes = set(sizes)

    for _ in range(1):
        env = get_env_for_lib(lib_name)
        size_csv = ",".join(str(sz) for sz in sizes)
        set_env_pair(env, "PERF_MSG_SIZES", "BENCH_MSG_SIZES", size_csv)
        set_env_pair(env, "PERF_MULTI_PATTERN", "BENCH_MULTI_PATTERN", pattern_name)
        set_env_pair(
            env,
            "PERF_MULTI_STREAM_MSG_SIZES",
            "BENCH_MULTI_STREAM_MSG_SIZES",
            size_csv,
        )

        clients_value = env_pair_value(env, "PERF_MULTI_CLIENTS", "BENCH_MULTI_CLIENTS")
        if not clients_value:
            clients_value = str(multi_pattern_default_clients(pattern_name, transport))
        set_env_pair(env, "PERF_MULTI_CLIENTS", "BENCH_MULTI_CLIENTS", clients_value)

        connect_value = env_pair_value(
            env,
            "PERF_MULTI_CONNECT_CONCURRENCY",
            "BENCH_MULTI_CONNECT_CONCURRENCY",
        )
        if not connect_value:
            try:
                clients = int(clients_value)
            except ValueError:
                clients = multi_pattern_default_clients(pattern_name, transport)
            connect_value = str(resolve_pattern_connect_concurrency(max(1, clients)))
        set_env_pair(
            env,
            "PERF_MULTI_CONNECT_CONCURRENCY",
            "BENCH_MULTI_CONNECT_CONCURRENCY",
            connect_value,
        )

        drain_value = env_pair_value(env, "PERF_MULTI_DRAIN_MS", "BENCH_MULTI_DRAIN_MS")
        if not drain_value:
            drain_value = str(multi_pattern_default_drain_ms(pattern_name))
        set_env_pair(env, "PERF_MULTI_DRAIN_MS", "BENCH_MULTI_DRAIN_MS", drain_value)

        if IS_WINDOWS:
            cmd = [binary_path, lib_name, transport, str(fallback_size)]
        elif os.environ.get("BENCH_TASKSET") == "1" or os.environ.get("PERF_TASKSET") == "1":
            cmd = ["taskset", "-c", "1", binary_path, lib_name, transport, str(fallback_size)]
        else:
            cmd = [binary_path, lib_name, transport, str(fallback_size)]

        try:
            sampled = run_command_with_metrics(cmd, env, timeout_sec)
            stdout = sampled.get("stdout", "") or ""
            stderr = sampled.get("stderr", "") or ""
            stderr_lower = stderr.lower()

            if sampled.get("timed_out", False):
                return {
                    "status": "fail",
                    "parsed": {},
                    "timed_out": True,
                    "returncode": sampled.get("returncode", -1),
                    "cpu_pct": sampled.get("cpu_pct"),
                    "mem_mb": sampled.get("mem_mb"),
                    "reason": "timeout",
                    "warnings": [],
                }
            if sampled.get("returncode", 0) != 0:
                return {
                    "status": "fail",
                    "parsed": {},
                    "timed_out": False,
                    "returncode": sampled.get("returncode", -1),
                    "cpu_pct": sampled.get("cpu_pct"),
                    "mem_mb": sampled.get("mem_mb"),
                    "reason": f"non_zero_exit_{sampled.get('returncode', -1)}",
                    "warnings": [],
                }

            parsed = {}
            warnings = []
            for line in stdout.splitlines():
                parsed_line, warning = parse_result_line(line, transport, expected_sizes)
                if warning:
                    warnings.append(warning)
                if not parsed_line:
                    continue
                line_transport, line_size, metric, value = parsed_line
                metric_name = normalize_multi_metric_name(metric)
                key = f"{line_transport}|{line_size}|{metric_name}"
                if key in parsed:
                    warnings.append(
                        "duplicate RESULT metric detected; keeping last value: "
                        f"{pattern_name} {transport} {line_size}B {metric_name}"
                    )
                parsed[key] = value

            token_status, token_reason = detect_special_status(
                stdout, lib_name, pattern_name, transport
            )

            if parsed:
                cpu_sample = sampled.get("cpu_pct")
                mem_sample = sampled.get("mem_mb")
                for sz in sizes:
                    client_cpu_key = f"{transport}|{sz}|client_cpu_pct"
                    client_mem_key = f"{transport}|{sz}|client_mem_mb"
                    server_cpu_key = f"{transport}|{sz}|server_cpu_pct"
                    server_mem_key = f"{transport}|{sz}|server_mem_mb"

                    if cpu_sample is not None:
                        parsed.setdefault(client_cpu_key, float(cpu_sample))
                        parsed.setdefault(server_cpu_key, float(cpu_sample))
                    else:
                        parsed.setdefault(client_cpu_key, 0.0)
                        parsed.setdefault(server_cpu_key, 0.0)

                    if mem_sample is not None:
                        parsed.setdefault(client_mem_key, float(mem_sample))
                        parsed.setdefault(server_mem_key, float(mem_sample))
                    else:
                        parsed.setdefault(client_mem_key, 0.0)
                        parsed.setdefault(server_mem_key, 0.0)

                return {
                    "status": "success",
                    "parsed": parsed,
                    "timed_out": False,
                    "returncode": 0,
                    "cpu_pct": sampled.get("cpu_pct"),
                    "mem_mb": sampled.get("mem_mb"),
                    "reason": "",
                    "warnings": warnings,
                }

            if token_status in ("unsupported", "skip"):
                return {
                    "status": token_status,
                    "parsed": {},
                    "timed_out": False,
                    "returncode": 0,
                    "cpu_pct": sampled.get("cpu_pct"),
                    "mem_mb": sampled.get("mem_mb"),
                    "reason": token_reason if token_reason else token_status,
                    "warnings": warnings,
                }
            if "protocol not supported" in stderr_lower:
                return {
                    "status": "unsupported",
                    "parsed": {},
                    "timed_out": False,
                    "returncode": 0,
                    "cpu_pct": sampled.get("cpu_pct"),
                    "mem_mb": sampled.get("mem_mb"),
                    "reason": "unsupported",
                    "warnings": warnings,
                }
            return {
                "status": "fail",
                "parsed": {},
                "timed_out": False,
                "returncode": sampled.get("returncode", -1),
                "cpu_pct": sampled.get("cpu_pct"),
                "mem_mb": sampled.get("mem_mb"),
                "reason": "no_data",
                "warnings": warnings,
            }
        except Exception:
            return {
                "status": "fail",
                "parsed": {},
                "timed_out": False,
                "returncode": -1,
                "cpu_pct": None,
                "mem_mb": None,
                "reason": "exception",
                "warnings": [],
            }

    return {
        "status": "fail",
        "parsed": {},
        "timed_out": False,
        "returncode": -1,
        "cpu_pct": None,
        "mem_mb": None,
        "reason": "no_data",
        "warnings": [],
    }


def run_multi_sizes_test(_binary_name, lib_name, transport, sizes, pattern_name):
    mono_binary = resolve_multi_stream_mono_binary(pattern_name)
    if mono_binary:
        mono_path = os.path.join(BUILD_DIR, mono_binary + EXE_SUFFIX)
        if os.path.exists(mono_path):
            return run_multi_sizes_test_stream_mono(
                mono_binary, lib_name, transport, sizes, pattern_name
            )

    names = resolve_multi_binary_names(pattern_name)
    if names:
        server_path = os.path.join(BUILD_DIR, names["server"] + EXE_SUFFIX)
        client_path = os.path.join(BUILD_DIR, names["client"] + EXE_SUFFIX)
        if os.path.exists(server_path) and os.path.exists(client_path):
            return run_multi_sizes_test_split(
                names["server"], names["client"], lib_name, transport, sizes, pattern_name
            )
        return {
            "status": "fail",
            "parsed": {},
            "timed_out": False,
            "returncode": -1,
            "cpu_pct": None,
            "mem_mb": None,
            "reason": "missing_split_binaries" if not mono_binary else "missing_stream_binaries",
            "warnings": [],
        }
    return {
        "status": "fail",
        "parsed": {},
        "timed_out": False,
        "returncode": -1,
        "cpu_pct": None,
        "mem_mb": None,
        "reason": "invalid_multi_pattern",
        "warnings": [],
    }


def run_single_test(binary_name, lib_name, transport, size, pattern_name=""):
    """Runs a single binary for one specific config."""
    binary_path = os.path.join(BUILD_DIR, binary_name + EXE_SUFFIX)
    timeout_sec = 60
    if pattern_name.startswith("MULTI_"):
        timeout_sec = max(
            60,
            parse_env_int("BENCH_MULTI_WARMUP_SECONDS", 3)
            + parse_env_int("BENCH_MULTI_DURATION_SECONDS", 5)
            + 30,
        )
    if pattern_name in STREAM_VARIANT_PATTERNS:
        timeout_sec = max(
            120,
            parse_env_int("BENCH_MULTI_WARMUP_SECONDS", 3)
            + parse_env_int("BENCH_MULTI_DURATION_SECONDS", 5)
            + 90,
        )

    env = get_env_for_lib(lib_name)
    if pattern_name.startswith("MULTI_"):
        set_env_pair(env, "PERF_MULTI_PATTERN", "BENCH_MULTI_PATTERN", pattern_name)
    if pattern_name == "STREAM" and transport in ("tls", "ws", "wss"):
        env["BENCH_MSG_COUNT"] = "5000"
        env["BENCH_WARMUP_COUNT"] = "100"

    try:
        if IS_WINDOWS:
            cmd = [binary_path, lib_name, transport, str(size)]
        elif os.environ.get("BENCH_TASKSET") == "1" or os.environ.get("PERF_TASKSET") == "1":
            cmd = ["taskset", "-c", "1", binary_path, lib_name, transport, str(size)]
        else:
            cmd = [binary_path, lib_name, transport, str(size)]

        sampled = run_command_with_metrics(cmd, env, timeout_sec)
        if sampled.get("timed_out", False):
            return None

        stderr = (sampled.get("stderr", "") or "").lower()
        if "protocol not supported" in stderr:
            return [{"metric": "_unsupported_transport_", "value": 1.0}]
        if sampled.get("returncode", 0) != 0:
            return []

        parsed = []
        for line in (sampled.get("stdout", "") or "").splitlines():
            if line.startswith("RESULT,"):
                p = line.split(",")
                if len(p) >= 7:
                    try:
                        metric = p[5].strip().lower()
                        parsed.append({"metric": metric, "value": float(p[6])})
                    except ValueError:
                        continue
        seen = {entry.get("metric", "").strip().lower() for entry in parsed}
        cpu_pct = sampled.get("cpu_pct")
        mem_mb = sampled.get("mem_mb")
        if cpu_pct is not None and "cpu_pct" not in seen:
            parsed.append({"metric": "cpu_pct", "value": float(cpu_pct)})
        if mem_mb is not None and "mem_mb" not in seen:
            parsed.append({"metric": "mem_mb", "value": float(mem_mb)})
        if parsed:
            return parsed
        return []
    except Exception:
        return []


def collect_data(binary_name, lib_name, pattern_name, num_runs, transports=None):
    print(f"  > Benchmarking {lib_name} for {pattern_name}...")
    final_stats = {}  # (tr, size, metric) -> median value
    failures = []

    if transports is None:
        transports = TRANSPORTS

    run_cooldown_ms = max(
        0, parse_env_int("BENCH_MULTI_RUN_COOLDOWN_MS", DEFAULT_RUN_COOLDOWN_MS)
    )
    transport_transition_ms = max(
        0, parse_env_int("BENCH_MULTI_TRANSPORT_TRANSITION_MS", 3000)
    )

    for tr_idx, tr in enumerate(transports):
        has_next_transport = (tr_idx + 1) < len(transports)

        def maybe_transport_cooldown():
            if (
                not pattern_name.startswith("MULTI_")
                or transport_transition_ms <= 0
                or not has_next_transport
            ):
                return
            print(
                f"[transport-cooldown={transport_transition_ms}ms] ",
                end="",
                flush=True,
            )
            time.sleep(transport_transition_ms / 1000.0)

        sizes = MULTI_STREAM_MSG_SIZES if pattern_name in STREAM_VARIANT_PATTERNS else MSG_SIZES

        if pattern_name.startswith("MULTI_"):
            size_tag = ",".join(f"{sz}B" for sz in sizes)
            print(f"    Testing {tr} | {size_tag}: ", end="", flush=True)
            metrics_raw = {}
            failed_runs = 0
            expected_keys = []
            for sz in sizes:
                expected_keys.append(f"{tr}|{sz}|throughput")
                expected_keys.append(f"{tr}|{sz}|bandwidth")
                expected_keys.append(f"{tr}|{sz}|latency")

            tr_supported = True
            for i in range(num_runs):
                print(f"{i+1} ", end="", flush=True)
                has_next_run = (i + 1) < num_runs

                def maybe_cooldown():
                    if run_cooldown_ms <= 0 or not has_next_run:
                        return
                    print(f"[cooldown={run_cooldown_ms}ms] ", end="", flush=True)
                    time.sleep(run_cooldown_ms / 1000.0)

                outcome = run_multi_sizes_test(binary_name, lib_name, tr, sizes, pattern_name)
                rc = outcome.get("returncode", 0)
                for warning in outcome.get("warnings", []) or []:
                    print(f"warning: {warning}", file=sys.stderr)

                outcome_status = outcome.get("status", "fail")
                if outcome.get("timed_out", False):
                    failed_runs += 1
                    for sz in sizes:
                        failures.append((pattern_name, lib_name, tr, sz, "timeout"))
                    if FAIL_FAST:
                        print(f"(failures={failed_runs}) ", end="", flush=True)
                        print("aborting")
                        return final_stats, failures
                    maybe_cooldown()
                    continue

                if outcome_status == "unsupported":
                    tr_supported = False
                    for sz in sizes:
                        final_stats[f"{tr}|{sz}|throughput"] = 0
                        final_stats[f"{tr}|{sz}|bandwidth"] = 0
                        final_stats[f"{tr}|{sz}|latency"] = 0
                        final_stats[f"{tr}|{sz}|unsupported"] = 1
                    print("unsupported ", end="", flush=True)
                    break

                if outcome_status == "skip":
                    tr_supported = False
                    skip_reason = outcome.get("reason", "skip")
                    for sz in sizes:
                        final_stats[f"{tr}|{sz}|throughput"] = 0
                        final_stats[f"{tr}|{sz}|bandwidth"] = 0
                        final_stats[f"{tr}|{sz}|latency"] = 0
                        final_stats[f"{tr}|{sz}|skip"] = 1
                        final_stats[f"{tr}|{sz}|skip_reason"] = skip_reason
                    print("skip ", end="", flush=True)
                    break

                parsed = outcome.get("parsed", {})
                if not parsed:
                    failed_runs += 1
                    base_reason = outcome.get("reason", "")
                    if base_reason:
                        reason = base_reason
                    else:
                        reason = f"no_data_rc_{rc}" if rc not in (0, None) else "no_data"
                    for sz in sizes:
                        failures.append((pattern_name, lib_name, tr, sz, reason))
                    if FAIL_FAST:
                        print(f"(failures={failed_runs}) ", end="", flush=True)
                        print("aborting")
                        return final_stats, failures
                    maybe_cooldown()
                    continue

                missing = [key for key in expected_keys if key not in parsed]
                if missing:
                    failed_runs += 1
                    for key in missing:
                        parts = key.split("|")
                        sz = int(parts[1])
                        metric = parts[2]
                        suffix = f"_rc_{rc}" if rc not in (0, None) else ""
                        failures.append((pattern_name, lib_name, tr, sz, f"missing_{metric}{suffix}"))
                    if FAIL_FAST:
                        print(f"(failures={failed_runs}) ", end="", flush=True)
                        print("aborting")
                        return final_stats, failures

                for key, value in parsed.items():
                    metrics_raw.setdefault(key, []).append(value)
                cpu_sample = outcome.get("cpu_pct")
                mem_sample = outcome.get("mem_mb")
                for sz in sizes:
                    cpu_key = f"{tr}|{sz}|client_cpu_pct"
                    mem_key = f"{tr}|{sz}|client_mem_mb"
                    if cpu_sample is not None and cpu_key not in parsed:
                        metrics_raw.setdefault(cpu_key, []).append(float(cpu_sample))
                    if mem_sample is not None and mem_key not in parsed:
                        metrics_raw.setdefault(mem_key, []).append(float(mem_sample))
                maybe_cooldown()

            if not tr_supported:
                print("Done")
                maybe_transport_cooldown()
                continue

            for key in expected_keys:
                vals = metrics_raw.get(key, [])
                final_stats[key] = statistics.median(vals) if vals else 0
            for sz in sizes:
                for optional_metric in (
                    "client_cpu_pct",
                    "client_mem_mb",
                    "server_cpu_pct",
                    "server_mem_mb",
                ):
                    optional_key = f"{tr}|{sz}|{optional_metric}"
                    vals = metrics_raw.get(optional_key, [])
                    if vals:
                        final_stats[optional_key] = statistics.median(vals)

            if failed_runs:
                print(f"(failures={failed_runs}) ", end="", flush=True)
            print("Done")
            maybe_transport_cooldown()
            continue

        tr_supported = True
        for idx, sz in enumerate(sizes):
            print(f"    Testing {tr} | {sz}B: ", end="", flush=True)
            metrics_raw = {}  # metric_name -> list of values
            failed_runs = 0

            if not tr_supported:
                final_stats[f"{tr}|{sz}|throughput"] = 0
                final_stats[f"{tr}|{sz}|bandwidth"] = 0
                final_stats[f"{tr}|{sz}|latency"] = 0
                final_stats[f"{tr}|{sz}|unsupported"] = 1
                print("unsupported", end=" ", flush=True)
                print("Done")
                continue

            for i in range(num_runs):
                print(f"{i+1} ", end="", flush=True)
                has_next_run = (i + 1) < num_runs

                def maybe_cooldown():
                    if run_cooldown_ms <= 0 or not has_next_run:
                        return
                    print(f"[cooldown={run_cooldown_ms}ms] ", end="", flush=True)
                    time.sleep(run_cooldown_ms / 1000.0)

                results = run_single_test(binary_name, lib_name, tr, sz, pattern_name)
                if results is None:
                    failed_runs += 1
                    failures.append((pattern_name, lib_name, tr, sz, "timeout"))
                    if FAIL_FAST:
                        print(f"(failures={failed_runs}) ", end="", flush=True)
                        print("aborting")
                        return final_stats, failures
                    maybe_cooldown()
                    continue
                if not results:
                    failed_runs += 1
                    failures.append((pattern_name, lib_name, tr, sz, "no_data"))
                    if FAIL_FAST:
                        print(f"(failures={failed_runs}) ", end="", flush=True)
                        print("aborting")
                        return final_stats, failures
                    maybe_cooldown()
                    continue
                if len(results) == 1 and results[0].get("metric") == "_unsupported_transport_":
                    print("unsupported", end=" ", flush=True)
                    print("Done")
                    tr_supported = False
                    final_stats[f"{tr}|{sz}|throughput"] = 0
                    final_stats[f"{tr}|{sz}|bandwidth"] = 0
                    final_stats[f"{tr}|{sz}|latency"] = 0
                    final_stats[f"{tr}|{sz}|unsupported"] = 1
                    for next_idx in range(idx + 1, len(sizes)):
                        next_sz = sizes[next_idx]
                        final_stats[f"{tr}|{next_sz}|throughput"] = 0
                        final_stats[f"{tr}|{next_sz}|bandwidth"] = 0
                        final_stats[f"{tr}|{next_sz}|latency"] = 0
                        final_stats[f"{tr}|{next_sz}|unsupported"] = 1
                    if FAIL_FAST:
                        break
                    maybe_cooldown()
                    continue
                for r in results:
                    m = r["metric"]
                    metrics_raw.setdefault(m, []).append(r["value"])
                maybe_cooldown()

            for m, vals in metrics_raw.items():
                final_stats[f"{tr}|{sz}|{m}"] = statistics.median(vals) if vals else 0
            if failed_runs:
                print(f"(failures={failed_runs}) ", end="", flush=True)
            print("Done")
        maybe_transport_cooldown()
    return final_stats, failures


def format_throughput(pattern_name, throughput_per_sec):
    unit = "Kops/s" if is_echo_pattern(pattern_name) else "Kmsg/s"
    return f"{throughput_per_sec/1e3:6.2f} {unit}"


def format_bandwidth(bandwidth_mb_s):
    return f"{bandwidth_mb_s:8.2f} MB/s"


def get_os_label():
    system = platform.system()
    release = platform.release()
    if system and release:
        return f"{system} {release}"
    return platform.platform()


def get_cpu_model():
    try:
        if platform.system() == "Linux":
            with open("/proc/cpuinfo", "r", encoding="utf-8", errors="ignore") as f:
                for line in f:
                    if line.lower().startswith("model name"):
                        parts = line.split(":", 1)
                        if len(parts) == 2:
                            model = parts[1].strip()
                            if model:
                                return model
        elif platform.system() == "Darwin":
            output = subprocess.check_output(
                ["sysctl", "-n", "machdep.cpu.brand_string"],
                text=True,
                stderr=subprocess.DEVNULL,
            ).strip()
            if output:
                return output
        cpu = platform.processor().strip()
        if cpu:
            return cpu
    except Exception:
        pass
    return "unknown"


def get_load_avg():
    if hasattr(os, "getloadavg"):
        try:
            vals = os.getloadavg()
            return " ".join(f"{v:.2f}" for v in vals)
        except OSError:
            return ""
    return ""


def get_commit_short_sha():
    try:
        return (
            subprocess.check_output(
                ["git", "-C", ROOT_DIR, "rev-parse", "--short", "HEAD"],
                text=True,
                stderr=subprocess.DEVNULL,
            )
            .strip()
            .strip()
        )
    except Exception:
        return "unknown"


def detect_build_type(build_dir):
    base = os.path.basename(build_dir)
    if base in BUILD_CONFIG_DIRS:
        return base

    cmake_dir = derive_cmake_build_dir(build_dir)
    if cmake_dir:
        cache = os.path.join(cmake_dir, "CMakeCache.txt")
        if os.path.isfile(cache):
            try:
                with open(cache, "r", encoding="utf-8", errors="ignore") as f:
                    for line in f:
                        if line.startswith("CMAKE_BUILD_TYPE:STRING="):
                            value = line.split("=", 1)[1].strip()
                            if value:
                                return value
            except OSError:
                pass
    return "Release"


def resolve_clients_meta(selected_patterns):
    env_clients = os.environ.get("BENCH_MULTI_CLIENTS", "").strip()
    if env_clients.isdigit():
        return env_clients

    if not selected_patterns or not all(is_multi_pattern(p) for p in selected_patterns):
        return ""

    stream_default = parse_env_int("BENCH_MULTI_DEFAULT_STREAM_CLIENTS", 10000)
    general_default = parse_env_int("BENCH_MULTI_DEFAULT_CLIENTS", 100)
    if len(selected_patterns) == 1 and selected_patterns[0] in STREAM_VARIANT_PATTERNS:
        return str(stream_default)
    if all(p in STREAM_VARIANT_PATTERNS for p in selected_patterns):
        return str(stream_default)
    return str(general_default)


def build_meta_items(mode, num_runs, selected_patterns):
    meta_items = []
    meta_items.append(("os", get_os_label()))
    meta_items.append(("cpu", get_cpu_model()))
    meta_items.append(("cores", str(os.cpu_count() or 0)))
    meta_items.append(("build", detect_build_type(BUILD_DIR)))
    meta_items.append(("commit", get_commit_short_sha()))
    local_now = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
    meta_items.append(("timestamp", local_now))
    load_avg = get_load_avg()
    if load_avg:
        meta_items.append(("load_avg", load_avg))
    meta_items.append(("mode", mode))
    meta_items.append(("runs", str(num_runs)))
    clients = resolve_clients_meta(selected_patterns)
    if clients:
        meta_items.append(("clients", clients))
    return meta_items


def print_meta_lines(meta_items):
    for key, value in meta_items:
        print(f"META,{key},{value}")


def resolve_results_root(configured=""):
    if configured:
        return os.path.abspath(configured)
    env_configured = os.environ.get("BENCH_RESULTS_DIR", "").strip()
    if env_configured:
        return os.path.abspath(env_configured)
    return os.path.join(SCRIPT_DIR, "results")


def resolve_multi_results_dirs(results_root):
    multi_root = os.path.join(results_root, "multi")
    return {
        "root": multi_root,
        "tmp": os.path.join(multi_root, "tmp"),
        "report": os.path.join(multi_root, "report"),
        "baseline": os.path.join(multi_root, "baseline"),
    }


def resolve_results_max_files():
    raw = os.environ.get("BENCH_RESULTS_MAX_FILES", "").strip()
    if not raw:
        return 100
    try:
        value = int(raw)
    except ValueError:
        return 100
    if value < 1:
        return 100
    return value


def prune_result_files(dir_path, max_files, exclude_names=None):
    if max_files < 1 or not os.path.isdir(dir_path):
        return

    excludes = set(exclude_names or [])
    candidates = []
    for entry in os.listdir(dir_path):
        if entry in excludes:
            continue
        if not entry.endswith(".txt"):
            continue
        path = os.path.join(dir_path, entry)
        if not os.path.isfile(path):
            continue
        candidates.append(entry)

    if len(candidates) <= max_files:
        return

    candidates.sort()
    remove_count = len(candidates) - max_files
    for entry in candidates[:remove_count]:
        path = os.path.join(dir_path, entry)
        try:
            os.remove(path)
        except OSError:
            continue


def parse_result_file(path, multi_only=False):
    meta = {}
    results = {}
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            for raw_line in f:
                line = raw_line.strip()
                if not line:
                    continue
                if line.startswith("META,"):
                    parts = line.split(",", 2)
                    if len(parts) == 3:
                        meta[parts[1]] = parts[2]
                    continue
                if not line.startswith("RESULT,"):
                    continue
                parts = line.split(",")
                if len(parts) < 7:
                    continue
                pattern = parts[2].strip().upper()
                if multi_only and not is_multi_pattern(pattern):
                    continue
                transport = parts[3].strip()
                metric = parts[5].strip().lower()
                if metric not in ("throughput", "bandwidth", "latency"):
                    continue
                try:
                    size = int(parts[4])
                    value = float(parts[6])
                except ValueError:
                    continue
                results[(pattern, transport, size, metric)] = value
    except OSError:
        pass
    return meta, results


def collect_result_files(result_dir):
    files = []
    if not os.path.isdir(result_dir):
        return files

    for entry in os.listdir(result_dir):
        if not entry.endswith(".txt"):
            continue
        path = os.path.join(result_dir, entry)
        if not os.path.isfile(path):
            continue
        files.append(path)

    files.sort(reverse=True)
    return files


def load_rolling_baseline(tmp_dir, rolling_n, keys_of_interest):
    samples = {key: [] for key in keys_of_interest}
    if not samples:
        return {}, {}

    result_files = collect_result_files(tmp_dir)[: max(1, int(rolling_n))]
    for path in result_files:
        meta, results = parse_result_file(path, multi_only=True)
        if meta.get("status", "").strip().lower() != "complete":
            continue
        if not results:
            continue
        for key, value in results.items():
            if key not in samples:
                continue
            samples[key].append(value)

    baseline = {}
    counts = {}
    for key, vals in samples.items():
        if vals:
            baseline[key] = statistics.median(vals)
        if vals:
            counts[key] = len(vals)
    return baseline, counts


def resolve_fixed_baseline_path(baseline_dir, explicit_path):
    if explicit_path:
        return os.path.abspath(explicit_path)
    return os.path.join(baseline_dir, "latest.txt")


def load_threshold_rules():
    path = os.environ.get("BENCH_THRESHOLDS_FILE", "").strip() or DEFAULT_THRESHOLDS_FILE
    if not os.path.isfile(path):
        return [], []

    warnings = []
    try:
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        warnings.append(f"threshold file parse failed ({path}): {exc}; using defaults")
        return [], warnings

    if not isinstance(data, dict):
        warnings.append(f"threshold file must be a JSON object: {path}; using defaults")
        return [], warnings

    rules = []
    for order, (raw_key, raw_value) in enumerate(data.items()):
        if not isinstance(raw_key, str):
            warnings.append(f"ignored invalid threshold key (non-string): {raw_key!r}")
            continue
        parts = raw_key.split("/")
        if len(parts) != 3:
            warnings.append(f"ignored invalid threshold key format: {raw_key}")
            continue
        pattern = parts[0].strip().upper()
        transport = parts[1].strip().lower()
        metric = parts[2].strip().lower()
        if metric not in ("throughput", "bandwidth", "latency"):
            warnings.append(f"ignored invalid threshold metric: {raw_key}")
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

        warn_value = None
        fail_value = None
        if "warning" in raw_value:
            try:
                warn_value = float(raw_value["warning"])
            except (TypeError, ValueError):
                warnings.append(f"ignored threshold key due to non-numeric warning: {raw_key}")
                continue
            warn_value, sign_warning = normalize_threshold_value(metric, "warning", warn_value)
            if sign_warning:
                warnings.append(f"{raw_key}: {sign_warning}")
        if "fail" in raw_value:
            try:
                fail_value = float(raw_value["fail"])
            except (TypeError, ValueError):
                warnings.append(f"ignored threshold key due to non-numeric fail: {raw_key}")
                continue
            fail_value, sign_warning = normalize_threshold_value(metric, "fail", fail_value)
            if sign_warning:
                warnings.append(f"{raw_key}: {sign_warning}")

        if warn_value is None and fail_value is None:
            continue
        rules.append(
            {
                "pattern": pattern,
                "transport": transport,
                "metric": metric,
                "warning": warn_value,
                "fail": fail_value,
                "rank": threshold_rank(pattern, transport),
                "order": order,
            }
        )

    rules.sort(key=lambda item: (item["rank"], item["order"]))
    return rules, warnings


def resolve_threshold_for_key(rules, pattern, transport, metric, defaults):
    if metric in ("throughput", "bandwidth"):
        warning = defaults["warn_throughput"]
        fail = defaults["fail_throughput"]
    else:
        warning = defaults["warn_latency"]
        fail = defaults["fail_latency"]

    p = pattern.upper()
    t = transport.lower()
    for rule in rules:
        if rule["metric"] != metric:
            continue
        if rule["pattern"] not in (p, "*"):
            continue
        if rule["transport"] not in (t, "*"):
            continue
        if rule["warning"] is not None:
            warning = rule["warning"]
        if rule["fail"] is not None:
            fail = rule["fail"]
        return warning, fail
    return warning, fail


def compare_against_baseline(current_results, baseline_results, mode, thresholds, threshold_rules):
    warnings = []
    failures = []
    missing = []

    for key in sorted(current_results.keys()):
        current_value = current_results[key]
        baseline_value = baseline_results.get(key)
        metric = key[3]
        if metric not in ("throughput", "bandwidth", "latency"):
            continue
        if baseline_value is None or baseline_value <= 0:
            missing.append(key)
            continue

        pattern, transport, size, metric = key
        delta_pct = ((current_value - baseline_value) / baseline_value) * 100.0
        warn_limit, fail_limit = resolve_threshold_for_key(
            threshold_rules, pattern, transport, metric, thresholds
        )

        if metric in ("throughput", "bandwidth"):
            warn_hit = delta_pct <= warn_limit
            fail_hit = delta_pct <= fail_limit
            regression_pct = -delta_pct
        else:
            warn_hit = delta_pct >= warn_limit
            fail_hit = delta_pct >= fail_limit
            regression_pct = delta_pct

        if not warn_hit:
            continue

        row = {
            "pattern": pattern,
            "transport": transport,
            "size": size,
            "metric": metric,
            "current": current_value,
            "baseline": baseline_value,
            "regression_pct": regression_pct,
            "warn_limit": warn_limit,
            "fail_limit": fail_limit,
        }

        if mode == MODE_GATE and fail_hit:
            failures.append(row)
        else:
            warnings.append(row)

    return warnings, failures, missing


def sanitize_baseline_version(version):
    clean = version.strip()
    clean = clean.replace("/", "_")
    clean = clean.replace("\\", "_")
    clean = clean.replace(" ", "_")
    return clean


def write_machine_result_file(path, meta_items, result_map, table_lines):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp_path = f"{path}.tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        for key, value in meta_items:
            f.write(f"META,{key},{value}\n")
        for key in sorted(result_map.keys()):
            pattern, transport, size, metric = key
            value = result_map[key]
            f.write(
                f"RESULT,current,{pattern},{transport},{size},{metric},{value:.2f}\n"
            )
        f.write("TABLE\n")
        for line in table_lines:
            f.write(f"{line}\n")
    os.replace(tmp_path, path)


def write_report_table_file(path, table_lines):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp_path = f"{path}.tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        for line in table_lines:
            f.write(f"{line}\n")
    os.replace(tmp_path, path)


def update_latest_baseline_pointer(target_path, baseline_dir):
    os.makedirs(baseline_dir, exist_ok=True)
    latest_path = os.path.join(baseline_dir, "latest.txt")

    if IS_WINDOWS:
        shutil.copyfile(target_path, latest_path)
        return

    rel_name = os.path.basename(target_path)
    tmp_link = f"{latest_path}.tmp"
    try:
        if os.path.lexists(tmp_link):
            os.unlink(tmp_link)
        os.symlink(rel_name, tmp_link)
        os.replace(tmp_link, latest_path)
    except OSError:
        # Fallback for filesystems without symlink support.
        if os.path.lexists(tmp_link):
            os.unlink(tmp_link)
        shutil.copyfile(target_path, latest_path)


def save_fixed_baseline(baseline_dir, version, meta_items, current_results, table_lines):
    clean_version = sanitize_baseline_version(version)
    if not clean_version:
        raise ValueError("baseline version is empty")

    baseline_path = os.path.join(baseline_dir, f"{clean_version}.txt")
    write_machine_result_file(baseline_path, meta_items, current_results, table_lines)
    update_latest_baseline_pointer(baseline_path, baseline_dir)
    return baseline_path, len(current_results)


def build_failure_lookup(failures, pattern_name):
    lookup = {}
    for pattern, _lib_name, transport, size, reason in failures:
        if pattern != pattern_name:
            continue
        lookup.setdefault((transport, size), set()).add(reason)
    return lookup


def format_compare_row(row):
    return (
        f"- {row['pattern']} {row['transport']} {row['size']}B {row['metric']}: "
        f"current={row['current']:.2f}, baseline={row['baseline']:.2f}, "
        f"regression={row['regression_pct']:.2f}% "
        f"(warn={row['warn_limit']:.2f}%, fail={row['fail_limit']:.2f}%)"
    )


def build_result_filename(tag=""):
    ts = datetime.datetime.now().astimezone().strftime("%Y%m%d_%H%M%S")
    platform_tag = platform.system().strip().lower() or "unknown"
    if platform_tag.startswith("darwin"):
        platform_tag = "macos"
    elif platform_tag.startswith("windows"):
        platform_tag = "windows"
    elif platform_tag.startswith("linux"):
        platform_tag = "linux"
    name = f"bench_{platform_tag}_{ts}"
    clean_tag = re.sub(r"[^A-Za-z0-9._-]+", "_", (tag or "").strip())
    if clean_tag:
        name = f"{name}_{clean_tag}"
    return f"{name}.txt"


def build_result_basename(tag=""):
    filename = build_result_filename(tag)
    if filename.endswith(".txt"):
        return filename[:-4]
    return filename


def emit_result_lines(result_map):
    for key in sorted(result_map.keys()):
        pattern, transport, size, metric = key
        value = result_map[key]
        print(f"RESULT,current,{pattern},{transport},{size},{metric},{value:.2f}")


def parse_args():
    usage = (
        "Usage: run_comparison.py [PATTERN] [options]\n\n"
        "Measure current zlink benchmarks.\n\n"
        "Note: PATTERN=ALL includes STREAM and MULTI_* patterns by default.\n\n"
        "Options:\n"
        "  --runs N                     Iterations per configuration (default: 1)\n"
        "  --build-dir PATH             Build directory (default: core/build/<platform>-<arch>)\n"
        "  --pin-cpu                    Pin CPU core during benchmarks (Linux taskset)\n"
        "  --mode MODE                  observe|trend|gate (default: observe)\n"
        "  --rolling-n N                Rolling baseline window (default: 10)\n"
        "  --results-dir PATH           Results root directory (default: core/perf/results)\n"
        "  --results-tag NAME           Optional suffix tag for saved filenames\n"
        "  --result-file PATH           Explicit tmp result file path\n"
        "  --result                     Save report result when status is complete\n"
        "  --save [VERSION]             Save fixed baseline (VERSION omitted => timestamp filename)\n"
        "  --baseline-file PATH         Fixed baseline override path for gate mode\n"
        "  --warn-throughput-pct N      Throughput warning drop threshold (default: 10)\n"
        "  --fail-throughput-pct N      Throughput fail drop threshold (default: 15)\n"
        "  --warn-latency-pct N         Latency warning increase threshold (default: 10)\n"
        "  --fail-latency-pct N         Latency fail increase threshold (default: 15)\n"
        "  --multi-transport-transition-ms N  Transport transition cooldown (ms)\n"
        "  --multi-pattern-transition-ms N    Pattern transition cooldown (ms)\n"
        "  --multi-server-ready-timeout-ms N  Server READY wait timeout (ms)\n"
        "  --multi-server-shutdown-timeout-ms N  Server shutdown wait timeout (ms)\n"
        "  --multi-server-bind-port N    Server bind port (0=auto)\n"
        "  -h, --help                   Show this help\n"
        "  Missing benchmark binaries are auto-built via cmake --build.\n"
    )
    p_req = "ALL"
    num_runs = DEFAULT_NUM_RUNS
    build_dir = ""
    pin_cpu = False

    mode = os.environ.get("BENCH_MODE", MODE_OBSERVE).strip().lower()
    rolling_n = max(1, parse_env_int("BENCH_ROLLING_N", 10))
    results_dir = os.environ.get("BENCH_RESULTS_DIR", "").strip()
    results_tag = os.environ.get("BENCH_RESULTS_TAG", "").strip()
    result_file = ""
    save_report = False
    save_baseline_version = os.environ.get("BENCH_SAVE_BASELINE", "").strip()
    save_baseline_requested = bool(save_baseline_version)
    baseline_file = os.environ.get("BENCH_BASELINE_FILE", "").strip()
    warn_throughput_pct = parse_env_float(
        "BENCH_WARN_THROUGHPUT_PCT", DEFAULT_WARN_THROUGHPUT_PCT
    )
    fail_throughput_pct = parse_env_float(
        "BENCH_FAIL_THROUGHPUT_PCT", DEFAULT_FAIL_THROUGHPUT_PCT
    )
    warn_latency_pct = parse_env_float("BENCH_WARN_LATENCY_PCT", DEFAULT_WARN_LATENCY_PCT)
    fail_latency_pct = parse_env_float("BENCH_FAIL_LATENCY_PCT", DEFAULT_FAIL_LATENCY_PCT)
    transport_transition_ms = max(
        0, parse_env_int("BENCH_MULTI_TRANSPORT_TRANSITION_MS", 3000)
    )
    pattern_transition_ms = max(
        0, parse_env_int("BENCH_MULTI_PATTERN_TRANSITION_MS", 3000)
    )
    server_ready_timeout_ms = max(
        0, parse_env_int("BENCH_MULTI_SERVER_READY_TIMEOUT_MS", 10000)
    )
    server_shutdown_timeout_ms = max(
        0, parse_env_int("BENCH_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS", 5000)
    )
    server_bind_port = max(0, parse_env_int("BENCH_MULTI_SERVER_BIND_PORT", 0))

    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg in ("-h", "--help"):
            print(usage)
            sys.exit(0)
        if arg == "--pin-cpu":
            pin_cpu = True
        elif arg == "--runs":
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
        elif arg == "--mode":
            if i + 1 >= len(sys.argv):
                print("Error: --mode requires a value.", file=sys.stderr)
                sys.exit(1)
            mode = sys.argv[i + 1].strip().lower()
            i += 1
        elif arg == "--rolling-n":
            if i + 1 >= len(sys.argv):
                print("Error: --rolling-n requires a value.", file=sys.stderr)
                sys.exit(1)
            try:
                rolling_n = int(sys.argv[i + 1])
            except ValueError:
                print("Error: --rolling-n must be an integer.", file=sys.stderr)
                sys.exit(1)
            i += 1
        elif arg == "--results-dir":
            if i + 1 >= len(sys.argv):
                print("Error: --results-dir requires a value.", file=sys.stderr)
                sys.exit(1)
            results_dir = sys.argv[i + 1].strip()
            i += 1
        elif arg == "--results-tag":
            if i + 1 >= len(sys.argv):
                print("Error: --results-tag requires a value.", file=sys.stderr)
                sys.exit(1)
            results_tag = sys.argv[i + 1].strip()
            i += 1
        elif arg.startswith("--results-tag="):
            results_tag = arg.split("=", 1)[1].strip()
        elif arg == "--result-file":
            if i + 1 >= len(sys.argv):
                print("Error: --result-file requires a value.", file=sys.stderr)
                sys.exit(1)
            result_file = sys.argv[i + 1].strip()
            i += 1
        elif arg == "--result" or arg == "--save-report":
            save_report = True
        elif arg == "--save":
            save_baseline_requested = True
            if i + 1 < len(sys.argv) and not sys.argv[i + 1].startswith("--"):
                save_baseline_version = sys.argv[i + 1].strip()
                i += 1
        elif arg.startswith("--save="):
            save_baseline_requested = True
            save_baseline_version = arg.split("=", 1)[1].strip()
        elif arg == "--save-baseline":
            print(
                "Error: --save-baseline is removed. Use --save [VERSION].",
                file=sys.stderr,
            )
            sys.exit(1)
        elif arg == "--baseline-file":
            if i + 1 >= len(sys.argv):
                print("Error: --baseline-file requires a value.", file=sys.stderr)
                sys.exit(1)
            baseline_file = sys.argv[i + 1].strip()
            i += 1
        elif arg == "--warn-throughput-pct":
            if i + 1 >= len(sys.argv):
                print("Error: --warn-throughput-pct requires a value.", file=sys.stderr)
                sys.exit(1)
            try:
                warn_throughput_pct = float(sys.argv[i + 1])
            except ValueError:
                print("Error: --warn-throughput-pct must be a number.", file=sys.stderr)
                sys.exit(1)
            i += 1
        elif arg == "--fail-throughput-pct":
            if i + 1 >= len(sys.argv):
                print("Error: --fail-throughput-pct requires a value.", file=sys.stderr)
                sys.exit(1)
            try:
                fail_throughput_pct = float(sys.argv[i + 1])
            except ValueError:
                print("Error: --fail-throughput-pct must be a number.", file=sys.stderr)
                sys.exit(1)
            i += 1
        elif arg == "--warn-latency-pct":
            if i + 1 >= len(sys.argv):
                print("Error: --warn-latency-pct requires a value.", file=sys.stderr)
                sys.exit(1)
            try:
                warn_latency_pct = float(sys.argv[i + 1])
            except ValueError:
                print("Error: --warn-latency-pct must be a number.", file=sys.stderr)
                sys.exit(1)
            i += 1
        elif arg == "--fail-latency-pct":
            if i + 1 >= len(sys.argv):
                print("Error: --fail-latency-pct requires a value.", file=sys.stderr)
                sys.exit(1)
            try:
                fail_latency_pct = float(sys.argv[i + 1])
            except ValueError:
                print("Error: --fail-latency-pct must be a number.", file=sys.stderr)
                sys.exit(1)
            i += 1
        elif arg == "--multi-transport-transition-ms":
            if i + 1 >= len(sys.argv):
                print(
                    "Error: --multi-transport-transition-ms requires a value.",
                    file=sys.stderr,
                )
                sys.exit(1)
            try:
                transport_transition_ms = int(sys.argv[i + 1])
            except ValueError:
                print(
                    "Error: --multi-transport-transition-ms must be an integer.",
                    file=sys.stderr,
                )
                sys.exit(1)
            i += 1
        elif arg == "--multi-pattern-transition-ms":
            if i + 1 >= len(sys.argv):
                print(
                    "Error: --multi-pattern-transition-ms requires a value.",
                    file=sys.stderr,
                )
                sys.exit(1)
            try:
                pattern_transition_ms = int(sys.argv[i + 1])
            except ValueError:
                print(
                    "Error: --multi-pattern-transition-ms must be an integer.",
                    file=sys.stderr,
                )
                sys.exit(1)
            i += 1
        elif arg == "--multi-server-ready-timeout-ms":
            if i + 1 >= len(sys.argv):
                print(
                    "Error: --multi-server-ready-timeout-ms requires a value.",
                    file=sys.stderr,
                )
                sys.exit(1)
            try:
                server_ready_timeout_ms = int(sys.argv[i + 1])
            except ValueError:
                print(
                    "Error: --multi-server-ready-timeout-ms must be an integer.",
                    file=sys.stderr,
                )
                sys.exit(1)
            i += 1
        elif arg == "--multi-server-shutdown-timeout-ms":
            if i + 1 >= len(sys.argv):
                print(
                    "Error: --multi-server-shutdown-timeout-ms requires a value.",
                    file=sys.stderr,
                )
                sys.exit(1)
            try:
                server_shutdown_timeout_ms = int(sys.argv[i + 1])
            except ValueError:
                print(
                    "Error: --multi-server-shutdown-timeout-ms must be an integer.",
                    file=sys.stderr,
                )
                sys.exit(1)
            i += 1
        elif arg == "--multi-server-bind-port":
            if i + 1 >= len(sys.argv):
                print(
                    "Error: --multi-server-bind-port requires a value.",
                    file=sys.stderr,
                )
                sys.exit(1)
            try:
                server_bind_port = int(sys.argv[i + 1])
            except ValueError:
                print(
                    "Error: --multi-server-bind-port must be an integer.",
                    file=sys.stderr,
                )
                sys.exit(1)
            i += 1
        elif arg.startswith("--"):
            print(f"Error: unknown option: {arg}", file=sys.stderr)
            print("Use --help to see available options.", file=sys.stderr)
            sys.exit(1)
        elif not arg.startswith("--") and p_req == "ALL":
            p_req = arg.upper()
        elif not arg.startswith("--"):
            print(f"Error: unexpected positional argument: {arg}", file=sys.stderr)
            print("Use comma-separated PATTERN in a single positional arg.", file=sys.stderr)
            sys.exit(1)
        i += 1

    if num_runs < 1:
        print("Error: --runs must be >= 1.", file=sys.stderr)
        sys.exit(1)
    if rolling_n < 1:
        print("Error: --rolling-n must be >= 1.", file=sys.stderr)
        sys.exit(1)
    if mode not in VALID_MODES:
        print("Error: --mode must be one of observe|trend|gate.", file=sys.stderr)
        sys.exit(1)

    for key, value in (
        ("warn-throughput-pct", warn_throughput_pct),
        ("fail-throughput-pct", fail_throughput_pct),
        ("warn-latency-pct", warn_latency_pct),
        ("fail-latency-pct", fail_latency_pct),
    ):
        if value < 0:
            print(f"Error: --{key} must be >= 0.", file=sys.stderr)
            sys.exit(1)
    for key, value in (
        ("multi-transport-transition-ms", transport_transition_ms),
        ("multi-pattern-transition-ms", pattern_transition_ms),
        ("multi-server-ready-timeout-ms", server_ready_timeout_ms),
        ("multi-server-shutdown-timeout-ms", server_shutdown_timeout_ms),
    ):
        if value < 0:
            print(f"Error: --{key} must be >= 0.", file=sys.stderr)
            sys.exit(1)
    if server_bind_port < 0 or server_bind_port > 65535:
        print("Error: --multi-server-bind-port must be in range 0..65535.", file=sys.stderr)
        sys.exit(1)

    return {
        "pattern_request": p_req,
        "num_runs": num_runs,
        "build_dir": build_dir,
        "pin_cpu": pin_cpu,
        "mode": mode,
        "rolling_n": rolling_n,
        "results_dir": results_dir,
        "results_tag": results_tag,
        "result_file": result_file,
        "save_report": save_report,
        "save_baseline_requested": save_baseline_requested,
        "save_baseline_version": save_baseline_version,
        "baseline_file": baseline_file,
        "warn_throughput_pct": warn_throughput_pct,
        "fail_throughput_pct": fail_throughput_pct,
        "warn_latency_pct": warn_latency_pct,
        "fail_latency_pct": fail_latency_pct,
        "transport_transition_ms": transport_transition_ms,
        "pattern_transition_ms": pattern_transition_ms,
        "server_ready_timeout_ms": server_ready_timeout_ms,
        "server_shutdown_timeout_ms": server_shutdown_timeout_ms,
        "server_bind_port": server_bind_port,
    }


def main():
    global BUILD_DIR, CURRENT_LIB_DIR, base_env

    args = parse_args()
    p_req = args["pattern_request"]
    num_runs = args["num_runs"]
    build_dir = args["build_dir"]
    pin_cpu = args["pin_cpu"]

    if build_dir:
        BUILD_DIR = normalize_build_dir(build_dir)
    else:
        BUILD_DIR = normalize_build_dir(BUILD_DIR)

    CURRENT_LIB_DIR = derive_current_lib_dir(BUILD_DIR)
    if pin_cpu:
        base_env["BENCH_TASKSET"] = "1"
        base_env["PERF_TASKSET"] = "1"
        os.environ["BENCH_TASKSET"] = "1"
        os.environ["PERF_TASKSET"] = "1"
    timeout_env_values = {
        "BENCH_MULTI_SERVER_READY_TIMEOUT_MS": str(args["server_ready_timeout_ms"]),
        "PERF_MULTI_SERVER_READY_TIMEOUT_MS": str(args["server_ready_timeout_ms"]),
        "BENCH_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS": str(
            args["server_shutdown_timeout_ms"]
        ),
        "PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS": str(
            args["server_shutdown_timeout_ms"]
        ),
        "BENCH_MULTI_SERVER_BIND_PORT": str(args["server_bind_port"]),
        "PERF_MULTI_SERVER_BIND_PORT": str(args["server_bind_port"]),
    }
    for env_key, env_value in timeout_env_values.items():
        base_env[env_key] = env_value
        os.environ[env_key] = env_value
    sync_perf_bench_aliases(base_env)

    comparisons = [
        ("perf_pair", "PAIR"),
        ("perf_pubsub", "PUBSUB"),
        ("perf_dealer_dealer", "DEALER_DEALER"),
        ("perf_dealer_router", "DEALER_ROUTER"),
        ("perf_router_router", "ROUTER_ROUTER"),
        ("perf_router_router_poll", "ROUTER_ROUTER_POLL"),
        ("perf_stream", "STREAM"),
        ("comp_current_multi_dealer_dealer_client", "MULTI_DEALER_DEALER"),
        ("comp_current_multi_dealer_router_client", "MULTI_DEALER_ROUTER"),
        ("comp_current_multi_router_router_client", "MULTI_ROUTER_ROUTER"),
        ("comp_current_multi_pubsub_client", "MULTI_PUBSUB"),
        ("comp_current_multi_gateway_client", "MULTI_GATEWAY"),
        ("comp_current_multi_spot_client", "MULTI_SPOT"),
        ("comp_current_multi_stream_client", "MULTI_STREAM"),
        ("comp_current_multi_stream_callback_client", "MULTI_STREAM_CALLBACK"),
        ("comp_current_multi_stream_len32be_client", "MULTI_STREAM_LEN32BE"),
        ("perf_gateway", "GATEWAY"),
        ("perf_spot", "SPOT"),
    ]

    if p_req == "ALL":
        requested = None
    else:
        requested = {p.strip().upper() for p in p_req.split(",") if p.strip()}
        if not requested:
            print("Error: --pattern requires at least one value.", file=sys.stderr)
            sys.exit(1)

    selected_patterns = [
        p_name for _, p_name in comparisons if requested is None or p_name in requested
    ]
    multi_only_run = bool(selected_patterns) and all(
        is_multi_pattern(p) for p in selected_patterns
    )

    mode = args["mode"]
    if not multi_only_run and mode != MODE_OBSERVE:
        print(
            f"Warning: mode={mode} applies to MULTI_* runs only. Falling back to observe.",
            file=sys.stderr,
        )
        mode = MODE_OBSERVE

    results_root = resolve_results_root(args["results_dir"])
    results_layout = resolve_multi_results_dirs(results_root)
    results_dir = results_layout["root"]
    meta_items = build_meta_items(mode, num_runs, selected_patterns)
    print_meta_lines(meta_items)

    missing_current = collect_missing_patterns(comparisons, requested)
    skip_auto_build = (
        os.environ.get("BENCH_SKIP_AUTO_BUILD", "0") == "1"
        or os.environ.get("BENCH_NO_AUTOBUILD", "0") == "1"
    )
    if missing_current and not skip_auto_build:
        cmake_build_dir = derive_cmake_build_dir(BUILD_DIR)
        if not cmake_build_dir:
            print(
                f"Error: missing benchmark binaries in {BUILD_DIR} and failed to derive CMake build dir.",
                file=sys.stderr,
            )
            print(
                "Hint: pass --build-dir as a CMake build root or its bin(/Release) directory.",
                file=sys.stderr,
            )
            if missing_current:
                print("  current missing: " + ", ".join(missing_current), file=sys.stderr)
            sys.exit(1)

        build_targets = collect_missing_build_targets(comparisons, requested)
        build_rc = run_cmake_build(cmake_build_dir, build_targets)
        if build_rc != 0:
            print(f"Error: auto-build failed with exit code {build_rc}.", file=sys.stderr)
            sys.exit(build_rc)

    missing_current = collect_missing_patterns(comparisons, requested)

    if missing_current:
        if skip_auto_build:
            print(
                "Error: benchmark binaries are missing and auto-build is disabled "
                "(BENCH_SKIP_AUTO_BUILD=1 or BENCH_NO_AUTOBUILD=1).",
                file=sys.stderr,
            )
        print(
            "Error: current benchmark binaries are missing for patterns: "
            + ", ".join(missing_current),
            file=sys.stderr,
        )
        if not skip_auto_build:
            print(
                "Auto-build was attempted but some current binaries are still missing.",
                file=sys.stderr,
            )
        sys.exit(1)

    all_failures = []
    all_skips = []
    status_counts = {"success": 0, "unsupported": 0, "skip": 0, "fail": 0}
    current_results = {}
    expected_result_lines = 0
    actual_result_lines = 0
    table_lines = []

    selected_comparisons = [
        (current_bin, p_name)
        for current_bin, p_name in comparisons
        if requested is None or p_name in requested
    ]
    pattern_transition_ms = args["pattern_transition_ms"] if multi_only_run else 0

    for pattern_idx, (current_bin, p_name) in enumerate(selected_comparisons):
        if table_lines:
            print("")
            print(PATTERN_SEPARATOR)
            print("")
            table_lines.extend(["", PATTERN_SEPARATOR, ""])

        pattern_header = f"## PATTERN: {p_name} ({pattern_direction_label(p_name)})"
        print(pattern_header)
        table_lines.append(pattern_header)

        pattern_transports = select_transports(p_name)
        if not pattern_transports:
            print(f"  Skipping {p_name}: no matching transports.")
            all_skips.append((p_name, "no_matching_transports"))
            status_counts["skip"] += 1
            continue

        c_stats, failures = collect_data(
            current_bin, "current", p_name, num_runs, pattern_transports
        )
        all_failures.extend(failures)
        failure_lookup = build_failure_lookup(failures, p_name)

        # Print table and classify statuses.
        size_w = 8
        tp_w = 16
        bw_w = 12
        lat_w = 12
        cpu_w = 6
        mem_w = 8
        is_multi = is_multi_pattern(p_name)
        sizes = MULTI_STREAM_MSG_SIZES if p_name in STREAM_VARIANT_PATTERNS else MSG_SIZES
        for tr in pattern_transports:
            transport_header = f"### Transport: {tr}"
            print(f"\n{transport_header}")
            table_lines.append("")
            table_lines.append(transport_header)
            if is_multi:
                table_header = (
                    f"| {'Size':<{size_w}} | {'Throughput':>{tp_w}} | {'Bandwidth':>{bw_w}} | {'Latency':>{lat_w}} | {'C.CPU%':>{cpu_w}} | {'C.Mem MB':>{mem_w}} | {'S.CPU%':>{cpu_w}} | {'S.Mem MB':>{mem_w}} |"
                )
                table_sep = (
                    f"|{'-' * (size_w + 2)}|{'-' * (tp_w + 2)}|{'-' * (bw_w + 2)}|{'-' * (lat_w + 2)}|{'-' * (cpu_w + 2)}|{'-' * (mem_w + 2)}|{'-' * (cpu_w + 2)}|{'-' * (mem_w + 2)}|"
                )
            else:
                table_header = (
                    f"| {'Size':<{size_w}} | {'Throughput':>{tp_w}} | {'Bandwidth':>{bw_w}} | {'Latency':>{lat_w}} | {'CPU%':>{cpu_w}} | {'Mem MB':>{mem_w}} |"
                )
                table_sep = (
                    f"|{'-' * (size_w + 2)}|{'-' * (tp_w + 2)}|{'-' * (bw_w + 2)}|{'-' * (lat_w + 2)}|{'-' * (cpu_w + 2)}|{'-' * (mem_w + 2)}|"
                )
            print(table_header)
            print(table_sep)
            table_lines.append(table_header)
            table_lines.append(table_sep)
            for sz in sizes:
                unsupported = bool(c_stats.get(f"{tr}|{sz}|unsupported", 0))
                skipped = bool(c_stats.get(f"{tr}|{sz}|skip", 0))
                reasons = set(failure_lookup.get((tr, sz), set()))

                tp_key = f"{tr}|{sz}|throughput"
                bw_key = f"{tr}|{sz}|bandwidth"
                lat_key = f"{tr}|{sz}|latency"
                cpu_key = f"{tr}|{sz}|client_cpu_pct" if is_multi else f"{tr}|{sz}|cpu_pct"
                mem_key = f"{tr}|{sz}|client_mem_mb" if is_multi else f"{tr}|{sz}|mem_mb"
                server_cpu_key = f"{tr}|{sz}|server_cpu_pct"
                server_mem_key = f"{tr}|{sz}|server_mem_mb"
                has_tp = tp_key in c_stats
                has_bw = bw_key in c_stats
                has_lat = lat_key in c_stats
                if not unsupported and not skipped and (not has_tp or not has_bw or not has_lat):
                    reasons.add("missing_result")

                if unsupported:
                    status = "unsupported"
                    tp_s = "N/A"
                    bw_s = "N/A"
                    lat_s = "N/A"
                    cpu_s = "N/A"
                    mem_s = "N/A"
                    server_cpu_s = "N/A"
                    server_mem_s = "N/A"
                elif skipped:
                    status = "skip"
                    tp_s = "N/A"
                    bw_s = "N/A"
                    lat_s = "N/A"
                    cpu_s = "N/A"
                    mem_s = "N/A"
                    server_cpu_s = "N/A"
                    server_mem_s = "N/A"
                    skip_reason = c_stats.get(f"{tr}|{sz}|skip_reason", "skip")
                    all_skips.append((f"{p_name} {tr} {sz}B", str(skip_reason)))
                elif reasons:
                    status = "fail"
                    tp_s = "FAIL"
                    bw_s = "FAIL"
                    lat_s = "FAIL"
                    cpu_s = "N/A"
                    mem_s = "N/A"
                    server_cpu_s = "N/A"
                    server_mem_s = "N/A"
                    expected_result_lines += 3
                else:
                    status = "success"
                    ct = c_stats.get(tp_key, 0)
                    cb = c_stats.get(bw_key, 0)
                    cl = c_stats.get(lat_key, 0)
                    current_results[(p_name, tr, sz, "throughput")] = ct
                    current_results[(p_name, tr, sz, "bandwidth")] = cb
                    current_results[(p_name, tr, sz, "latency")] = cl
                    tp_s = format_throughput(p_name, ct)
                    bw_s = format_bandwidth(cb)
                    lat_s = f"{cl:8.2f} us"
                    expected_result_lines += 3
                    actual_result_lines += 3

                    if cpu_key in c_stats:
                        cpu_v = c_stats[cpu_key]
                        current_metric = "client_cpu_pct" if is_multi else "cpu_pct"
                        current_results[(p_name, tr, sz, current_metric)] = cpu_v
                        cpu_s = f"{cpu_v:4.1f}"
                    else:
                        cpu_s = "N/A"

                    if mem_key in c_stats:
                        mem_v = c_stats[mem_key]
                        current_metric = "client_mem_mb" if is_multi else "mem_mb"
                        current_results[(p_name, tr, sz, current_metric)] = mem_v
                        mem_s = f"{mem_v:6.1f}"
                    else:
                        mem_s = "N/A"

                    if is_multi and server_cpu_key in c_stats:
                        server_cpu_v = c_stats[server_cpu_key]
                        current_results[(p_name, tr, sz, "server_cpu_pct")] = server_cpu_v
                        server_cpu_s = f"{server_cpu_v:4.1f}"
                    else:
                        server_cpu_s = "N/A"
                    if is_multi and server_mem_key in c_stats:
                        server_mem_v = c_stats[server_mem_key]
                        current_results[(p_name, tr, sz, "server_mem_mb")] = server_mem_v
                        server_mem_s = f"{server_mem_v:6.1f}"
                    else:
                        server_mem_s = "N/A"

                status_counts[status] += 1
                if is_multi:
                    row_line = (
                        f"| {f'{sz}B':<{size_w}} | {tp_s:>{tp_w}} | {bw_s:>{bw_w}} | {lat_s:>{lat_w}} | {cpu_s:>{cpu_w}} | {mem_s:>{mem_w}} | {server_cpu_s:>{cpu_w}} | {server_mem_s:>{mem_w}} |"
                    )
                else:
                    row_line = (
                        f"| {f'{sz}B':<{size_w}} | {tp_s:>{tp_w}} | {bw_s:>{bw_w}} | {lat_s:>{lat_w}} | {cpu_s:>{cpu_w}} | {mem_s:>{mem_w}} |"
                    )
                print(row_line)
                table_lines.append(row_line)

        if pattern_transition_ms > 0 and (pattern_idx + 1) < len(selected_comparisons):
            print(f"[pattern-cooldown={pattern_transition_ms}ms]")
            time.sleep(pattern_transition_ms / 1000.0)

    if current_results:
        print("\n## Result Data")
        emit_result_lines(current_results)

    thresholds = {
        "warn_throughput": -abs(args["warn_throughput_pct"]),
        "fail_throughput": -abs(args["fail_throughput_pct"]),
        "warn_latency": abs(args["warn_latency_pct"]),
        "fail_latency": abs(args["fail_latency_pct"]),
    }
    threshold_rules, threshold_warnings = load_threshold_rules()
    if threshold_warnings:
        print("\n## Threshold Overrides")
        for warning in threshold_warnings:
            print(f"- {warning}")

    compare_results = {
        key: value
        for key, value in current_results.items()
        if key[3] in ("throughput", "bandwidth", "latency")
    }
    run_status = "complete" if expected_result_lines == actual_result_lines else "partial"

    compare_warnings = []
    compare_failures = []
    compare_missing = []
    gate_blockers = []
    save_baseline_errors = []

    if multi_only_run and mode in (MODE_TREND, MODE_GATE):
        baseline_results = {}
        if mode == MODE_TREND:
            baseline_results, counts = load_rolling_baseline(
                results_layout["tmp"], args["rolling_n"], set(compare_results.keys())
            )
            print("\n## Baseline")
            print(f"- mode: {mode}")
            print(
                f"- source: rolling median from results/multi/tmp (window={args['rolling_n']})"
            )
            if not baseline_results:
                print("- baseline: unavailable (no historical samples)")
            else:
                print(f"- baseline keys: {len(baseline_results)}")
                if counts:
                    min_count = min(counts.values())
                    max_count = max(counts.values())
                    print(f"- sample count range: {min_count}..{max_count}")
        else:
            baseline_path = resolve_fixed_baseline_path(
                results_layout["baseline"], args["baseline_file"]
            )
            print("\n## Baseline")
            print(f"- mode: {mode}")
            print(f"- source: {baseline_path}")
            if not os.path.isfile(baseline_path):
                gate_blockers.append(f"baseline_file_missing:{baseline_path}")
                print("- baseline: missing")
            else:
                _, baseline_results = parse_result_file(baseline_path, multi_only=True)
                print(f"- baseline keys: {len(baseline_results)}")

        if baseline_results and compare_results:
            compare_warnings, compare_failures, compare_missing = compare_against_baseline(
                compare_results, baseline_results, mode, thresholds, threshold_rules
            )
        elif mode == MODE_GATE and compare_results:
            compare_missing = sorted(compare_results.keys())

        if compare_warnings or compare_failures or compare_missing:
            print("\n## Baseline Comparison")
            if compare_warnings:
                print("### Warnings")
                for row in compare_warnings:
                    print(format_compare_row(row))
            if compare_failures:
                print("### Failures")
                for row in compare_failures:
                    print(format_compare_row(row))
            if compare_missing:
                print("### Missing Baseline Keys")
                for pattern, transport, size, metric in compare_missing:
                    print(f"- {pattern} {transport} {size}B {metric}")

        if mode == MODE_GATE:
            if compare_failures:
                gate_blockers.append(f"threshold_failures:{len(compare_failures)}")

    final_meta_items = list(meta_items)
    final_meta_items.append(("status", run_status))
    final_meta_items.append(("expected", str(expected_result_lines)))
    final_meta_items.append(("actual", str(actual_result_lines)))

    tmp_result_file = args["result_file"]
    if not tmp_result_file:
        tag = args["results_tag"]
        tmp_result_file = os.path.join(results_layout["tmp"], build_result_filename(tag))
    write_machine_result_file(tmp_result_file, final_meta_items, current_results, table_lines)
    max_files = resolve_results_max_files()
    prune_result_files(os.path.dirname(tmp_result_file), max_files)
    print(f"\nSaved tmp result file: {tmp_result_file}")

    if args["save_report"]:
        print("\n## Save Report")
        if run_status != "complete":
            print("- skipped: status is partial")
        else:
            tag = args["results_tag"]
            report_file = os.path.join(results_layout["report"], build_result_filename(tag))
            write_report_table_file(report_file, table_lines)
            prune_result_files(results_layout["report"], max_files)
            print(f"- saved: {report_file}")

    if args["save_baseline_requested"]:
        print("\n## Save Baseline")
        try:
            if run_status != "complete":
                raise ValueError("partial run cannot be saved as baseline")
            baseline_version = args["save_baseline_version"].strip()
            if not baseline_version:
                baseline_version = build_result_basename(args["results_tag"])
            baseline_path, saved_count = save_fixed_baseline(
                results_layout["baseline"],
                baseline_version,
                final_meta_items,
                current_results,
                table_lines,
            )
            prune_result_files(
                results_layout["baseline"],
                max_files,
                exclude_names={"latest.txt"},
            )
            print(f"- saved: {baseline_path}")
            print(f"- saved keys: {saved_count}")
            print(
                f"- latest pointer: {os.path.join(results_layout['baseline'], 'latest.txt')}"
            )
        except ValueError as exc:
            print(f"- error: {exc}", file=sys.stderr)
            gate_blockers.append(f"save_baseline_error:{exc}")
            save_baseline_errors.append(str(exc))
        except OSError as exc:
            print(f"- error: failed to save baseline: {exc}", file=sys.stderr)
            gate_blockers.append(f"save_baseline_error:{exc}")
            save_baseline_errors.append(str(exc))

    print("\n## Status Summary")
    print(f"- success: {status_counts['success']}")
    print(f"- unsupported: {status_counts['unsupported']}")
    print(f"- skip: {status_counts['skip']}")
    print(f"- fail: {status_counts['fail']}")
    print(f"- expected result lines: {expected_result_lines}")
    print(f"- actual result lines: {actual_result_lines}")
    print(f"- status: {run_status}")

    if all_skips:
        print("\n## Skips")
        for pattern, reason in all_skips:
            print(f"- {pattern}: {reason}")

    if all_failures:
        print("\n## Failures")
        unique_failures = sorted(set(all_failures), key=lambda item: (item[0], item[2], item[3], item[4]))
        for pattern, lib_name, tr, sz, reason in unique_failures:
            print(f"- {pattern} {lib_name} {tr} {sz}B: {reason}")

    if gate_blockers:
        print("\n## Gate Blockers")
        for blocker in gate_blockers:
            print(f"- {blocker}")

    runtime_errors = bool(save_baseline_errors)
    gate_failures = False
    for blocker in gate_blockers:
        if blocker.startswith("threshold_failures:"):
            gate_failures = True
        else:
            runtime_errors = True

    exit_code = 0
    if runtime_errors:
        exit_code = 1
    if gate_failures:
        exit_code = max(exit_code, 2)

    raise SystemExit(exit_code)


if __name__ == "__main__":
    main()
