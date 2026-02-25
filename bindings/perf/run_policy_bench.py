#!/usr/bin/env python3
"""Policy-compliant binding benchmark runner for single/multi suites.

This script is intentionally independent from bindings/bench/common.
"""

from __future__ import annotations

import argparse
import datetime as dt
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
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

ROOT_DIR = Path(__file__).resolve().parents[2]
BINDINGS_DIR = ROOT_DIR / "bindings"
CORE_PERF_DIR = ROOT_DIR / "core" / "perf"

IS_WINDOWS = os.name == "nt"
IS_LINUX = platform.system().lower() == "linux"
IS_MACOS = platform.system().lower() == "darwin"

MetricKey = Tuple[str, str, str, int, str]  # lib, pattern, transport, size, metric

SINGLE_PATTERNS: List[str] = [
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

MULTI_PATTERNS: List[str] = [
    "MULTI_DEALER_DEALER",
    "MULTI_DEALER_ROUTER",
    "MULTI_ROUTER_ROUTER",
    "MULTI_PUBSUB",
    "MULTI_GATEWAY",
    "MULTI_SPOT",
    "MULTI_STREAM",
    "MULTI_STREAM_CALLBACK",
    "MULTI_STREAM_LEN32BE",
]

SINGLE_STREAM_PATTERNS = {
    "STREAM",
    "STREAM_CALLBACK",
    "STREAM_LEN32BE",
    "GATEWAY",
    "SPOT",
}

SINGLE_STREAM_SIZE_PATTERNS = {
    "STREAM",
    "STREAM_CALLBACK",
    "STREAM_LEN32BE",
}

SINGLE_SOCKET_PATTERNS = {
    "PAIR",
    "PUBSUB",
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "ROUTER_ROUTER_POLL",
}

DEFAULT_MSG_SIZES = [64, 256, 1024, 65536, 131072, 262144]
DEFAULT_STREAM_MSG_SIZES = [64, 256, 1024, 65536]
DEFAULT_SINGLE_SOCKET_TRANSPORTS = (
    ["tcp", "tls", "ws", "wss", "inproc"] + ([] if IS_WINDOWS else ["ipc"])
)
DEFAULT_STREAM_TRANSPORTS = ["tcp", "tls", "ws", "wss"]
DEFAULT_MULTI_TRANSPORTS = ["tcp", "tls", "ws", "wss"]

SINGLE_ECHO_PATTERNS: set[str] = set()

SINGLE_ONE_WAY_PATTERNS = set(SINGLE_PATTERNS)

MULTI_ECHO_PATTERNS = {
    "MULTI_DEALER_ROUTER",
    "MULTI_ROUTER_ROUTER",
    "MULTI_STREAM",
    "MULTI_STREAM_CALLBACK",
    "MULTI_STREAM_LEN32BE",
}

MULTI_ONE_WAY_PATTERNS = {
    "MULTI_DEALER_DEALER",
    "MULTI_PUBSUB",
    "MULTI_GATEWAY",
    "MULTI_SPOT",
}

MULTI_STREAM_PATTERNS = {
    "MULTI_STREAM",
    "MULTI_STREAM_CALLBACK",
    "MULTI_STREAM_LEN32BE",
}

STREAM_SINGLE_PATTERNS = {
    "STREAM",
    "STREAM_CALLBACK",
    "STREAM_LEN32BE",
}

SINGLE_TO_MULTI_STREAM_PATTERN = {
    "STREAM": "MULTI_STREAM",
    "STREAM_CALLBACK": "MULTI_STREAM_CALLBACK",
    "STREAM_LEN32BE": "MULTI_STREAM_LEN32BE",
}

STREAM_CLIENT_DIR = BINDINGS_DIR / "bench" / "stream_client"
STREAM_CLIENT_BUILD_SCRIPT = STREAM_CLIENT_DIR / "build.sh"
STREAM_CLIENT_BIN = STREAM_CLIENT_DIR / "build" / (
    "bench_stream_client.exe" if IS_WINDOWS else "bench_stream_client"
)

DEFAULT_THRESHOLDS = {
    "throughput": {"warning": -10.0, "fail": -15.0},
    "bandwidth": {"warning": -10.0, "fail": -15.0},
    "latency": {"warning": 10.0, "fail": 15.0},
}

ARCH_MACHINE = platform.machine().lower()
if ARCH_MACHINE in ("x86_64", "amd64"):
    ARCH_FAMILY = "x64"
elif ARCH_MACHINE in ("aarch64", "arm64"):
    ARCH_FAMILY = "arm64"
else:
    ARCH_FAMILY = ARCH_MACHINE


class Tee:
    def __init__(self, output_path: Optional[Path]):
        self._fh = None
        if output_path:
            output_path.parent.mkdir(parents=True, exist_ok=True)
            self._fh = output_path.open("w", encoding="utf-8")

    def print(self, msg: str = "", *, end: str = "\n", flush: bool = False) -> None:
        sys.stdout.write(msg + end)
        if self._fh:
            self._fh.write(msg + end)
        if flush:
            sys.stdout.flush()
            if self._fh:
                self._fh.flush()

    def close(self) -> None:
        if self._fh:
            self._fh.close()
            self._fh = None


@dataclass
class RunOutcome:
    status: str  # success | unsupported | skip | fail
    metrics: Dict[str, float]
    reason: str = ""
    warnings: Optional[List[str]] = None


@dataclass
class ComboStats:
    status: str  # success | unsupported | skip | fail
    throughput: float = 0.0
    bandwidth: float = 0.0
    latency: float = 0.0
    cpu_pct: Optional[float] = None
    mem_mb: Optional[float] = None
    client_cpu_pct: Optional[float] = None
    client_mem_mb: Optional[float] = None
    server_cpu_pct: Optional[float] = None
    server_mem_mb: Optional[float] = None
    reason: str = ""


@dataclass
class SuiteConfig:
    binding: str
    suite: str  # single | multi
    mode: str
    runs: int
    reuse_build: bool
    pin_cpu: bool
    io_threads: Optional[int]
    msg_sizes: List[int]
    msg_sizes_overridden: bool
    transports_override: Optional[List[str]]
    result: bool  # report save flag (--result)
    save_baseline: Optional[str]  # --save [VER], None=off, ""=timestamp auto
    results_root: Path
    results_tag: str
    output: Optional[Path]
    rolling_n: int
    baseline_version: str
    baseline_file: Optional[Path]
    multi_warmup_seconds: int
    multi_duration_seconds: int
    multi_clients: int
    multi_hwm: int
    multi_sndtimeo_ms: int
    multi_rcvtimeo_ms: int
    multi_connect_concurrency: Optional[int]
    multi_drain_ms: Optional[int]
    multi_transport_transition_ms: int
    multi_pattern_transition_ms: int
    multi_server_ready_timeout_ms: int
    multi_server_shutdown_timeout_ms: int
    multi_server_bind_port: int
    multi_connect_ready_timeout_ms: int
    multi_monitor_hwm: int


def parse_csv_ints(value: str) -> List[int]:
    out: List[int] = []
    for part in value.split(","):
        part = part.strip()
        if not part:
            continue
        parsed = int(part)
        if parsed <= 0:
            raise ValueError(f"invalid positive integer: {part}")
        out.append(parsed)
    if not out:
        raise ValueError("empty integer list")
    return out


def parse_csv_names(value: str) -> List[str]:
    out: List[str] = []
    for part in value.split(","):
        part = part.strip().lower()
        if not part:
            continue
        out.append(part)
    if not out:
        raise ValueError("empty name list")
    return out


def parse_patterns(value: str, suite: str) -> List[str]:
    default = SINGLE_PATTERNS if suite == "single" else MULTI_PATTERNS
    if value.upper() == "ALL":
        return list(default)

    requested = [p.strip().upper() for p in value.split(",") if p.strip()]
    if not requested:
        raise ValueError("--pattern requires at least one pattern")

    unknown = [p for p in requested if p not in default]
    if unknown:
        raise ValueError("unsupported patterns: " + ", ".join(sorted(set(unknown))))

    return [p for p in default if p in requested]


def pattern_direction(pattern: str, suite: str) -> str:
    p = pattern.upper()
    if suite == "single":
        if p in SINGLE_ECHO_PATTERNS:
            return "echo"
        if p in SINGLE_ONE_WAY_PATTERNS:
            return "one-way"
    else:
        if p in MULTI_ECHO_PATTERNS:
            return "echo"
        if p in MULTI_ONE_WAY_PATTERNS:
            return "one-way"
    return "one-way"


def throughput_unit_label(pattern: str, suite: str) -> str:
    return "Kops/s" if pattern_direction(pattern, suite) == "echo" else "Kmsg/s"


def platform_tag() -> str:
    if IS_WINDOWS:
        return "windows"
    if IS_MACOS:
        return "macos"
    return "linux"


def utc_file_stamp() -> str:
    # Policy requires result filename timestamps in local time.
    return dt.datetime.now().strftime("%Y%m%d_%H%M%S")


def local_offset_iso() -> str:
    return dt.datetime.now().astimezone().replace(microsecond=0).isoformat()


def sanitize_tag(tag: str) -> str:
    return re.sub(r"[^a-zA-Z0-9._-]+", "_", tag.strip()) if tag else ""


def env_first(*names: str) -> Optional[str]:
    for name in names:
        value = os.environ.get(name)
        if value is not None and value != "":
            return value
    return None


def result_file_name(tag: str) -> str:
    name = f"perf_{platform_tag()}_{utc_file_stamp()}"
    clean = sanitize_tag(tag)
    if clean:
        name += f"_{clean}"
    return name + ".txt"


def binding_perf_dir(binding: str) -> Path:
    return BINDINGS_DIR / binding / "perf"


def suite_dirs(cfg: SuiteConfig) -> Tuple[Path, Path, Path]:
    suite_root = cfg.results_root / cfg.suite
    return suite_root / "tmp", suite_root / "report", suite_root / "baseline"


def enforce_retention(path: Path, max_files: int = 100, *, exclude: Optional[Sequence[str]] = None) -> None:
    if not path.exists():
        return
    excluded = set(exclude or [])
    files = [p for p in path.iterdir() if p.is_file() and p.name not in excluded]
    if len(files) <= max_files:
        return
    files.sort(key=lambda p: p.name)
    for p in files[: len(files) - max_files]:
        try:
            p.unlink()
        except OSError:
            pass


def detect_os_arch_tokens() -> Tuple[str, str, str, str]:
    py_arch = "x86_64" if ARCH_FAMILY == "x64" else "aarch64"
    if IS_WINDOWS:
        py_os = "windows"
        node_os = "win32"
        dotnet_os = "win"
        cpp_os = "windows"
    elif IS_MACOS:
        py_os = "darwin"
        node_os = "darwin"
        dotnet_os = "osx"
        cpp_os = "darwin"
    else:
        py_os = "linux"
        node_os = "linux"
        dotnet_os = "linux"
        cpp_os = "linux"
    return py_os, node_os, dotnet_os, cpp_os


def binding_native_candidates(binding: str) -> List[Path]:
    py_os, node_os, dotnet_os, cpp_os = detect_os_arch_tokens()

    if binding == "python":
        lib_name = "zlink.dll" if IS_WINDOWS else ("libzlink.dylib" if IS_MACOS else "libzlink.so")
        return [
            BINDINGS_DIR / "python" / "src" / "zlink" / "native" / f"{py_os}-{ 'x86_64' if ARCH_FAMILY == 'x64' else 'aarch64' }" / lib_name
        ]

    if binding == "java":
        lib_name = "zlink.dll" if IS_WINDOWS else ("libzlink.dylib" if IS_MACOS else "libzlink.so")
        return [
            BINDINGS_DIR / "java" / "src" / "main" / "resources" / "native" / f"{py_os}-{ 'x86_64' if ARCH_FAMILY == 'x64' else 'aarch64' }" / lib_name
        ]

    if binding == "dotnet":
        rid_arch = ARCH_FAMILY
        native_dir = BINDINGS_DIR / "dotnet" / "runtimes" / f"{dotnet_os}-{rid_arch}" / "native"
        lib_name = "zlink.dll" if IS_WINDOWS else ("libzlink.dylib" if IS_MACOS else "libzlink.so")
        return [native_dir / lib_name]

    if binding == "node":
        rid = f"{node_os}-{ARCH_FAMILY}"
        return [
            BINDINGS_DIR / "node" / "prebuilds" / rid / ("zlink.dll" if IS_WINDOWS else ("libzlink.dylib" if IS_MACOS else "libzlink.so"))
        ]

    if binding == "cpp":
        lib_name = "zlink.dll" if IS_WINDOWS else ("libzlink.dylib" if IS_MACOS else "libzlink.so")
        return [
            BINDINGS_DIR / "cpp" / "native" / f"{cpp_os}-{ 'x86_64' if ARCH_FAMILY == 'x64' else 'aarch64' }" / lib_name
        ]

    return []


def binding_native_dir(binding: str) -> Path:
    candidates = binding_native_candidates(binding)
    if not candidates:
        raise RuntimeError(f"unsupported binding: {binding}")
    return candidates[0].parent


def ensure_distribution_only(binding: str) -> Path:
    candidates = binding_native_candidates(binding)
    missing = [str(p) for p in candidates if not p.exists()]
    if missing:
        raise RuntimeError(
            "missing distribution libraries. run bindings/update_zlink_libs.sh first:\n"
            + "\n".join(f"  - {m}" for m in missing)
        )

    core_build_markers = (
        str(ROOT_DIR / "core" / "build"),
        str(ROOT_DIR / "build"),
    )

    for env_name in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "PATH"):
        raw = os.environ.get(env_name)
        if not raw:
            continue
        for segment in raw.split(os.pathsep):
            seg_norm = segment.replace("\\", "/").lower()
            for marker in core_build_markers:
                if marker.replace("\\", "/").lower() in seg_norm:
                    raise RuntimeError(
                        f"disallowed runtime path in {env_name}: {segment} (core/build is forbidden for bindings perf)"
                    )

    return candidates[0].parent


def detect_zlink_version(lib_path: Path) -> Optional[str]:
    try:
        import ctypes

        loader = ctypes.WinDLL if IS_WINDOWS else ctypes.CDLL
        lib = loader(str(lib_path))
        fn = lib.zlink_version
        fn.argtypes = [
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
        ]
        fn.restype = None
        major = ctypes.c_int()
        minor = ctypes.c_int()
        patch = ctypes.c_int()
        fn(ctypes.byref(major), ctypes.byref(minor), ctypes.byref(patch))
        return f"{major.value}.{minor.value}.{patch.value}"
    except Exception:
        return None


def build_binding_if_needed(cfg: SuiteConfig, logger: Tee) -> None:
    perf_dir = binding_perf_dir(cfg.binding)

    def run_checked(cmd: List[str], cwd: Optional[Path] = None, env: Optional[Dict[str, str]] = None) -> None:
        proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env)
        if proc.returncode != 0:
            raise RuntimeError("build failed: " + " ".join(cmd))

    artifact_exists = True
    if cfg.binding == "cpp":
        if cfg.suite == "multi":
            artifact = perf_dir / "multi" / "build" / (
                "perf_multi_main.exe" if IS_WINDOWS else "perf_multi_main"
            )
        else:
            artifact = perf_dir / "single" / "build" / (
                "perf_main.exe" if IS_WINDOWS else "perf_main"
            )
        artifact_exists = artifact.exists()
    elif cfg.binding == "dotnet":
        if cfg.suite == "multi":
            artifact = (
                perf_dir
                / "multi"
                / "Zlink.BindingBench.Multi"
                / "bin"
                / "Release"
                / "net8.0"
                / "Zlink.BindingBench.Multi.dll"
            )
        else:
            artifact = (
                perf_dir
                / "single"
                / "Zlink.BindingBench"
                / "bin"
                / "Release"
                / "net8.0"
                / "Zlink.BindingBench.dll"
            )
        artifact_exists = artifact.exists()
    elif cfg.binding == "java":
        java_class = "PerfMain.class"
        if cfg.suite == "multi":
            java_class = "PerfMultiMain.class"
        artifact = (
            BINDINGS_DIR
            / "java"
            / "build"
            / "classes"
            / "java"
            / "test"
            / "dev"
            / "kairoscode"
            / "zlink"
            / "integration"
            / "bench"
            / java_class
        )
        artifact_exists = artifact.exists()
    elif cfg.binding == "node":
        artifact = BINDINGS_DIR / "node" / "prebuilds" / ("win32-" + ARCH_FAMILY if IS_WINDOWS else ("darwin-" + ARCH_FAMILY if IS_MACOS else "linux-" + ARCH_FAMILY)) / "zlink.node"
        artifact_exists = artifact.exists()
    else:
        artifact_exists = True

    if cfg.reuse_build and artifact_exists:
        return

    if cfg.reuse_build and not artifact_exists:
        logger.print(
            "  > Reuse-build is enabled but artifact is missing; building once."
        )

    logger.print(f"  > Building binding runner for {cfg.binding}...")

    if cfg.binding == "cpp":
        out = perf_dir / cfg.suite / "build"
        out.mkdir(parents=True, exist_ok=True)
        cpp_native = binding_native_dir("cpp")
        if cfg.suite == "multi":
            multi_sources = [
                perf_dir / "multi" / "perf_multi_main.cpp",
                perf_dir / "multi" / "perf_multi_dealer_dealer_server.cpp",
                perf_dir / "multi" / "perf_multi_dealer_dealer_client.cpp",
                perf_dir / "multi" / "perf_multi_dealer_router_server.cpp",
                perf_dir / "multi" / "perf_multi_dealer_router_client.cpp",
                perf_dir / "multi" / "perf_multi_router_router_server.cpp",
                perf_dir / "multi" / "perf_multi_router_router_client.cpp",
                perf_dir / "multi" / "perf_multi_pubsub_server.cpp",
                perf_dir / "multi" / "perf_multi_pubsub_client.cpp",
                perf_dir / "multi" / "perf_multi_gateway_server.cpp",
                perf_dir / "multi" / "perf_multi_gateway_client.cpp",
                perf_dir / "multi" / "perf_multi_spot_server.cpp",
                perf_dir / "multi" / "perf_multi_spot_client.cpp",
                perf_dir / "multi" / "perf_multi_stream_server.cpp",
                perf_dir / "multi" / "perf_multi_stream_client.cpp",
                perf_dir / "multi" / "perf_multi_stream_callback_server.cpp",
                perf_dir / "multi" / "perf_multi_stream_callback_client.cpp",
                perf_dir / "multi" / "perf_multi_stream_len32be_server.cpp",
                perf_dir / "multi" / "perf_multi_stream_len32be_client.cpp",
            ]
            cmd = [
                "c++",
                "-O3",
                "-std=c++17",
                f"-I{ROOT_DIR / 'core' / 'include'}",
                f"-I{BINDINGS_DIR / 'cpp' / 'include'}",
                f"-L{cpp_native}",
                "-lzlink",
                f"-Wl,-rpath,{cpp_native}",
                "-o",
                str(out / ("perf_multi_main.exe" if IS_WINDOWS else "perf_multi_main")),
            ]
            cmd[5:5] = [str(src) for src in multi_sources]
        else:
            cmd = [
                "c++",
                "-O3",
                "-std=c++17",
                f"-I{ROOT_DIR / 'core' / 'include'}",
                f"-I{BINDINGS_DIR / 'cpp' / 'include'}",
                str(perf_dir / "single" / "perf_main.cpp"),
                str(perf_dir / "single" / "perf_pair.cpp"),
                str(perf_dir / "single" / "perf_pubsub.cpp"),
                str(perf_dir / "single" / "perf_dealer_dealer.cpp"),
                str(perf_dir / "single" / "perf_dealer_router.cpp"),
                str(perf_dir / "single" / "perf_router_router.cpp"),
                str(perf_dir / "single" / "perf_router_router_poll.cpp"),
                str(perf_dir / "single" / "perf_stream.cpp"),
                str(perf_dir / "single" / "perf_stream_callback.cpp"),
                str(perf_dir / "single" / "perf_gateway.cpp"),
                str(perf_dir / "single" / "perf_spot.cpp"),
                f"-L{cpp_native}",
                "-lzlink",
                f"-Wl,-rpath,{cpp_native}",
                "-o",
                str(out / ("perf_main.exe" if IS_WINDOWS else "perf_main")),
            ]
        run_checked(cmd)
    elif cfg.binding == "dotnet":
        dotnet_project = perf_dir / "single" / "Zlink.BindingBench" / "Zlink.BindingBench.csproj"
        if cfg.suite == "multi":
            dotnet_project = (
                perf_dir
                / "multi"
                / "Zlink.BindingBench.Multi"
                / "Zlink.BindingBench.Multi.csproj"
            )
        run_checked(["dotnet", "build", str(dotnet_project), "-c", "Release"])
    elif cfg.binding == "java":
        run_checked(["./gradlew", "-q", "classes", "testClasses"], cwd=BINDINGS_DIR / "java")
    elif cfg.binding == "node":
        node_lib = binding_native_candidates("node")[0]
        build_env = os.environ.copy()
        build_env["ZLINK_LIB_PATH"] = str(node_lib)
        if shutil.which("node-gyp"):
            run_checked(["node-gyp", "rebuild"], cwd=BINDINGS_DIR / "node", env=build_env)
        elif shutil.which("npx"):
            run_checked(
                ["npx", "--yes", "node-gyp", "rebuild"],
                cwd=BINDINGS_DIR / "node",
                env=build_env,
            )
        else:
            raise RuntimeError("node-gyp (or npx) is required")
        _ = node_lib  # keep lint happy; path validated separately.
    elif cfg.binding == "python":
        # Python runner uses source modules directly; no dedicated build step.
        return


def binding_cmd_prefix(cfg: SuiteConfig, requested_pattern: str) -> Tuple[List[str], str]:
    # MULTI_* 정책 패턴을 언어별 실행기 패턴으로 매핑한다.
    pattern_map_multi = {
        "MULTI_DEALER_DEALER": "DEALER_DEALER",
        "MULTI_DEALER_ROUTER": "DEALER_ROUTER",
        "MULTI_ROUTER_ROUTER": "ROUTER_ROUTER",
        "MULTI_PUBSUB": "PUBSUB",
        "MULTI_GATEWAY": "GATEWAY",
        "MULTI_SPOT": "SPOT",
        "MULTI_STREAM": "STREAM",
        "MULTI_STREAM_CALLBACK": "STREAM_CALLBACK",
        "MULTI_STREAM_LEN32BE": "STREAM_LEN32BE",
    }

    base_pattern = requested_pattern
    if cfg.suite == "multi":
        base_pattern = pattern_map_multi.get(requested_pattern, requested_pattern)

    if cfg.binding == "python":
        py = shutil.which("python3") or shutil.which("python")
        if not py:
            raise RuntimeError("python interpreter not found")
        cmd = [py, str(BINDINGS_DIR / "python" / "perf" / "single" / "perf_main.py"), base_pattern]
    elif cfg.binding == "node":
        node = shutil.which("node")
        if not node:
            raise RuntimeError("node not found")
        cmd = [node, str(BINDINGS_DIR / "node" / "perf" / "single" / "perf_main.js"), base_pattern]
    elif cfg.binding == "dotnet":
        dotnet = shutil.which("dotnet")
        if not dotnet:
            raise RuntimeError("dotnet not found")
        dll = BINDINGS_DIR / "dotnet" / "perf" / "single" / "Zlink.BindingBench" / "bin" / "Release" / "net8.0" / "Zlink.BindingBench.dll"
        cmd = [dotnet, str(dll), base_pattern]
    elif cfg.binding == "java":
        java = shutil.which("java")
        if not java:
            raise RuntimeError("java not found")
        cp = os.pathsep.join(
            [
                str(BINDINGS_DIR / "java" / "build" / "classes" / "java" / "main"),
                str(BINDINGS_DIR / "java" / "build" / "classes" / "java" / "test"),
                str(BINDINGS_DIR / "java" / "build" / "resources" / "main"),
            ]
        )
        cmd = [java, "-cp", cp, "dev.kairoscode.zlink.integration.bench.PerfMain", base_pattern]
    elif cfg.binding == "cpp":
        bin_name = "perf_main.exe" if IS_WINDOWS else "perf_main"
        runner = BINDINGS_DIR / "cpp" / "perf" / "single" / "build" / bin_name
        cmd = [str(runner), base_pattern]
    else:
        raise RuntimeError(f"unsupported binding: {cfg.binding}")

    return cmd, base_pattern


def multi_pattern_token(pattern: str) -> str:
    p = pattern.strip().upper()
    if p.startswith("MULTI_"):
        p = p[len("MULTI_") :]
    return p.lower()


def binding_split_multi_script(binding: str, role: str, pattern: str) -> Optional[Path]:
    token = multi_pattern_token(pattern)
    if binding == "python":
        path = (
            BINDINGS_DIR
            / "python"
            / "perf"
            / "multi"
            / f"perf_multi_{token}_{role}.py"
        )
        return path if path.exists() else None
    if binding == "node":
        path = (
            BINDINGS_DIR
            / "node"
            / "perf"
            / "multi"
            / f"perf_multi_{token}_{role}.js"
        )
        return path if path.exists() else None
    return None


def supports_split_multi(binding: str) -> bool:
    if binding in {"cpp", "dotnet", "java"}:
        return True
    probe_pattern = "MULTI_STREAM"
    return (
        binding_split_multi_script(binding, "server", probe_pattern) is not None
        and binding_split_multi_script(binding, "client", probe_pattern) is not None
    )


def binding_multi_role_command(
    cfg: SuiteConfig,
    role: str,
    pattern: str,
    transport: str,
    size: Optional[int] = None,
    endpoint: Optional[str] = None,
) -> List[str]:
    script = binding_split_multi_script(cfg.binding, role, pattern)

    if cfg.binding == "python":
        if not script:
            raise RuntimeError(f"split multi runner not found for binding={cfg.binding}")
        py = shutil.which("python3") or shutil.which("python")
        if not py:
            raise RuntimeError("python interpreter not found")
        cmd = [
            py,
            str(script),
            transport,
        ]
    elif cfg.binding == "node":
        if not script:
            raise RuntimeError(f"split multi runner not found for binding={cfg.binding}")
        node = shutil.which("node")
        if not node:
            raise RuntimeError("node not found")
        cmd = [
            node,
            str(script),
            transport,
        ]
    elif cfg.binding == "cpp":
        runner = BINDINGS_DIR / "cpp" / "perf" / "multi" / "build" / (
            "perf_multi_main.exe" if IS_WINDOWS else "perf_multi_main"
        )
        if role == "server":
            cmd = [
                str(runner),
                "--multi-server",
                pattern,
                transport,
                str(size if size is not None else 64),
            ]
        else:
            cmd = [
                str(runner),
                "--multi-client",
                pattern,
                transport,
                str(size if size is not None else 64),
            ]
    elif cfg.binding == "dotnet":
        dotnet = shutil.which("dotnet")
        if not dotnet:
            raise RuntimeError("dotnet not found")
        dll = (
            BINDINGS_DIR
            / "dotnet"
            / "perf"
            / "multi"
            / "Zlink.BindingBench.Multi"
            / "bin"
            / "Release"
            / "net8.0"
            / "Zlink.BindingBench.Multi.dll"
        )
        if role == "server":
            cmd = [
                dotnet,
                str(dll),
                "--multi-server",
                pattern,
                transport,
                str(size if size is not None else 64),
            ]
        else:
            cmd = [
                dotnet,
                str(dll),
                "--multi-client",
                pattern,
                transport,
                str(size if size is not None else 64),
            ]
    elif cfg.binding == "java":
        java = shutil.which("java")
        if not java:
            raise RuntimeError("java not found")
        cp = os.pathsep.join(
            [
                str(BINDINGS_DIR / "java" / "build" / "classes" / "java" / "main"),
                str(BINDINGS_DIR / "java" / "build" / "classes" / "java" / "test"),
                str(BINDINGS_DIR / "java" / "build" / "resources" / "main"),
            ]
        )
        if role == "server":
            cmd = [
                java,
                "-cp",
                cp,
                "dev.kairoscode.zlink.integration.bench.PerfMultiMain",
                "--multi-server",
                pattern,
                transport,
                str(size if size is not None else 64),
            ]
        else:
            cmd = [
                java,
                "-cp",
                cp,
                "dev.kairoscode.zlink.integration.bench.PerfMultiMain",
                "--multi-client",
                pattern,
                transport,
                str(size if size is not None else 64),
            ]
    else:
        raise RuntimeError(
            f"unsupported split multi binding: {cfg.binding}"
        )

    if size is not None and cfg.binding in {"python", "node"}:
        cmd.append(str(size))
    if endpoint:
        cmd.extend(["--endpoint", endpoint])
    return cmd


def prepare_runtime_env(cfg: SuiteConfig, native_dir: Path) -> Dict[str, str]:
    env = os.environ.copy()

    def set_perf_env(name: str, value: str) -> None:
        env[name] = value

    # hard reset dynamic loader paths to distribution-only paths
    if IS_WINDOWS:
        env["PATH"] = str(native_dir) + os.pathsep + env.get("PATH", "")
    elif IS_MACOS:
        env["DYLD_LIBRARY_PATH"] = str(native_dir)
    else:
        env["LD_LIBRARY_PATH"] = str(native_dir)

    # bindings consistently honor this env var when resolving the native core lib.
    native_candidates = binding_native_candidates(cfg.binding)
    if native_candidates:
        env["ZLINK_LIBRARY_PATH"] = str(native_candidates[0])

    if cfg.io_threads is not None:
        set_perf_env("PERF_IO_THREADS", str(cfg.io_threads))

    set_perf_env("PERF_MSG_SIZES", ",".join(str(s) for s in cfg.msg_sizes))

    if cfg.transports_override:
        set_perf_env("PERF_TRANSPORTS", ",".join(cfg.transports_override))

    if cfg.pin_cpu and not IS_WINDOWS:
        set_perf_env("PERF_TASKSET", "1")

    if cfg.suite == "multi":
        set_perf_env("PERF_MULTI_WARMUP_SECONDS", str(cfg.multi_warmup_seconds))
        set_perf_env(
            "PERF_MULTI_DURATION_SECONDS", str(cfg.multi_duration_seconds)
        )
        set_perf_env("PERF_MULTI_CLIENTS", str(cfg.multi_clients))
        set_perf_env("PERF_MULTI_HWM", str(cfg.multi_hwm))
        set_perf_env("PERF_MULTI_SNDTIMEO_MS", str(cfg.multi_sndtimeo_ms))
        set_perf_env("PERF_MULTI_RCVTIMEO_MS", str(cfg.multi_rcvtimeo_ms))
        set_perf_env(
            "PERF_MULTI_SERVER_READY_TIMEOUT_MS",
            str(cfg.multi_server_ready_timeout_ms),
        )
        set_perf_env(
            "PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS",
            str(cfg.multi_server_shutdown_timeout_ms),
        )
        set_perf_env("PERF_MULTI_SERVER_BIND_PORT", str(cfg.multi_server_bind_port))
        set_perf_env(
            "PERF_MULTI_CONNECT_READY_TIMEOUT_MS",
            str(cfg.multi_connect_ready_timeout_ms),
        )
        set_perf_env("PERF_MULTI_MONITOR_HWM", str(cfg.multi_monitor_hwm))
        if cfg.multi_connect_concurrency is not None:
            set_perf_env(
                "PERF_MULTI_CONNECT_CONCURRENCY",
                str(cfg.multi_connect_concurrency),
            )
        if cfg.multi_drain_ms is not None:
            set_perf_env("PERF_MULTI_DRAIN_MS", str(cfg.multi_drain_ms))

    return env


def env_int_from_map(env: Dict[str, str], name: str, default: int) -> int:
    raw = env.get(name)
    if raw is None or raw == "":
        raw = os.environ.get(name, "")
    try:
        parsed = int(str(raw).strip())
    except Exception:
        return default
    return parsed if parsed > 0 else default


def ensure_stream_client_bin() -> Path:
    if STREAM_CLIENT_BIN.exists():
        return STREAM_CLIENT_BIN
    if IS_WINDOWS:
        raise RuntimeError(
            f"shared stream client is missing: {STREAM_CLIENT_BIN}"
        )
    proc = subprocess.run(
        [str(STREAM_CLIENT_BUILD_SCRIPT)],
        cwd=str(STREAM_CLIENT_DIR),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "failed to build shared stream client:\n"
            + (proc.stdout or "")
            + (proc.stderr or "")
        )
    if not STREAM_CLIENT_BIN.exists():
        raise RuntimeError(f"shared stream client binary not found: {STREAM_CLIENT_BIN}")
    return STREAM_CLIENT_BIN


def stream_shared_client_cmd(
    cfg: SuiteConfig,
    env: Dict[str, str],
    pattern: str,
    transport: str,
    size: int,
    endpoint: str,
    suite: str,
) -> List[str]:
    client_bin = ensure_stream_client_bin()
    cmd: List[str] = [
        str(client_bin),
        "--pattern",
        pattern,
        "--transport",
        transport,
        "--endpoint",
        endpoint,
        "--sizes",
        str(size),
        "--runs",
        "1",
    ]

    if suite == "single":
        warmup_count = env_int_from_map(env, "PERF_WARMUP_COUNT", 1000)
        lat_count = env_int_from_map(env, "PERF_LAT_COUNT", 500)
        msg_count = env_int_from_map(env, "PERF_MSG_COUNT", 10000)
        io_threads = env_int_from_map(
            env, "PERF_IO_THREADS", cfg.io_threads if cfg.io_threads else 1
        )
        cmd.extend(
            [
                "--warmup-count",
                str(warmup_count),
                "--lat-count",
                str(lat_count),
                "--msg-count",
                str(msg_count),
                "--io-threads",
                str(io_threads),
                "--print-perf-result",
                "2",
                "--send-stop-token",
                "1",
                "--stop-token",
                "__zlink_perf_stop__",
            ]
        )
        return cmd

    clients = env_int_from_map(env, "PERF_MULTI_CLIENTS", cfg.multi_clients)
    warmup_seconds = env_int_from_map(
        env, "PERF_MULTI_WARMUP_SECONDS", cfg.multi_warmup_seconds
    )
    duration_seconds = env_int_from_map(
        env, "PERF_MULTI_DURATION_SECONDS", cfg.multi_duration_seconds
    )
    lat_count = env_int_from_map(env, "PERF_LAT_COUNT", 200)
    io_threads = env_int_from_map(
        env, "PERF_IO_THREADS", cfg.io_threads if cfg.io_threads else 1
    )
    latency_sample_rate = env_int_from_map(env, "PERF_MULTI_LATENCY_SAMPLE_RATE", 1)
    cmd.extend(
        [
            "--ccu",
            str(clients),
            "--warmup",
            str(warmup_seconds),
            "--duration",
            str(duration_seconds),
            "--lat-count",
            str(lat_count),
            "--io-threads",
            str(io_threads),
            "--latency-sample-rate",
            str(latency_sample_rate),
            "--print-perf-result",
            "2",
            "--send-stop-token",
            "1",
            "--stop-token",
            "__zlink_perf_stop__",
        ]
    )
    return cmd


def parse_result_line(line: str) -> Optional[Tuple[str, str, str, int, str, float]]:
    if not line.startswith("RESULT,"):
        return None
    parts = line.strip().split(",")
    if len(parts) != 7:
        return None
    try:
        lib = parts[1].strip()
        pattern = parts[2].strip().upper()
        transport = parts[3].strip().lower()
        size = int(parts[4].strip())
        metric = parts[5].strip().lower()
        value = float(parts[6].strip())
    except ValueError:
        return None
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
        return None
    return lib, pattern, transport, size, metric, value


def run_command_with_metrics(cmd: List[str], env: Dict[str, str], timeout_sec: int) -> Dict[str, object]:
    start = time.monotonic()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)

    cpu_start = None
    cpu_end = None
    rss_end = None

    def read_linux_cpu_ticks(pid: int) -> Optional[int]:
        try:
            with open(f"/proc/{pid}/stat", "r", encoding="utf-8", errors="ignore") as fh:
                parts = fh.read().strip().split()
            if len(parts) < 15:
                return None
            return int(parts[13]) + int(parts[14])
        except Exception:
            return None

    def read_linux_mem_mb(pid: int) -> Optional[float]:
        try:
            with open(f"/proc/{pid}/status", "r", encoding="utf-8", errors="ignore") as fh:
                for line in fh:
                    if line.startswith("VmRSS:"):
                        cols = line.split()
                        if len(cols) >= 2:
                            return float(cols[1]) / 1024.0
        except Exception:
            return None
        return None

    sampling_mode = ""
    win_handle = None
    read_win_cpu_ticks = None
    read_win_mem_mb = None
    close_win_handle = None

    if IS_LINUX and os.path.exists("/proc"):
        sampling_mode = "linux"
        cpu_start = read_linux_cpu_ticks(proc.pid)
        cpu_end = cpu_start
    elif IS_WINDOWS:
        try:
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

            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            psapi = ctypes.WinDLL("psapi", use_last_error=True)

            kernel32.OpenProcess.argtypes = [
                wintypes.DWORD,
                wintypes.BOOL,
                wintypes.DWORD,
            ]
            kernel32.OpenProcess.restype = wintypes.HANDLE
            kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
            kernel32.CloseHandle.restype = wintypes.BOOL
            kernel32.GetProcessTimes.argtypes = [
                wintypes.HANDLE,
                ctypes.POINTER(FILETIME),
                ctypes.POINTER(FILETIME),
                ctypes.POINTER(FILETIME),
                ctypes.POINTER(FILETIME),
            ]
            kernel32.GetProcessTimes.restype = wintypes.BOOL
            psapi.GetProcessMemoryInfo.argtypes = [
                wintypes.HANDLE,
                ctypes.POINTER(PROCESS_MEMORY_COUNTERS),
                wintypes.DWORD,
            ]
            psapi.GetProcessMemoryInfo.restype = wintypes.BOOL

            def _to_u64(ft: FILETIME) -> int:
                return (int(ft.dwHighDateTime) << 32) | int(ft.dwLowDateTime)

            def _read_win_cpu_ticks(handle) -> Optional[int]:
                if not handle:
                    return None
                creation = FILETIME()
                exit_time = FILETIME()
                kernel = FILETIME()
                user = FILETIME()
                ok = kernel32.GetProcessTimes(
                    handle,
                    ctypes.byref(creation),
                    ctypes.byref(exit_time),
                    ctypes.byref(kernel),
                    ctypes.byref(user),
                )
                if not ok:
                    return None
                return _to_u64(kernel) + _to_u64(user)

            def _read_win_mem_mb(handle) -> Optional[float]:
                if not handle:
                    return None
                counters = PROCESS_MEMORY_COUNTERS()
                counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
                ok = psapi.GetProcessMemoryInfo(
                    handle, ctypes.byref(counters), counters.cb
                )
                if not ok:
                    return None
                return float(counters.WorkingSetSize) / (1024.0 * 1024.0)

            access = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ
            win_handle = kernel32.OpenProcess(access, False, int(proc.pid))
            if win_handle:
                sampling_mode = "windows"
                read_win_cpu_ticks = _read_win_cpu_ticks
                read_win_mem_mb = _read_win_mem_mb
                close_win_handle = kernel32.CloseHandle
                cpu_start = read_win_cpu_ticks(win_handle)
                cpu_end = cpu_start
        except Exception:
            sampling_mode = ""

    timed_out = False
    while True:
        rc = proc.poll()
        if sampling_mode == "linux":
            tick = read_linux_cpu_ticks(proc.pid)
            if tick is not None:
                cpu_end = tick
            rss = read_linux_mem_mb(proc.pid)
            if rss is not None:
                rss_end = rss
        elif sampling_mode == "windows" and win_handle:
            tick = read_win_cpu_ticks(win_handle) if read_win_cpu_ticks else None
            if tick is not None:
                cpu_end = tick
            rss = read_win_mem_mb(win_handle) if read_win_mem_mb else None
            if rss is not None:
                rss_end = rss
        if rc is not None:
            break
        if (time.monotonic() - start) > timeout_sec:
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

    elapsed = max(1e-6, time.monotonic() - start)
    cpu_pct = None
    if sampling_mode == "linux" and cpu_start is not None and cpu_end is not None:
        try:
            hz = float(os.sysconf("SC_CLK_TCK"))
        except Exception:
            hz = 100.0
        delta = max(0.0, float(cpu_end - cpu_start)) / max(1.0, hz)
        ncpu = max(1.0, float(os.cpu_count() or 1))
        cpu_pct = (delta / (elapsed * ncpu)) * 100.0
    elif sampling_mode == "windows" and cpu_start is not None and cpu_end is not None:
        # FILETIME unit is 100ns.
        delta_cpu_sec = max(0.0, float(cpu_end - cpu_start)) / 10_000_000.0
        ncpu = max(1.0, float(os.cpu_count() or 1))
        cpu_pct = (delta_cpu_sec / (elapsed * ncpu)) * 100.0

    if win_handle and close_win_handle:
        try:
            close_win_handle(win_handle)
        except Exception:
            pass

    return {
        "returncode": proc.returncode,
        "stdout": stdout or "",
        "stderr": stderr or "",
        "timed_out": timed_out,
        "cpu_pct": cpu_pct,
        "mem_mb": rss_end,
    }


@dataclass
class ProcessCapture:
    proc: subprocess.Popen[str]
    stdout_lines: List[str]
    stderr_lines: List[str]
    stdout_queue: "queue.Queue[str]"
    _threads: List[threading.Thread]

    def wait(self, timeout: Optional[float] = None) -> int:
        return self.proc.wait(timeout=timeout)

    def poll(self) -> Optional[int]:
        return self.proc.poll()

    @property
    def returncode(self) -> Optional[int]:
        return self.proc.returncode

    def terminate(self) -> None:
        if self.proc.poll() is not None:
            return
        self.proc.terminate()

    def kill(self) -> None:
        if self.proc.poll() is not None:
            return
        self.proc.kill()

    def join_readers(self, timeout: float = 1.0) -> None:
        for th in self._threads:
            th.join(timeout=timeout)

    def stdout_text(self) -> str:
        return "".join(self.stdout_lines)

    def stderr_text(self) -> str:
        return "".join(self.stderr_lines)


def spawn_capture_process(cmd: List[str], env: Dict[str, str]) -> ProcessCapture:
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
        bufsize=1,
    )
    stdout_lines: List[str] = []
    stderr_lines: List[str] = []
    stdout_queue: "queue.Queue[str]" = queue.Queue()
    threads: List[threading.Thread] = []

    def _pump_stdout() -> None:
        stream = proc.stdout
        if stream is None:
            return
        try:
            for line in stream:
                stdout_lines.append(line)
                stdout_queue.put(line)
        finally:
            try:
                stream.close()
            except Exception:
                pass

    def _pump_stderr() -> None:
        stream = proc.stderr
        if stream is None:
            return
        try:
            for line in stream:
                stderr_lines.append(line)
        finally:
            try:
                stream.close()
            except Exception:
                pass

    t_out = threading.Thread(target=_pump_stdout, daemon=True)
    t_err = threading.Thread(target=_pump_stderr, daemon=True)
    t_out.start()
    t_err.start()
    threads.extend([t_out, t_err])

    return ProcessCapture(
        proc=proc,
        stdout_lines=stdout_lines,
        stderr_lines=stderr_lines,
        stdout_queue=stdout_queue,
        _threads=threads,
    )


def wait_for_server_ready(
    capture: ProcessCapture, timeout_ms: int
) -> Tuple[bool, Optional[str]]:
    deadline = time.monotonic() + (max(timeout_ms, 1) / 1000.0)
    endpoint: Optional[str] = None

    while time.monotonic() < deadline:
        if capture.poll() is not None and capture.stdout_queue.empty():
            break
        try:
            line = capture.stdout_queue.get(timeout=0.05)
        except queue.Empty:
            continue
        raw = line.strip()
        if not raw:
            continue
        if raw.startswith("READY,"):
            endpoint = raw.split(",", 1)[1].strip()
            if endpoint:
                return True, endpoint

    return False, endpoint


def parse_result_metrics_from_text(
    text: str, requested_pattern: str, transport: str, size: int
) -> Dict[str, float]:
    metrics: Dict[str, float] = {}
    accepted_patterns = {requested_pattern.strip().upper()}
    for raw in text.splitlines():
        parsed = parse_result_line(raw.strip())
        if not parsed:
            continue
        _, line_pattern, line_transport, line_size, metric, value = parsed
        if (
            line_pattern in accepted_patterns
            and line_transport == transport
            and line_size == size
        ):
            metrics[metric] = value
    return metrics


def detect_status_token(text: str) -> Optional[str]:
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("UNSUPPORTED,"):
            return "unsupported"
        if line.startswith("SKIP,"):
            return "skip"
    return None


def parse_run_outcome(
    requested_pattern: str,
    expected_output_pattern: str,
    transport: str,
    size: int,
    sampled: Dict[str, object],
    *,
    process_role: str = "single",
) -> RunOutcome:
    stdout = str(sampled.get("stdout", ""))
    stderr = str(sampled.get("stderr", ""))
    rc = int(sampled.get("returncode") or 0)

    if sampled.get("timed_out", False):
        return RunOutcome(status="fail", metrics={}, reason="timeout")

    status_token = detect_status_token(stdout)

    metrics: Dict[str, float] = {}
    warnings: List[str] = []
    accepted_patterns = {
        requested_pattern.strip().upper(),
        expected_output_pattern.strip().upper(),
    }
    for raw in stdout.splitlines():
        parsed = parse_result_line(raw)
        if not parsed:
            if raw.startswith("RESULT,"):
                warnings.append(f"invalid RESULT line ignored: {raw}")
            continue
        _, line_pattern, line_transport, line_size, metric, value = parsed
        if (
            line_pattern not in accepted_patterns
            or line_transport != transport
            or line_size != size
        ):
            continue
        key = metric
        if key in metrics:
            warnings.append(
                f"duplicate RESULT metric, keeping last: pattern={requested_pattern} transport={transport} size={size} metric={metric}"
            )
        metrics[key] = value

    if status_token == "unsupported" and not metrics:
        return RunOutcome(status="unsupported", metrics={}, reason="unsupported", warnings=warnings)
    if status_token == "skip" and not metrics:
        return RunOutcome(status="skip", metrics={}, reason="skip", warnings=warnings)

    if rc != 0 and not metrics:
        if stderr.strip():
            first = stderr.strip().splitlines()[0]
            warnings.append(
                f"{requested_pattern} {transport} {size} non-zero exit({rc}) stderr: {first}"
            )
        return RunOutcome(status="fail", metrics={}, reason=f"non_zero_exit_{rc}", warnings=warnings)

    if "throughput" not in metrics or "latency" not in metrics:
        return RunOutcome(status="fail", metrics=metrics, reason="no_data", warnings=warnings)

    if "bandwidth" not in metrics:
        return RunOutcome(status="fail", metrics=metrics, reason="no_bandwidth", warnings=warnings)

    # informational metrics fallback from process sampling
    if process_role == "client":
        if "client_cpu_pct" not in metrics and sampled.get("cpu_pct") is not None:
            metrics["client_cpu_pct"] = float(sampled["cpu_pct"])
        if "client_mem_mb" not in metrics and sampled.get("mem_mb") is not None:
            metrics["client_mem_mb"] = float(sampled["mem_mb"])
    else:
        if "cpu_pct" not in metrics and sampled.get("cpu_pct") is not None:
            metrics["cpu_pct"] = float(sampled["cpu_pct"])
        if "mem_mb" not in metrics and sampled.get("mem_mb") is not None:
            metrics["mem_mb"] = float(sampled["mem_mb"])

    return RunOutcome(status="success", metrics=metrics, warnings=warnings)


def resolve_transports_for_pattern(cfg: SuiteConfig, pattern: str) -> List[str]:
    if cfg.suite == "single":
        base = DEFAULT_STREAM_TRANSPORTS if pattern in SINGLE_STREAM_PATTERNS else DEFAULT_SINGLE_SOCKET_TRANSPORTS
    else:
        base = DEFAULT_MULTI_TRANSPORTS

    if not cfg.transports_override:
        return list(base)

    return [t for t in base if t in cfg.transports_override]


def resolve_sizes_for_pattern(cfg: SuiteConfig, pattern: str) -> List[int]:
    if cfg.msg_sizes_overridden:
        return list(cfg.msg_sizes)

    if cfg.suite == "single":
        if pattern in SINGLE_STREAM_SIZE_PATTERNS:
            return list(DEFAULT_STREAM_MSG_SIZES)
        return list(DEFAULT_MSG_SIZES)

    if pattern in MULTI_STREAM_PATTERNS:
        return list(DEFAULT_STREAM_MSG_SIZES)
    return list(DEFAULT_MSG_SIZES)


def sleep_ms(ms: int) -> None:
    if ms > 0:
        time.sleep(ms / 1000.0)


def aggregate_combo(outcomes: List[RunOutcome]) -> ComboStats:
    success = [o for o in outcomes if o.status == "success"]
    if success:
        thr_vals = [o.metrics["throughput"] for o in success if "throughput" in o.metrics]
        bw_vals = [o.metrics["bandwidth"] for o in success if "bandwidth" in o.metrics]
        lat_vals = [o.metrics["latency"] for o in success if "latency" in o.metrics]
        cpu_vals = [o.metrics["cpu_pct"] for o in success if "cpu_pct" in o.metrics]
        mem_vals = [o.metrics["mem_mb"] for o in success if "mem_mb" in o.metrics]
        client_cpu_vals = [
            o.metrics["client_cpu_pct"]
            for o in success
            if "client_cpu_pct" in o.metrics
        ]
        client_mem_vals = [
            o.metrics["client_mem_mb"]
            for o in success
            if "client_mem_mb" in o.metrics
        ]
        server_cpu_vals = [
            o.metrics["server_cpu_pct"]
            for o in success
            if "server_cpu_pct" in o.metrics
        ]
        server_mem_vals = [
            o.metrics["server_mem_mb"]
            for o in success
            if "server_mem_mb" in o.metrics
        ]
        return ComboStats(
            status="success",
            throughput=statistics.median(thr_vals) if thr_vals else 0.0,
            bandwidth=statistics.median(bw_vals) if bw_vals else 0.0,
            latency=statistics.median(lat_vals) if lat_vals else 0.0,
            cpu_pct=statistics.median(cpu_vals) if cpu_vals else None,
            mem_mb=statistics.median(mem_vals) if mem_vals else None,
            client_cpu_pct=statistics.median(client_cpu_vals)
            if client_cpu_vals
            else None,
            client_mem_mb=statistics.median(client_mem_vals)
            if client_mem_vals
            else None,
            server_cpu_pct=statistics.median(server_cpu_vals)
            if server_cpu_vals
            else None,
            server_mem_mb=statistics.median(server_mem_vals)
            if server_mem_vals
            else None,
        )

    if any(o.status == "unsupported" for o in outcomes):
        return ComboStats(status="unsupported", reason="unsupported")
    if any(o.status == "skip" for o in outcomes):
        return ComboStats(status="skip", reason="skip")

    reason = next((o.reason for o in outcomes if o.reason), "failed")
    return ComboStats(status="fail", reason=reason)


def format_thr(v: float, pattern: str, suite: str) -> str:
    unit = throughput_unit_label(pattern, suite)
    return f"{v/1000.0:8.2f} {unit}"


def format_bw(v: float) -> str:
    return f"{v:8.1f} MB/s"


def format_lat(v: float) -> str:
    return f"{v:8.2f} us"


def format_cpu(v: Optional[float]) -> str:
    if v is None:
        return "N/A"
    return f"{v:4.1f}"


def format_mem(v: Optional[float]) -> str:
    if v is None:
        return "N/A"
    return f"{v:6.1f}"


def build_table_lines(
    cfg: SuiteConfig,
    patterns: Sequence[str],
    combo_results: Dict[Tuple[str, str, int], ComboStats],
) -> List[str]:
    lines: List[str] = []
    divider = "=" * 79

    for pattern_index, pattern in enumerate(patterns):
        transports = resolve_transports_for_pattern(cfg, pattern)
        direction = pattern_direction(pattern, cfg.suite)
        if pattern_index > 0:
            lines.append("")
            lines.append(divider)
            lines.append("")
        lines.append(f"## PATTERN: {pattern} ({direction})")
        for tr in transports:
            lines.append("")
            lines.append(f"### Transport: {tr}")
            if cfg.suite == "multi":
                lines.append(
                    "| Size     |       Throughput | Bandwidth |      Latency | C.CPU% | C.Mem MB | S.CPU% | S.Mem MB |"
                )
                lines.append(
                    "|----------|------------------|-----------|--------------|--------|----------|--------|----------|"
                )
            else:
                lines.append(
                    "| Size     |       Throughput | Bandwidth |      Latency | CPU% | Mem MB |"
                )
                lines.append("|----------|------------------|-----------|--------------|------|--------|")

            for size in resolve_sizes_for_pattern(cfg, pattern):
                combo = combo_results.get((pattern, tr, size))
                if combo is None or combo.status != "success":
                    if cfg.suite == "multi":
                        lines.append(
                            f"| {size}B | N/A | N/A | N/A | N/A | N/A | N/A | N/A |"
                        )
                    else:
                        lines.append(f"| {size}B | N/A | N/A | N/A | N/A | N/A |")
                    continue

                if cfg.suite == "multi":
                    client_cpu = (
                        combo.client_cpu_pct
                        if combo.client_cpu_pct is not None
                        else combo.cpu_pct
                    )
                    client_mem = (
                        combo.client_mem_mb
                        if combo.client_mem_mb is not None
                        else combo.mem_mb
                    )
                    server_cpu = combo.server_cpu_pct
                    server_mem = combo.server_mem_mb
                    lines.append(
                        f"| {size}B | {format_thr(combo.throughput, pattern, cfg.suite):>16} | {format_bw(combo.bandwidth):>9} | {format_lat(combo.latency):>12} | {format_cpu(client_cpu):>6} | {format_mem(client_mem):>8} | {format_cpu(server_cpu):>6} | {format_mem(server_mem):>8} |"
                    )
                else:
                    lines.append(
                        f"| {size}B | {format_thr(combo.throughput, pattern, cfg.suite):>16} | {format_bw(combo.bandwidth):>9} | {format_lat(combo.latency):>12} | {format_cpu(combo.cpu_pct):>4} | {format_mem(combo.mem_mb):>6} |"
                    )

    if lines and lines[0] == "":
        return lines[1:]
    return lines


def collect_host_meta() -> Dict[str, str]:
    meta = {
        "os": platform.platform(),
        "cpu": platform.processor() or platform.machine(),
        "cores": str(os.cpu_count() or 1),
        "build": "Release",
        "commit": "unknown",
        "timestamp": local_offset_iso(),
    }

    try:
        meta["commit"] = (
            subprocess.check_output(["git", "-C", str(ROOT_DIR), "rev-parse", "--short", "HEAD"], text=True)
            .strip()
        )
    except Exception:
        pass

    if IS_LINUX:
        try:
            load_avg = os.getloadavg()
            meta["load_avg"] = f"{load_avg[0]:.2f} {load_avg[1]:.2f} {load_avg[2]:.2f}"
        except Exception:
            pass

    return meta


def parse_result_file(path: Path) -> Tuple[Dict[str, str], Dict[MetricKey, float]]:
    meta: Dict[str, str] = {}
    results: Dict[MetricKey, float] = {}
    with path.open("r", encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("META,"):
                parts = line.split(",", 2)
                if len(parts) == 3:
                    meta[parts[1]] = parts[2]
                continue
            parsed = parse_result_line(line)
            if not parsed:
                continue
            lib, pattern, transport, size, metric, value = parsed
            results[(lib, pattern, transport, size, metric)] = value
    return meta, results


def load_rolling_baseline(source_dir: Path, n: int, keys: Iterable[MetricKey]) -> Tuple[Dict[MetricKey, float], Dict[MetricKey, int]]:
    baseline: Dict[MetricKey, float] = {}
    counts: Dict[MetricKey, int] = {}
    wanted = list(keys)
    buckets: Dict[MetricKey, List[float]] = {k: [] for k in wanted}

    files = sorted(
        [p for p in source_dir.glob("perf_*.txt") if p.is_file()],
        key=lambda p: p.name,
        reverse=True,
    )

    for file in files:
        try:
            meta, results = parse_result_file(file)
        except Exception:
            continue
        if meta.get("status") and meta.get("status") != "complete":
            continue
        for key in wanted:
            if len(buckets[key]) >= n:
                continue
            if key in results:
                buckets[key].append(results[key])
        if all(len(buckets[k]) >= n for k in wanted):
            break

    for key, values in buckets.items():
        if values:
            counts[key] = len(values)
        if values and len(values) >= 1:
            baseline[key] = statistics.median(values)

    return baseline, counts


def load_fixed_baseline_file(path: Path) -> Dict[MetricKey, float]:
    _, results = parse_result_file(path)
    return results


def threshold_file_path() -> Path:
    override = env_first("PERF_THRESHOLDS_FILE")
    if override:
        return Path(override)
    return CORE_PERF_DIR / "thresholds.json"


def load_threshold_overrides(logger: Tee) -> Dict[str, Dict[str, float]]:
    path = threshold_file_path()
    if not path.exists():
        return {}

    try:
        obj = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        logger.print(f"warning: failed to parse thresholds file {path}: {exc}")
        return {}

    if not isinstance(obj, dict):
        logger.print(f"warning: thresholds file must be an object: {path}")
        return {}

    out: Dict[str, Dict[str, float]] = {}
    key_re = re.compile(r"^[^/]+/[^/]+/[^/]+$")

    for key, value in obj.items():
        if not isinstance(key, str) or not key_re.match(key):
            logger.print(f"warning: invalid threshold key ignored: {key}")
            continue
        if not isinstance(value, dict):
            logger.print(f"warning: invalid threshold entry ignored: {key}")
            continue

        metric = key.split("/")[-1].lower()
        parsed: Dict[str, float] = {}
        for field in ("warning", "fail"):
            if field not in value:
                continue
            raw = value[field]
            if not isinstance(raw, (int, float)):
                logger.print(f"warning: {key}.{field} is not numeric")
                continue
            num = float(raw)
            if metric in ("throughput", "bandwidth") and num > 0:
                logger.print(f"warning: {key}.{field} should be <=0, applying sign flip")
                num = -num
            if metric == "latency" and num < 0:
                logger.print(f"warning: {key}.{field} should be >=0, applying sign flip")
                num = -num
            parsed[field] = num
        if parsed:
            out[key] = parsed

    return out


def resolve_threshold(
    overrides: Dict[str, Dict[str, float]],
    pattern: str,
    transport: str,
    metric: str,
) -> Dict[str, float]:
    candidates = [
        f"{pattern}/{transport}/{metric}",
        f"{pattern}/*/{metric}",
        f"*/{transport}/{metric}",
        f"*/*/{metric}",
    ]

    base = dict(DEFAULT_THRESHOLDS[metric])
    for key in candidates:
        hit = overrides.get(key)
        if not hit:
            continue
        if "warning" in hit:
            base["warning"] = hit["warning"]
        if "fail" in hit:
            base["fail"] = hit["fail"]
        break

    return base


def compare_to_baseline(
    cfg: SuiteConfig,
    logger: Tee,
    current: Dict[MetricKey, float],
    baseline: Dict[MetricKey, float],
    overrides: Dict[str, Dict[str, float]],
) -> Tuple[List[str], List[str], int]:
    warnings: List[str] = []
    fails: List[str] = []
    skipped = 0

    for key in sorted(current.keys()):
        _, pattern, transport, size, metric = key
        if metric not in ("throughput", "bandwidth", "latency"):
            continue
        baseline_val = baseline.get(key)
        if baseline_val is None or abs(baseline_val) < 1e-12:
            skipped += 1
            warnings.append(
                f"{pattern} {transport} {size} {metric}: baseline missing/zero -> skipped"
            )
            continue

        cur = current[key]
        delta_pct = ((cur - baseline_val) / baseline_val) * 100.0
        th = resolve_threshold(overrides, pattern, transport, metric)
        warn = th["warning"]
        fail = th["fail"]

        if metric in ("throughput", "bandwidth"):
            if delta_pct <= fail:
                msg = f"{pattern} {transport} {size} {metric}: {delta_pct:+.2f}% (fail {fail:+.2f}%)"
                if cfg.mode == "gate":
                    fails.append(msg)
                else:
                    warnings.append(msg)
            elif delta_pct <= warn:
                warnings.append(
                    f"{pattern} {transport} {size} {metric}: {delta_pct:+.2f}% (warn {warn:+.2f}%)"
                )
        else:
            if delta_pct >= fail:
                msg = f"{pattern} {transport} {size} {metric}: {delta_pct:+.2f}% (fail +{fail:.2f}%)"
                if cfg.mode == "gate":
                    fails.append(msg)
                else:
                    warnings.append(msg)
            elif delta_pct >= warn:
                warnings.append(
                    f"{pattern} {transport} {size} {metric}: {delta_pct:+.2f}% (warn +{warn:.2f}%)"
                )

    return warnings, fails, skipped


def render_meta_result_lines(meta: Dict[str, str], results: Dict[MetricKey, float]) -> List[str]:
    lines: List[str] = []
    for key in (
        "os",
        "cpu",
        "cores",
        "build",
        "commit",
        "timestamp",
        "load_avg",
        "mode",
        "runs",
        "clients",
        "status",
        "expected",
        "actual",
    ):
        if key in meta:
            lines.append(f"META,{key},{meta[key]}")

    metric_order = {
        "throughput": 0,
        "bandwidth": 1,
        "latency": 2,
        "cpu_pct": 3,
        "mem_mb": 4,
        "client_cpu_pct": 3,
        "client_mem_mb": 4,
        "server_cpu_pct": 5,
        "server_mem_mb": 6,
    }

    def result_sort_key(item: Tuple[MetricKey, float]) -> Tuple[str, str, str, int, int]:
        (lib, pattern, transport, size, metric), _ = item
        return (
            lib,
            pattern,
            transport,
            size,
            metric_order.get(metric, 99),
        )

    for (lib, pattern, transport, size, metric), value in sorted(
        results.items(), key=result_sort_key
    ):
        lines.append(
            f"RESULT,{lib},{pattern},{transport},{size},{metric},{value:.2f}"
        )

    return lines


def write_tmp_or_baseline_file(
    path: Path,
    meta: Dict[str, str],
    results: Dict[MetricKey, float],
    table_lines: Sequence[str],
) -> None:
    lines = render_meta_result_lines(meta, results)
    lines.append("TABLE")
    lines.extend(table_lines)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_report_file(path: Path, table_lines: Sequence[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = "\n".join(table_lines).rstrip()
    if payload:
        payload += "\n"
    path.write_text(payload, encoding="utf-8")


def update_latest_symlink(baseline_dir: Path, target_file: Path) -> None:
    latest = baseline_dir / "latest.txt"
    try:
        if latest.exists() or latest.is_symlink():
            latest.unlink()
        latest.symlink_to(target_file.name)
    except Exception:
        # Windows fallback: copy file.
        try:
            shutil.copy2(target_file, latest)
        except Exception:
            pass


def single_default_runs(mode: str) -> int:
    if mode == "observe":
        return 1
    if mode == "trend":
        return 3
    return 5


def multi_default_runs(mode: str) -> int:
    if mode in ("observe", "trend"):
        return 3
    return 5


def make_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description="bindings perf policy runner")
    ap.add_argument("--binding", required=False)
    ap.add_argument("--suite", choices=("single", "multi"), required=True)
    ap.add_argument("--pattern", default="ALL")
    ap.add_argument("--build-dir", default="")  # accepted for CLI compatibility
    ap.add_argument("--runs", type=int, default=0)
    ap.add_argument("--reuse-build", action="store_true")
    ap.add_argument("--pin-cpu", action="store_true")
    ap.add_argument("--io-threads", type=int, default=None)
    ap.add_argument("--msg-sizes", default="")
    ap.add_argument("--transports", default="")
    ap.add_argument("--output", default="")
    ap.add_argument("--result", action="store_true")
    ap.add_argument(
        "--save",
        nargs="?",
        const="",
        default=None,
        help="save baseline to baseline/ (optional version)",
    )
    ap.add_argument("--results-dir", default="")
    ap.add_argument("--results-tag", default="")

    ap.add_argument("--mode", choices=("observe", "trend", "gate"), default="observe")
    ap.add_argument("--rolling-n", type=int, default=0)
    ap.add_argument("--baseline", default="latest")
    ap.add_argument("--baseline-file", default="")

    ap.add_argument("--multi-warmup-seconds", type=int, default=3)
    ap.add_argument("--multi-duration-seconds", type=int, default=5)
    ap.add_argument("--multi-clients", type=int, default=0)
    ap.add_argument("--multi-hwm", type=int, default=100000)
    ap.add_argument("--multi-sndtimeo-ms", type=int, default=5000)
    ap.add_argument("--multi-rcvtimeo-ms", type=int, default=5000)
    ap.add_argument("--multi-connect-concurrency", type=int, default=0)
    ap.add_argument("--multi-drain-ms", type=int, default=-1)
    ap.add_argument("--multi-transport-transition-ms", type=int, default=3000)
    ap.add_argument("--multi-pattern-transition-ms", type=int, default=3000)
    ap.add_argument("--multi-server-ready-timeout-ms", type=int, default=10000)
    ap.add_argument("--multi-server-shutdown-timeout-ms", type=int, default=5000)
    ap.add_argument("--multi-server-bind-port", type=int, default=0)
    ap.add_argument("--multi-connect-ready-timeout-ms", type=int, default=5000)
    ap.add_argument("--multi-monitor-hwm", type=int, default=200000)

    return ap


def resolve_config(args: argparse.Namespace) -> Tuple[SuiteConfig, List[str]]:
    binding = (args.binding or os.environ.get("BINDING") or "").strip().lower()
    if binding not in {"python", "node", "dotnet", "java", "cpp"}:
        raise ValueError("--binding is required and must be one of: python,node,dotnet,java,cpp")

    patterns = parse_patterns(args.pattern, args.suite)

    runs = args.runs
    if runs <= 0:
        runs = single_default_runs(args.mode) if args.suite == "single" else multi_default_runs(args.mode)

    if runs <= 0:
        raise ValueError("runs must be positive")

    reuse_default = False if args.suite == "single" else True
    reuse_build = args.reuse_build or reuse_default

    msg_sizes = DEFAULT_MSG_SIZES
    msg_sizes_overridden = False
    if args.msg_sizes:
        msg_sizes = parse_csv_ints(args.msg_sizes)
        msg_sizes_overridden = True
    else:
        env_msg_sizes = env_first("PERF_MSG_SIZES")
        if env_msg_sizes:
            msg_sizes = parse_csv_ints(env_msg_sizes)
            msg_sizes_overridden = True

    transports_override = None
    if args.transports:
        transports_override = parse_csv_names(args.transports)
    else:
        env_transports = env_first("PERF_TRANSPORTS")
        if env_transports:
            transports_override = parse_csv_names(env_transports)

    results_root = Path(args.results_dir) if args.results_dir else binding_perf_dir(binding) / "results"
    output = Path(args.output) if args.output else None
    baseline_file = Path(args.baseline_file) if args.baseline_file else None

    rolling_n_raw = env_first("PERF_ROLLING_N")
    rolling_n = args.rolling_n if args.rolling_n > 0 else int(rolling_n_raw or "10")
    if rolling_n <= 0:
        rolling_n = 10

    multi_clients = args.multi_clients
    if multi_clients <= 0:
        multi_clients = int(env_first("PERF_MULTI_CLIENTS") or "1000")

    def env_int_if_default(cli_value: int, default_value: int, env_name: str) -> int:
        raw = env_first(env_name)
        if raw is None:
            return cli_value
        try:
            parsed = int(raw)
        except ValueError:
            return cli_value
        if cli_value == default_value:
            return parsed
        return cli_value

    multi_warmup_seconds = max(
        0,
        env_int_if_default(
            args.multi_warmup_seconds, 3, "PERF_MULTI_WARMUP_SECONDS"
        ),
    )
    multi_duration_seconds = max(
        1,
        env_int_if_default(
            args.multi_duration_seconds, 5, "PERF_MULTI_DURATION_SECONDS"
        ),
    )
    multi_hwm = max(
        1, env_int_if_default(args.multi_hwm, 100000, "PERF_MULTI_HWM")
    )
    multi_sndtimeo_ms = max(
        1,
        env_int_if_default(args.multi_sndtimeo_ms, 5000, "PERF_MULTI_SNDTIMEO_MS"),
    )
    multi_rcvtimeo_ms = max(
        1,
        env_int_if_default(args.multi_rcvtimeo_ms, 5000, "PERF_MULTI_RCVTIMEO_MS"),
    )
    multi_transport_transition_ms = max(
        0,
        env_int_if_default(
            args.multi_transport_transition_ms,
            3000,
            "PERF_MULTI_TRANSPORT_TRANSITION_MS",
        ),
    )
    multi_pattern_transition_ms = max(
        0,
        env_int_if_default(
            args.multi_pattern_transition_ms, 3000, "PERF_MULTI_PATTERN_TRANSITION_MS"
        ),
    )
    multi_server_ready_timeout_ms = max(
        0,
        env_int_if_default(
            args.multi_server_ready_timeout_ms, 10000, "PERF_MULTI_SERVER_READY_TIMEOUT_MS"
        ),
    )
    multi_server_shutdown_timeout_ms = max(
        0,
        env_int_if_default(
            args.multi_server_shutdown_timeout_ms,
            5000,
            "PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS",
        ),
    )
    multi_server_bind_port = env_int_if_default(
        args.multi_server_bind_port, 0, "PERF_MULTI_SERVER_BIND_PORT"
    )
    multi_server_bind_port = max(0, min(65535, multi_server_bind_port))
    multi_connect_ready_timeout_ms = max(
        0,
        env_int_if_default(
            args.multi_connect_ready_timeout_ms,
            5000,
            "PERF_MULTI_CONNECT_READY_TIMEOUT_MS",
        ),
    )
    multi_monitor_hwm = max(
        0,
        env_int_if_default(args.multi_monitor_hwm, 200000, "PERF_MULTI_MONITOR_HWM"),
    )
    multi_connect_concurrency = args.multi_connect_concurrency
    if multi_connect_concurrency <= 0:
        raw_cc = env_first("PERF_MULTI_CONNECT_CONCURRENCY")
        if raw_cc:
            try:
                multi_connect_concurrency = int(raw_cc)
            except ValueError:
                multi_connect_concurrency = 0
    if multi_connect_concurrency <= 0:
        multi_connect_concurrency = None

    multi_drain_ms = args.multi_drain_ms
    if multi_drain_ms < 0:
        raw_drain = env_first("PERF_MULTI_DRAIN_MS")
        if raw_drain:
            try:
                multi_drain_ms = int(raw_drain)
            except ValueError:
                multi_drain_ms = -1
    if multi_drain_ms < 0:
        multi_drain_ms = None

    cfg = SuiteConfig(
        binding=binding,
        suite=args.suite,
        mode=args.mode,
        runs=runs,
        reuse_build=reuse_build,
        pin_cpu=args.pin_cpu,
        io_threads=args.io_threads,
        msg_sizes=msg_sizes,
        msg_sizes_overridden=msg_sizes_overridden,
        transports_override=transports_override,
        result=args.result,
        save_baseline=args.save if args.save is not None else None,
        results_root=results_root,
        results_tag=args.results_tag,
        output=output,
        rolling_n=rolling_n,
        baseline_version=args.baseline,
        baseline_file=baseline_file,
        multi_warmup_seconds=multi_warmup_seconds,
        multi_duration_seconds=multi_duration_seconds,
        multi_clients=max(1, multi_clients),
        multi_hwm=multi_hwm,
        multi_sndtimeo_ms=multi_sndtimeo_ms,
        multi_rcvtimeo_ms=multi_rcvtimeo_ms,
        multi_connect_concurrency=multi_connect_concurrency,
        multi_drain_ms=multi_drain_ms,
        multi_transport_transition_ms=multi_transport_transition_ms,
        multi_pattern_transition_ms=multi_pattern_transition_ms,
        multi_server_ready_timeout_ms=multi_server_ready_timeout_ms,
        multi_server_shutdown_timeout_ms=multi_server_shutdown_timeout_ms,
        multi_server_bind_port=multi_server_bind_port,
        multi_connect_ready_timeout_ms=multi_connect_ready_timeout_ms,
        multi_monitor_hwm=multi_monitor_hwm,
    )

    return cfg, patterns


def run_single_pattern_transport(
    cfg: SuiteConfig,
    pattern: str,
    transport: str,
    sizes: Sequence[int],
    env: Dict[str, str],
    logger: Tee,
    combo_results: Dict[Tuple[str, str, int], ComboStats],
    failures: List[Tuple[str, str, int, str]],
    warnings: List[str],
) -> None:
    for size in sizes:
        logger.print(f"    Testing {transport} | {size}B: ", end="", flush=True)
        outcomes: List[RunOutcome] = []
        for run_idx in range(cfg.runs):
            logger.print(f"{run_idx + 1} ", end="", flush=True)
            timeout = int(env_first("PERF_SINGLE_TIMEOUT_SECONDS") or "120")
            if (
                pattern in STREAM_SINGLE_PATTERNS
                and supports_split_multi(cfg.binding)
                and cfg.binding != "dotnet"
            ):
                server_pattern = SINGLE_TO_MULTI_STREAM_PATTERN.get(pattern, pattern)
                server_cmd = binding_multi_role_command(
                    cfg,
                    "server",
                    server_pattern,
                    transport,
                    size=size,
                )
                server_cap = spawn_capture_process(server_cmd, env)
                ready_ok, endpoint = wait_for_server_ready(
                    server_cap, cfg.multi_server_ready_timeout_ms
                )
                if not ready_ok or not endpoint:
                    if server_cap.poll() is None:
                        server_cap.terminate()
                        try:
                            server_cap.wait(timeout=0.5)
                        except subprocess.TimeoutExpired:
                            server_cap.kill()
                    server_cap.join_readers()
                    server_stdout = server_cap.stdout_text()
                    status_token = detect_status_token(server_stdout)
                    if status_token == "unsupported":
                        outcome = RunOutcome(
                            status="unsupported",
                            metrics={},
                            reason="unsupported",
                        )
                    elif status_token == "skip":
                        outcome = RunOutcome(status="skip", metrics={}, reason="skip")
                    else:
                        outcome = RunOutcome(
                            status="fail",
                            metrics={},
                            reason="server_ready_timeout",
                        )
                    if status_token == "unsupported":
                        warnings.append(
                            f"server reported unsupported before READY: pattern={pattern} transport={transport} size={size}"
                        )
                    elif status_token == "skip":
                        warnings.append(
                            f"server reported skip before READY: pattern={pattern} transport={transport} size={size}"
                        )
                    else:
                        warnings.append(
                            f"server READY timeout: pattern={pattern} transport={transport} size={size}"
                        )
                else:
                    client_cmd = stream_shared_client_cmd(
                        cfg=cfg,
                        env=env,
                        pattern=pattern,
                        transport=transport,
                        size=size,
                        endpoint=endpoint,
                        suite="single",
                    )
                    sampled = run_command_with_metrics(client_cmd, env, timeout)
                    outcome = parse_run_outcome(
                        pattern,
                        pattern,
                        transport,
                        size,
                        sampled,
                        process_role="single",
                    )

                    server_shutdown_timeout = max(
                        1, cfg.multi_server_shutdown_timeout_ms
                    ) / 1000.0
                    server_timed_out = False
                    try:
                        server_cap.wait(timeout=server_shutdown_timeout)
                    except subprocess.TimeoutExpired:
                        server_timed_out = True
                        server_cap.terminate()
                        try:
                            server_cap.wait(timeout=0.5)
                        except subprocess.TimeoutExpired:
                            server_cap.kill()
                    server_cap.join_readers()
                    if server_timed_out and outcome.status == "success":
                        outcome = RunOutcome(
                            status="fail",
                            metrics=outcome.metrics,
                            reason="server_shutdown_timeout",
                        )
                    elif (
                        server_cap.returncode is not None
                        and server_cap.returncode != 0
                        and outcome.status == "success"
                    ):
                        outcome = RunOutcome(
                            status="fail",
                            metrics=outcome.metrics,
                            reason=f"server_non_zero_exit_{server_cap.returncode}",
                        )
            else:
                cmd_prefix, out_pattern = binding_cmd_prefix(cfg, pattern)
                cmd = cmd_prefix + [transport, str(size)]
                sampled = run_command_with_metrics(cmd, env, timeout)
                outcome = parse_run_outcome(
                    pattern, out_pattern, transport, size, sampled
                )
            outcomes.append(outcome)
            if outcome.warnings:
                warnings.extend(outcome.warnings)

        combo = aggregate_combo(outcomes)
        combo_results[(pattern, transport, size)] = combo
        if combo.status == "fail":
            failures.append((pattern, transport, size, combo.reason))
            logger.print("(failures=1) Done")
        elif combo.status == "unsupported":
            logger.print("unsupported Done")
        elif combo.status == "skip":
            logger.print("skip Done")
        else:
            logger.print("Done")


def run_multi_pattern_transport(
    cfg: SuiteConfig,
    pattern: str,
    transport: str,
    sizes: Sequence[int],
    env: Dict[str, str],
    logger: Tee,
    combo_results: Dict[Tuple[str, str, int], ComboStats],
    failures: List[Tuple[str, str, int, str]],
    warnings: List[str],
) -> None:
    size_tag = ",".join(f"{size}B" for size in sizes)
    logger.print(f"    Testing {transport} | {size_tag}: ", end="", flush=True)

    combo_outcomes: Dict[Tuple[str, str, int], List[RunOutcome]] = {
        (pattern, transport, size): [] for size in sizes
    }
    timeout = max(120, cfg.multi_warmup_seconds + cfg.multi_duration_seconds + 60)

    for run_idx in range(cfg.runs):
        logger.print(f"{run_idx + 1} ", end="", flush=True)
        for size in sizes:
            combo_key = (pattern, transport, size)
            if supports_split_multi(cfg.binding):
                server_cmd = binding_multi_role_command(
                    cfg,
                    "server",
                    pattern,
                    transport,
                    size=size,
                )
                server_cap = spawn_capture_process(server_cmd, env)
                ready_ok, endpoint = wait_for_server_ready(
                    server_cap, cfg.multi_server_ready_timeout_ms
                )
                if not ready_ok or not endpoint:
                    if server_cap.poll() is None:
                        server_cap.terminate()
                        try:
                            server_cap.wait(timeout=0.5)
                        except subprocess.TimeoutExpired:
                            server_cap.kill()
                    server_cap.join_readers()
                    server_stdout = server_cap.stdout_text()
                    status_token = detect_status_token(server_stdout)
                    if status_token == "unsupported":
                        outcome = RunOutcome(
                            status="unsupported",
                            metrics={},
                            reason="unsupported",
                        )
                    elif status_token == "skip":
                        outcome = RunOutcome(status="skip", metrics={}, reason="skip")
                    else:
                        outcome = RunOutcome(
                            status="fail",
                            metrics={},
                            reason="server_ready_timeout",
                        )
                    combo_outcomes[combo_key].append(outcome)
                    if status_token == "unsupported":
                        warnings.append(
                            f"server reported unsupported before READY: pattern={pattern} transport={transport} size={size}"
                        )
                    elif status_token == "skip":
                        warnings.append(
                            f"server reported skip before READY: pattern={pattern} transport={transport} size={size}"
                        )
                    else:
                        warnings.append(
                            f"server READY timeout: pattern={pattern} transport={transport} size={size}"
                        )
                    continue

                if pattern in MULTI_STREAM_PATTERNS and cfg.binding != "dotnet":
                    client_cmd = stream_shared_client_cmd(
                        cfg=cfg,
                        env=env,
                        pattern=pattern,
                        transport=transport,
                        size=size,
                        endpoint=endpoint,
                        suite="multi",
                    )
                else:
                    client_cmd = binding_multi_role_command(
                        cfg,
                        "client",
                        pattern,
                        transport,
                        size=size,
                        endpoint=endpoint,
                    )
                sampled = run_command_with_metrics(client_cmd, env, timeout)
                outcome = parse_run_outcome(
                    pattern,
                    pattern,
                    transport,
                    size,
                    sampled,
                    process_role="client",
                )

                # Server-side informational metrics may be emitted by the server process.
                server_shutdown_timeout = max(
                    1, cfg.multi_server_shutdown_timeout_ms
                ) / 1000.0
                server_timed_out = False
                try:
                    server_cap.wait(timeout=server_shutdown_timeout)
                except subprocess.TimeoutExpired:
                    server_timed_out = True
                    server_cap.terminate()
                    try:
                        server_cap.wait(timeout=0.5)
                    except subprocess.TimeoutExpired:
                        server_cap.kill()

                server_cap.join_readers()
                server_stdout = server_cap.stdout_text()
                server_metrics = parse_result_metrics_from_text(
                    server_stdout, pattern, transport, size
                )
                if "server_cpu_pct" in server_metrics:
                    outcome.metrics["server_cpu_pct"] = server_metrics["server_cpu_pct"]
                if "server_mem_mb" in server_metrics:
                    outcome.metrics["server_mem_mb"] = server_metrics["server_mem_mb"]

                if server_timed_out and outcome.status == "success":
                    outcome = RunOutcome(
                        status="fail",
                        metrics=outcome.metrics,
                        reason="server_shutdown_timeout",
                    )
                elif (
                    server_cap.returncode is not None
                    and server_cap.returncode != 0
                    and outcome.status == "success"
                ):
                    outcome = RunOutcome(
                        status="fail",
                        metrics=outcome.metrics,
                        reason=f"server_non_zero_exit_{server_cap.returncode}",
                    )
            else:
                cmd_prefix, out_pattern = binding_cmd_prefix(cfg, pattern)
                cmd = cmd_prefix + [transport, str(size)]
                sampled = run_command_with_metrics(cmd, env, timeout)
                outcome = parse_run_outcome(
                    pattern, out_pattern, transport, size, sampled
                )
            combo_outcomes[combo_key].append(outcome)
            if outcome.warnings:
                warnings.extend(outcome.warnings)

        if run_idx + 1 < cfg.runs:
            sleep_ms(int(env_first("PERF_MULTI_RUN_COOLDOWN_MS") or "3000"))

    for size in sizes:
        combo_key = (pattern, transport, size)
        combo = aggregate_combo(combo_outcomes[combo_key])
        combo_results[combo_key] = combo
        if combo.status == "fail":
            failures.append((pattern, transport, size, combo.reason))

    if any(combo_results[(pattern, transport, size)].status == "fail" for size in sizes):
        logger.print("(failures=1) Done")
    elif all(
        combo_results[(pattern, transport, size)].status == "unsupported"
        for size in sizes
    ):
        logger.print("unsupported Done")
    elif all(combo_results[(pattern, transport, size)].status == "skip" for size in sizes):
        logger.print("skip Done")
    else:
        logger.print("Done")


def execute_benchmarks(
    cfg: SuiteConfig,
    patterns: Sequence[str],
    env: Dict[str, str],
    logger: Tee,
) -> Tuple[
    Dict[Tuple[str, str, int], ComboStats],
    List[Tuple[str, str, int, str]],
    List[str],
]:
    combo_results: Dict[Tuple[str, str, int], ComboStats] = {}
    failures: List[Tuple[str, str, int, str]] = []
    warnings: List[str] = []

    for pattern_index, pattern in enumerate(patterns):
        logger.print("")
        logger.print(f"  > Benchmarking current for {pattern}...")

        transports = resolve_transports_for_pattern(cfg, pattern)
        if not transports:
            logger.print("  Skipping pattern: no transports selected")
            continue

        for transport_index, transport in enumerate(transports):
            sizes = resolve_sizes_for_pattern(cfg, pattern)
            if cfg.suite == "single":
                run_single_pattern_transport(
                    cfg,
                    pattern,
                    transport,
                    sizes,
                    env,
                    logger,
                    combo_results,
                    failures,
                    warnings,
                )
            else:
                run_multi_pattern_transport(
                    cfg,
                    pattern,
                    transport,
                    sizes,
                    env,
                    logger,
                    combo_results,
                    failures,
                    warnings,
                )
                if transport_index + 1 < len(transports):
                    sleep_ms(cfg.multi_transport_transition_ms)

        if cfg.suite == "multi" and pattern_index + 1 < len(patterns):
            sleep_ms(cfg.multi_pattern_transition_ms)

    return combo_results, failures, warnings


def build_result_map(
    cfg: SuiteConfig, combo_results: Dict[Tuple[str, str, int], ComboStats]
) -> Dict[MetricKey, float]:
    result_map: Dict[MetricKey, float] = {}

    for (pattern, transport, size), combo in combo_results.items():
        if combo.status != "success":
            continue

        result_map[("current", pattern, transport, size, "throughput")] = (
            combo.throughput
        )
        result_map[("current", pattern, transport, size, "bandwidth")] = combo.bandwidth
        result_map[("current", pattern, transport, size, "latency")] = combo.latency

        if cfg.suite == "multi":
            client_cpu = (
                combo.client_cpu_pct
                if combo.client_cpu_pct is not None
                else combo.cpu_pct
            )
            client_mem = (
                combo.client_mem_mb
                if combo.client_mem_mb is not None
                else combo.mem_mb
            )
            if client_cpu is not None:
                result_map[
                    ("current", pattern, transport, size, "client_cpu_pct")
                ] = client_cpu
            if client_mem is not None:
                result_map[
                    ("current", pattern, transport, size, "client_mem_mb")
                ] = client_mem
            if combo.server_cpu_pct is not None:
                result_map[
                    ("current", pattern, transport, size, "server_cpu_pct")
                ] = combo.server_cpu_pct
            if combo.server_mem_mb is not None:
                result_map[
                    ("current", pattern, transport, size, "server_mem_mb")
                ] = combo.server_mem_mb
        else:
            if combo.cpu_pct is not None:
                result_map[("current", pattern, transport, size, "cpu_pct")] = (
                    combo.cpu_pct
                )
            if combo.mem_mb is not None:
                result_map[("current", pattern, transport, size, "mem_mb")] = combo.mem_mb

    return result_map


def compute_completion_status(
    combo_results: Dict[Tuple[str, str, int], ComboStats]
) -> Tuple[str, int, int]:
    total = len(combo_results)
    unsupported = sum(1 for combo in combo_results.values() if combo.status == "unsupported")
    skipped = sum(1 for combo in combo_results.values() if combo.status == "skip")
    success = sum(1 for combo in combo_results.values() if combo.status == "success")

    expected = max(0, (total - unsupported - skipped) * 3)
    actual = success * 3
    status = "complete" if expected == actual else "partial"
    return status, expected, actual


def run() -> int:
    parser = make_parser()
    args = parser.parse_args()

    cfg, patterns = resolve_config(args)
    logger = Tee(cfg.output)

    exit_code = 0

    try:
        if env_first("PERF_COMPARISON_SCRIPT"):
            raise RuntimeError(
                "PERF_COMPARISON_SCRIPT override is not allowed by perf policy"
            )

        native_dir = ensure_distribution_only(cfg.binding)
        versions = []
        for lib in binding_native_candidates(cfg.binding):
            v = detect_zlink_version(lib)
            versions.append((lib, v))
        ver_text = ", ".join(f"{p.name}:{v or 'unknown'}" for p, v in versions)
        logger.print(f"Using distribution native libs ({cfg.binding}): {ver_text}")

        build_binding_if_needed(cfg, logger)

        env = prepare_runtime_env(cfg, native_dir)

        combo_results, failures, warnings = execute_benchmarks(
            cfg, patterns, env, logger
        )

        table_lines = build_table_lines(cfg, patterns, combo_results)
        for line in table_lines:
            logger.print(line)

        if warnings:
            logger.print("")
            logger.print("## Warnings")
            for msg in warnings:
                logger.print(f"- {msg}")

        if failures:
            logger.print("")
            logger.print("## Failures")
            for pattern, tr, size, reason in failures:
                logger.print(f"- {pattern} current {tr} {size}B: {reason}")

        # Build Tier1 result map and META
        host_meta = collect_host_meta()
        host_meta["mode"] = cfg.mode
        host_meta["runs"] = str(cfg.runs)
        if cfg.suite == "multi":
            host_meta["clients"] = str(cfg.multi_clients)

        result_map = build_result_map(cfg, combo_results)
        status, expected, actual = compute_completion_status(combo_results)

        host_meta["status"] = status
        host_meta["expected"] = str(expected)
        host_meta["actual"] = str(actual)

        tmp_dir, report_dir, baseline_dir = suite_dirs(cfg)
        tmp_dir.mkdir(parents=True, exist_ok=True)
        report_dir.mkdir(parents=True, exist_ok=True)
        baseline_dir.mkdir(parents=True, exist_ok=True)

        # tmp is always saved (policy v1.3).
        tmp_file = tmp_dir / result_file_name(cfg.results_tag)
        write_tmp_or_baseline_file(tmp_file, host_meta, result_map, table_lines)
        enforce_retention(tmp_dir)
        logger.print(f"Saved result file: {tmp_file}")

        # --result => report save (complete-only).
        if cfg.result:
            if status == "complete":
                report_file = report_dir / result_file_name(cfg.results_tag)
                write_report_file(report_file, table_lines)
                enforce_retention(report_dir)
                logger.print(f"Saved report file: {report_file}")
            else:
                logger.print("warning: status=partial, --result(report) skipped")

        # --save [VER] => baseline save (complete-only).
        if cfg.save_baseline is not None:
            if status != "complete":
                logger.print("error: cannot save baseline for partial run")
                exit_code = max(exit_code, 1)
            else:
                baseline_name = (
                    f"{cfg.save_baseline}.txt"
                    if cfg.save_baseline
                    else result_file_name(cfg.results_tag)
                )
                baseline_file = baseline_dir / baseline_name
                write_tmp_or_baseline_file(
                    baseline_file, host_meta, result_map, table_lines
                )
                update_latest_symlink(baseline_dir, baseline_file)
                enforce_retention(baseline_dir, exclude=["latest.txt"])
                logger.print(f"Saved baseline file: {baseline_file}")

        # Mode comparisons
        baseline_data: Dict[MetricKey, float] = {}
        if cfg.mode == "trend":
            keys_of_interest = [
                key
                for key in result_map.keys()
                if key[4] in ("throughput", "bandwidth", "latency")
            ]

            baseline_data, _ = load_rolling_baseline(
                tmp_dir, cfg.rolling_n, keys_of_interest
            )

            if not baseline_data:
                logger.print(
                    "warning: no rolling baseline available, skipping trend comparison"
                )
            else:
                logger.print(
                    f"Trend baseline loaded from tmp (rolling_n={cfg.rolling_n})"
                )
        elif cfg.mode == "gate":
            if cfg.baseline_file is not None:
                baseline_file = cfg.baseline_file
            else:
                baseline_name = cfg.baseline_version
                baseline_file = baseline_dir / ("latest.txt" if baseline_name == "latest" else f"{baseline_name}.txt")

            if not baseline_file.exists():
                logger.print(f"error: baseline file not found: {baseline_file}")
                exit_code = max(exit_code, 1)
            else:
                baseline_data = load_fixed_baseline_file(baseline_file)
                logger.print(f"Gate baseline loaded: {baseline_file}")

        if cfg.mode in ("trend", "gate") and baseline_data:
            overrides = load_threshold_overrides(logger)
            warn_msgs, fail_msgs, _ = compare_to_baseline(cfg, logger, result_map, baseline_data, overrides)
            if warn_msgs:
                logger.print("")
                logger.print("## Regression Warnings")
                for msg in warn_msgs:
                    logger.print(f"- {msg}")
            if fail_msgs:
                logger.print("")
                logger.print("## Regression Failures")
                for msg in fail_msgs:
                    logger.print(f"- {msg}")
                if cfg.mode == "gate":
                    exit_code = max(exit_code, 2)

        if failures:
            exit_code = max(exit_code, 1)

    except ValueError as exc:
        logger.print(f"Error: {exc}")
        exit_code = max(exit_code, 1)
    except RuntimeError as exc:
        logger.print(f"Error: {exc}")
        exit_code = max(exit_code, 1)
    finally:
        logger.close()

    return exit_code


if __name__ == "__main__":
    raise SystemExit(run())
