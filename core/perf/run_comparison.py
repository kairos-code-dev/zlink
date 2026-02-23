#!/usr/bin/env python3
"""
core/perf - zlink benchmark runner
"""
import subprocess
import os
import sys
import statistics
import platform
import time

# Environment helpers
IS_WINDOWS = os.name == 'nt'
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""
SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
BUILD_CONFIG_DIRS = ("Release", "Debug", "RelWithDebInfo", "MinSizeRel")

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
        os.path.join(ROOT_DIR, "core", "build", f"{platform_tag}-{arch_tag}", "bin", "Release"),
        os.path.join(ROOT_DIR, "core", "build", "bin"),
    ]
    build_dir = next((p for p in possible_paths if os.path.exists(p)), possible_paths[0])
    build_root = build_dir
    base = os.path.basename(build_root)
    if base in ("Release", "Debug", "RelWithDebInfo", "MinSizeRel"):
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
    BUILD_DIR = os.path.join(
        ROOT_DIR, "core", "build", "windows-x64", "bin", "Release"
    )
    CURRENT_LIB_DIR = os.path.join(
        ROOT_DIR, "core", "build", "windows-x64", "bin", "Release"
    )
else:
    BUILD_DIR, CURRENT_LIB_DIR = resolve_linux_paths()

DEFAULT_NUM_RUNS = 1

def parse_env_list(name, cast_fn):
    val = os.environ.get(name)
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


def parse_env_int(name, default):
    val = os.environ.get(name)
    if not val:
        return default
    try:
        return int(val)
    except ValueError:
        return default

# Settings for loop
_env_transports = parse_env_list("BENCH_TRANSPORTS", str)

# Default transports for ZMP sockets (non-STREAM)
TRANSPORTS = ["tcp", "tls", "ws", "wss", "inproc"]
if not IS_WINDOWS:
    TRANSPORTS.append("ipc")

# STREAM socket uses different transports (raw TCP/TLS/WS/WSS)
STREAM_TRANSPORTS = ["tcp", "tls", "ws", "wss"]
FAIL_FAST = os.environ.get("BENCH_FAIL_FAST", "0") == "1"

def select_transports(pattern_name):
    base = STREAM_TRANSPORTS if pattern_name in (
        "STREAM",
        "MULTI_STREAM",
        "GATEWAY",
        "SPOT",
        "MULTI_GATEWAY",
        "MULTI_SPOT",
    ) else TRANSPORTS
    if not _env_transports:
        return list(base)
    return [t for t in base if t in _env_transports]

_env_sizes = parse_env_list("BENCH_MSG_SIZES", int)
if _env_sizes:
    MSG_SIZES = _env_sizes
else:
    MSG_SIZES = [64, 256, 1024, 65536, 131072, 262144]

_env_multi_stream_sizes = parse_env_list("BENCH_MULTI_STREAM_MSG_SIZES", int)
if _env_multi_stream_sizes:
    MULTI_STREAM_MSG_SIZES = _env_multi_stream_sizes
elif _env_sizes:
    MULTI_STREAM_MSG_SIZES = _env_sizes
else:
    MULTI_STREAM_MSG_SIZES = list(MSG_SIZES)

MULTI_STREAM_ATTEMPTS = parse_env_int("BENCH_MULTI_STREAM_ATTEMPTS", 2)
MULTI_RUN_ATTEMPTS = max(1, parse_env_int("BENCH_MULTI_ATTEMPTS", 2))
DEFAULT_RUN_COOLDOWN_MS = max(
    0, parse_env_int("BENCH_MULTI_RUN_COOLDOWN_MS", 3000)
)

base_env = os.environ.copy()

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

        current_path = os.path.join(BUILD_DIR, current_bin + EXE_SUFFIX)
        if not os.path.exists(current_path):
            missing_current.append(p_name)

    return sorted(set(missing_current))


def collect_missing_build_targets(comparisons, requested):
    targets = []
    for current_bin, p_name in comparisons:
        if requested is not None and p_name not in requested:
            continue

        current_path = os.path.join(BUILD_DIR, current_bin + EXE_SUFFIX)
        if not os.path.exists(current_path) and current_bin not in targets:
            targets.append(current_bin)

    return targets


def run_cmake_build(cmake_build_dir, targets):
    cmd = ["cmake", "--build", cmake_build_dir]
    if IS_WINDOWS:
        cmd.extend(["--config", "Release"])
    if targets:
        cmd.append("--target")
        cmd.extend(targets)

    print(f"  > Auto-building missing benchmark binaries in {cmake_build_dir}",
          flush=True)
    print(f"  > Build command: {' '.join(cmd)}", flush=True)

    try:
        return subprocess.run(cmd).returncode
    except FileNotFoundError:
        print("Error: cmake not found in PATH.", file=sys.stderr)
        return 127


def parse_result_line(line, transport, expected_sizes):
    if not line.startswith("RESULT,"):
        return None
    parts = line.split(",")
    if len(parts) < 7:
        return None
    try:
        line_transport = parts[3]
        line_size = int(parts[4])
        metric = parts[5].strip().lower()
        value = float(parts[6])
    except ValueError:
        return None
    if line_transport != transport or line_size not in expected_sizes:
        return None
    if metric not in ("throughput", "latency"):
        return None
    return line_transport, line_size, metric, value


def run_multi_sizes_test(binary_name, lib_name, transport, sizes, pattern_name):
    binary_path = os.path.join(BUILD_DIR, binary_name + EXE_SUFFIX)
    fallback_size = sizes[0] if sizes else 64
    duration_seconds = max(1, parse_env_int("BENCH_MULTI_DURATION_SECONDS", 5))
    timeout_sec = max(120, duration_seconds * max(1, len(sizes)) * 8 + 60)
    attempts = max(1, MULTI_RUN_ATTEMPTS)
    if pattern_name == "MULTI_STREAM":
        attempts = max(attempts, MULTI_STREAM_ATTEMPTS)
        timeout_sec = max(180, timeout_sec)

    expected_sizes = set(sizes)
    for attempt_idx in range(attempts):
        env = get_env_for_lib(lib_name)
        env["BENCH_MSG_SIZES"] = ",".join(str(sz) for sz in sizes)
        env["BENCH_MULTI_PATTERN"] = pattern_name
        if pattern_name == "MULTI_STREAM":
            env["BENCH_MULTI_STREAM_MSG_SIZES"] = ",".join(str(sz) for sz in sizes)

        try:
            if IS_WINDOWS:
                cmd = [binary_path, lib_name, transport, str(fallback_size)]
            elif os.environ.get("BENCH_TASKSET") == "1":
                cmd = [
                    "taskset",
                    "-c",
                    "1",
                    binary_path,
                    lib_name,
                    transport,
                    str(fallback_size),
                ]
            else:
                cmd = [binary_path, lib_name, transport, str(fallback_size)]

            result = subprocess.run(
                cmd, env=env, capture_output=True, text=True, timeout=timeout_sec
            )
            stderr = (result.stderr or "").lower()
            if "protocol not supported" in stderr:
                return {
                    "unsupported": True,
                    "parsed": {},
                    "timed_out": False,
                    "returncode": result.returncode,
                }
            if result.returncode != 0:
                if attempt_idx + 1 < attempts:
                    continue
                return {
                    "unsupported": False,
                    "parsed": {},
                    "timed_out": False,
                    "returncode": result.returncode,
                }

            parsed = {}
            for line in result.stdout.splitlines():
                parsed_line = parse_result_line(line, transport, expected_sizes)
                if not parsed_line:
                    continue
                line_transport, line_size, metric, value = parsed_line
                parsed[f"{line_transport}|{line_size}|{metric}"] = value

            if parsed:
                return {
                    "unsupported": False,
                    "parsed": parsed,
                    "timed_out": False,
                    "returncode": 0,
                }
            if attempt_idx + 1 < attempts:
                continue
            return {
                "unsupported": False,
                "parsed": {},
                "timed_out": False,
                "returncode": result.returncode,
            }
        except subprocess.TimeoutExpired:
            if attempt_idx + 1 < attempts:
                continue
            return {
                "unsupported": False,
                "parsed": {},
                "timed_out": True,
                "returncode": -1,
            }
        except Exception:
            if attempt_idx + 1 < attempts:
                continue
            return {
                "unsupported": False,
                "parsed": {},
                "timed_out": False,
                "returncode": -1,
            }

    return {"unsupported": False, "parsed": {}, "timed_out": False, "returncode": -1}

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
    attempts = 1
    if pattern_name == "MULTI_STREAM":
        # STREAM multi can sporadically need extra teardown time.
        timeout_sec = max(
            120,
            parse_env_int("BENCH_MULTI_WARMUP_SECONDS", 3)
            + parse_env_int("BENCH_MULTI_DURATION_SECONDS", 5)
            + 90,
        )
        attempts = max(1, MULTI_STREAM_ATTEMPTS)

    # For STREAM pattern with TLS/WS/WSS, limit msg count to avoid buffer/deadlock issues
    # WS has lower limits (~4K for 1KB+ messages), so use conservative 5000 for all
    for attempt_idx in range(attempts):
        env = get_env_for_lib(lib_name)
        if pattern_name.startswith("MULTI_"):
            env["BENCH_MULTI_PATTERN"] = pattern_name
        if pattern_name == "STREAM" and transport in ("tls", "ws", "wss"):
            env["BENCH_MSG_COUNT"] = "5000"
            env["BENCH_WARMUP_COUNT"] = "100"  # Reduce warmup as well

        try:
            # Args: [lib_name] [transport] [size]
            # Default: do not pin CPU. Enable with BENCH_TASKSET=1 on Linux.
            if IS_WINDOWS:
                cmd = [binary_path, lib_name, transport, str(size)]
            elif os.environ.get("BENCH_TASKSET") == "1":
                cmd = ["taskset", "-c", "1", binary_path, lib_name, transport,
                       str(size)]
            else:
                cmd = [binary_path, lib_name, transport, str(size)]

            result = subprocess.run(cmd,
                                    env=env,
                                    capture_output=True,
                                    text=True,
                                    timeout=timeout_sec)
            stderr = (result.stderr or "").lower()
            if "protocol not supported" in stderr:
                return [{"metric": "_unsupported_transport_", "value": 1.0}]
            if result.returncode != 0:
                if attempt_idx + 1 < attempts:
                    continue
                return []

            parsed = []
            for line in result.stdout.splitlines():
                if line.startswith("RESULT,"):
                    p = line.split(",")
                    if len(p) >= 7:
                        parsed.append({"metric": p[5], "value": float(p[6])})
            if parsed:
                return parsed
            if attempt_idx + 1 < attempts:
                continue
            return []
        except subprocess.TimeoutExpired:
            if attempt_idx + 1 < attempts:
                continue
            return None
        except Exception:
            if attempt_idx + 1 < attempts:
                continue
            return []

def collect_data(binary_name, lib_name, pattern_name, num_runs, transports=None):
    print(f"  > Benchmarking {lib_name} for {pattern_name}...")
    final_stats = {}  # (tr, size, metric) -> avg_value
    failures = []

    if transports is None:
        transports = TRANSPORTS

    run_cooldown_ms = max(
        0, parse_env_int("BENCH_MULTI_RUN_COOLDOWN_MS", DEFAULT_RUN_COOLDOWN_MS)
    )

    for tr in transports:
        sizes = (MULTI_STREAM_MSG_SIZES
                 if pattern_name == "MULTI_STREAM" else MSG_SIZES)

        if pattern_name.startswith("MULTI_"):
            size_tag = ",".join(f"{sz}B" for sz in sizes)
            print(f"    Testing {tr} | {size_tag}: ", end="", flush=True)
            metrics_raw = {}
            failed_runs = 0
            expected_keys = []
            for sz in sizes:
                expected_keys.append(f"{tr}|{sz}|throughput")
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

                outcome = run_multi_sizes_test(
                    binary_name, lib_name, tr, sizes, pattern_name
                )
                rc = outcome.get("returncode", 0)
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

                if outcome.get("unsupported", False):
                    tr_supported = False
                    for sz in sizes:
                        final_stats[f"{tr}|{sz}|throughput"] = 0
                        final_stats[f"{tr}|{sz}|latency"] = 0
                        final_stats[f"{tr}|{sz}|unsupported"] = 1
                    print("unsupported ", end="", flush=True)
                    break

                parsed = outcome.get("parsed", {})
                if not parsed:
                    failed_runs += 1
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
                        failures.append(
                            (pattern_name, lib_name, tr, sz, f"missing_{metric}{suffix}")
                        )
                    if FAIL_FAST:
                        print(f"(failures={failed_runs}) ", end="", flush=True)
                        print("aborting")
                        return final_stats, failures

                for key, value in parsed.items():
                    if key not in metrics_raw:
                        metrics_raw[key] = []
                    metrics_raw[key].append(value)
                maybe_cooldown()

            if not tr_supported:
                print("Done")
                continue

            for key in expected_keys:
                vals = metrics_raw.get(key, [])
                final_stats[key] = statistics.median(vals) if vals else 0

            if failed_runs:
                print(f"(failures={failed_runs}) ", end="", flush=True)
            print("Done")
            continue

        tr_supported = True
        for idx, sz in enumerate(sizes):
            print(f"    Testing {tr} | {sz}B: ", end="", flush=True)
            metrics_raw = {}  # metric_name -> list of values
            failed_runs = 0

            if not tr_supported:
                final_stats[f"{tr}|{sz}|throughput"] = 0
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
                    final_stats[f"{tr}|{sz}|latency"] = 0
                    final_stats[f"{tr}|{sz}|unsupported"] = 1
                    for next_idx in range(idx + 1, len(sizes)):
                        next_sz = sizes[next_idx]
                        final_stats[f"{tr}|{next_sz}|throughput"] = 0
                        final_stats[f"{tr}|{next_sz}|latency"] = 0
                        final_stats[f"{tr}|{next_sz}|unsupported"] = 1
                    if FAIL_FAST:
                        break
                    maybe_cooldown()
                    continue
                for r in results:
                    m = r['metric']
                    if m not in metrics_raw:
                        metrics_raw[m] = []
                    metrics_raw[m].append(r['value'])
                maybe_cooldown()

            for m, vals in metrics_raw.items():
                if vals:
                    avg = statistics.median(vals)
                else:
                    avg = 0
                final_stats[f"{tr}|{sz}|{m}"] = avg
            if failed_runs:
                print(f"(failures={failed_runs}) ", end="", flush=True)
            print("Done")
    return final_stats, failures


def format_throughput(size, msgs_per_sec):
    return f"{msgs_per_sec/1e3:6.2f} Kmsg/s"


def parse_args():
    usage = (
        "Usage: run_comparison.py [PATTERN] [options]\n\n"
        "Measure current zlink benchmarks.\n\n"
        "Note: PATTERN=ALL includes STREAM and MULTI_* patterns by default.\n\n"
        "Options:\n"
        "  --runs N                Iterations per configuration (default: 1)\n"
        "  --build-dir PATH        Build directory (default: core/build/<platform>-<arch>)\n"
        "  --pin-cpu               Pin CPU core during benchmarks (Linux taskset)\n"
        "  -h, --help              Show this help\n"
        "  Missing benchmark binaries are auto-built via cmake --build.\n"
        "\n"
        "Env:\n"
        "  BENCH_TASKSET=1         Enable taskset CPU pinning on Linux\n"
        "  BENCH_TRANSPORTS=list  Comma-separated transports (e.g., tcp,ws,wss)\n"
        "  BENCH_MULTI_CLIENTS=100\n"
        "  BENCH_MULTI_INFLIGHT=30\n"
        "  BENCH_MULTI_HWM=100000\n"
        "  BENCH_MULTI_SNDTIMEO_MS=5000\n"
        "  BENCH_MULTI_RCVTIMEO_MS=5000\n"
        "  BENCH_MULTI_CONNECT_CONCURRENCY=auto (clients>=10000 ? 1024 : 128)\n"
        "  BENCH_MULTI_WARMUP_SECONDS=3\n"
        "  BENCH_MULTI_DURATION_SECONDS=5\n"
        "  BENCH_MULTI_DRAIN_MS=300  (exception patterns) / 0 (default)\n"
        "  BENCH_MULTI_SIZE_TRANSITION_DRAIN_MS=300\n"
        "  BENCH_MULTI_RUN_COOLDOWN_MS=3000\n"
        "  BENCH_MULTI_ATTEMPTS=2\n"
        "  BENCH_MULTI_STREAM_MSG_SIZES=64,256,1024,65536,131072,262144\n"
        "  BENCH_MULTI_STREAM_ATTEMPTS=2\n"
    )
    p_req = "ALL"
    num_runs = DEFAULT_NUM_RUNS
    build_dir = ""
    pin_cpu = False

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
        elif arg.startswith("--"):
            print(f"Error: unknown option: {arg}", file=sys.stderr)
            print("Use --help to see available options.", file=sys.stderr)
            sys.exit(1)
        elif not arg.startswith("--") and p_req == "ALL":
            p_req = arg.upper()
        elif not arg.startswith("--"):
            print(f"Error: unexpected positional argument: {arg}",
                  file=sys.stderr)
            print("Use comma-separated PATTERN in a single positional arg.",
                  file=sys.stderr)
            sys.exit(1)
        i += 1

    if num_runs < 1:
        print("Error: --runs must be >= 1.", file=sys.stderr)
        sys.exit(1)

    return p_req, num_runs, build_dir, pin_cpu

def main():
    global BUILD_DIR, CURRENT_LIB_DIR, base_env
    p_req, num_runs, build_dir, pin_cpu = parse_args()
    if build_dir:
        BUILD_DIR = normalize_build_dir(build_dir)
    else:
        BUILD_DIR = normalize_build_dir(BUILD_DIR)

    CURRENT_LIB_DIR = derive_current_lib_dir(BUILD_DIR)
    if pin_cpu:
        base_env["BENCH_TASKSET"] = "1"

    comparisons = [
        ("comp_current_pair", "PAIR"),
        ("comp_current_pubsub", "PUBSUB"),
        ("comp_current_dealer_dealer", "DEALER_DEALER"),
        ("comp_current_dealer_router", "DEALER_ROUTER"),
        ("comp_current_router_router", "ROUTER_ROUTER"),
        ("comp_current_router_router_poll", "ROUTER_ROUTER_POLL"),
        ("comp_current_stream", "STREAM"),
        ("comp_current_multi_dealer_dealer", "MULTI_DEALER_DEALER"),
        ("comp_current_multi_dealer_router", "MULTI_DEALER_ROUTER"),
        ("comp_current_multi_router_router", "MULTI_ROUTER_ROUTER"),
        ("comp_current_multi_pubsub", "MULTI_PUBSUB"),
        ("comp_current_multi_gateway", "MULTI_GATEWAY"),
        ("comp_current_multi_spot", "MULTI_SPOT"),
        ("comp_current_multi_stream", "MULTI_STREAM"),
        ("comp_current_gateway", "GATEWAY"),
        ("comp_current_spot", "SPOT"),
    ]

    all_failures = []
    if p_req == "ALL":
        requested = None
    else:
        requested = {p.strip().upper() for p in p_req.split(",") if p.strip()}
        if not requested:
            print("Error: --pattern requires at least one value.", file=sys.stderr)
            sys.exit(1)

    missing_current = collect_missing_patterns(comparisons, requested)
    if missing_current:
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
                print(
                    "  current missing: " + ", ".join(missing_current),
                    file=sys.stderr,
                )
            sys.exit(1)

        build_targets = collect_missing_build_targets(comparisons, requested)
        build_rc = run_cmake_build(cmake_build_dir, build_targets)
        if build_rc != 0:
            print(f"Error: auto-build failed with exit code {build_rc}.",
                  file=sys.stderr)
            sys.exit(build_rc)

    missing_current = collect_missing_patterns(comparisons, requested)

    if missing_current:
        print(
            "Error: current benchmark binaries are missing for patterns: "
            + ", ".join(missing_current),
            file=sys.stderr,
        )
        print("Auto-build was attempted but some current binaries are still missing.",
              file=sys.stderr)
        sys.exit(1)

    for current_bin, p_name in comparisons:
        if requested is not None and p_name not in requested:
            continue

        print(f"\n## PATTERN: {p_name}")

        pattern_transports = select_transports(p_name)
        if not pattern_transports:
            print(f"  Skipping {p_name}: no matching transports.")
            continue

        c_stats, failures = collect_data(current_bin, "current", p_name, num_runs, pattern_transports)
        all_failures.extend(failures)

        # Print Table
        size_w = 8
        tp_w = 16
        lat_w = 12
        for tr in pattern_transports:
            print(f"\n### Transport: {tr}")
            print(
                f"| {'Size':<{size_w}} | {'Throughput':>{tp_w}} | {'Latency':>{lat_w}} |"
            )
            print(
                f"|{'-' * (size_w + 2)}|{'-' * (tp_w + 2)}|{'-' * (lat_w + 2)}|"
            )
            sizes = (MULTI_STREAM_MSG_SIZES
                     if p_name == "MULTI_STREAM" else MSG_SIZES)
            for sz in sizes:
                ct = c_stats.get(f"{tr}|{sz}|throughput", 0)
                cl = c_stats.get(f"{tr}|{sz}|latency", 0)
                c_unsupported = bool(c_stats.get(f"{tr}|{sz}|unsupported", 0))
                if c_unsupported:
                    ct_s = "N/A"
                    cl_s = "N/A"
                else:
                    ct_s = format_throughput(sz, ct)
                    cl_s = f"{cl:8.2f} us"
                print(
                    f"| {f'{sz}B':<{size_w}} | {ct_s:>{tp_w}} | {cl_s:>{lat_w}} |"
                )

    if all_failures:
        print("\n## Failures")
        for pattern, lib_name, tr, sz, reason in all_failures:
            print(f"- {pattern} {lib_name} {tr} {sz}B: {reason}")
        if FAIL_FAST:
            raise SystemExit(1)

if __name__ == "__main__":
    main()
