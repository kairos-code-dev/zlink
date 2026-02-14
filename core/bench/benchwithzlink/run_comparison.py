#!/usr/bin/env python3
"""
benchwithzlink - zlink version comparison benchmarks

Compares:
- baseline: Previous zlink version (from baseline/zlink_dist/<platform>-<arch>/)
- current: Current zlink build
"""
import subprocess
import os
import sys
import statistics
import json
import platform
import time

# Environment helpers
IS_WINDOWS = os.name == 'nt'
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""
SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))

def platform_arch_tag():
    sys_name = platform.system().lower()
    if "darwin" in sys_name:
        platform_tag = "macos"
    elif "windows" in sys_name:
        platform_tag = "windows"
    else:
        platform_tag = "linux"

    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        arch_tag = "x64"
    elif machine in ("aarch64", "arm64"):
        arch_tag = "arm64"
    else:
        arch_tag = machine
    return platform_tag, arch_tag

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
    baseline_lib_dir = os.path.abspath(
        os.path.join(
            ROOT_DIR,
            "core",
            "bench",
            "benchwithzlink",
            "baseline",
            "zlink_dist",
            f"{platform_tag}-{arch_tag}",
            "lib",
        )
    )
    build_root = build_dir
    base = os.path.basename(build_root)
    if base in ("Release", "Debug", "RelWithDebInfo", "MinSizeRel"):
        bin_root = os.path.dirname(build_root)
        if os.path.basename(bin_root) == "bin":
            build_root = os.path.dirname(bin_root)
    elif base == "bin":
        build_root = os.path.dirname(build_root)
    current_lib_dir = os.path.abspath(os.path.join(build_root, "lib"))
    return build_dir, baseline_lib_dir, current_lib_dir

def normalize_build_dir(path):
    if not path:
        return path
    abs_path = os.path.abspath(path)
    if os.path.isdir(abs_path):
        bin_dir = os.path.join(abs_path, "bin")
        release_dir = os.path.join(bin_dir, "Release")
        debug_dir = os.path.join(bin_dir, "Debug")
        # Check bin dir first on Linux
        if os.path.exists(os.path.join(bin_dir, "comp_current_pair" + EXE_SUFFIX)):
            return bin_dir
        if os.path.exists(os.path.join(abs_path, "comp_current_pair" + EXE_SUFFIX)):
            return abs_path
        if os.path.exists(os.path.join(release_dir, "comp_current_pair" + EXE_SUFFIX)):
            return release_dir
        if os.path.exists(os.path.join(debug_dir, "comp_current_pair" + EXE_SUFFIX)):
            return debug_dir
    return abs_path

def derive_current_lib_dir(build_dir):
    build_root = build_dir
    base = os.path.basename(build_root)
    if base in ("Release", "Debug", "RelWithDebInfo", "MinSizeRel"):
        bin_root = os.path.dirname(build_root)
        if os.path.basename(bin_root) == "bin":
            build_root = os.path.dirname(bin_root)
    elif base == "bin":
        build_root = os.path.dirname(build_root)
    return os.path.abspath(os.path.join(build_root, "lib"))

if IS_WINDOWS:
    BUILD_DIR = os.path.join(
        ROOT_DIR, "core", "build", "windows-x64", "bin", "Release"
    )
    BASELINE_LIB_DIR = os.path.join(
        ROOT_DIR,
        "core",
        "bench",
        "benchwithzlink",
        "baseline",
        "zlink_dist",
        "windows-x64",
        "bin",
    )
    CURRENT_LIB_DIR = os.path.join(
        ROOT_DIR, "core", "build", "windows-x64", "bin", "Release"
    )
else:
    BUILD_DIR, BASELINE_LIB_DIR, CURRENT_LIB_DIR = resolve_linux_paths()

DEFAULT_NUM_RUNS = 3
_platform_tag, _arch_tag = platform_arch_tag()
CACHE_FILE = os.path.join(
    ROOT_DIR,
    "core",
    "bench",
    "benchwithzlink",
    f"baseline_cache_{_platform_tag}-{_arch_tag}.json",
)

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

def select_transports(pattern_name):
    if pattern_name == "MULTI_STREAM":
        base = ["tcp"]
    else:
        base = STREAM_TRANSPORTS if pattern_name in ("STREAM", "GATEWAY",
                                                     "SPOT") else TRANSPORTS
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

MULTI_STREAM_SCENARIO = os.environ.get("BENCH_MULTI_STREAM_SCENARIO", "s2")
MULTI_STREAM_CCU = parse_env_int("BENCH_MULTI_STREAM_CCU", 10000)
MULTI_STREAM_INFLIGHT = parse_env_int("BENCH_MULTI_STREAM_INFLIGHT", 30)
MULTI_STREAM_WARMUP = parse_env_int("BENCH_MULTI_STREAM_WARMUP", 3)
MULTI_STREAM_MEASURE = parse_env_int("BENCH_MULTI_STREAM_MEASURE", 10)
MULTI_STREAM_DRAIN_TIMEOUT = parse_env_int("BENCH_MULTI_STREAM_DRAIN_TIMEOUT",
                                           10)
MULTI_STREAM_CONNECT_CONCURRENCY = parse_env_int(
    "BENCH_MULTI_STREAM_CONNECT_CONCURRENCY", 256)
MULTI_STREAM_CONNECT_TIMEOUT = parse_env_int("BENCH_MULTI_STREAM_CONNECT_TIMEOUT",
                                             10)
MULTI_STREAM_CONNECT_RETRIES = parse_env_int("BENCH_MULTI_STREAM_CONNECT_RETRIES",
                                             3)
MULTI_STREAM_CONNECT_RETRY_DELAY_MS = parse_env_int(
    "BENCH_MULTI_STREAM_CONNECT_RETRY_DELAY_MS", 100)
MULTI_STREAM_BACKLOG = parse_env_int("BENCH_MULTI_STREAM_BACKLOG", 32768)
MULTI_STREAM_HWM = parse_env_int("BENCH_MULTI_STREAM_HWM", 1000000)
MULTI_STREAM_SNDBUF = parse_env_int("BENCH_MULTI_STREAM_SNDBUF", 262144)
MULTI_STREAM_RCVBUF = parse_env_int("BENCH_MULTI_STREAM_RCVBUF", 262144)
MULTI_STREAM_IO_THREADS = parse_env_int(
    "BENCH_MULTI_STREAM_IO_THREADS",
    parse_env_int("BENCH_IO_THREADS", 32))
MULTI_STREAM_LATENCY_SAMPLE_RATE = parse_env_int(
    "BENCH_MULTI_STREAM_LATENCY_SAMPLE_RATE", 16)
MULTI_STREAM_BASE_PORT = parse_env_int("BENCH_MULTI_STREAM_BASE_PORT", 27110)
MULTI_STREAM_SERVER_BOOT_SEC = parse_env_int(
    "BENCH_MULTI_STREAM_SERVER_BOOT_SEC", 2)
MULTI_STREAM_ATTEMPTS = parse_env_int("BENCH_MULTI_STREAM_ATTEMPTS", 2)

_stream_scenario_ready = False
_stream_scenario_bin = ""
_multi_stream_port_counter = 0

base_env = os.environ.copy()

def get_env_for_lib(lib_name):
    env = base_env.copy()
    if IS_WINDOWS:
        if lib_name == "baseline":
            env["PATH"] = f"{BASELINE_LIB_DIR};{env.get('PATH', '')}"
        else:
            env["PATH"] = f"{CURRENT_LIB_DIR};{env.get('PATH', '')}"
    else:
        if lib_name == "baseline":
            env["LD_LIBRARY_PATH"] = f"{BASELINE_LIB_DIR}:{env.get('LD_LIBRARY_PATH', '')}"
        else:
            env["LD_LIBRARY_PATH"] = f"{CURRENT_LIB_DIR}:{env.get('LD_LIBRARY_PATH', '')}"
    return env


def derive_build_root_from_bin_dir(path):
    build_root = path
    base = os.path.basename(build_root)
    if base in ("Release", "Debug", "RelWithDebInfo", "MinSizeRel"):
        bin_root = os.path.dirname(build_root)
        if os.path.basename(bin_root) == "bin":
            build_root = os.path.dirname(bin_root)
    elif base == "bin":
        build_root = os.path.dirname(build_root)
    return build_root


def stream_scenario_bin_path():
    build_root = derive_build_root_from_bin_dir(BUILD_DIR)
    return os.path.join(build_root, "bin",
                        "test_scenario_stream_zlink" + EXE_SUFFIX)


def ensure_stream_scenario_binary():
    global _stream_scenario_ready
    global _stream_scenario_bin
    if _stream_scenario_ready:
        return _stream_scenario_bin

    _stream_scenario_ready = True
    candidate = stream_scenario_bin_path()
    if os.path.exists(candidate):
        _stream_scenario_bin = candidate
        return _stream_scenario_bin

    build_root = derive_build_root_from_bin_dir(BUILD_DIR)
    try:
        subprocess.run(
            ["cmake", "-S", ROOT_DIR, "-B", build_root, "-DZLINK_BUILD_TESTS=ON"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        jobs = os.cpu_count() or 1
        subprocess.run(
            ["cmake", "--build", build_root, "--target",
             "test_scenario_stream_zlink", "-j", str(jobs)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        _stream_scenario_bin = ""
        return _stream_scenario_bin

    candidate = stream_scenario_bin_path()
    if os.path.exists(candidate):
        _stream_scenario_bin = candidate
    return _stream_scenario_bin


def parse_scenario_result_line(line):
    if not line.startswith("RESULT "):
        return None
    fields = {}
    for token in line.strip().split()[1:]:
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = value
    return fields if fields else None


def next_multi_stream_port():
    global _multi_stream_port_counter
    _multi_stream_port_counter += 1
    return MULTI_STREAM_BASE_PORT + _multi_stream_port_counter


def run_multi_stream_test(lib_name, transport, size):
    if transport != "tcp":
        return []

    scenario_bin = ensure_stream_scenario_binary()
    if not scenario_bin:
        return []

    env = get_env_for_lib(lib_name)
    port = next_multi_stream_port()
    scenario_id = f"bench-ms-{lib_name}-{transport}-{size}-{port}"
    base_args = [
        "--scenario", MULTI_STREAM_SCENARIO,
        "--transport", transport,
        "--port", str(port),
        "--ccu", str(max(1, MULTI_STREAM_CCU)),
        "--size", str(max(16, int(size))),
        "--inflight", str(max(1, MULTI_STREAM_INFLIGHT)),
        "--warmup", str(max(1, MULTI_STREAM_WARMUP)),
        "--measure", str(max(1, MULTI_STREAM_MEASURE)),
        "--drain-timeout", str(max(1, MULTI_STREAM_DRAIN_TIMEOUT)),
        "--connect-concurrency", str(max(1, MULTI_STREAM_CONNECT_CONCURRENCY)),
        "--connect-timeout", str(max(1, MULTI_STREAM_CONNECT_TIMEOUT)),
        "--connect-retries", str(max(1, MULTI_STREAM_CONNECT_RETRIES)),
        "--connect-retry-delay-ms",
        str(max(0, MULTI_STREAM_CONNECT_RETRY_DELAY_MS)),
        "--backlog", str(max(1, MULTI_STREAM_BACKLOG)),
        "--hwm", str(max(1, MULTI_STREAM_HWM)),
        "--sndbuf", str(max(1, MULTI_STREAM_SNDBUF)),
        "--rcvbuf", str(max(1, MULTI_STREAM_RCVBUF)),
        "--io-threads", str(max(1, MULTI_STREAM_IO_THREADS)),
        "--latency-sample-rate", str(max(0, MULTI_STREAM_LATENCY_SAMPLE_RATE)),
    ]

    server_cmd = [
        scenario_bin,
        "--scenario-id", scenario_id + "-server",
        "--role", "server",
    ] + base_args
    client_cmd = [
        scenario_bin,
        "--scenario-id", scenario_id,
        "--role", "client",
    ] + base_args

    timeout_sec = max(
        60,
        MULTI_STREAM_WARMUP + MULTI_STREAM_MEASURE + MULTI_STREAM_DRAIN_TIMEOUT
        + MULTI_STREAM_CONNECT_TIMEOUT + 30,
    )

    attempts = max(1, MULTI_STREAM_ATTEMPTS)
    for _ in range(attempts):
        server_proc = None
        try:
            server_proc = subprocess.Popen(
                server_cmd,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            time.sleep(float(max(1, MULTI_STREAM_SERVER_BOOT_SEC)))
            client_res = subprocess.run(
                client_cmd,
                env=env,
                capture_output=True,
                text=True,
                timeout=timeout_sec,
            )

            parsed_result = None
            for line in client_res.stdout.splitlines():
                parsed = parse_scenario_result_line(line)
                if parsed is not None:
                    parsed_result = parsed
                    break
            if parsed_result is None:
                continue

            if parsed_result.get("pass_fail", "").upper() == "SKIP":
                return []

            throughput = float(parsed_result.get("throughput", 0.0))
            latency = float(parsed_result.get(
                "p95_us", parsed_result.get("p50_us", 0.0)))
            return [
                {"metric": "throughput", "value": throughput},
                {"metric": "latency", "value": latency},
            ]
        except Exception:
            pass
        finally:
            if server_proc is not None:
                try:
                    if server_proc.poll() is None:
                        server_proc.terminate()
                        server_proc.wait(timeout=5)
                except Exception:
                    try:
                        if server_proc.poll() is None:
                            server_proc.kill()
                    except Exception:
                        pass
        time.sleep(0.5)

    return []

def run_single_test(binary_name, lib_name, transport, size, pattern_name=""):
    """Runs a single binary for one specific config."""
    if pattern_name == "MULTI_STREAM":
        return run_multi_stream_test(lib_name, transport, size)

    binary_path = os.path.join(BUILD_DIR, binary_name + EXE_SUFFIX)
    env = get_env_for_lib(lib_name)

    # For STREAM pattern with TLS/WS/WSS, limit msg count to avoid buffer/deadlock issues
    # WS has lower limits (~4K for 1KB+ messages), so use conservative 5000 for all
    if pattern_name == "STREAM" and transport in ("tls", "ws", "wss"):
        env["BENCH_MSG_COUNT"] = "5000"
        env["BENCH_WARMUP_COUNT"] = "100"  # Reduce warmup as well

    try:
        # Args: [lib_name] [transport] [size]
        # Default: do not pin CPU. Enable with BENCH_TASKSET=1 on Linux.
        if IS_WINDOWS:
            cmd = [binary_path, lib_name, transport, str(size)]
        elif os.environ.get("BENCH_TASKSET") == "1":
            cmd = ["taskset", "-c", "1", binary_path, lib_name, transport, str(size)]
        else:
            cmd = [binary_path, lib_name, transport, str(size)]
        result = subprocess.run(cmd,
                                env=env,
                                capture_output=True,
                                text=True,
                                timeout=60)
        if result.returncode != 0:
            return []

        parsed = []
        for line in result.stdout.splitlines():
            if line.startswith("RESULT,"):
                p = line.split(",")
                if len(p) >= 7:
                    parsed.append({"metric": p[5], "value": float(p[6])})
        return parsed
    except subprocess.TimeoutExpired:
        return None
    except Exception:
        return []

def collect_data(binary_name, lib_name, pattern_name, num_runs, transports=None):
    print(f"  > Benchmarking {lib_name} for {pattern_name}...")
    final_stats = {}  # (tr, size, metric) -> avg_value
    failures = []

    if transports is None:
        transports = TRANSPORTS

    for tr in transports:
        sizes = (MULTI_STREAM_MSG_SIZES
                 if pattern_name == "MULTI_STREAM" else MSG_SIZES)

        for sz in sizes:
            print(f"    Testing {tr} | {sz}B: ", end="", flush=True)
            metrics_raw = {}  # metric_name -> list of values
            failed_runs = 0

            for i in range(num_runs):
                print(f"{i+1} ", end="", flush=True)
                results = run_single_test(binary_name, lib_name, tr, sz, pattern_name)
                if results is None:
                    failed_runs += 1
                    failures.append((pattern_name, lib_name, tr, sz, "timeout"))
                    continue
                if not results:
                    failed_runs += 1
                    failures.append((pattern_name, lib_name, tr, sz, "no_data"))
                    continue
                for r in results:
                    m = r['metric']
                    if m not in metrics_raw:
                        metrics_raw[m] = []
                    metrics_raw[m].append(r['value'])

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
        "Compare baseline zlink (previous version) vs current zlink (new build).\n\n"
        "Note: PATTERN=ALL includes STREAM and MULTI_STREAM by default.\n\n"
        "Options:\n"
        "  --refresh-baseline      Refresh baseline cache\n"
        "  --refresh-libzlink        Alias for --refresh-baseline\n"
        "  --current-only          Run only current benchmarks\n"
        "  --zlink-only            Alias for --current-only\n"
        "  --runs N                Iterations per configuration (default: 3)\n"
        "  --build-dir PATH        Build directory (default: core/build/<platform>-<arch>)\n"
        "  --pin-cpu               Pin CPU core during benchmarks (Linux taskset)\n"
        "  -h, --help              Show this help\n"
        "\n"
        "Env:\n"
        "  BENCH_TASKSET=1         Enable taskset CPU pinning on Linux\n"
        "  BENCH_TRANSPORTS=list  Comma-separated transports (e.g., tcp,ws,wss)\n"
        "  BENCH_MULTI_STREAM_SCENARIO=s2\n"
        "  BENCH_MULTI_STREAM_MSG_SIZES=64,256,1024,65536,131072,262144\n"
    )
    refresh = False
    p_req = "ALL"
    num_runs = DEFAULT_NUM_RUNS
    build_dir = ""
    current_only = False
    pin_cpu = False

    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg in ("-h", "--help"):
            print(usage)
            sys.exit(0)
        if arg == "--refresh-baseline" or arg == "--refresh-libzlink":
            refresh = True
        elif arg == "--current-only" or arg == "--zlink-only":
            current_only = True
        elif arg == "--pin-cpu":
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
        elif not arg.startswith("--") and p_req == "ALL":
            p_req = arg.upper()
        i += 1

    if num_runs < 1:
        print("Error: --runs must be >= 1.", file=sys.stderr)
        sys.exit(1)

    return p_req, refresh, num_runs, build_dir, current_only, pin_cpu

def main():
    global BUILD_DIR, CURRENT_LIB_DIR, base_env
    p_req, refresh, num_runs, build_dir, current_only, pin_cpu = parse_args()
    if build_dir:
        BUILD_DIR = normalize_build_dir(build_dir)
    else:
        BUILD_DIR = normalize_build_dir(BUILD_DIR)

    CURRENT_LIB_DIR = derive_current_lib_dir(BUILD_DIR)
    if pin_cpu:
        base_env["BENCH_TASKSET"] = "1"

    comparisons = [
        ("comp_baseline_pair", "comp_current_pair", "PAIR"),
        ("comp_baseline_pubsub", "comp_current_pubsub", "PUBSUB"),
        ("comp_baseline_dealer_dealer", "comp_current_dealer_dealer", "DEALER_DEALER"),
        ("comp_baseline_dealer_router", "comp_current_dealer_router", "DEALER_ROUTER"),
        ("comp_baseline_router_router", "comp_current_router_router", "ROUTER_ROUTER"),
        ("comp_baseline_router_router_poll", "comp_current_router_router_poll",
         "ROUTER_ROUTER_POLL"),
        ("comp_baseline_stream", "comp_current_stream", "STREAM"),
        ("comp_baseline_gateway", "comp_current_gateway", "GATEWAY"),
        ("comp_baseline_spot", "comp_current_spot", "SPOT"),
        ("multi_stream", "multi_stream", "MULTI_STREAM"),
    ]

    all_failures = []
    if p_req == "ALL":
        requested = None
    else:
        requested = {p.strip().upper() for p in p_req.split(",") if p.strip()}
        if not requested:
            print("Error: --pattern requires at least one value.", file=sys.stderr)
            sys.exit(1)

    needs_standard_bench = requested is None or any(
        p != "MULTI_STREAM" for p in requested)

    if needs_standard_bench:
        check_bin = os.path.join(BUILD_DIR, "comp_current_pair" + EXE_SUFFIX)
        if not os.path.exists(check_bin):
            print(f"Error: Binaries not found at {BUILD_DIR}.")
            print("Please build the project first or pass --build-dir.")
            return

    cache = {}
    if not current_only:
        if os.path.exists(CACHE_FILE):
            try:
                with open(CACHE_FILE, 'r') as f:
                    cache = json.load(f)
            except:
                pass
        else:
            os.makedirs(os.path.dirname(CACHE_FILE), exist_ok=True)

    missing_current = []
    missing_baseline = []
    for baseline_bin, current_bin, p_name in comparisons:
        if requested is not None and p_name not in requested:
            continue
        if p_name == "MULTI_STREAM":
            continue
        current_path = os.path.join(BUILD_DIR, current_bin + EXE_SUFFIX)
        if not os.path.exists(current_path):
            missing_current.append(p_name)
        if not current_only:
            baseline_path = os.path.join(BUILD_DIR, baseline_bin + EXE_SUFFIX)
            if not os.path.exists(baseline_path):
                missing_baseline.append(p_name)

    if missing_current:
        print(
            "Error: current benchmark binaries are missing for patterns: "
            + ", ".join(missing_current),
            file=sys.stderr,
        )
        print("Re-run without -ReuseBuild to configure/build benchmark targets.", file=sys.stderr)
        sys.exit(1)

    if missing_baseline:
        print(
            "Error: baseline benchmark binaries are missing for patterns: "
            + ", ".join(missing_baseline),
            file=sys.stderr,
        )
        print("Re-run with -WithBaseline and without -ReuseBuild to build baseline targets.", file=sys.stderr)
        sys.exit(1)

    for baseline_bin, current_bin, p_name in comparisons:
        if requested is not None and p_name not in requested:
            continue

        print(f"\n## PATTERN: {p_name}")

        pattern_transports = select_transports(p_name)
        if not pattern_transports:
            print(f"  Skipping {p_name}: no matching transports.")
            continue

        b_stats = {}  # Initialize for type checker
        if current_only:
            c_stats, failures = collect_data(current_bin, "current", p_name, num_runs, pattern_transports)
            all_failures.extend(failures)
        else:
            if refresh or p_name not in cache:
                b_stats, failures = collect_data(baseline_bin, "baseline", p_name, num_runs, pattern_transports)
                all_failures.extend(failures)
                cache[p_name] = b_stats
                with open(CACHE_FILE, 'w') as f:
                    json.dump(cache, f, indent=2)
            else:
                print(f"  [baseline] Using cached baseline.")
                b_stats = cache[p_name]

            c_stats, failures = collect_data(current_bin, "current", p_name, num_runs, pattern_transports)
            all_failures.extend(failures)

        # Print Table
        size_w = 6
        metric_w = 10
        val_w = 16
        diff_w = 9
        for tr in pattern_transports:
            print(f"\n### Transport: {tr}")
            if current_only:
                print(
                    f"| {'Size':<{size_w}} | {'Metric':<{metric_w}} | {'current':>{val_w}} |"
                )
                print(
                    f"|{'-' * (size_w + 2)}|{'-' * (metric_w + 2)}|{'-' * (val_w + 2)}|"
                )
            else:
                print(
                    f"| {'Size':<{size_w}} | {'Metric':<{metric_w}} | {'baseline':>{val_w}} | {'current':>{val_w}} | {'Diff (%)':>{diff_w}} |"
                )
                print(
                    f"|{'-' * (size_w + 2)}|{'-' * (metric_w + 2)}|{'-' * (val_w + 2)}|{'-' * (val_w + 2)}|{'-' * (diff_w + 2)}|"
                )
            sizes = (MULTI_STREAM_MSG_SIZES
                     if p_name == "MULTI_STREAM" else MSG_SIZES)
            for sz in sizes:
                ct = c_stats.get(f"{tr}|{sz}|throughput", 0)
                cl = c_stats.get(f"{tr}|{sz}|latency", 0)
                if current_only:
                    ct_s = format_throughput(sz, ct)
                    cl_s = f"{cl:8.2f} us"
                    print(
                        f"| {f'{sz}B':<{size_w}} | {'Throughput':<{metric_w}} | {ct_s:>{val_w}} |"
                    )
                    print(
                        f"| {f'{sz}B':<{size_w}} | {'Latency':<{metric_w}} | {cl_s:>{val_w}} |"
                    )
                else:
                    bt = b_stats.get(f"{tr}|{sz}|throughput", 0)
                    td = ((ct - bt) / bt * 100) if bt > 0 else 0
                    bl = b_stats.get(f"{tr}|{sz}|latency", 0)
                    ld = ((bl - cl) / bl * 100) if bl > 0 else 0
                    bt_s = format_throughput(sz, bt)
                    ct_s = format_throughput(sz, ct)
                    bl_s = f"{bl:8.2f} us"
                    cl_s = f"{cl:8.2f} us"
                    print(
                        f"| {f'{sz}B':<{size_w}} | {'Throughput':<{metric_w}} | {bt_s:>{val_w}} | {ct_s:>{val_w}} | {td:>+7.2f}% |"
                    )
                    print(
                        f"| {f'{sz}B':<{size_w}} | {'Latency':<{metric_w}} | {bl_s:>{val_w}} | {cl_s:>{val_w}} | {ld:>+7.2f}% |"
                    )

    if all_failures:
        print("\n## Failures")
        for pattern, lib_name, tr, sz, reason in all_failures:
            print(f"- {pattern} {lib_name} {tr} {sz}B: {reason}")

if __name__ == "__main__":
    main()
