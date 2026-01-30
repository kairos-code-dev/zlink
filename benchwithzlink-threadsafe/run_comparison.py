#!/usr/bin/env python3
"""
benchwithzlink-threadsafe - zlink thread-safe vs plain socket benchmarks

Compares:
- plain: zlink_socket
- threadsafe: zlink_socket_threadsafe
"""
import subprocess
import os
import sys
import statistics
import json

# Environment helpers
IS_WINDOWS = os.name == 'nt'
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""
SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))


def resolve_linux_paths():
    """Return build/library paths for Linux/WSL environments."""
    possible_paths = [
        os.path.join(ROOT_DIR, "build", "bin"),
        os.path.join(ROOT_DIR, "build", "bench", "bin"),
        os.path.join(ROOT_DIR, "build", "linux-x64", "bin"),
        os.path.join(ROOT_DIR, "build", "benchwithzlink-threadsafe"),
        os.path.join(ROOT_DIR, "build", "linux-x64", "benchwithzlink-threadsafe"),
    ]
    build_dir = next((p for p in possible_paths if os.path.exists(p)), possible_paths[0])
    lib_dir = os.path.abspath(os.path.join(ROOT_DIR, "build", "lib"))
    return build_dir, lib_dir


def normalize_build_dir(path):
    if not path:
        return path
    abs_path = os.path.abspath(path)
    if os.path.isdir(abs_path):
        bin_dir = os.path.join(abs_path, "bin")
        release_dir = os.path.join(bin_dir, "Release")
        debug_dir = os.path.join(bin_dir, "Debug")
        # Check bin dir first on Linux
        if os.path.exists(os.path.join(bin_dir, "comp_threadsafe_pair" + EXE_SUFFIX)):
            return bin_dir
        if os.path.exists(os.path.join(abs_path, "comp_threadsafe_pair" + EXE_SUFFIX)):
            return abs_path
        if os.path.exists(os.path.join(release_dir, "comp_threadsafe_pair" + EXE_SUFFIX)):
            return release_dir
        if os.path.exists(os.path.join(debug_dir, "comp_threadsafe_pair" + EXE_SUFFIX)):
            return debug_dir
    return abs_path


def derive_lib_dir(build_dir):
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
    BUILD_DIR = os.path.join("build", "windows-x64", "bin", "Release")
    LIB_DIR = os.path.abspath(os.path.join("build", "windows-x64", "bin", "Release"))
else:
    BUILD_DIR, LIB_DIR = resolve_linux_paths()

DEFAULT_NUM_RUNS = 3
CMP_TIMEOUT = int(os.environ.get("BENCH_RUN_TIMEOUT", "180"))
CACHE_FILE = os.path.join(ROOT_DIR, "benchwithzlink-threadsafe", "plain_cache.json")


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


# Settings for loop
_env_transports = parse_env_list("BENCH_TRANSPORTS", str)

# Default transports for ZMP sockets (non-STREAM)
TRANSPORTS = ["tcp", "ipc", "inproc"]

# STREAM socket uses different transports (raw TCP/TLS/WS/WSS)
STREAM_TRANSPORTS = ["tcp"]


def select_transports(pattern_name):
    base = STREAM_TRANSPORTS if pattern_name == "STREAM" else TRANSPORTS
    if not _env_transports:
        return list(base)
    return [t for t in base if t in _env_transports]


_env_sizes = parse_env_list("BENCH_MSG_SIZES", int)
if _env_sizes:
    MSG_SIZES = _env_sizes
else:
    MSG_SIZES = [64, 256, 1024, 65536, 131072, 262144]

base_env = os.environ.copy()


def get_env_for_variant(_name):
    env = base_env.copy()
    if IS_WINDOWS:
        env["PATH"] = f"{LIB_DIR};{env.get('PATH', '')}"
    else:
        env["LD_LIBRARY_PATH"] = f"{LIB_DIR}:{env.get('LD_LIBRARY_PATH', '')}"
    return env


def run_single_test(binary_name, variant_name, transport, size, pattern_name=""):
    """Runs a single binary for one specific config."""
    binary_path = os.path.join(BUILD_DIR, binary_name + EXE_SUFFIX)
    env = get_env_for_variant(variant_name)

    # For STREAM pattern with TLS/WS/WSS, limit msg count to avoid buffer/deadlock issues
    if pattern_name == "STREAM" and transport in ("tls", "ws", "wss"):
        env["BENCH_MSG_COUNT"] = "5000"
        env["BENCH_WARMUP_COUNT"] = "100"

    try:
        # Args: [variant_name] [transport] [size]
        # Use taskset on Linux to pin to CPU 1 for reduced variance unless disabled.
        if IS_WINDOWS:
            cmd = [binary_path, variant_name, transport, str(size)]
        elif os.environ.get("BENCH_NO_TASKSET"):
            cmd = [binary_path, variant_name, transport, str(size)]
        else:
            cmd = ["taskset", "-c", "1", binary_path, variant_name, transport, str(size)]
        result = subprocess.run(cmd,
                                env=env,
                                capture_output=True,
                                text=True,
                                timeout=CMP_TIMEOUT)
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


def collect_data(binary_name, variant_name, pattern_name, num_runs, transports=None):
    print(f"  > Benchmarking {variant_name} for {pattern_name}...")
    final_stats = {}  # (tr, size, metric) -> avg_value
    failures = []

    if transports is None:
        transports = TRANSPORTS

    for tr in transports:
        sizes = MSG_SIZES

        for sz in sizes:
            print(f"    Testing {tr} | {sz}B: ", end="", flush=True)
            metrics_raw = {}  # metric_name -> list of values
            failed_runs = 0

            for i in range(num_runs):
                print(f"{i+1} ", end="", flush=True)
                results = run_single_test(binary_name, variant_name, tr, sz, pattern_name)
                if results is None:
                    failures.append((pattern_name, variant_name, tr, sz, "timeout"))
                    print("FAILED (timeout)")
                    sys.exit(1)
                if not results:
                    failures.append((pattern_name, variant_name, tr, sz, "no_data"))
                    print("FAILED (no_data)")
                    sys.exit(1)
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
        "Compare plain vs threadsafe zlink sockets.\n\n"
        "Options:\n"
        "  --refresh-plain         Refresh plain cache\n"
        "  --refresh-baseline      Alias for --refresh-plain\n"
        "  --threadsafe-only       Run only threadsafe benchmarks\n"
        "  --current-only          Alias for --threadsafe-only\n"
        "  --zlink-only            Alias for --threadsafe-only\n"
        "  --runs N                Iterations per configuration (default: 3)\n"
        "  --build-dir PATH        Build directory (default: build/)\n"
        "  -h, --help              Show this help\n"
        "\n"
        "Env:\n"
        "  BENCH_NO_TASKSET=1      Disable taskset CPU pinning on Linux\n"
        "  BENCH_TRANSPORTS=list   Comma-separated transports (e.g., tcp)\n"
        "  BENCH_RUN_TIMEOUT=SEC   Per-run timeout in seconds (default: 180)\n"
    )
    refresh_plain = False
    p_req = "ALL"
    num_runs = DEFAULT_NUM_RUNS
    build_dir = ""
    threadsafe_only = False

    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg in ("-h", "--help"):
            print(usage)
            sys.exit(0)
        if arg in ("--refresh-plain", "--refresh-baseline"):
            refresh_plain = True
        elif arg in ("--threadsafe-only", "--current-only", "--zlink-only"):
            threadsafe_only = True
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
            p_req = arg
        i += 1

    if num_runs < 1:
        print("Error: --runs must be >= 1.", file=sys.stderr)
        sys.exit(1)

    return p_req, refresh_plain, num_runs, build_dir, threadsafe_only


def main():
    p_req, refresh_plain, num_runs, build_dir, threadsafe_only = parse_args()
    global BUILD_DIR, LIB_DIR
    if build_dir:
        BUILD_DIR = normalize_build_dir(build_dir)
    else:
        BUILD_DIR = normalize_build_dir(BUILD_DIR)

    LIB_DIR = derive_lib_dir(BUILD_DIR)

    # Check if any target binary exists
    check_bin = os.path.join(BUILD_DIR, "comp_threadsafe_pair" + EXE_SUFFIX)
    if not os.path.exists(check_bin):
        print(f"Error: Binaries not found at {BUILD_DIR}.")
        print("Please build the project first or pass --build-dir.")
        return

    cache = {}
    if not threadsafe_only:
        if os.path.exists(CACHE_FILE):
            try:
                with open(CACHE_FILE, 'r') as f:
                    cache = json.load(f)
            except:
                pass
        else:
            os.makedirs(os.path.dirname(CACHE_FILE), exist_ok=True)

    comparisons = [
        ("comp_plain_pair", "comp_threadsafe_pair", "PAIR"),
        ("comp_plain_pubsub", "comp_threadsafe_pubsub", "PUBSUB"),
        ("comp_plain_dealer_dealer", "comp_threadsafe_dealer_dealer", "DEALER_DEALER"),
        ("comp_plain_dealer_router", "comp_threadsafe_dealer_router", "DEALER_ROUTER"),
        ("comp_plain_router_router", "comp_threadsafe_router_router", "ROUTER_ROUTER"),
        ("comp_plain_router_router_poll", "comp_threadsafe_router_router_poll", "ROUTER_ROUTER_POLL"),
        ("comp_plain_stream", "comp_threadsafe_stream", "STREAM"),
    ]

    all_failures = []
    for plain_bin, threadsafe_bin, p_name in comparisons:
        if p_req != "ALL" and p_name != p_req:
            continue

        print(f"\n## PATTERN: {p_name}")

        pattern_transports = select_transports(p_name)
        if not pattern_transports:
            print(f"  Skipping {p_name}: no matching transports.")
            continue

        p_stats = {}
        if threadsafe_only:
            t_stats, failures = collect_data(threadsafe_bin, "threadsafe", p_name, num_runs, pattern_transports)
            all_failures.extend(failures)
        else:
            if refresh_plain or p_name not in cache:
                p_stats, failures = collect_data(plain_bin, "plain", p_name, num_runs, pattern_transports)
                all_failures.extend(failures)
                cache[p_name] = p_stats
                with open(CACHE_FILE, 'w') as f:
                    json.dump(cache, f, indent=2)
            else:
                print("  [plain] Using cached plain results.")
                p_stats = cache[p_name]

            t_stats, failures = collect_data(threadsafe_bin, "threadsafe", p_name, num_runs, pattern_transports)
            all_failures.extend(failures)

        # Print Table
        size_w = 6
        metric_w = 10
        val_w = 16
        diff_w = 9
        for tr in pattern_transports:
            print(f"\n### Transport: {tr}")
            if threadsafe_only:
                print(
                    f"| {'Size':<{size_w}} | {'Metric':<{metric_w}} | {'threadsafe':>{val_w}} |"
                )
                print(
                    f"|{'-' * (size_w + 2)}|{'-' * (metric_w + 2)}|{'-' * (val_w + 2)}|"
                )
            else:
                print(
                    f"| {'Size':<{size_w}} | {'Metric':<{metric_w}} | {'plain':>{val_w}} | {'threadsafe':>{val_w}} | {'Diff (%)':>{diff_w}} |"
                )
                print(
                    f"|{'-' * (size_w + 2)}|{'-' * (metric_w + 2)}|{'-' * (val_w + 2)}|{'-' * (val_w + 2)}|{'-' * (diff_w + 2)}|"
                )
            sizes = MSG_SIZES
            for sz in sizes:
                tt = t_stats.get(f"{tr}|{sz}|throughput", 0)
                tl = t_stats.get(f"{tr}|{sz}|latency", 0)
                if threadsafe_only:
                    tt_s = format_throughput(sz, tt)
                    tl_s = f"{tl:8.2f} us"
                    print(
                        f"| {f'{sz}B':<{size_w}} | {'Throughput':<{metric_w}} | {tt_s:>{val_w}} |"
                    )
                    print(
                        f"| {f'{sz}B':<{size_w}} | {'Latency':<{metric_w}} | {tl_s:>{val_w}} |"
                    )
                else:
                    pt = p_stats.get(f"{tr}|{sz}|throughput", 0)
                    td = ((tt - pt) / pt * 100) if pt > 0 else 0
                    pl = p_stats.get(f"{tr}|{sz}|latency", 0)
                    ld = ((pl - tl) / pl * 100) if pl > 0 else 0
                    pt_s = format_throughput(sz, pt)
                    tt_s = format_throughput(sz, tt)
                    pl_s = f"{pl:8.2f} us"
                    tl_s = f"{tl:8.2f} us"
                    print(
                        f"| {f'{sz}B':<{size_w}} | {'Throughput':<{metric_w}} | {pt_s:>{val_w}} | {tt_s:>{val_w}} | {td:>+7.2f}% |"
                    )
                    print(
                        f"| {f'{sz}B':<{size_w}} | {'Latency':<{metric_w}} | {pl_s:>{val_w}} | {tl_s:>{val_w}} | {ld:>+7.2f}% |"
                    )

    if all_failures:
        print("\n## Failures")
        for pattern, lib_name, tr, sz, reason in all_failures:
            print(f"- {pattern} {lib_name} {tr} {sz}B: {reason}")


if __name__ == "__main__":
    main()
