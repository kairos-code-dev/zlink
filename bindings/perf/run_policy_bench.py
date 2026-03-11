#!/usr/bin/env python3
"""Policy-compliant binding benchmark runner for single/multi suites.

This script is intentionally independent from bindings/bench/common.
"""

from __future__ import annotations

import argparse
import datetime as dt
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
from typing import Dict, List, Optional, Sequence, Set, Tuple

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
    "MULTI_GATEWAY",
    "MULTI_STREAM",
    "MULTI_STREAM_CALLBACK",
    "MULTI_STREAM_LEN32BE",
}

MULTI_ONE_WAY_PATTERNS = {
    "MULTI_DEALER_DEALER",
    "MULTI_PUBSUB",
    "MULTI_SPOT",
}

MULTI_STREAM_PATTERNS = {
    "MULTI_STREAM",
    "MULTI_STREAM_CALLBACK",
    "MULTI_STREAM_LEN32BE",
}

STREAM_VARIANT_PATTERNS = MULTI_STREAM_PATTERNS | {
    "STREAM",
    "STREAM_CALLBACK",
    "STREAM_LEN32BE",
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

STREAM_CLIENT_DIR = CORE_PERF_DIR / "common" / "streamclient"
STREAM_CLIENT_BUILD_SCRIPT = STREAM_CLIENT_DIR / "build.sh"
STREAM_CLIENT_BIN = STREAM_CLIENT_DIR / "build" / (
    "perf_stream_client.exe" if IS_WINDOWS else "perf_stream_client"
)

JAVA_RUNTIME_CP_CACHE: Dict[str, str] = {}

CPP_SINGLE_PATTERN_SPECS: Dict[str, Tuple[str, str, str]] = {
    "PAIR": ("perf_pair.cpp", "run_pattern_pair", "perf_pair"),
    "PUBSUB": ("perf_pubsub.cpp", "run_pattern_pubsub", "perf_pubsub"),
    "DEALER_DEALER": (
        "perf_dealer_dealer.cpp",
        "run_pattern_dealer_dealer",
        "perf_dealer_dealer",
    ),
    "DEALER_ROUTER": (
        "perf_dealer_router.cpp",
        "run_pattern_dealer_router",
        "perf_dealer_router",
    ),
    "ROUTER_ROUTER": (
        "perf_router_router.cpp",
        "run_pattern_router_router",
        "perf_router_router",
    ),
    "ROUTER_ROUTER_POLL": (
        "perf_router_router_poll.cpp",
        "run_pattern_router_router_poll",
        "perf_router_router_poll",
    ),
    "GATEWAY": ("perf_gateway.cpp", "run_pattern_gateway", "perf_gateway"),
    "SPOT": ("perf_spot.cpp", "run_pattern_spot", "perf_spot"),
}

CPP_MULTI_SERVER_PATTERN_SPECS: Dict[str, Tuple[str, str, str]] = {
    "MULTI_DEALER_DEALER": (
        "perf_dealer_dealer_server.cpp",
        "perf_dealer_dealer_server",
        "comp_src_dealer_dealer_server",
    ),
    "MULTI_DEALER_ROUTER": (
        "perf_dealer_router_server.cpp",
        "perf_dealer_router_server",
        "comp_src_dealer_router_server",
    ),
    "MULTI_ROUTER_ROUTER": (
        "perf_router_router_server.cpp",
        "perf_router_router_server",
        "comp_src_router_router_server",
    ),
    "MULTI_PUBSUB": (
        "perf_pubsub_server.cpp",
        "perf_pubsub_server",
        "comp_src_pubsub_server",
    ),
    "MULTI_GATEWAY": (
        "perf_gateway_server.cpp",
        "perf_gateway_server",
        "comp_src_gateway_server",
    ),
    "MULTI_SPOT": (
        "perf_spot_server.cpp",
        "perf_spot_server",
        "comp_src_spot_server",
    ),
    "MULTI_STREAM": (
        "perf_stream_server.cpp",
        "perf_stream_server",
        "comp_src_stream_server",
    ),
    "MULTI_STREAM_CALLBACK": (
        "perf_stream_callback_server.cpp",
        "perf_stream_callback_server",
        "comp_src_stream_callback_server",
    ),
    "MULTI_STREAM_LEN32BE": (
        "perf_stream_len32be_server.cpp",
        "perf_stream_len32be_server",
        "comp_src_stream_len32be_server",
    ),
}

CPP_MULTI_CLIENT_PATTERN_SPECS: Dict[str, Tuple[str, str, str]] = {
    "MULTI_DEALER_DEALER": (
        "perf_dealer_dealer_client.cpp",
        "perf_dealer_dealer_client",
        "comp_src_dealer_dealer_client",
    ),
    "MULTI_DEALER_ROUTER": (
        "perf_dealer_router_client.cpp",
        "perf_dealer_router_client",
        "comp_src_dealer_router_client",
    ),
    "MULTI_ROUTER_ROUTER": (
        "perf_router_router_client.cpp",
        "perf_router_router_client",
        "comp_src_router_router_client",
    ),
    "MULTI_PUBSUB": (
        "perf_pubsub_client.cpp",
        "perf_pubsub_client",
        "comp_src_pubsub_client",
    ),
    "MULTI_GATEWAY": (
        "perf_gateway_client.cpp",
        "perf_gateway_client",
        "comp_src_gateway_client",
    ),
    "MULTI_SPOT": (
        "perf_spot_client.cpp",
        "perf_spot_client",
        "comp_src_spot_client",
    ),
}

CPP_STREAM_SERVER_PATTERNS = (
    "MULTI_STREAM",
    "MULTI_STREAM_CALLBACK",
    "MULTI_STREAM_LEN32BE",
)

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
    status: str  # success | fail
    metrics: Dict[str, float]
    reason: str = ""
    warnings: Optional[List[str]] = None


@dataclass
class ComboStats:
    status: str  # success | fail
    throughput: float = 0.0
    bandwidth: float = 0.0
    latency: float = 0.0
    latency_p95: float = 0.0
    latency_p99: float = 0.0
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
    results_root: Path
    results_tag: str
    output: Optional[Path]
    multi_warmup_seconds: int
    multi_duration_seconds: int
    multi_clients: int
    multi_hwm: int
    multi_sndtimeo_ms: int
    multi_rcvtimeo_ms: int
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


def suite_dirs(cfg: SuiteConfig) -> Tuple[Path, Path]:
    suite_root = cfg.results_root / cfg.suite
    return suite_root / "tmp", suite_root / "report"


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


def cpp_binary_name(stem: str) -> str:
    return f"{stem}.exe" if IS_WINDOWS else stem


def build_binding_if_needed(cfg: SuiteConfig, logger: Tee) -> None:
    perf_dir = binding_perf_dir(cfg.binding)

    def run_checked(cmd: List[str], cwd: Optional[Path] = None, env: Optional[Dict[str, str]] = None) -> None:
        proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env)
        if proc.returncode != 0:
            raise RuntimeError("build failed: " + " ".join(cmd))

    artifact_exists = True
    if cfg.binding == "cpp":
        def cpp_artifacts_fresh(
            outputs: Sequence[Path], source_dirs: Sequence[Path]
        ) -> bool:
            if not outputs or any(not out.exists() for out in outputs):
                return False

            newest_source_mtime = 0.0
            for src_dir in source_dirs:
                if not src_dir.exists():
                    continue
                for src in src_dir.rglob("*"):
                    if not src.is_file():
                        continue
                    if src.suffix not in {".cpp", ".hpp", ".h", ".ipp"}:
                        continue
                    newest_source_mtime = max(
                        newest_source_mtime, src.stat().st_mtime
                    )

            oldest_output_mtime = min(out.stat().st_mtime for out in outputs)
            return oldest_output_mtime >= newest_source_mtime

        single_build_dir = perf_dir / "single" / "build"
        multi_build_dir = perf_dir / "multi" / "build"
        if cfg.suite == "multi":
            expected_bins: List[Path] = []
            for _, _, bin_stem in CPP_MULTI_SERVER_PATTERN_SPECS.values():
                expected_bins.append(multi_build_dir / cpp_binary_name(bin_stem))
            for _, _, bin_stem in CPP_MULTI_CLIENT_PATTERN_SPECS.values():
                expected_bins.append(multi_build_dir / cpp_binary_name(bin_stem))
            source_dirs = [
                perf_dir / "multi" / "common",
                perf_dir / "multi" / "src",
            ]
        else:
            expected_bins = []
            for _, _, bin_stem in CPP_SINGLE_PATTERN_SPECS.values():
                expected_bins.append(single_build_dir / cpp_binary_name(bin_stem))
            # Single stream patterns use split multi stream servers.
            for pattern in CPP_STREAM_SERVER_PATTERNS:
                _, _, bin_stem = CPP_MULTI_SERVER_PATTERN_SPECS[pattern]
                expected_bins.append(multi_build_dir / cpp_binary_name(bin_stem))
            source_dirs = [
                perf_dir / "single" / "common",
                perf_dir / "single" / "src",
                perf_dir / "multi" / "common",
                perf_dir / "multi" / "src",
            ]
        artifact_exists = cpp_artifacts_fresh(expected_bins, source_dirs)
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
        suite_dir = "single"
        if cfg.suite == "multi":
            java_class = "PerfMultiMain.class"
            suite_dir = "multi"
        artifact = (
            BINDINGS_DIR
            / "java"
            / "perf"
            / suite_dir
            / "Zlink.PerfBench"
            / "build"
            / "classes"
            / "java"
            / "main"
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
        single_out = perf_dir / "single" / "build"
        multi_out = perf_dir / "multi" / "build"
        single_out.mkdir(parents=True, exist_ok=True)
        multi_out.mkdir(parents=True, exist_ok=True)
        cpp_native = binding_native_dir("cpp")
        cxx = os.environ.get("CXX") or "c++"

        def compile_cpp(
            *,
            out_path: Path,
            include_dirs: Sequence[Path],
            sources: Sequence[Path],
            defines: Optional[Sequence[str]] = None,
        ) -> None:
            cmd: List[str] = [cxx, "-O3", "-std=c++17", "-pthread"]
            cmd.extend(f"-I{inc}" for inc in include_dirs)
            if defines:
                cmd.extend(f"-D{define}" for define in defines)
            cmd.extend(str(src) for src in sources)
            cmd.extend(
                [
                    f"-L{cpp_native}",
                    "-lzlink",
                    f"-Wl,-rpath,{cpp_native}",
                    "-o",
                    str(out_path),
                ]
            )
            run_checked(cmd)

        core_include = ROOT_DIR / "core" / "include"
        binding_include = BINDINGS_DIR / "cpp" / "include"

        single_common = perf_dir / "single" / "common"
        single_src = perf_dir / "single" / "src"
        single_common_sources = [
            single_common / "perf_single_common.cpp",
            single_common / "perf_single_runner.cpp",
        ]
        single_includes = [core_include, binding_include, single_common]

        multi_common = perf_dir / "multi" / "common"
        multi_src = perf_dir / "multi" / "src"
        multi_includes = [core_include, binding_include, multi_common]

        def build_cpp_single_patterns() -> None:
            for src_name, _, bin_stem in CPP_SINGLE_PATTERN_SPECS.values():
                compile_cpp(
                    out_path=single_out / cpp_binary_name(bin_stem),
                    include_dirs=single_includes,
                    sources=single_common_sources + [single_src / src_name],
                )

        def build_cpp_multi_servers(patterns: Sequence[str]) -> None:
            for pattern in patterns:
                src_name, _, bin_stem = CPP_MULTI_SERVER_PATTERN_SPECS[pattern]
                compile_cpp(
                    out_path=multi_out / cpp_binary_name(bin_stem),
                    include_dirs=multi_includes,
                    sources=[multi_src / src_name],
                )

        def build_cpp_multi_clients() -> None:
            for src_name, _, bin_stem in CPP_MULTI_CLIENT_PATTERN_SPECS.values():
                compile_cpp(
                    out_path=multi_out / cpp_binary_name(bin_stem),
                    include_dirs=multi_includes,
                    sources=[multi_src / src_name],
                )

        if cfg.suite == "multi":
            build_cpp_multi_servers(tuple(CPP_MULTI_SERVER_PATTERN_SPECS.keys()))
            build_cpp_multi_clients()
        else:
            build_cpp_single_patterns()
            build_cpp_multi_servers(CPP_STREAM_SERVER_PATTERNS)
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
        if cfg.suite == "multi":
            run_checked(["./gradlew", "-q", ":perf-multi:classes"], cwd=BINDINGS_DIR / "java")
        else:
            run_checked(["./gradlew", "-q", ":perf-single:classes"], cwd=BINDINGS_DIR / "java")
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
        cp = java_runtime_classpath(cfg.suite)
        cmd = [
            java,
            "--enable-native-access=ALL-UNNAMED",
            "-cp",
            cp,
            "dev.kairoscode.zlink.integration.bench.PerfMain",
            base_pattern,
        ]
    elif cfg.binding == "cpp":
        single_spec = CPP_SINGLE_PATTERN_SPECS.get(base_pattern)
        if not single_spec:
            raise RuntimeError(
                f"single runner is not defined for cpp pattern={base_pattern}"
            )
        _, _, bin_stem = single_spec
        runner = (
            BINDINGS_DIR
            / "cpp"
            / "perf"
            / "single"
            / "build"
            / cpp_binary_name(bin_stem)
        )
        cmd = [str(runner)]
    else:
        raise RuntimeError(f"unsupported binding: {cfg.binding}")

    return cmd, base_pattern


def java_runtime_classpath(suite: str) -> str:
    cached = JAVA_RUNTIME_CP_CACHE.get(suite)
    if cached:
        return cached

    task = ":perf-multi:printRuntimeClasspath" if suite == "multi" else ":perf-single:printRuntimeClasspath"
    result = subprocess.run(
        ["./gradlew", "-q", task],
        cwd=BINDINGS_DIR / "java",
        check=True,
        capture_output=True,
        text=True,
    )
    cp = result.stdout.strip().splitlines()[-1].strip()
    if not cp:
        raise RuntimeError(f"empty runtime classpath for java suite={suite}")
    JAVA_RUNTIME_CP_CACHE[suite] = cp
    return cp


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
    probe_pattern = "MULTI_DEALER_DEALER"
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
        pattern_key = pattern.strip().upper()
        if role == "server":
            server_spec = CPP_MULTI_SERVER_PATTERN_SPECS.get(pattern_key)
            if not server_spec:
                raise RuntimeError(
                    f"multi server runner is not defined for cpp pattern={pattern}"
                )
            _, _, bin_stem = server_spec
            runner = (
                BINDINGS_DIR
                / "cpp"
                / "perf"
                / "multi"
                / "build"
                / cpp_binary_name(bin_stem)
            )
            cmd = [
                str(runner),
                transport,
                str(size if size is not None else 64),
            ]
        else:
            client_spec = CPP_MULTI_CLIENT_PATTERN_SPECS.get(pattern_key)
            if not client_spec:
                raise RuntimeError(
                    f"multi client runner is not defined for cpp pattern={pattern}"
                )
            _, _, bin_stem = client_spec
            runner = (
                BINDINGS_DIR
                / "cpp"
                / "perf"
                / "multi"
                / "build"
                / cpp_binary_name(bin_stem)
            )
            cmd = [
                str(runner),
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
        if role != "server":
            cmd.extend(
                [
                    "--endpoint",
                    endpoint or "",
                ]
            )
    elif cfg.binding == "java":
        java = shutil.which("java")
        if not java:
            raise RuntimeError("java not found")
        cp = java_runtime_classpath("multi")
        if role == "server":
            cmd = [
                java,
                "--enable-native-access=ALL-UNNAMED",
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
                "--enable-native-access=ALL-UNNAMED",
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
        # Canonical core/perf names.
        set_perf_env("PERF_WARMUP_SECONDS", str(cfg.multi_warmup_seconds))
        set_perf_env("PERF_DURATION_SECONDS", str(cfg.multi_duration_seconds))
        if cfg.multi_clients > 0:
            set_perf_env("PERF_CLIENTS", str(cfg.multi_clients))
        if cfg.multi_hwm > 0:
            set_perf_env("PERF_HWM", str(cfg.multi_hwm))
        if cfg.multi_sndtimeo_ms > 0:
            set_perf_env("PERF_SNDTIMEO_MS", str(cfg.multi_sndtimeo_ms))
        if cfg.multi_rcvtimeo_ms > 0:
            set_perf_env("PERF_RCVTIMEO_MS", str(cfg.multi_rcvtimeo_ms))
        set_perf_env(
            "PERF_SERVER_READY_TIMEOUT_MS",
            str(cfg.multi_server_ready_timeout_ms),
        )
        set_perf_env(
            "PERF_SERVER_SHUTDOWN_TIMEOUT_MS",
            str(cfg.multi_server_shutdown_timeout_ms),
        )
        set_perf_env("PERF_SERVER_BIND_PORT", str(cfg.multi_server_bind_port))
        set_perf_env(
            "PERF_CONNECT_READY_TIMEOUT_MS",
            str(cfg.multi_connect_ready_timeout_ms),
        )
        if cfg.multi_monitor_hwm >= 0:
            set_perf_env("PERF_MONITOR_HWM", str(cfg.multi_monitor_hwm))

        # Dotnet follows core-style PERF_* only.
        # Other bindings may still rely on PERF_MULTI_* compatibility names.
        if cfg.binding != "dotnet":
            set_perf_env("PERF_MULTI_WARMUP_SECONDS", str(cfg.multi_warmup_seconds))
            set_perf_env(
                "PERF_MULTI_DURATION_SECONDS", str(cfg.multi_duration_seconds)
            )
            if cfg.multi_clients > 0:
                set_perf_env("PERF_MULTI_CLIENTS", str(cfg.multi_clients))
            if cfg.multi_hwm > 0:
                set_perf_env("PERF_MULTI_HWM", str(cfg.multi_hwm))
            if cfg.multi_sndtimeo_ms > 0:
                set_perf_env("PERF_MULTI_SNDTIMEO_MS", str(cfg.multi_sndtimeo_ms))
            if cfg.multi_rcvtimeo_ms > 0:
                set_perf_env("PERF_MULTI_RCVTIMEO_MS", str(cfg.multi_rcvtimeo_ms))
            set_perf_env(
                "PERF_MULTI_SERVER_READY_TIMEOUT_MS",
                str(cfg.multi_server_ready_timeout_ms),
            )
            set_perf_env(
                "PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS",
                str(cfg.multi_server_shutdown_timeout_ms),
            )
            set_perf_env(
                "PERF_MULTI_SERVER_BIND_PORT", str(cfg.multi_server_bind_port)
            )
            set_perf_env(
                "PERF_MULTI_CONNECT_READY_TIMEOUT_MS",
                str(cfg.multi_connect_ready_timeout_ms),
            )
            if cfg.multi_monitor_hwm >= 0:
                set_perf_env("PERF_MULTI_MONITOR_HWM", str(cfg.multi_monitor_hwm))
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


def stream_client_binary_is_stale() -> bool:
    if not STREAM_CLIENT_BIN.exists():
        return True
    try:
        binary_mtime = STREAM_CLIENT_BIN.stat().st_mtime
    except Exception:
        return True

    for path in STREAM_CLIENT_DIR.iterdir():
        if not path.is_file():
            continue
        if path.name == "build.sh" or path.suffix in (
            ".cpp",
            ".cc",
            ".cxx",
            ".c",
            ".hpp",
            ".h",
        ):
            try:
                if path.stat().st_mtime > binary_mtime:
                    return True
            except Exception:
                return True
    return False


def ensure_stream_client_bin() -> Path:
    if not stream_client_binary_is_stale():
        return STREAM_CLIENT_BIN
    if IS_WINDOWS:
        raise RuntimeError(
            f"shared stream client is missing or stale: {STREAM_CLIENT_BIN}"
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
        # Shared stream client is duration-based (async multi-connection).
        # Keep single-suite stream delegation aligned with its CLI contract.
        clients = env_int_from_map(
            env, "PERF_STREAM_SINGLE_CLIENTS", 1
        )
        warmup_seconds = env_int_from_map(env, "PERF_STREAM_SINGLE_WARMUP_SECONDS", 2)
        duration_seconds = env_int_from_map(env, "PERF_SINGLE_DURATION_SECONDS", 5)
        io_threads = env_int_from_map(
            env, "PERF_IO_THREADS", cfg.io_threads if cfg.io_threads else 1
        )
        cmd.extend(
            [
                "--ccu",
                str(clients),
                "--warmup",
                str(warmup_seconds),
                "--duration",
                str(duration_seconds),
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

    default_clients = (
        10000 if pattern in MULTI_STREAM_PATTERNS else 100
    )
    clients = env_int_from_map(
        env,
        "PERF_CLIENTS",
        cfg.multi_clients if cfg.multi_clients > 0 else default_clients,
    )
    if (
        cfg.binding == "dotnet"
        and transport.lower() == "wss"
        and size >= 65536
        and clients > 5000
    ):
        clients = 5000
    warmup_seconds = env_int_from_map(
        env, "PERF_WARMUP_SECONDS", cfg.multi_warmup_seconds
    )
    duration_seconds = env_int_from_map(
        env, "PERF_DURATION_SECONDS", cfg.multi_duration_seconds
    )
    lat_count = env_int_from_map(env, "PERF_LAT_COUNT", 200)
    io_threads = env_int_from_map(
        env, "PERF_IO_THREADS", cfg.io_threads if cfg.io_threads else 1
    )
    latency_sample_rate = env_int_from_map(
        env,
        "PERF_LATENCY_SAMPLE_RATE",
        env_int_from_map(env, "PERF_MULTI_LATENCY_SAMPLE_RATE", 1),
    )
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
        "latency_p95",
        "latency_p99",
        "cpu_pct",
        "mem_mb",
        "client_cpu_pct",
        "client_mem_mb",
        "server_cpu_pct",
        "server_mem_mb",
        "snd_pending_max",
        "rcv_pending_max",
        "rcv_pending_end",
        "server_snd_pending_max",
        "server_rcv_pending_max",
        "server_rcv_pending_end",
    ):
        return None
    return lib, pattern, transport, size, metric, value


def pattern_aliases(pattern: str) -> Set[str]:
    p = pattern.strip().upper()
    aliases = {p}
    if p.startswith("MULTI_"):
        aliases.add(p[len("MULTI_") :])
    elif p:
        aliases.add(f"MULTI_{p}")
    return aliases


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
    accepted_patterns = pattern_aliases(requested_pattern)
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


def effective_server_shutdown_timeout_ms(cfg: SuiteConfig, pattern: str) -> int:
    timeout_ms = max(1, cfg.multi_server_shutdown_timeout_ms)
    if cfg.binding == "dotnet":
        normalized = pattern.strip().upper()
        if normalized == "MULTI_SPOT":
            return max(timeout_ms, 7000)
        if normalized == "MULTI_DEALER_DEALER":
            return max(timeout_ms, 15000)
    return timeout_ms


def parse_run_outcome(
    requested_pattern: str,
    expected_output_pattern: str,
    transport: str,
    size: int,
    sampled: Dict[str, object],
    *,
    process_role: str = "single",
    require_percentiles: bool = False,
) -> RunOutcome:
    stdout = str(sampled.get("stdout", ""))
    stderr = str(sampled.get("stderr", ""))
    rc = int(sampled.get("returncode") or 0)

    if sampled.get("timed_out", False):
        return RunOutcome(status="fail", metrics={}, reason="timeout")

    status_token = detect_status_token(stdout)

    metrics: Dict[str, float] = {}
    warnings: List[str] = []
    accepted_patterns = pattern_aliases(requested_pattern) | pattern_aliases(
        expected_output_pattern
    )
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

    # Shared stream client always reports echo bandwidth (x2).
    # Policy defines single-suite patterns as one-way for bandwidth accounting.
    if (
        requested_pattern.strip().upper() in STREAM_SINGLE_PATTERNS
        and "bandwidth" in metrics
    ):
        metrics["bandwidth"] = metrics["bandwidth"] / 2.0

    # The shared STREAM client reports round-trip bandwidth. Single-suite
    # STREAM* policy expects one-way bandwidth, so normalize only when the
    # observed value clearly matches the 2x form.
    if (
        requested_pattern in STREAM_SINGLE_PATTERNS
        and "throughput" in metrics
        and "bandwidth" in metrics
    ):
        one_way_bw = (metrics["throughput"] * float(size)) / 1_000_000.0
        reported_bw = metrics["bandwidth"]
        if (
            reported_bw > 0.0
            and one_way_bw > 0.0
            and abs(reported_bw - (one_way_bw * 2.0)) / reported_bw < 0.20
        ):
            metrics["bandwidth"] = reported_bw / 2.0

    if status_token in ("unsupported", "skip"):
        return RunOutcome(
            status="fail",
            metrics=metrics,
            reason=status_token,
            warnings=warnings,
        )

    if rc != 0:
        if stderr.strip():
            first = stderr.strip().splitlines()[0]
            warnings.append(
                f"{requested_pattern} {transport} {size} non-zero exit({rc}) stderr: {first}"
            )
        return RunOutcome(
            status="fail",
            metrics=metrics,
            reason=f"non_zero_exit_{rc}",
            warnings=warnings,
        )

    if "throughput" not in metrics or "latency" not in metrics:
        return RunOutcome(status="fail", metrics=metrics, reason="no_data", warnings=warnings)

    if "bandwidth" not in metrics:
        return RunOutcome(status="fail", metrics=metrics, reason="no_bandwidth", warnings=warnings)

    if require_percentiles and (
        "latency_p95" not in metrics or "latency_p99" not in metrics
    ):
        return RunOutcome(status="fail", metrics=metrics, reason="no_percentiles", warnings=warnings)

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


def require_dotnet_percentiles(cfg: SuiteConfig, pattern: str) -> bool:
    if cfg.binding != "dotnet":
        return False
    p = pattern.strip().upper()
    if p in STREAM_SINGLE_PATTERNS or p in MULTI_STREAM_PATTERNS:
        # STREAM client path uses shared core stream client.
        return False
    return True


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
    if not outcomes:
        return ComboStats(status="fail", reason="failed")

    if any(o.status == "fail" for o in outcomes):
        reason = next((o.reason for o in outcomes if o.status == "fail" and o.reason), "failed")
        return ComboStats(status="fail", reason=reason)

    if all(o.status == "unsupported" for o in outcomes):
        return ComboStats(status="unsupported", reason="unsupported")
    if all(o.status == "skip" for o in outcomes):
        return ComboStats(status="skip", reason="skip")
    if not all(o.status == "success" for o in outcomes):
        reason = next((o.reason for o in outcomes if o.reason), "mixed_status")
        return ComboStats(status="fail", reason=reason)

    success = outcomes
    thr_vals = [o.metrics["throughput"] for o in success if "throughput" in o.metrics]
    bw_vals = [o.metrics["bandwidth"] for o in success if "bandwidth" in o.metrics]
    lat_vals = [o.metrics["latency"] for o in success if "latency" in o.metrics]
    lat_p95_vals = [
        o.metrics.get("latency_p95", o.metrics.get("latency", 0.0))
        for o in success
        if "latency" in o.metrics
    ]
    lat_p99_vals = [
        o.metrics.get("latency_p99", o.metrics.get("latency", 0.0))
        for o in success
        if "latency" in o.metrics
    ]
    cpu_vals = [o.metrics["cpu_pct"] for o in success if "cpu_pct" in o.metrics]
    mem_vals = [o.metrics["mem_mb"] for o in success if "mem_mb" in o.metrics]
    client_cpu_vals = [
        o.metrics["client_cpu_pct"] for o in success if "client_cpu_pct" in o.metrics
    ]
    client_mem_vals = [
        o.metrics["client_mem_mb"] for o in success if "client_mem_mb" in o.metrics
    ]
    server_cpu_vals = [
        o.metrics["server_cpu_pct"] for o in success if "server_cpu_pct" in o.metrics
    ]
    server_mem_vals = [
        o.metrics["server_mem_mb"] for o in success if "server_mem_mb" in o.metrics
    ]
    return ComboStats(
        status="success",
        throughput=statistics.median(thr_vals) if thr_vals else 0.0,
        bandwidth=statistics.median(bw_vals) if bw_vals else 0.0,
        latency=statistics.median(lat_vals) if lat_vals else 0.0,
        latency_p95=statistics.median(lat_p95_vals) if lat_p95_vals else 0.0,
        latency_p99=statistics.median(lat_p99_vals) if lat_p99_vals else 0.0,
        cpu_pct=statistics.median(cpu_vals) if cpu_vals else None,
        mem_mb=statistics.median(mem_vals) if mem_vals else None,
        client_cpu_pct=statistics.median(client_cpu_vals) if client_cpu_vals else None,
        client_mem_mb=statistics.median(client_mem_vals) if client_mem_vals else None,
        server_cpu_pct=statistics.median(server_cpu_vals) if server_cpu_vals else None,
        server_mem_mb=statistics.median(server_mem_vals) if server_mem_vals else None,
    )


def format_thr(v: float, pattern: str, suite: str) -> str:
    unit = throughput_unit_label(pattern, suite)
    return f"{v/1000.0:8.2f} {unit}"


def format_bw(v: float) -> str:
    return f"{v:8.1f} MB/s"


def format_lat(v: float, binding: str) -> str:
    if binding in {"cpp", "dotnet", "java"}:
        return f"{v:8.4f} ms"
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
        latency_header = (
            "Latency(ms)" if cfg.binding in {"cpp", "dotnet", "java"}
            else "Latency(us)"
        )
        latency_p95_header = (
            "Lat p95(ms)" if cfg.binding in {"cpp", "dotnet", "java"}
            else "Lat p95(us)"
        )
        latency_p99_header = (
            "Lat p99(ms)" if cfg.binding in {"cpp", "dotnet", "java"}
            else "Lat p99(us)"
        )
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
                    f"| Size     |       Throughput | Bandwidth | {latency_header:>12} | {latency_p95_header:>10} | {latency_p99_header:>10} | C.CPU% | C.Mem MB | S.CPU% | S.Mem MB |"
                )
                lines.append(
                    "|----------|------------------|-----------|--------------|----------|----------|--------|----------|--------|----------|"
                )
            else:
                lines.append(
                    f"| Size     |       Throughput | Bandwidth | {latency_header:>12} | {latency_p95_header:>10} | {latency_p99_header:>10} | CPU% | Mem MB |"
                )
                lines.append(
                    "|----------|------------------|-----------|--------------|----------|----------|------|--------|"
                )

            for size in resolve_sizes_for_pattern(cfg, pattern):
                combo = combo_results.get((pattern, tr, size))
                if combo is None or combo.status != "success":
                    if cfg.suite == "multi":
                        lines.append(
                            f"| {size}B | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |"
                        )
                    else:
                        lines.append(f"| {size}B | N/A | N/A | N/A | N/A | N/A | N/A | N/A |")
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
                        f"| {size}B | {format_thr(combo.throughput, pattern, cfg.suite):>16} | {format_bw(combo.bandwidth):>9} | {format_lat(combo.latency, cfg.binding):>12} | {format_lat(combo.latency_p95, cfg.binding):>8} | {format_lat(combo.latency_p99, cfg.binding):>8} | {format_cpu(client_cpu):>6} | {format_mem(client_mem):>8} | {format_cpu(server_cpu):>6} | {format_mem(server_mem):>8} |"
                    )
                else:
                    lines.append(
                        f"| {size}B | {format_thr(combo.throughput, pattern, cfg.suite):>16} | {format_bw(combo.bandwidth):>9} | {format_lat(combo.latency, cfg.binding):>12} | {format_lat(combo.latency_p95, cfg.binding):>8} | {format_lat(combo.latency_p99, cfg.binding):>8} | {format_cpu(combo.cpu_pct):>4} | {format_mem(combo.mem_mb):>6} |"
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
        "latency_p95": 3,
        "latency_p99": 4,
        "cpu_pct": 5,
        "mem_mb": 6,
        "client_cpu_pct": 5,
        "client_mem_mb": 6,
        "server_cpu_pct": 7,
        "server_mem_mb": 8,
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


def write_result_file(
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
    ap.add_argument("--results-dir", default="")
    ap.add_argument("--results-tag", default="")

    ap.add_argument("--mode", choices=("observe", "trend", "gate"), default="observe")

    ap.add_argument("--multi-warmup-seconds", type=int, default=2)
    ap.add_argument("--multi-duration-seconds", type=int, default=5)
    ap.add_argument("--multi-clients", type=int, default=0)
    ap.add_argument("--multi-hwm", type=int, default=-1)
    ap.add_argument("--multi-sndtimeo-ms", type=int, default=-1)
    ap.add_argument("--multi-rcvtimeo-ms", type=int, default=-1)
    ap.add_argument("--multi-connect-concurrency", type=int, default=1024)
    ap.add_argument("--multi-drain-ms", type=int, default=-1)
    ap.add_argument("--multi-transport-transition-ms", type=int, default=3000)
    ap.add_argument("--multi-pattern-transition-ms", type=int, default=3000)
    ap.add_argument("--multi-server-ready-timeout-ms", type=int, default=10000)
    ap.add_argument("--multi-server-shutdown-timeout-ms", type=int, default=5000)
    ap.add_argument("--multi-server-bind-port", type=int, default=0)
    ap.add_argument("--multi-connect-ready-timeout-ms", type=int, default=5000)
    ap.add_argument("--multi-monitor-hwm", type=int, default=-1)

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

    multi_clients = args.multi_clients
    if multi_clients <= 0:
        multi_clients = int(
            env_first("PERF_CLIENTS", "PERF_MULTI_CLIENTS") or "0"
        )

    def env_int_if_default(
        cli_value: int, default_value: int, *env_names: str
    ) -> int:
        raw = env_first(*env_names)
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
            args.multi_warmup_seconds,
            2,
            "PERF_WARMUP_SECONDS",
            "PERF_MULTI_WARMUP_SECONDS",
        ),
    )
    multi_duration_seconds = max(
        1,
        env_int_if_default(
            args.multi_duration_seconds,
            5,
            "PERF_DURATION_SECONDS",
            "PERF_MULTI_DURATION_SECONDS",
        ),
    )
    multi_hwm = env_int_if_default(
        args.multi_hwm, -1, "PERF_HWM", "PERF_MULTI_HWM"
    )
    multi_sndtimeo_ms = env_int_if_default(
        args.multi_sndtimeo_ms,
        -1,
        "PERF_SNDTIMEO_MS",
        "PERF_MULTI_SNDTIMEO_MS",
    )
    multi_rcvtimeo_ms = env_int_if_default(
        args.multi_rcvtimeo_ms,
        -1,
        "PERF_RCVTIMEO_MS",
        "PERF_MULTI_RCVTIMEO_MS",
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
            args.multi_server_ready_timeout_ms,
            15000,
            "PERF_SERVER_READY_TIMEOUT_MS",
            "PERF_MULTI_SERVER_READY_TIMEOUT_MS",
        ),
    )
    multi_server_shutdown_timeout_ms = max(
        0,
        env_int_if_default(
            args.multi_server_shutdown_timeout_ms,
            5000,
            "PERF_SERVER_SHUTDOWN_TIMEOUT_MS",
            "PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS",
        ),
    )
    multi_server_bind_port = env_int_if_default(
        args.multi_server_bind_port,
        0,
        "PERF_SERVER_BIND_PORT",
        "PERF_MULTI_SERVER_BIND_PORT",
    )
    multi_server_bind_port = max(0, min(65535, multi_server_bind_port))
    multi_connect_ready_timeout_ms = max(
        0,
        env_int_if_default(
            args.multi_connect_ready_timeout_ms,
            5000,
            "PERF_CONNECT_READY_TIMEOUT_MS",
            "PERF_MULTI_CONNECT_READY_TIMEOUT_MS",
        ),
    )
    multi_monitor_hwm = env_int_if_default(
        args.multi_monitor_hwm,
        -1,
        "PERF_MONITOR_HWM",
        "PERF_MULTI_MONITOR_HWM",
    )
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
        results_root=results_root,
        results_tag=args.results_tag,
        output=output,
        multi_warmup_seconds=multi_warmup_seconds,
        multi_duration_seconds=multi_duration_seconds,
        multi_clients=max(0, multi_clients),
        multi_hwm=multi_hwm,
        multi_sndtimeo_ms=multi_sndtimeo_ms,
        multi_rcvtimeo_ms=multi_rcvtimeo_ms,
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
                            status="fail",
                            metrics={},
                            reason="server_unsupported_before_ready",
                        )
                    elif status_token == "skip":
                        outcome = RunOutcome(
                            status="fail",
                            metrics={},
                            reason="server_skip_before_ready",
                        )
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
                        require_percentiles=require_dotnet_percentiles(cfg, pattern),
                    )

                    server_shutdown_timeout = (
                        effective_server_shutdown_timeout_ms(cfg, pattern) / 1000.0
                    )
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
                    pattern,
                    out_pattern,
                    transport,
                    size,
                    sampled,
                    require_percentiles=require_dotnet_percentiles(cfg, pattern),
                )
            outcomes.append(outcome)
            if outcome.warnings:
                warnings.extend(outcome.warnings)

        combo = aggregate_combo(outcomes)
        combo_results[(pattern, transport, size)] = combo
        if combo.status == "fail":
            failures.append((pattern, transport, size, combo.reason))
            logger.print("(failures=1) Done")
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
    pattern_env = env.copy()
    if "PERF_IO_THREADS" not in pattern_env or not str(
        pattern_env.get("PERF_IO_THREADS", "")
    ).strip():
        default_io_threads = 4 if pattern in STREAM_VARIANT_PATTERNS else 2
        pattern_env["PERF_IO_THREADS"] = str(default_io_threads)

    size_tag = ",".join(f"{size}B" for size in sizes)
    logger.print(f"    Testing {transport} | {size_tag}: ", end="", flush=True)

    combo_outcomes: Dict[Tuple[str, str, int], List[RunOutcome]] = {
        (pattern, transport, size): [] for size in sizes
    }
    timeout = max(120, cfg.multi_warmup_seconds + cfg.multi_duration_seconds + 60)
    stream_size_transition_ms = 0
    if cfg.binding == "dotnet" and pattern in MULTI_STREAM_PATTERNS:
        raw_stream_transition = env_first("PERF_MULTI_STREAM_SIZE_TRANSITION_MS")
        try:
            stream_size_transition_ms = int(raw_stream_transition or "5000")
        except ValueError:
            stream_size_transition_ms = 5000
        if stream_size_transition_ms < 0:
            stream_size_transition_ms = 0

    for run_idx in range(cfg.runs):
        logger.print(f"{run_idx + 1} ", end="", flush=True)
        for size_index, size in enumerate(sizes):
            combo_key = (pattern, transport, size)
            if supports_split_multi(cfg.binding):
                server_cmd = binding_multi_role_command(
                    cfg,
                    "server",
                    pattern,
                    transport,
                    size=size,
                )
                server_cap = spawn_capture_process(server_cmd, pattern_env)
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
                            status="fail",
                            metrics={},
                            reason="server_unsupported_before_ready",
                        )
                    elif status_token == "skip":
                        outcome = RunOutcome(
                            status="fail",
                            metrics={},
                            reason="server_skip_before_ready",
                        )
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

                if pattern in MULTI_STREAM_PATTERNS:
                    client_cmd = stream_shared_client_cmd(
                        cfg=cfg,
                        env=pattern_env,
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
                sampled = run_command_with_metrics(client_cmd, pattern_env, timeout)
                outcome = parse_run_outcome(
                    pattern,
                    pattern,
                    transport,
                    size,
                    sampled,
                    process_role="client",
                    require_percentiles=require_dotnet_percentiles(cfg, pattern),
                )

                # Server-side informational metrics may be emitted by the server process.
                server_shutdown_timeout = (
                    effective_server_shutdown_timeout_ms(cfg, pattern) / 1000.0
                )
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
                server_out_pattern = pattern
                if cfg.binding == "dotnet" and pattern.startswith("MULTI_"):
                    server_out_pattern = pattern[len("MULTI_") :]

                server_metrics = parse_result_metrics_from_text(
                    server_stdout, pattern, transport, size
                )
                if not server_metrics and server_out_pattern != pattern:
                    server_metrics = parse_result_metrics_from_text(
                        server_stdout, server_out_pattern, transport, size
                    )
                # DEALER_DEALER one-way in split multi can emit throughput/latency
                # on the server side (receiver), while the client is send-only.
                if (
                    pattern == "MULTI_DEALER_DEALER"
                    and outcome.status == "fail"
                    and outcome.reason in ("no_data", "no_bandwidth", "no_percentiles")
                ):
                    if (
                        "throughput" in server_metrics
                        and "bandwidth" in server_metrics
                        and "latency" in server_metrics
                    ):
                        merged = dict(server_metrics)
                        if "latency_p95" not in merged and "latency" in merged:
                            merged["latency_p95"] = merged["latency"]
                        if "latency_p99" not in merged and "latency_p95" in merged:
                            merged["latency_p99"] = merged["latency_p95"]
                        if (
                            "client_cpu_pct" not in merged
                            and sampled.get("cpu_pct") is not None
                        ):
                            merged["client_cpu_pct"] = float(sampled["cpu_pct"])
                        if (
                            "client_mem_mb" not in merged
                            and sampled.get("mem_mb") is not None
                        ):
                            merged["client_mem_mb"] = float(sampled["mem_mb"])
                        outcome = RunOutcome(
                            status="success",
                            metrics=merged,
                            warnings=outcome.warnings,
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
                sampled = run_command_with_metrics(cmd, pattern_env, timeout)
                outcome = parse_run_outcome(
                    pattern,
                    out_pattern,
                    transport,
                    size,
                    sampled,
                    require_percentiles=require_dotnet_percentiles(cfg, pattern),
                )
            combo_outcomes[combo_key].append(outcome)
            if outcome.warnings:
                warnings.extend(outcome.warnings)
            if stream_size_transition_ms > 0 and size_index + 1 < len(sizes):
                sleep_ms(stream_size_transition_ms)

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
                    transition_ms = cfg.multi_transport_transition_ms
                    if cfg.binding == "dotnet" and pattern in MULTI_STREAM_PATTERNS:
                        raw_stream_transition = env_first(
                            "PERF_MULTI_STREAM_TRANSPORT_TRANSITION_MS"
                        )
                        try:
                            stream_transition_ms = int(raw_stream_transition or "15000")
                        except ValueError:
                            stream_transition_ms = 15000
                        if stream_transition_ms > transition_ms:
                            transition_ms = stream_transition_ms
                    sleep_ms(transition_ms)

        if cfg.suite == "multi" and pattern_index + 1 < len(patterns):
            pattern_transition_ms = cfg.multi_pattern_transition_ms
            if cfg.binding == "dotnet" and pattern in MULTI_STREAM_PATTERNS:
                raw_stream_pattern_transition = env_first(
                    "PERF_MULTI_STREAM_PATTERN_TRANSITION_MS"
                )
                try:
                    stream_pattern_transition_ms = int(
                        raw_stream_pattern_transition or "15000"
                    )
                except ValueError:
                    stream_pattern_transition_ms = 15000
                if stream_pattern_transition_ms > pattern_transition_ms:
                    pattern_transition_ms = stream_pattern_transition_ms
            sleep_ms(pattern_transition_ms)

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
        result_map[("current", pattern, transport, size, "latency_p95")] = (
            combo.latency_p95
        )
        result_map[("current", pattern, transport, size, "latency_p99")] = (
            combo.latency_p99
        )

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
    combo_results: Dict[Tuple[str, str, int], ComboStats],
    required_metric_count: int = 3,
) -> Tuple[str, int, int]:
    total = len(combo_results)
    success = sum(1 for combo in combo_results.values() if combo.status == "success")

    expected = max(0, total * required_metric_count)
    actual = success * required_metric_count
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
        required_metrics = 5 if cfg.binding == "dotnet" else 3
        status, expected, actual = compute_completion_status(
            combo_results, required_metrics
        )

        host_meta["status"] = status
        host_meta["expected"] = str(expected)
        host_meta["actual"] = str(actual)

        tmp_dir, report_dir = suite_dirs(cfg)
        tmp_dir.mkdir(parents=True, exist_ok=True)
        report_dir.mkdir(parents=True, exist_ok=True)

        # tmp is always saved (policy v1.3).
        tmp_file = tmp_dir / result_file_name(cfg.results_tag)
        write_result_file(tmp_file, host_meta, result_map, table_lines)
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
