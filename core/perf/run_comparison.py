#!/usr/bin/env python3
"""
core/perf - zlink benchmark runner
"""
import datetime
import collections
import os
import platform
import queue
import re
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

LATENCY_P95_METRIC = "latency_p95"
LATENCY_P99_METRIC = "latency_p99"
REQUIRED_RESULT_METRICS = (
    "throughput",
    "bandwidth",
    "latency",
    LATENCY_P95_METRIC,
    LATENCY_P99_METRIC,
)
REQUIRED_RESULT_METRIC_COUNT = len(REQUIRED_RESULT_METRICS)
SERVER_QUEUE_METRICS = (
    "server_snd_pending_max",
    "server_rcv_pending_max",
    "server_rcv_pending_end",
)
PATTERN_SEPARATOR = "==============================================================================="
STREAM_VARIANT_PATTERNS = ("STREAM",)
PATTERN_ALIASES = {
    "STREAM": ("STREAM",),
    "STREAMS": STREAM_VARIANT_PATTERNS,
}
STREAM_SHARED_CLIENT_BINARY = "perf_stream_client"
STREAM_SERVER_BINARY_BY_PATTERN = {
    "STREAM": "comp_src_stream_server",
}
PATTERN_SUFFIX = {
    "DEALER_DEALER": "dealer_dealer",
    "DEALER_ROUTER": "dealer_router",
    "ROUTER_ROUTER": "router_router",
    "PUBSUB": "pubsub",
    "SPOT": "spot",
    "STREAM": "stream",
}
ECHO_PATTERNS = {
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "STREAM",
}
SINGLE_ECHO_PATTERNS = {
    "PAIR",
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
}
ALLOW_MULTI = os.environ.get("PERF_ALLOW_MULTI", "0") == "1"
SINGLE_COMPARISONS = [
    ("perf_pair", "PAIR"),
    ("perf_pubsub", "PUBSUB"),
    ("perf_dealer_dealer", "DEALER_DEALER"),
    ("perf_dealer_router", "DEALER_ROUTER"),
    ("perf_router_router", "ROUTER_ROUTER"),
    ("perf_spot", "SPOT"),
]
MULTI_COMPARISONS = [
    ("comp_src_dealer_dealer_client", "DEALER_DEALER"),
    ("comp_src_dealer_router_client", "DEALER_ROUTER"),
    ("comp_src_router_router_client", "ROUTER_ROUTER"),
    ("comp_src_pubsub_client", "PUBSUB"),
    ("comp_src_spot_client", "SPOT"),
    ("perf_stream_client", "STREAM"),
]
MULTI_PATTERN_NAMES = {pattern for _, pattern in MULTI_COMPARISONS}
SUPPORTED_MULTI_RECV_MODES = {
    "DEALER_DEALER": ("recv",),
    "DEALER_ROUTER": ("recv",),
    "ROUTER_ROUTER": ("recv",),
    "PUBSUB": ("recv",),
    "SPOT": ("recv", "callback"),
    "STREAM": ("recv", "callback"),
}

MULTI_ENV_ALIAS_MAP = {
    "PERF_STREAM_MSG_SIZES": "PERF_MULTI_STREAM_MSG_SIZES",
    "PERF_PATTERN": "PERF_MULTI_PATTERN",
    "PERF_CLIENTS": "PERF_MULTI_CLIENTS",
    "PERF_HWM": "PERF_MULTI_HWM",
    "PERF_WARMUP_SECONDS": "PERF_MULTI_WARMUP_SECONDS",
    "PERF_DURATION_SECONDS": "PERF_MULTI_DURATION_SECONDS",
    "PERF_SNDTIMEO_MS": "PERF_MULTI_SNDTIMEO_MS",
    "PERF_RCVTIMEO_MS": "PERF_MULTI_RCVTIMEO_MS",
    "PERF_CONNECT_CONCURRENCY": "PERF_MULTI_CONNECT_CONCURRENCY",
    "PERF_CONNECT_READY_TIMEOUT_MS": "PERF_MULTI_CONNECT_READY_TIMEOUT_MS",
    "PERF_MONITOR_HWM": "PERF_MULTI_MONITOR_HWM",
    "PERF_SERVER_READY_TIMEOUT_MS": "PERF_MULTI_SERVER_READY_TIMEOUT_MS",
    "PERF_SERVER_SHUTDOWN_TIMEOUT_MS": "PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS",
    "PERF_SERVER_BIND_PORT": "PERF_MULTI_SERVER_BIND_PORT",
    "PERF_SERVER_IO_THREADS": "PERF_MULTI_SERVER_IO_THREADS",
    "PERF_CLIENT_IO_THREADS": "PERF_MULTI_CLIENT_IO_THREADS",
    "PERF_STREAM_SERVER_IO_THREADS": "PERF_MULTI_STREAM_SERVER_IO_THREADS",
    "PERF_STREAM_CLIENT_IO_THREADS": "PERF_MULTI_STREAM_CLIENT_IO_THREADS",
    "PERF_DEFAULT_IO_THREADS": "PERF_MULTI_DEFAULT_IO_THREADS",
    "PERF_TIMEOUT_SECONDS": "PERF_MULTI_TIMEOUT_SECONDS",
    "PERF_RUN_COOLDOWN_MS": "PERF_MULTI_RUN_COOLDOWN_MS",
    "PERF_TRANSPORT_TRANSITION_MS": "PERF_MULTI_TRANSPORT_TRANSITION_MS",
    "PERF_PATTERN_TRANSITION_MS": "PERF_MULTI_PATTERN_TRANSITION_MS",
    "PERF_ACTIVE_WARMUP": "PERF_MULTI_ACTIVE_WARMUP",
    "PERF_SERVICE_CLIENTS": "PERF_MULTI_SERVICE_CLIENTS",
    "PERF_LATENCY_SAMPLE_CAP": "PERF_MULTI_LATENCY_SAMPLE_CAP",
    "PERF_SNDHWM": "PERF_MULTI_SNDHWM",
    "PERF_RCVHWM": "PERF_MULTI_RCVHWM",
    "PERF_SNDBUF": "PERF_MULTI_SNDBUF",
    "PERF_RCVBUF": "PERF_MULTI_RCVBUF",
    "PERF_PUBSUB_XPUB_NODROP": "PERF_MULTI_PUBSUB_XPUB_NODROP",
    "PERF_SPOT_XPUB_NODROP": "PERF_MULTI_SPOT_XPUB_NODROP",
}


class TeeStream:
    def __init__(self, *streams):
        self._streams = streams

    def write(self, data):
        for stream in self._streams:
            stream.write(data)
            stream.flush()
        return len(data)

    def flush(self):
        for stream in self._streams:
            stream.flush()


def normalize_multi_pattern_name(pattern_name):
    pattern = (pattern_name or "").strip().upper()
    if pattern.startswith("MULTI_"):
        pattern = pattern[6:]
    return pattern


def display_pattern_name(pattern_name):
    pattern = normalize_multi_pattern_name(pattern_name)
    if ALLOW_MULTI and pattern:
        return f"MULTI_{pattern}"
    return pattern


def display_multi_label(label):
    raw = (label or "").strip()
    if not ALLOW_MULTI or not raw:
        return raw
    if " " not in raw:
        return display_pattern_name(raw)
    head, tail = raw.split(" ", 1)
    return f"{display_pattern_name(head)} {tail}"


def is_echo_pattern(pattern_name):
    pattern = normalize_multi_pattern_name(pattern_name)
    if ALLOW_MULTI:
        return pattern in ECHO_PATTERNS
    return pattern in SINGLE_ECHO_PATTERNS


def expand_pattern_aliases(requested_patterns):
    expanded = set()
    for pattern in requested_patterns:
        normalized = normalize_multi_pattern_name(pattern)
        if not normalized:
            continue
        alias_members = PATTERN_ALIASES.get(normalized)
        if alias_members:
            expanded.update(alias_members)
            continue
        expanded.add(normalized)
    return expanded


def expand_pattern_aliases_ordered(requested_patterns):
    expanded = []
    seen = set()
    for pattern in requested_patterns:
        normalized = normalize_multi_pattern_name(pattern)
        if not normalized:
            continue
        alias_members = PATTERN_ALIASES.get(normalized)
        members = alias_members if alias_members else (normalized,)
        for member in members:
            if member in seen:
                continue
            seen.add(member)
            expanded.append(member)
    return expanded


def resolve_stream_server_binary(pattern_name):
    pattern = normalize_multi_pattern_name(pattern_name)
    return STREAM_SERVER_BINARY_BY_PATTERN.get(pattern, "")


def resolve_required_binaries(current_bin, pattern_name):
    pattern = normalize_multi_pattern_name(pattern_name)
    if ALLOW_MULTI:
        if pattern in STREAM_VARIANT_PATTERNS:
            server_binary = resolve_stream_server_binary(pattern)
            if not server_binary:
                return []
            return [server_binary, STREAM_SHARED_CLIENT_BINARY]
        bins = resolve_split_required_binaries(pattern)
        if bins:
            return bins
    return [current_bin]


def collect_unsupported_patterns(pattern_names, recv_mode):
    unsupported = []
    for pattern in pattern_names:
        normalized = normalize_multi_pattern_name(pattern)
        supported_modes = SUPPORTED_MULTI_RECV_MODES.get(normalized, ())
        if recv_mode not in supported_modes:
            unsupported.append(display_pattern_name(normalized))
    return unsupported


def resolve_split_required_binaries(pattern_name):
    pattern = normalize_multi_pattern_name(pattern_name)
    suffix = PATTERN_SUFFIX.get(pattern)
    if not suffix:
        return []
    return [
        f"comp_src_{suffix}_server",
        f"comp_src_{suffix}_client",
    ]


def pattern_direction_label(pattern_name):
    return "echo" if is_echo_pattern(pattern_name) else "one-way"


def compute_bandwidth_mb_s(pattern_name, size, throughput):
    direction_factor = 2.0 if is_echo_pattern(pattern_name) else 1.0
    return (float(throughput) * float(size) * direction_factor) / 1000000.0


def resolve_latency_triplet(latency, latency_p95, latency_p99):
    mean = latency
    p95 = latency_p95 if latency_p95 is not None else mean
    if p95 is None:
        p95 = mean
    p99 = latency_p99 if latency_p99 is not None else p95
    if p99 is None:
        p99 = p95 if p95 is not None else mean
    return mean, p95, p99


def resolve_linux_paths():
    """Return build/library paths rooted at the official core/build directory."""
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
    BUILD_DIR = normalize_build_dir(os.path.join(ROOT_DIR, "core", "build"))
    CURRENT_LIB_DIR = derive_current_lib_dir(BUILD_DIR)
else:
    BUILD_DIR, CURRENT_LIB_DIR = resolve_linux_paths()

DEFAULT_NUM_RUNS = 1


def _read_env_value(name, *fallback_names):
    keys = []
    for key in (name,) + fallback_names:
        if ALLOW_MULTI:
            alias = MULTI_ENV_ALIAS_MAP.get(key)
            if alias:
                keys.append(alias)
        keys.append(key)

    seen = set()
    for key in keys:
        if not key or key in seen:
            continue
        seen.add(key)
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


def resolve_output_capture_limit():
    return max(1024, parse_env_int("PERF_CAPTURE_MAX_BYTES", 4 * 1024 * 1024))


class BoundedTextBuffer:
    def __init__(self, limit):
        self._limit = max(1024, int(limit))
        self._chunks = collections.deque()
        self._size = 0
        self._truncated = False

    def append(self, text):
        if not text:
            return

        if len(text) >= self._limit:
            self._chunks.clear()
            tail = text[-self._limit :]
            self._chunks.append(tail)
            self._size = len(tail)
            self._truncated = True
            return

        self._chunks.append(text)
        self._size += len(text)
        while self._size > self._limit and self._chunks:
            removed = self._chunks.popleft()
            self._size -= len(removed)
            self._truncated = True

    def text(self):
        out = "".join(self._chunks)
        if not self._truncated:
            return out
        return "[output truncated]\n" + out


def resource_metrics_enabled():
    return os.environ.get("PERF_DISABLE_RESOURCE_METRICS", "0") != "1"


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


def compute_process_cpu_pct(elapsed_seconds, start_ticks_value, end_ticks_value, mode):
    if start_ticks_value is None or end_ticks_value is None:
        return None

    nproc = max(1.0, float(os.cpu_count() or 1))
    if mode == "linux":
        try:
            hz = float(os.sysconf("SC_CLK_TCK"))
        except (OSError, ValueError):
            hz = 100.0
        cpu_delta = max(0.0, float(end_ticks_value - start_ticks_value)) / max(1.0, hz)
        return (cpu_delta / (max(1e-6, elapsed_seconds) * nproc)) * 100.0
    if mode == "windows":
        cpu_delta = max(0.0, float(end_ticks_value - start_ticks_value)) / 10000000.0
        return (cpu_delta / (max(1e-6, elapsed_seconds) * nproc)) * 100.0
    return None


def create_external_process_sampler(pid):
    if not resource_metrics_enabled():
        def disabled_sample():
            return None, None, False

        def disabled_close():
            return None

        return disabled_sample, disabled_close

    sample_mode = ""
    proc_handle = None
    start_ticks = None

    if linux_proc_metrics_supported():
        sample_mode = "linux"
        start_ticks = read_proc_cpu_ticks(pid)
    elif windows_proc_metrics_supported():
        sample_mode = "windows"
        proc_handle = open_windows_process_for_metrics(pid)
        start_ticks = read_windows_cpu_ticks_100ns(proc_handle)

    started = time.monotonic()
    last_cpu = None
    last_mem = None

    def sample():
        nonlocal last_cpu, last_mem
        if not sample_mode:
            return None, None, False

        end_ticks = None
        mem_mb = None
        if sample_mode == "linux":
            end_ticks = read_proc_cpu_ticks(pid)
            mem_mb = read_proc_mem_mb(pid)
        elif sample_mode == "windows":
            end_ticks = read_windows_cpu_ticks_100ns(proc_handle)
            mem_mb = read_windows_mem_mb(proc_handle)

        cpu_pct = compute_process_cpu_pct(
            time.monotonic() - started, start_ticks, end_ticks, sample_mode
        )

        changed = False
        if cpu_pct is not None and cpu_pct != last_cpu:
            changed = True
        if mem_mb is not None and mem_mb != last_mem:
            changed = True
        if cpu_pct is not None:
            last_cpu = cpu_pct
        if mem_mb is not None:
            last_mem = mem_mb
        return cpu_pct, mem_mb, changed

    def close():
        close_windows_process_handle(proc_handle)

    return sample, close


def run_command_with_metrics(
    cmd,
    env,
    timeout_sec,
    on_stdout_line=None,
    on_stderr_line=None,
    on_sample=None,
    on_process_start=None,
):
    started = time.monotonic()
    proc = subprocess.Popen(
        cmd,
        env=env,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    if on_process_start is not None:
        try:
            on_process_start(proc)
        except Exception:
            pass
    out_queue = queue.Queue()
    capture_limit = resolve_output_capture_limit()
    stdout_buffer = BoundedTextBuffer(capture_limit)
    stderr_buffer = BoundedTextBuffer(capture_limit)
    stream_done = {"stdout": False, "stderr": False}

    def _reader(pipe, stream_name):
        try:
            for line in iter(pipe.readline, ""):
                out_queue.put((stream_name, line))
        except Exception:
            pass
        finally:
            out_queue.put((stream_name, None))

    reader_threads = [
        threading.Thread(target=_reader, args=(proc.stdout, "stdout"), daemon=True),
        threading.Thread(target=_reader, args=(proc.stderr, "stderr"), daemon=True),
    ]
    for t in reader_threads:
        t.start()

    def _drain_output_queue(block_timeout=0.0):
        while True:
            try:
                if block_timeout and block_timeout > 0:
                    stream_name, line = out_queue.get(timeout=block_timeout)
                    block_timeout = 0.0
                else:
                    stream_name, line = out_queue.get_nowait()
            except queue.Empty:
                break

            if line is None:
                stream_done[stream_name] = True
                continue

            if stream_name == "stdout":
                stdout_buffer.append(line)
                if on_stdout_line is not None:
                    try:
                        on_stdout_line(line)
                    except Exception:
                        pass
            else:
                stderr_buffer.append(line)
                if on_stderr_line is not None:
                    try:
                        on_stderr_line(line)
                    except Exception:
                        pass

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
    last_sample_cpu = None
    last_sample_mem = None

    try:
        while True:
            _drain_output_queue()
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

            if on_sample is not None:
                sample_cpu = compute_process_cpu_pct(
                    time.monotonic() - started, start_ticks, end_ticks, sample_mode
                )
                sample_mem = end_mem_mb
                changed = False
                if sample_cpu is not None and sample_cpu != last_sample_cpu:
                    changed = True
                if sample_mem is not None and sample_mem != last_sample_mem:
                    changed = True
                if changed:
                    try:
                        on_sample(sample_cpu, sample_mem)
                    except Exception:
                        pass
                    if sample_cpu is not None:
                        last_sample_cpu = sample_cpu
                    if sample_mem is not None:
                        last_sample_mem = sample_mem

            if rc is not None and stream_done["stdout"] and stream_done["stderr"]:
                break
            if (time.monotonic() - started) > timeout_sec:
                timed_out = True
                proc.kill()
                break
            time.sleep(0.02)

        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            timed_out = True

        for t in reader_threads:
            try:
                t.join(timeout=1.0)
            except Exception:
                pass
        _drain_output_queue(block_timeout=0.01)

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
    cpu_pct = compute_process_cpu_pct(elapsed, start_ticks, end_ticks, sample_mode)

    return {
        "returncode": proc.returncode,
        "stdout": stdout_buffer.text(),
        "stderr": stderr_buffer.text(),
        "timed_out": timed_out,
        "cpu_pct": cpu_pct,
        "mem_mb": end_mem_mb,
    }


# Settings for loop
_env_transports = parse_env_list("PERF_TRANSPORTS", str)

# Default transports for ZMP sockets (non-STREAM)
TRANSPORTS = ["tcp", "tls", "ws", "wss", "inproc"]
if not IS_WINDOWS:
    TRANSPORTS.append("ipc")

# Default transports for multi non-service patterns.
NON_SERVICE_TRANSPORTS = ["tcp", "tls", "ws", "wss"]
if not IS_WINDOWS:
    NON_SERVICE_TRANSPORTS.append("ipc")

# STREAM socket uses different transports (raw TCP/TLS/WS/WSS)
STREAM_TRANSPORTS = ["tcp", "tls", "ws", "wss"]
FAIL_FAST = os.environ.get("PERF_FAIL_FAST", "0") == "1"


def is_pattern(pattern_name):
    if not ALLOW_MULTI:
        return False
    pattern = (pattern_name or "").strip().upper()
    return pattern in MULTI_PATTERN_NAMES


def select_transports(pattern_name):
    service_or_stream = pattern_name in STREAM_VARIANT_PATTERNS or pattern_name == "SPOT"
    if pattern_name == "SPOT":
        base = STREAM_TRANSPORTS
    elif service_or_stream:
        base = STREAM_TRANSPORTS
    elif is_pattern(pattern_name):
        base = NON_SERVICE_TRANSPORTS
    else:
        base = TRANSPORTS
    if not _env_transports:
        return list(base)
    return [t for t in base if t in _env_transports]


_env_sizes = parse_env_list("PERF_MSG_SIZES", int)
if _env_sizes:
    MSG_SIZES = _env_sizes
else:
    MSG_SIZES = [64, 256, 1024, 65536, 131072, 262144]

_env_stream_sizes = parse_env_list(
    "PERF_STREAM_MSG_SIZES",
    int,
)
if _env_stream_sizes:
    STREAM_MSG_SIZES = _env_stream_sizes
elif _env_sizes:
    # Keep stream sizes aligned with --msg-sizes/PERF_MSG_SIZES
    # unless a stream-specific override is explicitly provided.
    STREAM_MSG_SIZES = _env_sizes
else:
    STREAM_MSG_SIZES = [64, 256, 1024, 65536]

DEFAULT_RUN_COOLDOWN_MS = max(
    0,
    parse_env_int(
        "PERF_RUN_COOLDOWN_MS",
        3000,
    ),
)

base_env = os.environ.copy()

ENV_ALIAS_KEYS = (
    "PERF_TRANSPORTS",
    "PERF_MSG_SIZES",
    "PERF_STREAM_MSG_SIZES",
    "PERF_PATTERN",
    "PERF_CLIENTS",
    "PERF_HWM",
    "PERF_WARMUP_SECONDS",
    "PERF_DURATION_SECONDS",
    "PERF_SNDTIMEO_MS",
    "PERF_RCVTIMEO_MS",
    "PERF_CONNECT_CONCURRENCY",
    "PERF_CONNECT_READY_TIMEOUT_MS",
    "PERF_MONITOR_HWM",
    "PERF_SERVER_READY_TIMEOUT_MS",
    "PERF_SERVER_SHUTDOWN_TIMEOUT_MS",
    "PERF_SERVER_BIND_PORT",
    "PERF_SERVER_IO_THREADS",
    "PERF_CLIENT_IO_THREADS",
    "PERF_STREAM_SERVER_IO_THREADS",
    "PERF_STREAM_CLIENT_IO_THREADS",
    "PERF_IO_THREADS",
    "PERF_DEFAULT_IO_THREADS",
    "PERF_MAX_SOCKETS",
    "PERF_LAT_COUNT",
    "PERF_LAT_TIMEOUT_MS",
    "PERF_STREAM_NON_TCP_CLIENTS_MAX",
    "PERF_STREAM_SEND_BATCH",
    "PERF_STREAM_SEND_WORKERS",
    "PERF_STREAM_DRAIN_IDLE_MS",
    "PERF_STREAM_DRAIN_MAX_MS",
    "PERF_STREAM_DRAIN_RELAY_BUDGET",
)
def env_pair_value(env, key):
    if ALLOW_MULTI:
        alias = MULTI_ENV_ALIAS_MAP.get(key)
        if alias:
            value = env.get(alias, "").strip()
            if value:
                return value
    return env.get(key, "").strip()


def set_env_pair(env, key, value):
    env[key] = str(value)
    if ALLOW_MULTI:
        alias = MULTI_ENV_ALIAS_MAP.get(key)
        if alias:
            env[alias] = str(value)


def get_env_for_lib(_lib_name):
    env = base_env.copy()
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

        required_bins = resolve_required_binaries(current_bin, p_name)

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

        required_bins = resolve_required_binaries(current_bin, p_name)

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
        LATENCY_P95_METRIC,
        LATENCY_P99_METRIC,
        "cpu_pct",
        "mem_mb",
        "client_cpu_pct",
        "client_mem_mb",
        "server_cpu_pct",
        "server_mem_mb",
        "server_snd_pending_max",
        "server_rcv_pending_max",
        "server_rcv_pending_end",
    ):
        return None, f"ignored unknown RESULT metric '{metric}': {line.strip()}"

    return (line_transport, line_size, metric, value), None


def parse_result_connect_line(line, transport, expected_sizes):
    if not line.startswith("RESULT_CONNECT,"):
        return None, None

    parts = line.strip().split(",")
    if len(parts) != 11:
        return (
            None,
            f"ignored malformed RESULT_CONNECT line (field_count={len(parts)}): {line.strip()}",
        )

    try:
        line_transport = parts[3].strip().lower()
        line_size = int(parts[4].strip())
        ok_label = parts[5].strip().lower()
        ok_value = float(parts[6].strip())
        fail_label = parts[7].strip().lower()
        fail_value = float(parts[8].strip())
        target_label = parts[9].strip().lower()
        target_value = float(parts[10].strip())
    except ValueError:
        return None, f"ignored malformed RESULT_CONNECT numeric field: {line.strip()}"

    if ok_label != "connect_ok" or fail_label != "connect_fail" or target_label != "connect_target":
        return (
            None,
            f"ignored malformed RESULT_CONNECT labels: {line.strip()}",
        )

    if line_transport != transport.lower() or line_size not in expected_sizes:
        return None, None

    return (line_transport, line_size, ok_value, fail_value, target_value), None


def _emit_result_metric_callback(
    result_line_callback, line_transport, line_size, metric_name, value
):
    if result_line_callback is None:
        return
    try:
        result_line_callback(line_transport, line_size, metric_name, value)
    except Exception:
        pass


def emit_result_metrics_from_line(
    line, transport, expected_sizes, result_line_callback
):
    if result_line_callback is None:
        return

    parsed_line, _ = parse_result_line(line, transport, expected_sizes)
    if parsed_line:
        line_transport, line_size, metric, value = parsed_line
        metric_name = normalize_metric_name(metric)
        _emit_result_metric_callback(
            result_line_callback, line_transport, line_size, metric_name, value
        )
        return

    connect_line, _ = parse_result_connect_line(line, transport, expected_sizes)
    if not connect_line:
        return

    line_transport, line_size, ok_value, fail_value, target_value = connect_line
    _emit_result_metric_callback(
        result_line_callback, line_transport, line_size, "connect_ok", ok_value
    )
    _emit_result_metric_callback(
        result_line_callback, line_transport, line_size, "connect_fail", fail_value
    )
    _emit_result_metric_callback(
        result_line_callback,
        line_transport,
        line_size,
        "connect_target",
        target_value,
    )


def parse_special_token(line):
    stripped = line.strip()
    if stripped.startswith("UNSUPPORTED,"):
        parts = stripped.split(",", 4)
        if len(parts) >= 4:
            return (
                "unsupported",
                parts[1].strip(),
                normalize_multi_pattern_name(parts[2].strip()),
                parts[3].strip().lower(),
            )
    if stripped.startswith("SKIP,"):
        parts = stripped.split(",", 5)
        if len(parts) >= 5:
            reason = parts[4].strip() if len(parts) >= 5 else ""
            return (
                "skip",
                parts[1].strip(),
                normalize_multi_pattern_name(parts[2].strip()),
                parts[3].strip().lower(),
                reason,
            )
    return None


def detect_special_status(stdout, expected_lib, expected_pattern, expected_transport):
    expected_pattern = normalize_multi_pattern_name(expected_pattern)
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


def pattern_default_clients(pattern_name, transport=None):
    if pattern_name in STREAM_VARIANT_PATTERNS:
        base = 10000
        tr = (transport or "").strip().lower()
        if tr in ("tls", "ws", "wss"):
            non_tcp_cap = max(
                1, parse_env_int("PERF_STREAM_NON_TCP_CLIENTS_MAX", 10000)
            )
            return min(base, non_tcp_cap)
        return base
    return 100


def pattern_default_hwm(pattern_name):
    if pattern_name in STREAM_VARIANT_PATTERNS:
        return 10
    return 100


def pattern_default_io_threads(pattern_name):
    if pattern_name in STREAM_VARIANT_PATTERNS:
        return 4
    if normalize_multi_pattern_name(pattern_name) == "SPOT":
        return max(1, parse_env_int("PERF_DEFAULT_IO_THREADS", 8))
    return max(1, parse_env_int("PERF_DEFAULT_IO_THREADS", 4))


def resolve_pattern_connect_concurrency(clients):
    if clients >= 10000:
        return 1024
    return 128


def normalize_metric_name(metric):
    if metric == "cpu_pct":
        return "client_cpu_pct"
    if metric == "mem_mb":
        return "client_mem_mb"
    return metric


def should_warn_duplicate_metric(metric_name):
    # Queue probe metrics can be emitted multiple times; keep the first value and
    # suppress duplicate warnings for these keys.
    if metric_name in (
        "server_snd_pending_max",
        "server_rcv_pending_max",
        "server_rcv_pending_end",
    ):
        return False
    return True


def resolve_binary_names(pattern_name):
    suffix = PATTERN_SUFFIX.get(pattern_name)
    if not suffix:
        return None
    return {
        "server": f"comp_src_{suffix}_server",
        "client": f"comp_src_{suffix}_client",
    }


def parse_ready_endpoint(line):
    stripped = line.strip()
    if not stripped.startswith("READY,"):
        return ""
    parts = stripped.split(",", 1)
    if len(parts) != 2:
        return ""
    return parts[1].strip()


def parse_control_ready_endpoint(line):
    stripped = line.strip()
    if not stripped.startswith("CONTROL_READY,"):
        return ""
    parts = stripped.split(",", 1)
    if len(parts) != 2:
        return ""
    return parts[1].strip()


def parse_client_ready_size(line):
    stripped = line.strip()
    if not stripped.startswith("CLIENT_READY,"):
        return None
    parts = stripped.split(",", 1)
    if len(parts) != 2:
        return None
    try:
        size_value = int(parts[1].strip())
    except (TypeError, ValueError):
        return None
    return size_value if size_value > 0 else None


def parse_client_endpoint(line):
    stripped = line.strip()
    if not stripped.startswith("CLIENT_CONTROL_ENDPOINT,"):
        return None
    parts = stripped.split(",", 1)
    if len(parts) != 2:
        return None
    endpoint = parts[1].strip()
    return endpoint or None


def parse_control_connected_endpoint(line):
    stripped = line.strip()
    if not stripped.startswith("CONTROL_CONNECTED,"):
        return None
    parts = stripped.split(",", 1)
    if len(parts) != 2:
        return None
    endpoint = parts[1].strip()
    return endpoint or None


def parse_client_done_size(line):
    stripped = line.strip()
    if not stripped.startswith("CLIENT_DONE,"):
        return None
    parts = stripped.split(",", 1)
    if len(parts) != 2:
        return None
    try:
        size_value = int(parts[1].strip())
    except (TypeError, ValueError):
        return None
    return size_value if size_value > 0 else None


def summarize_server_startup_detail(stdout_text, stderr_text, max_len=180):
    def pick_detail(text):
        if not text:
            return ""
        fallback = ""
        for raw in reversed(text.splitlines()):
            line = raw.strip()
            if not line:
                continue
            if line.startswith("READY,"):
                continue
            if not fallback:
                fallback = line
            lower = line.lower()
            if (
                "[multi-" in line
                or " t+" in line
                or "failed" in lower
                or "error" in lower
                or "timeout" in lower
            ):
                return line
        return fallback

    detail = pick_detail(stderr_text) or pick_detail(stdout_text)
    if not detail:
        return ""
    detail = detail.replace("\r", " ").replace("\n", " ").strip()
    if len(detail) > max_len:
        detail = detail[:max_len]
    return detail


def _pipe_reader(pipe, stream_name, out_queue):
    try:
        for line in iter(pipe.readline, ""):
            out_queue.put((stream_name, line))
    except Exception:
        pass
    finally:
        out_queue.put((stream_name, None))


def build_bench_cmd(binary_path, args):
    if IS_WINDOWS:
        return [binary_path] + list(args)
    if os.environ.get("PERF_TASKSET") == "1":
        return ["taskset", "-c", "1", binary_path] + list(args)
    return [binary_path] + list(args)


def _resolve_server_timeouts(pattern_name, transport, ready_timeout_ms, shutdown_timeout_ms):
    if pattern_name == "SPOT" and transport in ("tls", "wss"):
        ready_timeout_ms = max(ready_timeout_ms, 20000)
        shutdown_timeout_ms = max(shutdown_timeout_ms, 10000)
    return ready_timeout_ms, shutdown_timeout_ms


def _resolve_io_threads(env, io_key, pattern_name):
    io_value = env_pair_value(env, io_key)
    if not io_value and pattern_name in STREAM_VARIANT_PATTERNS:
        if io_key == "PERF_SERVER_IO_THREADS":
            io_value = env_pair_value(env, "PERF_STREAM_SERVER_IO_THREADS")
        elif io_key == "PERF_CLIENT_IO_THREADS":
            io_value = env_pair_value(env, "PERF_STREAM_CLIENT_IO_THREADS")
    if not io_value:
        io_value = env_pair_value(env, "PERF_IO_THREADS")
    if not io_value:
        io_value = str(pattern_default_io_threads(pattern_name))
    try:
        return max(1, int(io_value))
    except (TypeError, ValueError):
        return pattern_default_io_threads(pattern_name)


def _prepare_case_env(
    lib_name,
    transport,
    sizes,
    pattern_name,
    force_stream_sizes=False,
):
    env = get_env_for_lib(lib_name)
    size_csv = ",".join(str(sz) for sz in sizes)
    set_env_pair(env, "PERF_MSG_SIZES", size_csv)
    set_env_pair(env, "PERF_PATTERN", pattern_name)
    if force_stream_sizes or pattern_name in STREAM_VARIANT_PATTERNS:
        set_env_pair(env, "PERF_STREAM_MSG_SIZES", size_csv)

    clients_value = env_pair_value(env, "PERF_CLIENTS")
    if not clients_value:
        clients_value = str(pattern_default_clients(pattern_name, transport))
    set_env_pair(env, "PERF_CLIENTS", clients_value)
    try:
        clients_int = max(1, int(clients_value))
    except ValueError:
        clients_int = pattern_default_clients(pattern_name, transport)

    connect_value = env_pair_value(env, "PERF_CONNECT_CONCURRENCY")
    if not connect_value:
        connect_value = str(resolve_pattern_connect_concurrency(clients_int))
    set_env_pair(env, "PERF_CONNECT_CONCURRENCY", connect_value)

    if pattern_name == "SPOT":
        spot_idle_sleep_ms = max(1, parse_env_int("PERF_SPOT_IDLE_SLEEP_MS", 1))
        set_env_pair(env, "ZLINK_SPOT_IDLE_SLEEP_MS", spot_idle_sleep_ms)

    if pattern_name in STREAM_VARIANT_PATTERNS:
        stream_timeout_ms = parse_env_int("PERF_STREAM_TIMEOUT_MS", 0)
        if stream_timeout_ms <= 0:
            if transport in ("ws", "wss"):
                stream_timeout_ms = 20000
            else:
                stream_timeout_ms = 5000
        set_env_pair(env, "PERF_STREAM_TIMEOUT_MS", stream_timeout_ms)

    server_io_threads_int = _resolve_io_threads(
        env, "PERF_SERVER_IO_THREADS", pattern_name
    )
    client_io_threads_int = _resolve_io_threads(
        env, "PERF_CLIENT_IO_THREADS", pattern_name
    )
    return env, size_csv, clients_int, server_io_threads_int, client_io_threads_int


def run_sizes_test_stream_shared(
    server_binary_name,
    lib_name,
    transport,
    sizes,
    pattern_name,
    result_line_callback=None,
):
    if len(sizes) > 1:
        size_transition_ms = max(
            0, parse_env_int("PERF_STREAM_SIZE_TRANSITION_MS", 3000)
        )
        merged = {
            "status": "success",
            "parsed": {},
            "timed_out": False,
            "returncode": 0,
            "cpu_pct": None,
            "mem_mb": None,
            "reason": "",
            "warnings": [],
        }
        failure_reasons = []
        for size in sizes:
            outcome = run_sizes_test_stream_shared(
                server_binary_name,
                lib_name,
                transport,
                [size],
                pattern_name,
                result_line_callback=result_line_callback,
            )
            merged["parsed"].update(outcome.get("parsed", {}) or {})
            merged["warnings"].extend(outcome.get("warnings", []) or [])
            if outcome.get("cpu_pct") is not None:
                merged["cpu_pct"] = outcome.get("cpu_pct")
            if outcome.get("mem_mb") is not None:
                merged["mem_mb"] = outcome.get("mem_mb")
            merged["returncode"] = max(
                int(merged.get("returncode", 0) or 0),
                int(outcome.get("returncode", 0) or 0),
            )
            merged["timed_out"] = bool(merged["timed_out"] or outcome.get("timed_out"))

            status = outcome.get("status", "fail")
            if status != "success":
                merged["status"] = "fail"
                reason = (outcome.get("reason", "") or "").strip() or f"size_{size}_failed"
                failure_reasons.append(f"{size}:{reason}")

            if size_transition_ms > 0 and size != sizes[-1]:
                time.sleep(size_transition_ms / 1000.0)

        if failure_reasons:
            merged["reason"] = ";".join(failure_reasons)
        return merged

    server_binary_path = os.path.join(BUILD_DIR, server_binary_name + EXE_SUFFIX)
    shared_client_path = os.path.join(BUILD_DIR, STREAM_SHARED_CLIENT_BINARY + EXE_SUFFIX)
    if not os.path.exists(server_binary_path) or not os.path.exists(shared_client_path):
        return {
            "status": "fail",
            "parsed": {},
            "timed_out": False,
            "returncode": -1,
            "cpu_pct": None,
            "mem_mb": None,
            "reason": "missing_stream_shared_binaries",
            "warnings": [],
        }

    duration_seconds = max(1, parse_env_int("PERF_DURATION_SECONDS", 5))
    timeout_override = parse_env_int("PERF_TIMEOUT_SECONDS", 0)
    if timeout_override > 0:
        timeout_sec = timeout_override
    else:
        timeout_sec = max(60, duration_seconds * max(1, len(sizes)) * 4 + 30)
    ready_timeout_ms = max(0, parse_env_int("PERF_SERVER_READY_TIMEOUT_MS", 10000))
    shutdown_timeout_ms = max(0, parse_env_int("PERF_SERVER_SHUTDOWN_TIMEOUT_MS", 5000))
    ready_timeout_ms, shutdown_timeout_ms = _resolve_server_timeouts(
        pattern_name, transport, ready_timeout_ms, shutdown_timeout_ms
    )
    bind_port = max(0, parse_env_int("PERF_SERVER_BIND_PORT", 0))
    expected_sizes = set(sizes)

    (
        env,
        size_csv,
        clients_int,
        server_io_threads_int,
        client_io_threads_int,
    ) = _prepare_case_env(
        lib_name,
        transport,
        sizes,
        pattern_name,
        force_stream_sizes=True,
    )

    set_env_pair(env, "PERF_SERVER_READY_TIMEOUT_MS", ready_timeout_ms)
    set_env_pair(env, "PERF_SERVER_SHUTDOWN_TIMEOUT_MS", shutdown_timeout_ms)
    set_env_pair(env, "PERF_SERVER_BIND_PORT", bind_port)
    warmup_seconds = max(
        0, parse_env_int("PERF_WARMUP_SECONDS", 2)
    )

    server_cmd = build_bench_cmd(server_binary_path, [lib_name, transport])
    shared_client_args = [
        "--transport",
        transport,
        "--pattern",
        pattern_name,
        "--sizes",
        size_csv,
        "--runs",
        "1",
        "--warmup",
        str(warmup_seconds),
        "--duration",
        str(duration_seconds),
        "--ccu",
        str(clients_int),
        "--io-threads",
        str(client_io_threads_int),
        "--print-perf-result",
        "1",
        "--send-stop-token",
        "0",
    ]
    client_cmd = build_bench_cmd(shared_client_path, shared_client_args)
    server_env = env.copy()
    client_env = env.copy()
    set_env_pair(server_env, "PERF_IO_THREADS", server_io_threads_int)
    set_env_pair(client_env, "PERF_IO_THREADS", client_io_threads_int)
    if pattern_name == "PUBSUB":
        set_env_pair(client_env, "PERF_WAIT_SERVER_STOP_AFTER_CLIENT_DONE", 1)
    server_proc = None
    close_server_sampler = None
    sample_server_metrics = None
    capture_limit = resolve_output_capture_limit()
    server_stdout_buffer = BoundedTextBuffer(capture_limit)
    server_stderr_buffer = BoundedTextBuffer(capture_limit)
    out_queue = queue.Queue()
    reader_threads = []
    debug_transitions = os.getenv("PERF_DEBUG_TRANSITIONS") is not None
    control_connected = [False]
    pending_ready_sizes = set()
    client_proc = [None]

    def append_server_stdout_line(line):
        server_stdout_buffer.append(line)
        if parse_control_connected_endpoint(line):
            control_connected[0] = True
            try:
                if client_proc[0] and client_proc[0].stdin:
                    client_proc[0].stdin.write(line)
                    client_proc[0].stdin.flush()
            except Exception:
                pass
        emit_result_metrics_from_line(
            line, transport, expected_sizes, result_line_callback
        )

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
                append_server_stdout_line(line)
            else:
                server_stderr_buffer.append(line)
                if debug_transitions:
                    sys.stderr.write(f"[server] {line}")

    def pump_server_output_nonblocking():
        while True:
            try:
                stream_name, line = out_queue.get_nowait()
            except queue.Empty:
                break
            if line is None:
                continue
            if stream_name == "stdout":
                append_server_stdout_line(line)
            else:
                server_stderr_buffer.append(line)
                if debug_transitions:
                    sys.stderr.write(f"[server] {line}")

    try:
        server_proc = subprocess.Popen(
            server_cmd,
            env=server_env,
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
        control_endpoint = ""
        control_endpoint = ""
        control_endpoint = ""
        early_status = ""
        early_reason = ""
        ready_deadline = time.monotonic() + (ready_timeout_ms / 1000.0)
        while (not endpoint or not control_endpoint) and time.monotonic() <= ready_deadline:
            try:
                stream_name, line = out_queue.get(timeout=0.05)
            except queue.Empty:
                if server_proc.poll() is not None:
                    break
                continue
            if line is None:
                continue
            if stream_name == "stdout":
                append_server_stdout_line(line)
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
                parsed_ctrl_ep = parse_control_ready_endpoint(line)
                if parsed_ctrl_ep:
                    control_endpoint = parsed_ctrl_ep
            else:
                server_stderr_buffer.append(line)

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

        if not endpoint or not control_endpoint:
            pump_server_output_nonblocking()
            was_running_before_stop = server_proc and server_proc.poll() is None
            stop_server()
            drain_server_output()
            reason = "server_ready_timeout"
            if (
                server_proc
                and not was_running_before_stop
                and server_proc.poll() is not None
            ):
                reason = f"server_exit_before_ready_{server_proc.returncode}"
            startup_detail = summarize_server_startup_detail(
                server_stdout_buffer.text(), server_stderr_buffer.text()
            )
            if startup_detail:
                reason = f"{reason}::{startup_detail}"
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

        line_transport = transport.lower()
        final_size = sizes[-1] if sizes else fallback_size
        sample_server_metrics, close_server_sampler = create_external_process_sampler(
            server_proc.pid
        )
        current_live_size = [sizes[0] if sizes else None]
        last_server_cpu = [None]
        last_server_mem = [None]
        last_server_size = [None]
        last_client_cpu = [None]
        last_client_mem = [None]
        last_client_size = [None]
        queue_probe_requested_sizes = set()

        def maybe_send_size_start(size_value):
            if not control_connected[0] or size_value is None:
                return
            try:
                if server_proc.stdin:
                    server_proc.stdin.write(f"START,{size_value}\n")
                    server_proc.stdin.flush()
                if client_proc[0] and client_proc[0].stdin:
                    client_proc[0].stdin.write(f"START,{size_value}\n")
                    client_proc[0].stdin.flush()
                pending_ready_sizes.discard(size_value)
            except Exception:
                pass

        def update_live_size_from_line(line):
            parsed_line, _warning = parse_result_line(
                line, transport, expected_sizes
            )
            if parsed_line:
                current_live_size[0] = parsed_line[1]
                return
            connect_line, _connect_warning = parse_result_connect_line(
                line, transport, expected_sizes
            )
            if connect_line:
                current_live_size[0] = connect_line[1]

        def request_server_queue_probe(size_value):
            nonlocal server_proc
            if server_proc is None or server_proc.poll() is not None:
                return
            try:
                size_int = int(size_value)
            except (TypeError, ValueError):
                return
            if size_int <= 0 or size_int in queue_probe_requested_sizes:
                return
            try:
                if server_proc.stdin:
                    server_proc.stdin.write(f"QUEUE,{size_int}\n")
                    server_proc.stdin.flush()
                    queue_probe_requested_sizes.add(size_int)
            except Exception:
                return

        def emit_live_server_metrics(force=False):
            if result_line_callback is None or sample_server_metrics is None:
                return
            sample_cpu, sample_mem, changed = sample_server_metrics()
            if sample_cpu is not None:
                last_server_cpu[0] = float(sample_cpu)
            if sample_mem is not None:
                last_server_mem[0] = float(sample_mem)

            target_size = current_live_size[0]
            if target_size not in expected_sizes:
                return
            size_changed = target_size != last_server_size[0]
            if not changed and not force and not size_changed:
                return

            if last_server_cpu[0] is not None:
                _emit_result_metric_callback(
                    result_line_callback,
                    line_transport,
                    target_size,
                    "server_cpu_pct",
                    last_server_cpu[0],
                )
            if last_server_mem[0] is not None:
                _emit_result_metric_callback(
                    result_line_callback,
                    line_transport,
                    target_size,
                    "server_mem_mb",
                    last_server_mem[0],
                )
            last_server_size[0] = target_size

        def emit_live_client_metrics(sample_cpu, sample_mem, force=False):
            if result_line_callback is None:
                return

            if sample_cpu is not None:
                last_client_cpu[0] = float(sample_cpu)
            if sample_mem is not None:
                last_client_mem[0] = float(sample_mem)

            target_size = current_live_size[0]
            if target_size not in expected_sizes:
                return
            size_changed = target_size != last_client_size[0]
            if sample_cpu is None and sample_mem is None and not force and not size_changed:
                return

            if last_client_cpu[0] is not None:
                _emit_result_metric_callback(
                    result_line_callback,
                    line_transport,
                    target_size,
                    "client_cpu_pct",
                    last_client_cpu[0],
                )
            if last_client_mem[0] is not None:
                _emit_result_metric_callback(
                    result_line_callback,
                    line_transport,
                    target_size,
                    "client_mem_mb",
                    last_client_mem[0],
                )
            last_client_size[0] = target_size

        def on_client_stdout_line(line):
            pump_server_output_nonblocking()
            client_endpoint = parse_client_endpoint(line)
            if client_endpoint:
                try:
                    if server_proc.stdin:
                        server_proc.stdin.write(
                            f"CONNECT_CONTROL,{client_endpoint}\n"
                        )
                        server_proc.stdin.flush()
                except Exception:
                    pass
                return
            ready_size = parse_client_ready_size(line)
            if ready_size is not None:
                pending_ready_sizes.add(ready_size)
                maybe_send_size_start(ready_size)
                return
            update_live_size_from_line(line)
            emit_result_metrics_from_line(
                line, transport, expected_sizes, result_line_callback
            )
            parsed_line, _warning = parse_result_line(
                line, transport, expected_sizes
            )
            if parsed_line:
                _line_transport, line_size, metric_name, _value = parsed_line
                if normalize_metric_name(metric_name) == "throughput":
                    request_server_queue_probe(line_size)
            if control_connected[0] and pending_ready_sizes:
                maybe_send_size_start(next(iter(pending_ready_sizes)))
            emit_live_server_metrics()
            emit_live_client_metrics(None, None)

        def on_client_sample(sample_cpu, sample_mem):
            pump_server_output_nonblocking()
            if control_connected[0] and pending_ready_sizes:
                maybe_send_size_start(next(iter(pending_ready_sizes)))
            emit_live_server_metrics()
            emit_live_client_metrics(sample_cpu, sample_mem)

        sampled = run_command_with_metrics(
            client_cmd
            + ["--endpoint", endpoint, "--control-endpoint", control_endpoint],
            client_env,
            timeout_sec,
            on_stdout_line=on_client_stdout_line,
            on_sample=on_client_sample,
            on_process_start=lambda proc: client_proc.__setitem__(0, proc),
        )
        pump_server_output_nonblocking()
        emit_live_server_metrics(force=True)
        emit_live_client_metrics(None, None, force=True)
        client_stdout = sampled.get("stdout", "") or ""
        client_stderr = sampled.get("stderr", "") or ""
        if debug_transitions and client_stderr:
            sys.stderr.write(client_stderr)
        progress_meta = {
            "server_endpoint": endpoint,
            "server_control_endpoint": control_endpoint,
            "client_stderr": client_stderr,
        }

        stop_server()
        drain_server_output()
        server_rc = server_proc.returncode if server_proc else 0

        combined_stdout = server_stdout_buffer.text() + client_stdout
        combined_stderr = server_stderr_buffer.text() + client_stderr
        stderr_lower = combined_stderr.lower()

        parsed = {}
        warnings = []
        for line in combined_stdout.splitlines():
            parsed_line, warning = parse_result_line(line, transport, expected_sizes)
            if warning:
                warnings.append(warning)
            if not parsed_line:
                connect_line, connect_warning = parse_result_connect_line(
                    line, transport, expected_sizes
                )
                if connect_warning:
                    warnings.append(connect_warning)
                if not connect_line:
                    continue
                line_transport, line_size, ok_value, fail_value, target_value = connect_line
                parsed[f"{line_transport}|{line_size}|connect_ok"] = ok_value
                parsed[f"{line_transport}|{line_size}|connect_fail"] = fail_value
                parsed[f"{line_transport}|{line_size}|connect_target"] = target_value
                continue

            line_transport, line_size, metric, value = parsed_line
            metric_name = normalize_metric_name(metric)
            key = f"{line_transport}|{line_size}|{metric_name}"
            if key in parsed:
                if metric_name in SERVER_QUEUE_METRICS:
                    continue
                if should_warn_duplicate_metric(metric_name):
                    warnings.append(
                        "duplicate RESULT metric detected; keeping last value: "
                        f"{pattern_name} {transport} {line_size}B {metric_name}"
                    )
            parsed[key] = value

        for sz in sizes:
            connect_target_key = f"{transport}|{sz}|connect_target"
            parsed.setdefault(connect_target_key, float(clients_int))

        token_status, token_reason = detect_special_status(
            combined_stdout, lib_name, pattern_name, transport
        )

        if sampled.get("timed_out", False):
            return {
                "status": "fail",
                "parsed": parsed,
                "timed_out": True,
                "returncode": sampled.get("returncode", -1),
                "cpu_pct": sampled.get("cpu_pct"),
                "mem_mb": sampled.get("mem_mb"),
                "reason": "timeout",
                "warnings": warnings,
                **progress_meta,
            }
        if sampled.get("returncode", 0) != 0:
            detail = summarize_server_startup_detail(client_stdout, client_stderr)
            reason = f"non_zero_exit_{sampled.get('returncode', -1)}"
            if detail:
                reason = f"{reason}_{detail}"
            return {
                "status": "fail",
                "parsed": parsed,
                "timed_out": False,
                "returncode": sampled.get("returncode", -1),
                "cpu_pct": sampled.get("cpu_pct"),
                "mem_mb": sampled.get("mem_mb"),
                "reason": reason,
                "warnings": warnings,
                **progress_meta,
            }
        if server_rc not in (0, None):
            detail = summarize_server_startup_detail(
                server_stdout_buffer.text(), server_stderr_buffer.text()
            )
            reason = f"server_non_zero_exit_{server_rc}"
            if detail:
                reason = f"{reason}::{detail}"
            return {
                "status": "fail",
                "parsed": parsed,
                "timed_out": False,
                "returncode": server_rc,
                "cpu_pct": sampled.get("cpu_pct"),
                "mem_mb": sampled.get("mem_mb"),
                "reason": reason,
                "warnings": warnings,
                **progress_meta,
            }

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
                **progress_meta,
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
                **progress_meta,
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
                **progress_meta,
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
            **progress_meta,
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
    finally:
        if close_server_sampler is not None:
            close_server_sampler()

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


def run_sizes_test_split(
    server_binary_name,
    client_binary_name,
    lib_name,
    transport,
    sizes,
    pattern_name,
    result_line_callback=None,
):
    server_binary_path = os.path.join(BUILD_DIR, server_binary_name + EXE_SUFFIX)
    client_binary_path = os.path.join(BUILD_DIR, client_binary_name + EXE_SUFFIX)
    fallback_size = sizes[0] if sizes else 64
    duration_seconds = max(1, parse_env_int("PERF_DURATION_SECONDS", 5))
    timeout_override = parse_env_int("PERF_TIMEOUT_SECONDS", 0)
    if timeout_override > 0:
        timeout_sec = timeout_override
    else:
        size_count = max(1, len(sizes))
        has_large_payload = any(sz >= 131072 for sz in sizes)
        is_secure_transport = transport in ("tls", "wss")
        if pattern_name == "SPOT":
            timeout_sec = max(180, duration_seconds * size_count * 8 + 80)
        elif is_secure_transport and has_large_payload:
            timeout_sec = max(240, duration_seconds * size_count * 12 + 60)
        else:
            timeout_sec = max(45, duration_seconds * size_count * 3 + 20)

    ready_timeout_ms = max(0, parse_env_int("PERF_SERVER_READY_TIMEOUT_MS", 10000))
    shutdown_timeout_ms = max(0, parse_env_int("PERF_SERVER_SHUTDOWN_TIMEOUT_MS", 5000))
    ready_timeout_ms, shutdown_timeout_ms = _resolve_server_timeouts(
        pattern_name, transport, ready_timeout_ms, shutdown_timeout_ms
    )
    bind_port = max(0, parse_env_int("PERF_SERVER_BIND_PORT", 0))
    expected_sizes = set(sizes)

    (
        env,
        size_csv,
        clients_int,
        server_io_threads_int,
        client_io_threads_int,
    ) = _prepare_case_env(
        lib_name,
        transport,
        sizes,
        pattern_name,
    )

    set_env_pair(env, "PERF_SERVER_READY_TIMEOUT_MS", ready_timeout_ms)
    set_env_pair(env, "PERF_SERVER_SHUTDOWN_TIMEOUT_MS", shutdown_timeout_ms)
    set_env_pair(env, "PERF_SERVER_BIND_PORT", bind_port)
    server_cmd = build_bench_cmd(server_binary_path, [lib_name, transport])
    client_cmd = build_bench_cmd(
        client_binary_path, [lib_name, transport, str(fallback_size)]
    )
    server_env = env.copy()
    client_env = env.copy()
    set_env_pair(server_env, "PERF_IO_THREADS", server_io_threads_int)
    set_env_pair(client_env, "PERF_IO_THREADS", client_io_threads_int)

    server_proc = None
    close_server_sampler = None
    sample_server_metrics = None
    capture_limit = resolve_output_capture_limit()
    server_stdout_buffer = BoundedTextBuffer(capture_limit)
    server_stderr_buffer = BoundedTextBuffer(capture_limit)
    out_queue = queue.Queue()
    reader_threads = []
    debug_transitions = os.getenv("PERF_DEBUG_TRANSITIONS") is not None
    control_connected = [False]
    pending_ready_sizes = set()
    client_proc = [None]

    def append_server_stdout_line(line):
        server_stdout_buffer.append(line)
        if parse_control_connected_endpoint(line):
            control_connected[0] = True
            try:
                if client_proc[0] and client_proc[0].stdin:
                    client_proc[0].stdin.write(line)
                    client_proc[0].stdin.flush()
            except Exception:
                pass
        emit_result_metrics_from_line(
            line, transport, expected_sizes, result_line_callback
        )

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
                append_server_stdout_line(line)
            else:
                server_stderr_buffer.append(line)
                if debug_transitions:
                    sys.stderr.write(f"[server] {line}")

    def pump_server_output_nonblocking():
        while True:
            try:
                stream_name, line = out_queue.get_nowait()
            except queue.Empty:
                break
            if line is None:
                continue
            if stream_name == "stdout":
                append_server_stdout_line(line)
            else:
                server_stderr_buffer.append(line)
                if debug_transitions:
                    sys.stderr.write(f"[server] {line}")

    try:
        server_proc = subprocess.Popen(
            server_cmd,
            env=server_env,
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
        control_endpoint = ""
        early_status = ""
        early_reason = ""
        ready_deadline = time.monotonic() + (ready_timeout_ms / 1000.0)
        while (not endpoint or not control_endpoint) and time.monotonic() <= ready_deadline:
            try:
                stream_name, line = out_queue.get(timeout=0.05)
            except queue.Empty:
                if server_proc.poll() is not None:
                    break
                continue
            if line is None:
                continue
            if stream_name == "stdout":
                append_server_stdout_line(line)
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
                parsed_ctrl_ep = parse_control_ready_endpoint(line)
                if parsed_ctrl_ep:
                    control_endpoint = parsed_ctrl_ep
            else:
                server_stderr_buffer.append(line)

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

        if not endpoint or not control_endpoint:
            pump_server_output_nonblocking()
            was_running_before_stop = server_proc and server_proc.poll() is None
            stop_server()
            drain_server_output()
            reason = "server_ready_timeout"
            if (
                server_proc
                and not was_running_before_stop
                and server_proc.poll() is not None
            ):
                reason = f"server_exit_before_ready_{server_proc.returncode}"
            startup_detail = summarize_server_startup_detail(
                server_stdout_buffer.text(), server_stderr_buffer.text()
            )
            if startup_detail:
                reason = f"{reason}::{startup_detail}"
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

        line_transport = transport.lower()
        sample_server_metrics, close_server_sampler = create_external_process_sampler(
            server_proc.pid
        )
        current_live_size = [sizes[0] if sizes else None]
        last_server_cpu = [None]
        last_server_mem = [None]
        last_server_size = [None]
        queue_probe_requested_sizes = set()
        stop_requested_sizes = set()

        def maybe_send_size_start(size_value):
            if not control_connected[0] or size_value is None:
                return
            try:
                if server_proc.stdin:
                    server_proc.stdin.write(f"START,{size_value}\n")
                    server_proc.stdin.flush()
                if client_proc[0] and client_proc[0].stdin:
                    client_proc[0].stdin.write(f"START,{size_value}\n")
                    client_proc[0].stdin.flush()
                pending_ready_sizes.discard(size_value)
            except Exception:
                pass

        def update_live_size_from_line(line):
            parsed_line, _warning = parse_result_line(
                line, transport, expected_sizes
            )
            if parsed_line:
                current_live_size[0] = parsed_line[1]
                return
            connect_line, _connect_warning = parse_result_connect_line(
                line, transport, expected_sizes
            )
            if connect_line:
                current_live_size[0] = connect_line[1]

        def request_server_queue_probe(size_value):
            nonlocal server_proc
            if server_proc is None or server_proc.poll() is not None:
                return
            try:
                size_int = int(size_value)
            except (TypeError, ValueError):
                return
            if size_int <= 0 or size_int in queue_probe_requested_sizes:
                return
            try:
                if server_proc.stdin:
                    server_proc.stdin.write(f"QUEUE,{size_int}\n")
                    server_proc.stdin.flush()
                    queue_probe_requested_sizes.add(size_int)
            except Exception:
                return

        def emit_live_server_metrics(force=False):
            if result_line_callback is None or sample_server_metrics is None:
                return
            sample_cpu, sample_mem, changed = sample_server_metrics()
            if sample_cpu is not None:
                last_server_cpu[0] = float(sample_cpu)
            if sample_mem is not None:
                last_server_mem[0] = float(sample_mem)

            target_size = current_live_size[0]
            if target_size not in expected_sizes:
                return
            size_changed = target_size != last_server_size[0]
            if not changed and not force and not size_changed:
                return

            if last_server_cpu[0] is not None:
                _emit_result_metric_callback(
                    result_line_callback,
                    line_transport,
                    target_size,
                    "server_cpu_pct",
                    last_server_cpu[0],
                )
            if last_server_mem[0] is not None:
                _emit_result_metric_callback(
                    result_line_callback,
                    line_transport,
                    target_size,
                    "server_mem_mb",
                    last_server_mem[0],
                )
            last_server_size[0] = target_size

        def on_client_stdout_line(line):
            pump_server_output_nonblocking()
            client_endpoint = parse_client_endpoint(line)
            if client_endpoint:
                try:
                    if server_proc.stdin:
                        server_proc.stdin.write(
                            f"CONNECT_CONTROL,{client_endpoint}\n"
                        )
                        server_proc.stdin.flush()
                except Exception:
                    pass
                return
            ready_size = parse_client_ready_size(line)
            if ready_size is not None:
                pending_ready_sizes.add(ready_size)
                maybe_send_size_start(ready_size)
                return
            done_size = parse_client_done_size(line)
            if done_size is not None:
                if done_size == final_size and done_size not in stop_requested_sizes:
                    try:
                        if server_proc.stdin:
                            server_proc.stdin.write("STOP\n")
                            server_proc.stdin.flush()
                            stop_requested_sizes.add(done_size)
                    except Exception:
                        pass
                return
            update_live_size_from_line(line)
            emit_result_metrics_from_line(
                line, transport, expected_sizes, result_line_callback
            )
            if control_connected[0] and pending_ready_sizes:
                maybe_send_size_start(next(iter(pending_ready_sizes)))
            parsed_line, _warning = parse_result_line(
                line, transport, expected_sizes
            )
            if parsed_line:
                _line_transport, line_size, metric_name, _value = parsed_line
                if normalize_metric_name(metric_name) == "throughput":
                    request_server_queue_probe(line_size)
            emit_live_server_metrics()

        client_cmd = client_cmd + [
            "--endpoint",
            endpoint,
            "--control-endpoint",
            control_endpoint,
        ]
        sampled = run_command_with_metrics(
            client_cmd,
            client_env,
            timeout_sec,
            on_stdout_line=on_client_stdout_line,
            on_sample=lambda _cpu, _mem: (
                pump_server_output_nonblocking(),
                maybe_send_size_start(next(iter(pending_ready_sizes)))
                if control_connected[0] and pending_ready_sizes
                else None,
                emit_live_server_metrics(),
            ),
            on_process_start=lambda proc: client_proc.__setitem__(0, proc),
        )
        pump_server_output_nonblocking()
        emit_live_server_metrics(force=True)
        client_stdout = sampled.get("stdout", "") or ""
        client_stderr = sampled.get("stderr", "") or ""
        if debug_transitions and client_stderr:
            sys.stderr.write(client_stderr)
        progress_meta = {
            "server_endpoint": endpoint,
            "server_control_endpoint": control_endpoint,
            "client_stderr": client_stderr,
        }

        stop_server()
        drain_server_output()
        server_rc = server_proc.returncode if server_proc else 0

        combined_stdout = server_stdout_buffer.text() + client_stdout
        combined_stderr = server_stderr_buffer.text() + client_stderr
        stderr_lower = combined_stderr.lower()

        parsed = {}
        warnings = []
        for line in combined_stdout.splitlines():
            parsed_line, warning = parse_result_line(line, transport, expected_sizes)
            if warning:
                warnings.append(warning)
            if not parsed_line:
                continue
            line_transport, line_size, metric, value = parsed_line
            metric_name = normalize_metric_name(metric)
            key = f"{line_transport}|{line_size}|{metric_name}"
            if key in parsed:
                if metric_name in SERVER_QUEUE_METRICS:
                    continue
                if should_warn_duplicate_metric(metric_name):
                    warnings.append(
                        "duplicate RESULT metric detected; keeping last value: "
                        f"{pattern_name} {transport} {line_size}B {metric_name}"
                    )
            parsed[key] = value

        token_status, token_reason = detect_special_status(
            combined_stdout, lib_name, pattern_name, transport
        )

        if sampled.get("timed_out", False):
            return {
                "status": "fail",
                "parsed": parsed,
                "timed_out": True,
                "returncode": sampled.get("returncode", -1),
                "cpu_pct": sampled.get("cpu_pct"),
                "mem_mb": sampled.get("mem_mb"),
                "reason": "timeout",
                "warnings": warnings,
                **progress_meta,
            }
        if sampled.get("returncode", 0) != 0:
            detail = summarize_server_startup_detail(client_stdout, client_stderr)
            reason = f"non_zero_exit_{sampled.get('returncode', -1)}"
            if detail:
                reason = f"{reason}_{detail}"
            return {
                "status": "fail",
                "parsed": parsed,
                "timed_out": False,
                "returncode": sampled.get("returncode", -1),
                "cpu_pct": sampled.get("cpu_pct"),
                "mem_mb": sampled.get("mem_mb"),
                "reason": reason,
                "warnings": warnings,
                **progress_meta,
            }
        if server_rc not in (0, None):
            detail = summarize_server_startup_detail(
                server_stdout_buffer.text(), server_stderr_buffer.text()
            )
            reason = f"server_non_zero_exit_{server_rc}"
            if detail:
                reason = f"{reason}::{detail}"
            return {
                "status": "fail",
                "parsed": parsed,
                "timed_out": False,
                "returncode": server_rc,
                "cpu_pct": sampled.get("cpu_pct"),
                "mem_mb": sampled.get("mem_mb"),
                "reason": reason,
                "warnings": warnings,
                **progress_meta,
            }

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
                **progress_meta,
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
                **progress_meta,
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
                **progress_meta,
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
            **progress_meta,
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
    finally:
        if close_server_sampler is not None:
            close_server_sampler()

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


def run_sizes_test(
    _binary_name,
    lib_name,
    transport,
    sizes,
    pattern_name,
    result_line_callback=None,
):
    size_list = list(sizes) if sizes else [64]

    if pattern_name in STREAM_VARIANT_PATTERNS:
        server_binary = resolve_stream_server_binary(pattern_name)
        if not server_binary:
            return {
                "status": "fail",
                "parsed": {},
                "timed_out": False,
                "returncode": -1,
                "cpu_pct": None,
                "mem_mb": None,
                "reason": "invalid_stream_server_pattern",
                "warnings": [],
            }

        def run_one_size_case(case_size):
            return run_sizes_test_stream_shared(
                server_binary,
                lib_name,
                transport,
                [case_size],
                pattern_name,
                result_line_callback=result_line_callback,
            )

    else:
        names = resolve_binary_names(pattern_name)
        if not names:
            return {
                "status": "fail",
                "parsed": {},
                "timed_out": False,
                "returncode": -1,
                "cpu_pct": None,
                "mem_mb": None,
                "reason": "invalid_pattern",
                "warnings": [],
            }

        server_path = os.path.join(BUILD_DIR, names["server"] + EXE_SUFFIX)
        client_path = os.path.join(BUILD_DIR, names["client"] + EXE_SUFFIX)
        if not os.path.exists(server_path) or not os.path.exists(client_path):
            return {
                "status": "fail",
                "parsed": {},
                "timed_out": False,
                "returncode": -1,
                "cpu_pct": None,
                "mem_mb": None,
                "reason": "missing_split_binaries",
                "warnings": [],
            }

        def run_one_size_case(case_size):
            return run_sizes_test_split(
                names["server"],
                names["client"],
                lib_name,
                transport,
                [case_size],
                pattern_name,
                result_line_callback=result_line_callback,
            )

    merged = {
        "status": "success",
        "parsed": {},
        "timed_out": False,
        "returncode": 0,
        "cpu_pct": None,
        "mem_mb": None,
        "reason": "",
        "warnings": [],
    }
    for size in size_list:
        isolated = run_one_size_case(size)
        merged["warnings"].extend(isolated.get("warnings", []))
        merged["parsed"].update(isolated.get("parsed", {}))

        cpu_pct = isolated.get("cpu_pct")
        if cpu_pct is not None:
            if merged["cpu_pct"] is None:
                merged["cpu_pct"] = cpu_pct
            else:
                merged["cpu_pct"] = max(merged["cpu_pct"], cpu_pct)

        mem_mb = isolated.get("mem_mb")
        if mem_mb is not None:
            if merged["mem_mb"] is None:
                merged["mem_mb"] = mem_mb
            else:
                merged["mem_mb"] = max(merged["mem_mb"], mem_mb)

        if isolated.get("status") != "success":
            merged["status"] = isolated.get("status", "fail")
            merged["timed_out"] = isolated.get("timed_out", False)
            merged["returncode"] = isolated.get("returncode", -1)
            reason = isolated.get("reason", "size_case_failed")
            merged["reason"] = f"{reason}_size_{size}"
            return merged

    return merged


def run_single_test(binary_name, lib_name, transport, size, pattern_name=""):
    """Runs a single binary for one specific config."""
    binary_path = os.path.join(BUILD_DIR, binary_name + EXE_SUFFIX)
    timeout_sec = 60
    if is_pattern(pattern_name):
        timeout_sec = max(
            60,
            parse_env_int("PERF_WARMUP_SECONDS", 2)
            + parse_env_int("PERF_DURATION_SECONDS", 5)
            + 30,
        )
    if pattern_name in STREAM_VARIANT_PATTERNS:
        timeout_sec = max(
            120,
            parse_env_int("PERF_WARMUP_SECONDS", 2)
            + parse_env_int("PERF_DURATION_SECONDS", 5)
            + 90,
        )

    env = get_env_for_lib(lib_name)
    if is_pattern(pattern_name):
        set_env_pair(env, "PERF_PATTERN", pattern_name)
    try:
        if IS_WINDOWS:
            cmd = [binary_path, lib_name, transport, str(size)]
        elif os.environ.get("PERF_TASKSET") == "1":
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


def _table_header_line(is_multi):
    size_w = 8
    tp_w = 18
    bw_w = 14
    lat_w = 13
    lat95_w = 13
    lat99_w = 13
    cpu_w = 7
    mem_w = 8
    sndq_w = 9
    rcvq_w = 9
    rcvend_w = 9
    if is_multi:
        return (
            f"| {'Size':<{size_w}} | {'Throughput':>{tp_w}} | {'Bandwidth':>{bw_w}} | "
            f"{'Lat.Mean(ms)':>{lat_w}} | {'Lat.P95(ms)':>{lat95_w}} | {'Lat.P99(ms)':>{lat99_w}} | "
            f"{'S.CPU%':>{cpu_w}} | {'S.Mem MB':>{mem_w}} | "
            f"{'Q.Snd.Max':>{sndq_w}} | {'Q.Rcv.Max':>{rcvq_w}} | {'Q.Rcv.End':>{rcvend_w}} |"
        )
    return (
        f"| {'Size':<{size_w}} | {'Throughput':>{tp_w}} | {'Bandwidth':>{bw_w}} | "
        f"{'Lat.Mean':>{lat_w}} | {'Lat.P95':>{lat95_w}} | {'Lat.P99':>{lat99_w}} | "
        f"{'CPU%':>{cpu_w}} | {'Mem MB':>{mem_w}} |"
    )


def _table_separator_line(is_multi):
    size_w = 8
    tp_w = 18
    bw_w = 14
    lat_w = 13
    lat95_w = 13
    lat99_w = 13
    cpu_w = 7
    mem_w = 8
    sndq_w = 9
    rcvq_w = 9
    rcvend_w = 9
    if is_multi:
        return (
            f"|{'-' * (size_w + 2)}|{'-' * (tp_w + 2)}|{'-' * (bw_w + 2)}|"
            f"{'-' * (lat_w + 2)}|{'-' * (lat95_w + 2)}|{'-' * (lat99_w + 2)}|"
            f"{'-' * (cpu_w + 2)}|{'-' * (mem_w + 2)}|"
            f"{'-' * (sndq_w + 2)}|{'-' * (rcvq_w + 2)}|{'-' * (rcvend_w + 2)}|"
        )
    return (
        f"|{'-' * (size_w + 2)}|{'-' * (tp_w + 2)}|{'-' * (bw_w + 2)}|"
        f"{'-' * (lat_w + 2)}|{'-' * (lat95_w + 2)}|{'-' * (lat99_w + 2)}|"
        f"{'-' * (cpu_w + 2)}|{'-' * (mem_w + 2)}|"
    )


def _emit_table_header(emit, is_multi, indent):
    emit(f"{indent}{_table_header_line(is_multi)}")
    emit(f"{indent}{_table_separator_line(is_multi)}")


def _emit_table_row(
    emit,
    pattern_name,
    is_multi,
    indent,
    size,
    status,
    throughput=None,
    bandwidth=None,
    latency=None,
    latency_p95=None,
    latency_p99=None,
    client_cpu=None,
    client_mem=None,
    server_cpu=None,
    server_mem=None,
    server_sndq_max=None,
    server_rcvq_max=None,
    server_rcvq_end=None,
):
    size_w = 8
    tp_w = 16
    bw_w = 12
    lat_w = 12
    lat95_w = 12
    lat99_w = 12
    cpu_w = 6
    mem_w = 8
    sndq_w = 9
    rcvq_w = 9
    rcvend_w = 9
    if status == "success":
        latency_mean, latency_p95_value, latency_p99_value = resolve_latency_triplet(
            latency, latency_p95, latency_p99
        )
        tp_s = format_throughput(pattern_name, throughput if throughput is not None else 0.0)
        bw_s = format_bandwidth(bandwidth if bandwidth is not None else 0.0)
        if is_multi:
            lat_s = (
                f"{((latency_mean if latency_mean is not None else 0.0) / 1000.0):9.3f} ms"
            )
            lat95_s = (
                f"{((latency_p95_value if latency_p95_value is not None else 0.0) / 1000.0):9.3f} ms"
            )
            lat99_s = (
                f"{((latency_p99_value if latency_p99_value is not None else 0.0) / 1000.0):9.3f} ms"
            )
        else:
            lat_s = f"{(latency_mean if latency_mean is not None else 0.0):9.3f} us"
            lat95_s = f"{(latency_p95_value if latency_p95_value is not None else 0.0):9.3f} us"
            lat99_s = f"{(latency_p99_value if latency_p99_value is not None else 0.0):9.3f} us"
        cpu_s = f"{float(client_cpu):6.3f}" if client_cpu is not None else "N/A"
        mem_s = f"{float(client_mem):8.3f}" if client_mem is not None else "N/A"
        server_cpu_s = f"{float(server_cpu):6.3f}" if server_cpu is not None else "N/A"
        server_mem_s = f"{float(server_mem):8.3f}" if server_mem is not None else "N/A"
        server_sndq_s = (
            f"{int(round(float(server_sndq_max))):d}"
            if server_sndq_max is not None
            else "N/A"
        )
        server_rcvq_max_s = (
            f"{int(round(float(server_rcvq_max))):d}"
            if server_rcvq_max is not None
            else "N/A"
        )
        server_rcvq_end_s = (
            f"{int(round(float(server_rcvq_end))):d}"
            if server_rcvq_end is not None
            else "N/A"
        )
    else:
        token = "UNSUPPORTED" if status == "unsupported" else "FAIL"
        tp_s = token
        bw_s = token
        lat_s = token
        lat95_s = token
        lat99_s = token
        cpu_s = "N/A"
        mem_s = "N/A"
        server_cpu_s = "N/A"
        server_mem_s = "N/A"
        server_sndq_s = "N/A"
        server_rcvq_max_s = "N/A"
        server_rcvq_end_s = "N/A"

    if is_multi:
        row_line = (
            f"| {f'{size}B':<{size_w}} | {tp_s:>{tp_w}} | {bw_s:>{bw_w}} | "
            f"{lat_s:>{lat_w}} | {lat95_s:>{lat95_w}} | {lat99_s:>{lat99_w}} | "
            f"{server_cpu_s:>{cpu_w}} | {server_mem_s:>{mem_w}} | "
            f"{server_sndq_s:>{sndq_w}} | {server_rcvq_max_s:>{rcvq_w}} | {server_rcvq_end_s:>{rcvend_w}} |"
        )
    else:
        row_line = (
            f"| {f'{size}B':<{size_w}} | {tp_s:>{tp_w}} | {bw_s:>{bw_w}} | "
            f"{lat_s:>{lat_w}} | {lat95_s:>{lat95_w}} | {lat99_s:>{lat99_w}} | "
            f"{cpu_s:>{cpu_w}} | {mem_s:>{mem_w}} |"
        )
    emit(f"{indent}{row_line}")


def collect_data(binary_name, lib_name, pattern_name, num_runs, transports=None, table_lines=None):
    def emit(line):
        print(line, flush=True)
        if table_lines is not None:
            table_lines.append(line)

    emit(f"  > Benchmarking {lib_name} for {display_pattern_name(pattern_name)}...")
    final_stats = {}
    failures = []

    if transports is None:
        transports = TRANSPORTS

    run_cooldown_ms = max(
        0, parse_env_int("PERF_RUN_COOLDOWN_MS", DEFAULT_RUN_COOLDOWN_MS)
    )
    transport_transition_ms = max(
        0, parse_env_int("PERF_TRANSPORT_TRANSITION_MS", 3000)
    )

    is_multi = is_pattern(pattern_name)
    sizes = STREAM_MSG_SIZES if pattern_name in STREAM_VARIANT_PATTERNS else MSG_SIZES

    for tr_idx, tr in enumerate(transports):
        has_next_transport = (tr_idx + 1) < len(transports)

        def maybe_transport_cooldown():
            if not is_multi or transport_transition_ms <= 0 or not has_next_transport:
                return
            emit(f"    [transport cooldown {transport_transition_ms}ms]")
            time.sleep(transport_transition_ms / 1000.0)

        if is_multi:
            pattern_runs = num_runs
            if pattern_name in STREAM_VARIANT_PATTERNS:
                stream_runs_limit = max(
                    1,
                    parse_env_int(
                        "PERF_STREAM_RUNS",
                        parse_env_int("PERF_STREAM_RUNS", 1),
                    ),
                )
                pattern_runs = min(num_runs, stream_runs_limit)

            size_tag = ",".join(f"{sz}B" for sz in sizes)
            emit(f"    Testing {tr} | {size_tag}:")
            show_run_labels = pattern_runs > 1
            if not show_run_labels:
                _emit_table_header(emit, True, "      ")

            expected_keys = []
            for sz in sizes:
                expected_keys.append(f"{tr}|{sz}|throughput")
                expected_keys.append(f"{tr}|{sz}|bandwidth")
                expected_keys.append(f"{tr}|{sz}|latency")
                expected_keys.append(f"{tr}|{sz}|{LATENCY_P95_METRIC}")
                expected_keys.append(f"{tr}|{sz}|{LATENCY_P99_METRIC}")

            metrics_raw = {}
            run_failed_sizes = set()
            tr_unsupported = False
            tr_skipped = False
            skip_reason = "skip"

            for run_idx in range(pattern_runs):
                run_no = run_idx + 1
                has_next_run = (run_idx + 1) < pattern_runs

                def maybe_cooldown():
                    if run_cooldown_ms <= 0 or not has_next_run:
                        return
                    emit(f"      [cooldown {run_cooldown_ms}ms]")
                    time.sleep(run_cooldown_ms / 1000.0)

                if show_run_labels:
                    emit(f"      run {run_no}/{pattern_runs}:")
                    _emit_table_header(emit, True, "        ")
                    row_indent = "        "
                else:
                    row_indent = "      "

                live_metrics = {}
                live_emitted_sizes = set()
                live_emitted_with_server = set()

                def maybe_emit_live_row(sz):
                    if sz in live_emitted_with_server:
                        return
                    tp_key = f"{tr}|{sz}|throughput"
                    bw_key = f"{tr}|{sz}|bandwidth"
                    lat_key = f"{tr}|{sz}|latency"
                    lat95_key = f"{tr}|{sz}|{LATENCY_P95_METRIC}"
                    lat99_key = f"{tr}|{sz}|{LATENCY_P99_METRIC}"
                    tp_value = live_metrics.get(tp_key)
                    bw_value = live_metrics.get(bw_key)
                    lat_value = live_metrics.get(lat_key)
                    lat95_value = live_metrics.get(lat95_key)
                    lat99_value = live_metrics.get(lat99_key)
                    server_cpu_value = live_metrics.get(f"{tr}|{sz}|server_cpu_pct")
                    server_mem_value = live_metrics.get(f"{tr}|{sz}|server_mem_mb")
                    server_sndq_value = live_metrics.get(f"{tr}|{sz}|server_snd_pending_max")
                    server_rcvq_max_value = live_metrics.get(
                        f"{tr}|{sz}|server_rcv_pending_max"
                    )
                    server_rcvq_end_value = live_metrics.get(
                        f"{tr}|{sz}|server_rcv_pending_end"
                    )
                    has_server_sndq = server_sndq_value is not None
                    has_server_rcvq_max = server_rcvq_max_value is not None
                    has_server_rcvq_end = server_rcvq_end_value is not None
                    has_full_server_metrics = (
                        has_server_sndq
                        and has_server_rcvq_max
                        and has_server_rcvq_end
                    )
                    if not has_full_server_metrics:
                        return
                    if sz in live_emitted_sizes:
                        return

                    if (
                        tp_value is None
                        or bw_value is None
                        or lat_value is None
                    ):
                        return
                    if lat95_value is None or lat99_value is None:
                        return
                    _lat_mean, lat95_value, lat99_value = resolve_latency_triplet(
                        lat_value, lat95_value, lat99_value
                    )

                    if pattern_name in STREAM_VARIANT_PATTERNS:
                        connect_ok_key = f"{tr}|{sz}|connect_ok"
                        connect_target_key = f"{tr}|{sz}|connect_target"
                        connect_ok_value = live_metrics.get(connect_ok_key)
                        connect_target_value = live_metrics.get(connect_target_key)
                        if connect_ok_value is None or connect_target_value is None:
                            return
                        connect_ok_int = int(round(float(connect_ok_value)))
                        connect_target_int = int(round(float(connect_target_value)))
                        if connect_target_int <= 0:
                            connect_target_int = pattern_default_clients(
                                pattern_name, tr
                            )
                        connect_required_int = max(1, connect_target_int)
                        if connect_ok_int < connect_required_int:
                            return

                    _emit_table_row(
                        emit,
                        pattern_name,
                        True,
                        row_indent,
                        sz,
                        "success",
                        throughput=tp_value,
                        bandwidth=bw_value,
                        latency=lat_value,
                        latency_p95=lat95_value,
                        latency_p99=lat99_value,
                        server_cpu=server_cpu_value,
                        server_mem=server_mem_value,
                        server_sndq_max=server_sndq_value,
                        server_rcvq_max=server_rcvq_max_value,
                        server_rcvq_end=server_rcvq_end_value,
                    )
                    live_emitted_sizes.add(sz)
                    if has_full_server_metrics:
                        live_emitted_with_server.add(sz)

                def on_result_metric(line_transport, line_size, metric_name, value):
                    if line_transport != tr.lower() or line_size not in sizes:
                        return
                    key = f"{line_transport}|{line_size}|{metric_name}"
                    live_metrics[key] = value
                    maybe_emit_live_row(line_size)

                outcome = run_sizes_test(
                    binary_name,
                    lib_name,
                    tr,
                    sizes,
                    pattern_name,
                    result_line_callback=on_result_metric,
                )
                rc = outcome.get("returncode", 0)
                for warning in outcome.get("warnings", []) or []:
                    print(f"warning: {warning}", file=sys.stderr)

                outcome_status = outcome.get("status", "fail")
                if outcome_status == "unsupported":
                    tr_unsupported = True
                    for sz in sizes:
                        if sz in live_emitted_sizes:
                            continue
                        _emit_table_row(
                            emit,
                            pattern_name,
                            True,
                            row_indent,
                            sz,
                            "unsupported",
                        )
                    break

                if outcome_status == "skip":
                    tr_skipped = True
                    skip_reason = outcome.get("reason", "skip")
                    for sz in sizes:
                        if sz in live_emitted_sizes:
                            continue
                        _emit_table_row(
                            emit,
                            pattern_name,
                            True,
                            row_indent,
                            sz,
                            "fail",
                        )
                    break

                parsed = outcome.get("parsed", {}) or {}
                for key, value in parsed.items():
                    metrics_raw.setdefault(key, []).append(value)

                timed_out_run = outcome.get("timed_out", False)
                if timed_out_run and not parsed:
                    for sz in sizes:
                        if sz in live_emitted_sizes:
                            continue
                        failures.append((pattern_name, lib_name, tr, sz, "timeout"))
                        run_failed_sizes.add(sz)
                        _emit_table_row(
                            emit,
                            pattern_name,
                            True,
                            row_indent,
                            sz,
                            "fail",
                        )
                    if FAIL_FAST:
                        return final_stats, failures
                    maybe_cooldown()
                    continue

                if not parsed:
                    base_reason = outcome.get("reason", "")
                    reason = base_reason if base_reason else (
                        f"no_data_rc_{rc}" if rc not in (0, None) else "no_data"
                    )
                    for sz in sizes:
                        if sz in live_emitted_sizes:
                            continue
                        failures.append((pattern_name, lib_name, tr, sz, reason))
                        run_failed_sizes.add(sz)
                        _emit_table_row(
                            emit,
                            pattern_name,
                            True,
                            row_indent,
                            sz,
                            "fail",
                        )
                    if FAIL_FAST:
                        return final_stats, failures
                    maybe_cooldown()
                    continue

                for sz in sizes:
                    tp_key = f"{tr}|{sz}|throughput"
                    bw_key = f"{tr}|{sz}|bandwidth"
                    lat_key = f"{tr}|{sz}|latency"
                    lat95_key = f"{tr}|{sz}|{LATENCY_P95_METRIC}"
                    lat99_key = f"{tr}|{sz}|{LATENCY_P99_METRIC}"
                    connect_ok_key = f"{tr}|{sz}|connect_ok"
                    connect_target_key = f"{tr}|{sz}|connect_target"

                    missing = []
                    if tp_key not in parsed:
                        missing.append("throughput")
                    if bw_key not in parsed:
                        missing.append("bandwidth")
                    if lat_key not in parsed:
                        missing.append("latency")
                    if lat95_key not in parsed:
                        missing.append(LATENCY_P95_METRIC)
                    if lat99_key not in parsed:
                        missing.append(LATENCY_P99_METRIC)

                    if missing:
                        run_failed_sizes.add(sz)
                        outcome_reason = (outcome.get("reason", "") or "").strip()
                        for metric in missing:
                            if timed_out_run:
                                reason = f"timeout_missing_{metric}"
                            else:
                                if outcome_reason:
                                    reason = f"missing_{metric}_{outcome_reason}"
                                else:
                                    suffix = f"_rc_{rc}" if rc not in (0, None) else ""
                                    reason = f"missing_{metric}{suffix}"
                            failures.append((pattern_name, lib_name, tr, sz, reason))
                        if sz not in live_emitted_sizes:
                            _emit_table_row(
                                emit,
                                pattern_name,
                                True,
                                row_indent,
                                sz,
                                "fail",
                            )
                        if FAIL_FAST:
                            return final_stats, failures
                        continue

                    if pattern_name in STREAM_VARIANT_PATTERNS:
                        connect_ok_value = parsed.get(connect_ok_key)
                        connect_target_value = parsed.get(connect_target_key)
                        if connect_ok_value is None or connect_target_value is None:
                            run_failed_sizes.add(sz)
                            failures.append(
                                (
                                    pattern_name,
                                    lib_name,
                                    tr,
                                    sz,
                                    "missing_connect_stats",
                                )
                            )
                            if sz not in live_emitted_sizes:
                                _emit_table_row(
                                    emit,
                                    pattern_name,
                                    True,
                                    row_indent,
                                    sz,
                                    "fail",
                                )
                            if FAIL_FAST:
                                return final_stats, failures
                            continue

                        connect_ok_int = int(round(float(connect_ok_value)))
                        connect_target_int = int(round(float(connect_target_value)))
                        if connect_target_int <= 0:
                            connect_target_int = pattern_default_clients(
                                pattern_name, tr
                            )
                        connect_required_int = max(1, connect_target_int)
                        if connect_ok_int < connect_required_int:
                            run_failed_sizes.add(sz)
                            reason = (
                                "connect_not_enough_ok_"
                                f"{connect_ok_int}_target_{connect_target_int}"
                                f"_min_{connect_required_int}"
                            )
                            failures.append((pattern_name, lib_name, tr, sz, reason))
                            if sz not in live_emitted_sizes:
                                _emit_table_row(
                                    emit,
                                    pattern_name,
                                    True,
                                    row_indent,
                                    sz,
                                    "fail",
                                )
                            if FAIL_FAST:
                                return final_stats, failures
                            continue

                    if sz in live_emitted_sizes:
                        if (
                            pattern_name not in STREAM_VARIANT_PATTERNS
                            and sz not in live_emitted_with_server
                        ):
                            server_cpu_value = parsed.get(f"{tr}|{sz}|server_cpu_pct")
                            server_mem_value = parsed.get(f"{tr}|{sz}|server_mem_mb")
                            server_sndq_value = parsed.get(f"{tr}|{sz}|server_snd_pending_max")
                            server_rcvq_max_value = parsed.get(
                                f"{tr}|{sz}|server_rcv_pending_max"
                            )
                            server_rcvq_end_value = parsed.get(
                                f"{tr}|{sz}|server_rcv_pending_end"
                            )
                            if (
                                server_cpu_value is not None
                                or server_mem_value is not None
                                or server_sndq_value is not None
                                or server_rcvq_max_value is not None
                                or server_rcvq_end_value is not None
                            ):
                                _emit_table_row(
                                    emit,
                                    pattern_name,
                                    True,
                                    row_indent,
                                    sz,
                                    "success",
                                    throughput=parsed.get(tp_key, 0.0),
                                    bandwidth=parsed.get(bw_key, 0.0),
                                    latency=parsed.get(lat_key, 0.0),
                                    latency_p95=parsed.get(lat95_key),
                                    latency_p99=parsed.get(lat99_key),
                                    server_cpu=server_cpu_value,
                                    server_mem=server_mem_value,
                                    server_sndq_max=server_sndq_value,
                                    server_rcvq_max=server_rcvq_max_value,
                                    server_rcvq_end=server_rcvq_end_value,
                                )
                                live_emitted_with_server.add(sz)
                        continue
                    _emit_table_row(
                        emit,
                        pattern_name,
                        True,
                        row_indent,
                        sz,
                        "success",
                        throughput=parsed.get(tp_key, 0.0),
                        bandwidth=parsed.get(bw_key, 0.0),
                        latency=parsed.get(lat_key, 0.0),
                        latency_p95=parsed.get(lat95_key),
                        latency_p99=parsed.get(lat99_key),
                        server_cpu=parsed.get(f"{tr}|{sz}|server_cpu_pct"),
                        server_mem=parsed.get(f"{tr}|{sz}|server_mem_mb"),
                        server_sndq_max=parsed.get(f"{tr}|{sz}|server_snd_pending_max"),
                        server_rcvq_max=parsed.get(f"{tr}|{sz}|server_rcv_pending_max"),
                        server_rcvq_end=parsed.get(f"{tr}|{sz}|server_rcv_pending_end"),
                    )
                    live_emitted_sizes.add(sz)

                if tr_unsupported or tr_skipped:
                    break
                maybe_cooldown()

            if tr_unsupported:
                for sz in sizes:
                    final_stats[f"{tr}|{sz}|throughput"] = 0
                    final_stats[f"{tr}|{sz}|bandwidth"] = 0
                    final_stats[f"{tr}|{sz}|latency"] = 0
                    final_stats[f"{tr}|{sz}|{LATENCY_P95_METRIC}"] = 0
                    final_stats[f"{tr}|{sz}|{LATENCY_P99_METRIC}"] = 0
                    final_stats[f"{tr}|{sz}|unsupported"] = 1
                if show_run_labels:
                    emit("      median:")
                    _emit_table_header(emit, True, "        ")
                    for sz in sizes:
                        _emit_table_row(
                            emit,
                            pattern_name,
                            True,
                            "        ",
                            sz,
                            "unsupported",
                        )
                emit(f"    Testing {tr}: Done")
                maybe_transport_cooldown()
                continue

            if tr_skipped:
                for sz in sizes:
                    final_stats[f"{tr}|{sz}|throughput"] = 0
                    final_stats[f"{tr}|{sz}|bandwidth"] = 0
                    final_stats[f"{tr}|{sz}|latency"] = 0
                    final_stats[f"{tr}|{sz}|{LATENCY_P95_METRIC}"] = 0
                    final_stats[f"{tr}|{sz}|{LATENCY_P99_METRIC}"] = 0
                    final_stats[f"{tr}|{sz}|skip"] = 1
                    final_stats[f"{tr}|{sz}|skip_reason"] = skip_reason
                if show_run_labels:
                    emit("      median:")
                    _emit_table_header(emit, True, "        ")
                    for sz in sizes:
                        _emit_table_row(
                            emit,
                            pattern_name,
                            True,
                            "        ",
                            sz,
                            "fail",
                        )
                emit(f"    Testing {tr}: Done")
                maybe_transport_cooldown()
                continue

            for key in expected_keys:
                vals = metrics_raw.get(key, [])
                final_stats[key] = statistics.median(vals) if vals else 0
            for sz in sizes:
                for optional_metric in (
                    "server_cpu_pct",
                    "server_mem_mb",
                    "server_snd_pending_max",
                    "server_rcv_pending_max",
                    "server_rcv_pending_end",
                    "connect_ok",
                    "connect_fail",
                    "connect_target",
                ):
                    optional_key = f"{tr}|{sz}|{optional_metric}"
                    vals = metrics_raw.get(optional_key, [])
                    if vals:
                        final_stats[optional_key] = statistics.median(vals)

            if show_run_labels:
                emit("      median:")
                _emit_table_header(emit, True, "        ")
                for sz in sizes:
                    tp_key = f"{tr}|{sz}|throughput"
                    bw_key = f"{tr}|{sz}|bandwidth"
                    lat_key = f"{tr}|{sz}|latency"
                    lat95_key = f"{tr}|{sz}|{LATENCY_P95_METRIC}"
                    lat99_key = f"{tr}|{sz}|{LATENCY_P99_METRIC}"
                    if sz in run_failed_sizes or not (
                        metrics_raw.get(tp_key)
                        and metrics_raw.get(bw_key)
                        and metrics_raw.get(lat_key)
                        and metrics_raw.get(lat95_key)
                        and metrics_raw.get(lat99_key)
                    ):
                        _emit_table_row(
                            emit,
                            pattern_name,
                            True,
                            "        ",
                            sz,
                            "fail",
                        )
                        continue
                    _emit_table_row(
                        emit,
                        pattern_name,
                        True,
                        "        ",
                        sz,
                        "success",
                        throughput=final_stats.get(tp_key, 0.0),
                        bandwidth=final_stats.get(bw_key, 0.0),
                        latency=final_stats.get(lat_key, 0.0),
                        latency_p95=final_stats.get(lat95_key),
                        latency_p99=final_stats.get(lat99_key),
                        server_cpu=final_stats.get(f"{tr}|{sz}|server_cpu_pct"),
                        server_mem=final_stats.get(f"{tr}|{sz}|server_mem_mb"),
                        server_sndq_max=final_stats.get(f"{tr}|{sz}|server_snd_pending_max"),
                        server_rcvq_max=final_stats.get(f"{tr}|{sz}|server_rcv_pending_max"),
                        server_rcvq_end=final_stats.get(f"{tr}|{sz}|server_rcv_pending_end"),
                    )

            emit(f"    Testing {tr}: Done")
            maybe_transport_cooldown()
            continue

        emit(f"    Testing {tr}:")
        show_run_labels = num_runs > 1
        if not show_run_labels:
            _emit_table_header(emit, False, "      ")

        metrics_raw = {}
        tr_unsupported = False
        run_failed_sizes = set()

        for run_idx in range(num_runs):
            run_no = run_idx + 1
            if tr_unsupported:
                break

            if show_run_labels:
                emit(f"      run {run_no}/{num_runs}:")
                _emit_table_header(emit, False, "        ")
                row_indent = "        "
            else:
                row_indent = "      "

            for sz_idx, sz in enumerate(sizes):
                results = run_single_test(binary_name, lib_name, tr, sz, pattern_name)
                if results is None:
                    failures.append((pattern_name, lib_name, tr, sz, "timeout"))
                    run_failed_sizes.add(sz)
                    _emit_table_row(
                        emit,
                        pattern_name,
                        False,
                        row_indent,
                        sz,
                        "fail",
                    )
                    if FAIL_FAST:
                        return final_stats, failures
                    continue

                if not results:
                    failures.append((pattern_name, lib_name, tr, sz, "no_data"))
                    run_failed_sizes.add(sz)
                    _emit_table_row(
                        emit,
                        pattern_name,
                        False,
                        row_indent,
                        sz,
                        "fail",
                    )
                    if FAIL_FAST:
                        return final_stats, failures
                    continue

                if len(results) == 1 and results[0].get("metric") == "_unsupported_transport_":
                    tr_unsupported = True
                    for rem_sz in sizes[sz_idx:]:
                        _emit_table_row(
                            emit,
                            pattern_name,
                            False,
                            row_indent,
                            rem_sz,
                            "unsupported",
                        )
                    break

                result_by_metric = {}
                for r in results:
                    metric = r.get("metric", "").strip().lower()
                    try:
                        value = float(r.get("value"))
                    except (TypeError, ValueError):
                        continue
                    result_by_metric[metric] = value

                latency_value = result_by_metric.get("latency")
                if latency_value is not None:
                    _, lat95_value, lat99_value = resolve_latency_triplet(
                        latency_value,
                        result_by_metric.get(LATENCY_P95_METRIC),
                        result_by_metric.get(LATENCY_P99_METRIC),
                    )
                    result_by_metric.setdefault(LATENCY_P95_METRIC, lat95_value)
                    result_by_metric.setdefault(LATENCY_P99_METRIC, lat99_value)

                for metric, value in result_by_metric.items():
                    metrics_raw.setdefault(f"{tr}|{sz}|{metric}", []).append(value)

                missing = []
                for metric in REQUIRED_RESULT_METRICS:
                    if metric not in result_by_metric:
                        missing.append(metric)
                if missing:
                    run_failed_sizes.add(sz)
                    for metric in missing:
                        failures.append((pattern_name, lib_name, tr, sz, f"missing_{metric}"))
                    _emit_table_row(
                        emit,
                        pattern_name,
                        False,
                        row_indent,
                        sz,
                        "fail",
                    )
                    if FAIL_FAST:
                        return final_stats, failures
                    continue

                _emit_table_row(
                    emit,
                    pattern_name,
                    False,
                    row_indent,
                    sz,
                    "success",
                    throughput=result_by_metric.get("throughput"),
                    bandwidth=result_by_metric.get("bandwidth"),
                    latency=result_by_metric.get("latency"),
                    latency_p95=result_by_metric.get(LATENCY_P95_METRIC),
                    latency_p99=result_by_metric.get(LATENCY_P99_METRIC),
                    client_cpu=result_by_metric.get("cpu_pct"),
                    client_mem=result_by_metric.get("mem_mb"),
                )

        if tr_unsupported:
            for sz in sizes:
                final_stats[f"{tr}|{sz}|throughput"] = 0
                final_stats[f"{tr}|{sz}|bandwidth"] = 0
                final_stats[f"{tr}|{sz}|latency"] = 0
                final_stats[f"{tr}|{sz}|{LATENCY_P95_METRIC}"] = 0
                final_stats[f"{tr}|{sz}|{LATENCY_P99_METRIC}"] = 0
                final_stats[f"{tr}|{sz}|unsupported"] = 1
            if show_run_labels:
                emit("      median:")
                _emit_table_header(emit, False, "        ")
                for sz in sizes:
                    _emit_table_row(
                        emit,
                        pattern_name,
                        False,
                        "        ",
                        sz,
                        "unsupported",
                    )
            emit(f"    Testing {tr}: Done")
            continue

        for key, vals in metrics_raw.items():
            if vals:
                final_stats[key] = statistics.median(vals)

        if show_run_labels:
            emit("      median:")
            _emit_table_header(emit, False, "        ")
            for sz in sizes:
                tp_key = f"{tr}|{sz}|throughput"
                bw_key = f"{tr}|{sz}|bandwidth"
                lat_key = f"{tr}|{sz}|latency"
                lat95_key = f"{tr}|{sz}|{LATENCY_P95_METRIC}"
                lat99_key = f"{tr}|{sz}|{LATENCY_P99_METRIC}"
                if sz in run_failed_sizes or not (
                    tp_key in final_stats
                    and bw_key in final_stats
                    and lat_key in final_stats
                    and lat95_key in final_stats
                    and lat99_key in final_stats
                ):
                    _emit_table_row(
                        emit,
                        pattern_name,
                        False,
                        "        ",
                        sz,
                        "fail",
                    )
                    continue
                _emit_table_row(
                    emit,
                    pattern_name,
                    False,
                    "        ",
                    sz,
                    "success",
                    throughput=final_stats.get(tp_key),
                    bandwidth=final_stats.get(bw_key),
                    latency=final_stats.get(lat_key),
                    latency_p95=final_stats.get(lat95_key),
                    latency_p99=final_stats.get(lat99_key),
                    client_cpu=final_stats.get(f"{tr}|{sz}|cpu_pct"),
                    client_mem=final_stats.get(f"{tr}|{sz}|mem_mb"),
                )

        emit(f"    Testing {tr}: Done")

    return final_stats, failures
def format_throughput(pattern_name, throughput_per_sec):
    unit = "Kops/s" if is_echo_pattern(pattern_name) else "Kmsg/s"
    return f"{throughput_per_sec/1e3:8.3f} {unit}"


def format_bandwidth(bandwidth_mb_s):
    return f"{bandwidth_mb_s:10.3f} MB/s"


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
    env_clients = _read_env_value("PERF_CLIENTS") or ""
    if env_clients.isdigit():
        return env_clients

    if not selected_patterns or not all(is_pattern(p) for p in selected_patterns):
        return ""

    stream_default = 10000
    general_default = 100
    if len(selected_patterns) == 1 and selected_patterns[0] in STREAM_VARIANT_PATTERNS:
        return str(stream_default)
    if all(p in STREAM_VARIANT_PATTERNS for p in selected_patterns):
        return str(stream_default)
    return str(general_default)


def build_meta_items(num_runs, selected_patterns):
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
    meta_items.append(("runs", str(num_runs)))
    clients = resolve_clients_meta(selected_patterns)
    if clients:
        meta_items.append(("clients", clients))
    return meta_items


def print_meta_lines(meta_items):
    for key, value in meta_items:
        print(f"META,{key},{value}")


def build_effective_option_items(args, selected_patterns):
    transports = []
    sizes = []
    for pattern in selected_patterns:
        transports.extend(select_transports(pattern))
        if pattern in STREAM_VARIANT_PATTERNS:
            sizes.extend(STREAM_MSG_SIZES)
        else:
            sizes.extend(MSG_SIZES)

    unique_transports = sorted(set(transports))
    unique_sizes = sorted(set(sizes))
    num_runs = args["num_runs"]
    only = bool(selected_patterns) and all(
        is_pattern(pattern) for pattern in selected_patterns
    )

    items = [
        ("runs", str(num_runs)),
        ("recv_mode", args["recv_mode"]),
        (
            "patterns",
            ",".join(display_pattern_name(pattern) for pattern in selected_patterns)
            if selected_patterns
            else "none",
        ),
        ("transports", ",".join(unique_transports) if unique_transports else "none"),
        ("msg_sizes", ",".join(str(sz) for sz in unique_sizes) if unique_sizes else "none"),
    ]

    if only:
        hwm_raw = _read_env_value("PERF_HWM") or ""
        default_hwm_values = set()
        for pattern in selected_patterns:
            default_hwm_values.add(pattern_default_hwm(pattern))
        if hwm_raw:
            base_hwm = max(1, parse_env_int("PERF_HWM", 100))
            hwm_display = str(base_hwm)
        elif not default_hwm_values:
            base_hwm = 100
            hwm_display = "100 (default)"
        elif len(default_hwm_values) == 1:
            base_hwm = next(iter(default_hwm_values))
            hwm_display = f"{base_hwm} (default)"
        else:
            base_hwm = max(default_hwm_values)
            hwm_display = (
                "mixed-default("
                + ",".join(str(v) for v in sorted(default_hwm_values))
                + ")"
            )

        sndhwm = parse_env_int("PERF_SNDHWM", base_hwm)
        rcvhwm = parse_env_int("PERF_RCVHWM", base_hwm)
        sndbuf = _read_env_value("PERF_SNDBUF") or ""
        rcvbuf = _read_env_value("PERF_RCVBUF") or ""
        sndhwm_display = str(sndhwm)
        rcvhwm_display = str(rcvhwm)
        if not (_read_env_value("PERF_SNDHWM") or ""):
            sndhwm_display = hwm_display
        if not (_read_env_value("PERF_RCVHWM") or ""):
            rcvhwm_display = hwm_display
        timeout_override = parse_env_int("PERF_TIMEOUT_SECONDS", 0)
        service_clients = parse_env_int("PERF_SERVICE_CLIENTS", 0)
        default_clients = 100
        default_stream_clients = 10000
        explicit_server_io = _read_env_value("PERF_SERVER_IO_THREADS") or ""
        explicit_client_io = _read_env_value("PERF_CLIENT_IO_THREADS") or ""
        explicit_stream_server_io = (
            _read_env_value("PERF_STREAM_SERVER_IO_THREADS") or ""
        )
        explicit_stream_client_io = (
            _read_env_value("PERF_STREAM_CLIENT_IO_THREADS") or ""
        )
        explicit_perf_io = _read_env_value("PERF_IO_THREADS") or ""
        if explicit_server_io:
            server_io_threads = max(1, parse_env_int("PERF_SERVER_IO_THREADS", 4))
            server_io_display = str(server_io_threads)
        elif (
            explicit_stream_server_io
            and selected_patterns
            and all(p in STREAM_VARIANT_PATTERNS for p in selected_patterns)
        ):
            server_io_threads = max(
                1, parse_env_int("PERF_STREAM_SERVER_IO_THREADS", 4)
            )
            server_io_display = f"{server_io_threads} (stream override)"
        elif explicit_perf_io:
            server_io_threads = max(1, parse_env_int("PERF_IO_THREADS", 4))
            server_io_display = f"{server_io_threads} (from PERF_IO_THREADS)"
        else:
            default_server_io_values = {
                pattern_default_io_threads(pattern)
                for pattern in selected_patterns
            }
            if len(default_server_io_values) == 1:
                server_io_threads = next(iter(default_server_io_values))
                server_io_display = f"{server_io_threads} (default)"
            else:
                server_io_threads = max(default_server_io_values)
                server_io_display = (
                    "mixed-default("
                    + ",".join(str(v) for v in sorted(default_server_io_values))
                    + ")"
                )

        if explicit_client_io:
            client_io_threads = max(1, parse_env_int("PERF_CLIENT_IO_THREADS", 4))
            client_io_display = str(client_io_threads)
        elif (
            explicit_stream_client_io
            and selected_patterns
            and all(p in STREAM_VARIANT_PATTERNS for p in selected_patterns)
        ):
            client_io_threads = max(
                1, parse_env_int("PERF_STREAM_CLIENT_IO_THREADS", 4)
            )
            client_io_display = f"{client_io_threads} (stream override)"
        elif explicit_perf_io:
            client_io_threads = max(1, parse_env_int("PERF_IO_THREADS", 4))
            client_io_display = f"{client_io_threads} (from PERF_IO_THREADS)"
        else:
            default_client_io_values = {
                pattern_default_io_threads(pattern)
                for pattern in selected_patterns
            }
            if len(default_client_io_values) == 1:
                client_io_threads = next(iter(default_client_io_values))
                client_io_display = f"{client_io_threads} (default)"
            else:
                client_io_threads = max(default_client_io_values)
                client_io_display = (
                    "mixed-default("
                    + ",".join(str(v) for v in sorted(default_client_io_values))
                    + ")"
                )

        clients_meta = resolve_clients_meta(selected_patterns) or "100"
        try:
            clients_for_connect = max(1, int(clients_meta))
        except ValueError:
            clients_for_connect = 100

        connect_raw = _read_env_value("PERF_CONNECT_CONCURRENCY") or ""
        connect_display = (
            connect_raw
            if connect_raw
            else f"{resolve_pattern_connect_concurrency(clients_for_connect)} (default)"
        )

        items.extend(
            [
                ("warmup_seconds", str(parse_env_int("PERF_WARMUP_SECONDS", 2))),
                ("active_warmup", str(parse_env_int("PERF_ACTIVE_WARMUP", 0))),
                ("duration_seconds", str(parse_env_int("PERF_DURATION_SECONDS", 5))),
                ("clients", clients_meta),
                ("default_clients", str(default_clients)),
                ("default_stream_clients", str(default_stream_clients)),
                (
                    "service_clients",
                    str(service_clients) if service_clients > 0 else "auto",
                ),
                ("server_io_threads", server_io_display),
                ("client_io_threads", client_io_display),
                ("hwm", hwm_display),
                ("sndhwm", sndhwm_display),
                ("rcvhwm", rcvhwm_display),
                ("sndbuf", sndbuf or "default(os)"),
                ("rcvbuf", rcvbuf or "default(os)"),
                ("sndtimeo_ms", str(parse_env_int("PERF_SNDTIMEO_MS", 200))),
                ("rcvtimeo_ms", str(parse_env_int("PERF_RCVTIMEO_MS", 200))),
                ("connect_concurrency", connect_display),
                (
                    "connect_ready_timeout_ms",
                    str(parse_env_int("PERF_CONNECT_READY_TIMEOUT_MS", 5000)),
                ),
                ("monitor_hwm", str(parse_env_int("PERF_MONITOR_HWM", 1000))),
                ("server_ready_timeout_ms", str(args["server_ready_timeout_ms"])),
                (
                    "server_shutdown_timeout_ms",
                    str(args["server_shutdown_timeout_ms"]),
                ),
                ("server_bind_port", str(args["server_bind_port"])),
                ("transport_transition_ms", str(args["transport_transition_ms"])),
                ("pattern_transition_ms", str(args["pattern_transition_ms"])),
                (
                    "lat_timeout_ms",
                    str(parse_env_int("PERF_LAT_TIMEOUT_MS", 5000)),
                ),
                (
                    "stream_non_tcp_clients_max",
                    str(parse_env_int("PERF_STREAM_NON_TCP_CLIENTS_MAX", 10000)),
                ),
                (
                    "disable_resource_metrics",
                    str(max(0, parse_env_int("PERF_DISABLE_RESOURCE_METRICS", 0))),
                ),
                (
                    "timeout_seconds",
                    str(timeout_override) if timeout_override > 0 else "auto",
                ),
            ]
        )
    else:
        base_hwm = parse_env_int("PERF_SINGLE_HWM", 1000)
        sndhwm = parse_env_int("PERF_SINGLE_SNDHWM", base_hwm)
        rcvhwm = parse_env_int("PERF_SINGLE_RCVHWM", base_hwm)
        duration_seconds = args["single_duration_seconds"]
        if duration_seconds is None:
            duration_seconds = parse_env_int("PERF_SINGLE_DURATION_SECONDS", 5)
        timeout_override = parse_env_int("PERF_SINGLE_TIMEOUT_SECONDS", 0)
        items.extend(
            [
                ("single_duration_seconds", str(duration_seconds)),
                ("single_hwm", str(base_hwm)),
                ("single_sndhwm", str(sndhwm)),
                ("single_rcvhwm", str(rcvhwm)),
                (
                    "single_timeout_seconds",
                    str(timeout_override) if timeout_override > 0 else "auto",
                ),
            ]
        )

    return items


def print_effective_options(label, option_items):
    print(f"\n## Effective Options ({label})")
    for key, value in option_items:
        print(f"- {key}: {value}")


def resolve_results_root(configured=""):
    if configured:
        return os.path.abspath(configured)
    env_configured = os.environ.get("PERF_RESULTS_DIR", "").strip()
    if env_configured:
        return os.path.abspath(env_configured)
    return os.path.join(SCRIPT_DIR, "results")


def resolve_results_dirs(results_root):
    root = os.path.join(results_root, "multi")
    return {
        "root": root,
        "report": os.path.join(root, "report"),
    }


def resolve_results_max_files():
    raw = os.environ.get("PERF_RESULTS_MAX_FILES", "").strip()
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


def build_failure_lookup(failures, pattern_name):
    lookup = {}
    for pattern, _lib_name, transport, size, reason in failures:
        if pattern != pattern_name:
            continue
        lookup.setdefault((transport, size), set()).add(reason)
    return lookup


def build_result_filename(tag=""):
    ts = datetime.datetime.now().astimezone().strftime("%Y%m%d_%H%M%S")
    platform_tag = platform.system().strip().lower() or "unknown"
    if platform_tag.startswith("darwin"):
        platform_tag = "macos"
    elif platform_tag.startswith("windows"):
        platform_tag = "windows"
    elif platform_tag.startswith("linux"):
        platform_tag = "linux"
    recv_mode = (os.environ.get("PERF_RECV_MODE", "recv") or "recv").strip().lower()
    if recv_mode not in ("recv", "callback"):
        recv_mode = "recv"
    name = f"perf_{platform_tag}_{recv_mode}_{ts}"
    clean_tag = re.sub(r"[^A-Za-z0-9._-]+", "_", (tag or "").strip())
    if clean_tag:
        name = f"{name}_{clean_tag}"
    return f"{name}.txt"


def emit_result_lines(result_map):
    for key in sorted(result_map.keys()):
        pattern, transport, size, metric = key
        value = result_map[key]
        print(
            f"RESULT,current,{display_pattern_name(pattern)},"
            f"{transport},{size},{metric},{value:.3f}"
        )


def parse_args():
    mode_scope = "multi patterns" if ALLOW_MULTI else "single patterns"
    usage = (
        "Usage: run_comparison.py [PATTERN] [options]\n\n"
        "Measure current zlink benchmarks.\n\n"
        f"Note: PATTERN=ALL includes {mode_scope} by default.\n\n"
        "Options:\n"
        "  --runs N                     Iterations per configuration (default: 1)\n"
        "  --duration N                 Override PERF_SINGLE_DURATION_SECONDS (single patterns)\n"
        "  --build-dir PATH             Official build directory (default: core/build)\n"
        "  --pin-cpu                    Pin CPU core during benchmarks (Linux taskset)\n"
        "  --results-dir PATH           Results root directory (default: core/perf/results)\n"
        "  --results-tag NAME           Optional suffix tag for saved filenames\n"
        "  --result-file PATH           Explicit result file path\n"
        "  --recv MODE                  Receive model: recv|callback (default: recv)\n"
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
    single_duration_seconds = None
    build_dir = ""
    pin_cpu = False

    results_dir = os.environ.get("PERF_RESULTS_DIR", "").strip()
    results_tag = os.environ.get("PERF_RESULTS_TAG", "").strip()
    result_file = ""
    recv_mode = (os.environ.get("PERF_RECV_MODE", "recv") or "recv").strip().lower()
    transport_transition_ms = max(
        0, parse_env_int("PERF_TRANSPORT_TRANSITION_MS", 3000)
    )
    pattern_transition_ms = max(
        0, parse_env_int("PERF_PATTERN_TRANSITION_MS", 3000)
    )
    server_ready_timeout_ms = max(
        0, parse_env_int("PERF_SERVER_READY_TIMEOUT_MS", 10000)
    )
    server_shutdown_timeout_ms = max(
        0, parse_env_int("PERF_SERVER_SHUTDOWN_TIMEOUT_MS", 5000)
    )
    server_bind_port = max(0, parse_env_int("PERF_SERVER_BIND_PORT", 0))

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
        elif arg == "--duration":
            if i + 1 >= len(sys.argv):
                print("Error: --duration requires a value.", file=sys.stderr)
                sys.exit(1)
            try:
                single_duration_seconds = int(sys.argv[i + 1])
            except ValueError:
                print("Error: --duration must be an integer.", file=sys.stderr)
                sys.exit(1)
            i += 1
        elif arg.startswith("--duration="):
            try:
                single_duration_seconds = int(arg.split("=", 1)[1])
            except ValueError:
                print("Error: --duration must be an integer.", file=sys.stderr)
                sys.exit(1)
        elif arg == "--build-dir":
            if i + 1 >= len(sys.argv):
                print("Error: --build-dir requires a value.", file=sys.stderr)
                sys.exit(1)
            build_dir = sys.argv[i + 1]
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
        elif arg == "--recv":
            if i + 1 >= len(sys.argv):
                print("Error: --recv requires a value.", file=sys.stderr)
                sys.exit(1)
            recv_mode = sys.argv[i + 1].strip().lower()
            i += 1
        elif arg.startswith("--recv="):
            recv_mode = arg.split("=", 1)[1].strip().lower()
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
    if single_duration_seconds is not None and single_duration_seconds < 1:
        print("Error: --duration must be >= 1.", file=sys.stderr)
        sys.exit(1)
    if recv_mode not in ("recv", "callback"):
        print("Error: --recv must be 'recv' or 'callback'.", file=sys.stderr)
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
        "single_duration_seconds": single_duration_seconds,
        "build_dir": build_dir,
        "pin_cpu": pin_cpu,
        "results_dir": results_dir,
        "results_tag": results_tag,
        "result_file": result_file,
        "recv_mode": recv_mode,
        "transport_transition_ms": transport_transition_ms,
        "pattern_transition_ms": pattern_transition_ms,
        "server_ready_timeout_ms": server_ready_timeout_ms,
        "server_shutdown_timeout_ms": server_shutdown_timeout_ms,
        "server_bind_port": server_bind_port,
    }


def main():
    global BUILD_DIR, CURRENT_LIB_DIR, base_env

    args = parse_args()
    os.environ["PERF_RECV_MODE"] = args["recv_mode"]
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
        base_env["PERF_TASKSET"] = "1"
        os.environ["PERF_TASKSET"] = "1"
    if args["single_duration_seconds"] is not None:
        duration_seconds = str(args["single_duration_seconds"])
        base_env["PERF_SINGLE_DURATION_SECONDS"] = duration_seconds
        os.environ["PERF_SINGLE_DURATION_SECONDS"] = duration_seconds
    timeout_env_values = {
        "PERF_SERVER_READY_TIMEOUT_MS": str(args["server_ready_timeout_ms"]),
        "PERF_SERVER_SHUTDOWN_TIMEOUT_MS": str(
            args["server_shutdown_timeout_ms"]
        ),
        "PERF_SERVER_BIND_PORT": str(args["server_bind_port"]),
    }
    for env_key, env_value in timeout_env_values.items():
        base_env[env_key] = env_value
        os.environ[env_key] = env_value

    comparisons = list(MULTI_COMPARISONS if ALLOW_MULTI else SINGLE_COMPARISONS)

    if p_req == "ALL":
        requested = None
        requested_order = None
    else:
        requested_tokens = [p.strip().upper() for p in p_req.split(",") if p.strip()]
        if not requested_tokens:
            print("Error: --pattern requires at least one value.", file=sys.stderr)
            sys.exit(1)
        requested_order = expand_pattern_aliases_ordered(requested_tokens)
        requested = set(requested_order)

    known_patterns = {p_name for _, p_name in comparisons}
    if requested is not None:
        unknown_patterns = sorted(requested - known_patterns)
        if unknown_patterns:
            print(
                "Error: unsupported patterns: " + ", ".join(unknown_patterns),
                file=sys.stderr,
            )
            sys.exit(1)

    if requested_order is None:
        selected_patterns = [p_name for _, p_name in comparisons]
    else:
        selected_patterns = list(requested_order)
    unsupported_patterns = collect_unsupported_patterns(
        selected_patterns, args["recv_mode"]
    )
    if unsupported_patterns:
        print(
            "Error: unsupported --recv mode for patterns: "
            + ", ".join(sorted(set(unsupported_patterns))),
            file=sys.stderr,
        )
        sys.exit(1)
    only_run = bool(selected_patterns) and all(is_pattern(p) for p in selected_patterns)

    results_root = resolve_results_root(args["results_dir"])
    results_layout = resolve_results_dirs(results_root)
    result_file = args["result_file"]
    if not result_file:
        tag = args["results_tag"]
        result_file = os.path.join(results_layout["report"], build_result_filename(tag))

    missing_current = collect_missing_patterns(comparisons, requested)
    skip_auto_build = (
        os.environ.get("PERF_SKIP_AUTO_BUILD", "0") == "1"
        or os.environ.get("PERF_NO_AUTOBUILD", "0") == "1"
    )
    if missing_current and not skip_auto_build:
        cmake_build_dir = derive_cmake_build_dir(BUILD_DIR)
        if not cmake_build_dir:
            print(
                f"Error: missing benchmark binaries in {BUILD_DIR} and failed to derive CMake build dir.",
                file=sys.stderr,
            )
            print(
                "Hint: pass --build-dir as the CMake build root core/build or its bin(/Release) directory.",
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
                "(PERF_SKIP_AUTO_BUILD=1 or PERF_NO_AUTOBUILD=1).",
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

    result_parent = os.path.dirname(result_file)
    if result_parent:
        os.makedirs(result_parent, exist_ok=True)
    result_log_fh = open(result_file, "w", encoding="utf-8", buffering=1)
    orig_stdout = sys.stdout
    orig_stderr = sys.stderr
    sys.stdout = TeeStream(orig_stdout, result_log_fh)
    sys.stderr = TeeStream(orig_stderr, result_log_fh)

    meta_items = build_meta_items(num_runs, selected_patterns)
    print_meta_lines(meta_items)
    effective_options = build_effective_option_items(args, selected_patterns)
    print_effective_options("start", effective_options)

    all_failures = []
    all_skips = []
    status_counts = {"success": 0, "unsupported": 0, "skip": 0, "fail": 0}
    current_results = {}
    expected_result_lines = 0
    actual_result_lines = 0
    comparison_by_pattern = {p_name: current_bin for current_bin, p_name in comparisons}
    selected_comparisons = [
        (comparison_by_pattern[p_name], p_name) for p_name in selected_patterns
    ]
    pattern_transition_ms = args["pattern_transition_ms"] if only_run else 0

    for pattern_idx, (current_bin, p_name) in enumerate(selected_comparisons):
        if pattern_idx > 0:
            print("")
            print(PATTERN_SEPARATOR)
            print("")

        pattern_header = (
            f"## PATTERN: {display_pattern_name(p_name)} "
            f"({pattern_direction_label(p_name)})"
        )
        print(pattern_header)

        pattern_transports = select_transports(p_name)
        if not pattern_transports:
            print(f"  Skipping {p_name}: no matching transports.")
            all_skips.append((p_name, "no_matching_transports"))
            status_counts["skip"] += 1
            continue

        c_stats, failures = collect_data(current_bin, "current", p_name, num_runs, pattern_transports, None)
        all_failures.extend(failures)
        failure_lookup = build_failure_lookup(failures, p_name)

        # Classify statuses and collect RESULT lines.
        is_multi = is_pattern(p_name)
        sizes = STREAM_MSG_SIZES if p_name in STREAM_VARIANT_PATTERNS else MSG_SIZES
        for tr in pattern_transports:
            for sz in sizes:
                unsupported = bool(c_stats.get(f"{tr}|{sz}|unsupported", 0))
                skipped = bool(c_stats.get(f"{tr}|{sz}|skip", 0))
                reasons = set(failure_lookup.get((tr, sz), set()))

                tp_key = f"{tr}|{sz}|throughput"
                bw_key = f"{tr}|{sz}|bandwidth"
                lat_key = f"{tr}|{sz}|latency"
                lat95_key = f"{tr}|{sz}|{LATENCY_P95_METRIC}"
                lat99_key = f"{tr}|{sz}|{LATENCY_P99_METRIC}"
                cpu_key = f"{tr}|{sz}|server_cpu_pct" if is_multi else f"{tr}|{sz}|cpu_pct"
                mem_key = f"{tr}|{sz}|server_mem_mb" if is_multi else f"{tr}|{sz}|mem_mb"
                server_cpu_key = f"{tr}|{sz}|server_cpu_pct"
                server_mem_key = f"{tr}|{sz}|server_mem_mb"
                server_sndq_key = f"{tr}|{sz}|server_snd_pending_max"
                server_rcvq_max_key = f"{tr}|{sz}|server_rcv_pending_max"
                server_rcvq_end_key = f"{tr}|{sz}|server_rcv_pending_end"
                has_tp = tp_key in c_stats
                has_bw = bw_key in c_stats
                has_lat = lat_key in c_stats
                if not unsupported and not skipped and (not has_tp or not has_bw or not has_lat):
                    reasons.add("missing_result")

                if unsupported:
                    status = "unsupported"
                elif skipped:
                    status = "skip"
                    skip_reason = c_stats.get(f"{tr}|{sz}|skip_reason", "skip")
                    all_skips.append((f"{p_name} {tr} {sz}B", str(skip_reason)))
                elif reasons:
                    status = "fail"
                    expected_result_lines += REQUIRED_RESULT_METRIC_COUNT
                else:
                    status = "success"
                    ct = c_stats.get(tp_key, 0)
                    cb = c_stats.get(bw_key, 0)
                    cl = c_stats.get(lat_key, 0)
                    _, cl95, cl99 = resolve_latency_triplet(
                        cl,
                        c_stats.get(lat95_key),
                        c_stats.get(lat99_key),
                    )
                    current_results[(p_name, tr, sz, "throughput")] = ct
                    current_results[(p_name, tr, sz, "bandwidth")] = cb
                    current_results[(p_name, tr, sz, "latency")] = cl
                    current_results[(p_name, tr, sz, LATENCY_P95_METRIC)] = cl95
                    current_results[(p_name, tr, sz, LATENCY_P99_METRIC)] = cl99
                    expected_result_lines += REQUIRED_RESULT_METRIC_COUNT
                    actual_result_lines += REQUIRED_RESULT_METRIC_COUNT

                    if cpu_key in c_stats:
                        cpu_v = c_stats[cpu_key]
                        current_metric = "server_cpu_pct" if is_multi else "cpu_pct"
                        current_results[(p_name, tr, sz, current_metric)] = cpu_v

                    if mem_key in c_stats:
                        mem_v = c_stats[mem_key]
                        current_metric = "server_mem_mb" if is_multi else "mem_mb"
                        current_results[(p_name, tr, sz, current_metric)] = mem_v
                    if is_multi:
                        if server_sndq_key in c_stats:
                            current_results[(p_name, tr, sz, "server_snd_pending_max")] = c_stats[
                                server_sndq_key
                            ]
                        if server_rcvq_max_key in c_stats:
                            current_results[(p_name, tr, sz, "server_rcv_pending_max")] = c_stats[
                                server_rcvq_max_key
                            ]
                        if server_rcvq_end_key in c_stats:
                            current_results[(p_name, tr, sz, "server_rcv_pending_end")] = c_stats[
                                server_rcvq_end_key
                            ]

                status_counts[status] += 1

        if pattern_transition_ms > 0 and (pattern_idx + 1) < len(selected_comparisons):
            print(f"[pattern cooldown {pattern_transition_ms}ms]")
            time.sleep(pattern_transition_ms / 1000.0)

    print_effective_options("result", effective_options)
    if current_results:
        print("\n## Result Data")
        emit_result_lines(current_results)

    run_status = "complete" if expected_result_lines == actual_result_lines else "partial"
    max_files = resolve_results_max_files()
    prune_result_files(
        os.path.dirname(result_file),
        max_files,
        exclude_names=[os.path.basename(result_file)],
    )
    print(f"\nSaved result file: {result_file} (status={run_status})")

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
            print(f"- {display_multi_label(pattern)}: {reason}")

    if all_failures:
        print("\n## Failures")
        unique_failures = sorted(set(all_failures), key=lambda item: (item[0], item[2], item[3], item[4]))
        for pattern, lib_name, tr, sz, reason in unique_failures:
            print(
                f"- {display_pattern_name(pattern)} {lib_name} "
                f"{tr} {sz}B: {reason}"
            )

    sys.stdout = orig_stdout
    sys.stderr = orig_stderr
    result_log_fh.close()
    if run_status != "complete":
        raise SystemExit(1)
    raise SystemExit(0)


if __name__ == "__main__":
    main()
