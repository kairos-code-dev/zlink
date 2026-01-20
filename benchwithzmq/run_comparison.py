#!/usr/bin/env python3
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
        os.path.join(ROOT_DIR, "build", "bench", "bin"),
        os.path.join(ROOT_DIR, "build", "linux-x64", "bin"),
        os.path.join(ROOT_DIR, "build", "benchwithzmq"),
        os.path.join(ROOT_DIR, "build", "linux-x64", "benchwithzmq"),
    ]
    build_dir = next((p for p in possible_paths if os.path.exists(p)), possible_paths[0])
    libzmq_lib_dir = os.path.abspath(
        os.path.join(ROOT_DIR, "benchwithzmq", "libzmq", "libzmq_dist", "lib")
    )
    env_libzmq_dir = os.environ.get("BENCH_LIBZMQ_LIB_DIR")
    if env_libzmq_dir:
        libzmq_lib_dir = os.path.abspath(env_libzmq_dir)
    zlink_lib_dir = os.path.abspath(os.path.join(ROOT_DIR, "build", "bench", "lib"))
    return build_dir, libzmq_lib_dir, zlink_lib_dir

def normalize_build_dir(path):
    if not path:
        return path
    abs_path = os.path.abspath(path)
    if os.path.isdir(abs_path):
        bin_dir = os.path.join(abs_path, "bin")
        release_dir = os.path.join(bin_dir, "Release")
        debug_dir = os.path.join(bin_dir, "Debug")
        if os.path.exists(os.path.join(abs_path, "comp_zlink_pair" + EXE_SUFFIX)):
            return abs_path
        if os.path.exists(os.path.join(bin_dir, "comp_zlink_pair" + EXE_SUFFIX)):
            return bin_dir
        if os.path.exists(os.path.join(release_dir, "comp_zlink_pair" + EXE_SUFFIX)):
            return release_dir
        if os.path.exists(os.path.join(debug_dir, "comp_zlink_pair" + EXE_SUFFIX)):
            return debug_dir
    return abs_path

def derive_zlink_lib_dir(build_dir):
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
    LIBZMQ_LIB_DIR = os.path.abspath(os.path.join("benchwithzmq", "libzmq", "libzmq_dist", "bin"))
    env_libzmq_dir = os.environ.get("BENCH_LIBZMQ_BIN_DIR")
    if env_libzmq_dir:
        LIBZMQ_LIB_DIR = os.path.abspath(env_libzmq_dir)
    ZLINK_LIB_DIR = os.path.abspath(os.path.join("build", "windows-x64", "bin", "Release"))
else:
    BUILD_DIR, LIBZMQ_LIB_DIR, ZLINK_LIB_DIR = resolve_linux_paths()

DEFAULT_NUM_RUNS = 3  # Reduced from 10 for faster testing
CACHE_FILE = os.path.join(ROOT_DIR, "benchwithzmq", "libzmq_cache.json")

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
if _env_transports:
    TRANSPORTS = _env_transports
else:
    TRANSPORTS = ["tcp", "inproc"]
    if not IS_WINDOWS:
        TRANSPORTS.append("ipc")  # IPC is more stable for benchmarks on Linux

_env_sizes = parse_env_list("BENCH_MSG_SIZES", int)
if _env_sizes:
    MSG_SIZES = _env_sizes
else:
    MSG_SIZES = [64, 256, 1024, 65536, 131072, 262144]

base_env = os.environ.copy()

def get_env_for_lib(lib_name):
    env = base_env.copy()
    if IS_WINDOWS:
        env["PATH"] = f"{LIBZMQ_LIB_DIR};{env.get('PATH', '')}"
    else:
        if lib_name == "zlink":
            env["LD_LIBRARY_PATH"] = f"{ZLINK_LIB_DIR}:{env.get('LD_LIBRARY_PATH', '')}"
        else:
            env["LD_LIBRARY_PATH"] = f"{LIBZMQ_LIB_DIR}:{env.get('LD_LIBRARY_PATH', '')}"
    return env

def run_single_test(binary_name, lib_name, transport, size):
    """Runs a single binary for one specific config."""
    binary_path = os.path.join(BUILD_DIR, binary_name + EXE_SUFFIX)
    env = get_env_for_lib(lib_name)
    try:
        # Args: [lib_name] [transport] [size]
        # Use taskset on Linux to pin to CPU 1 for reduced variance unless disabled.
        if IS_WINDOWS:
            cmd = [binary_path, lib_name, transport, str(size)]
        elif os.environ.get("BENCH_NO_TASKSET"):
            cmd = [binary_path, lib_name, transport, str(size)]
        else:
            cmd = ["taskset", "-c", "1", binary_path, lib_name, transport, str(size)]
        result = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=60)
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

def collect_data(binary_name, lib_name, pattern_name, num_runs):
    print(f"  > Benchmarking {lib_name} for {pattern_name}...")
    final_stats = {} # (tr, size, metric) -> avg_value
    failures = []
    
    for tr in TRANSPORTS:
        for sz in MSG_SIZES:
            print(f"    Testing {tr} | {sz}B: ", end="", flush=True)
            metrics_raw = {} # metric_name -> list of values
            failed_runs = 0
            
            for i in range(num_runs):
                print(f"{i+1} ", end="", flush=True)
                results = run_single_test(binary_name, lib_name, tr, sz)
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
                    if m not in metrics_raw: metrics_raw[m] = []
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
    if size <= 1024:
        return f"{msgs_per_sec/1e6:6.2f} Mmsg/s"
    return f"{(msgs_per_sec * size)/1e6:6.2f} MB/s"


def parse_args():
    usage = (
        "Usage: run_comparison.py [PATTERN] [options]\n\n"
        "Options:\n"
        "  --refresh-libzmq        Refresh libzmq baseline cache\n"
        "  --zlink-only            Run only zlink benchmarks\n"
        "  --runs N                Iterations per configuration (default: 3)\n"
        "  --build-dir PATH        Build directory (default: build/bench)\n"
        "  -h, --help              Show this help\n"
        "\n"
        "Env:\n"
        "  BENCH_NO_TASKSET=1      Disable taskset CPU pinning on Linux\n"
    )
    refresh = False
    p_req = "ALL"
    num_runs = DEFAULT_NUM_RUNS
    build_dir = ""
    zlink_only = False

    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg in ("-h", "--help"):
            print(usage)
            sys.exit(0)
        if arg == "--refresh-libzmq":
            refresh = True
        elif arg == "--zlink-only":
            zlink_only = True
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

    return p_req, refresh, num_runs, build_dir, zlink_only

def main():
    p_req, refresh, num_runs, build_dir, zlink_only = parse_args()
    if build_dir:
        build_dir = normalize_build_dir(build_dir)
        global BUILD_DIR, ZLINK_LIB_DIR
        BUILD_DIR = build_dir
        ZLINK_LIB_DIR = derive_zlink_lib_dir(build_dir)
    
    # Check if any target binary exists
    check_bin = os.path.join(BUILD_DIR, "comp_zlink_pair" + EXE_SUFFIX)
    if not os.path.exists(check_bin):
        print(f"Error: Binaries not found at {BUILD_DIR}.")
        print("Please build the project first or pass --build-dir.")
        return

    cache = {}
    if not zlink_only:
        if os.path.exists(CACHE_FILE):
            try:
                with open(CACHE_FILE, 'r') as f:
                    cache = json.load(f)
            except:
                pass
        else:
            os.makedirs(os.path.dirname(CACHE_FILE), exist_ok=True)

    comparisons = [
        ("comp_std_zmq_pair", "comp_zlink_pair", "PAIR"),
        ("comp_std_zmq_pubsub", "comp_zlink_pubsub", "PUBSUB"),
        ("comp_std_zmq_dealer_dealer", "comp_zlink_dealer_dealer", "DEALER_DEALER"),
        ("comp_std_zmq_dealer_router", "comp_zlink_dealer_router", "DEALER_ROUTER"),
        ("comp_std_zmq_router_router", "comp_zlink_router_router", "ROUTER_ROUTER"),
        ("comp_std_zmq_router_router_poll", "comp_zlink_router_router_poll",
         "ROUTER_ROUTER_POLL"),
    ]

    all_failures = []
    for std_bin, zlk_bin, p_name in comparisons:
        if p_req != "ALL" and p_name != p_req: continue
        
        print(f"\n## PATTERN: {p_name}")
        
        if zlink_only:
            z_stats, failures = collect_data(zlk_bin, "zlink", p_name, num_runs)
            all_failures.extend(failures)
        else:
            if refresh or p_name not in cache:
                s_stats, failures = collect_data(std_bin, "libzmq", p_name, num_runs)
                all_failures.extend(failures)
                cache[p_name] = s_stats
                with open(CACHE_FILE, 'w') as f: json.dump(cache, f, indent=2)
            else:
                print(f"  [libzmq] Using cached baseline.")
                s_stats = cache[p_name]

            z_stats, failures = collect_data(zlk_bin, "zlink", p_name, num_runs)
            all_failures.extend(failures)

        # Print Table
        size_w = 6
        metric_w = 10
        val_w = 16
        diff_w = 9
        for tr in TRANSPORTS:
            print(f"\n### Transport: {tr}")
            if zlink_only:
                print(
                    f"| {'Size':<{size_w}} | {'Metric':<{metric_w}} | {'zlink':>{val_w}} |"
                )
                print(
                    f"|{'-' * (size_w + 2)}|{'-' * (metric_w + 2)}|{'-' * (val_w + 2)}|"
                )
            else:
                print(
                    f"| {'Size':<{size_w}} | {'Metric':<{metric_w}} | {'Standard libzmq':>{val_w}} | {'zlink':>{val_w}} | {'Diff (%)':>{diff_w}} |"
                )
                print(
                    f"|{'-' * (size_w + 2)}|{'-' * (metric_w + 2)}|{'-' * (val_w + 2)}|{'-' * (val_w + 2)}|{'-' * (diff_w + 2)}|"
                )
            for sz in MSG_SIZES:
                zt = z_stats.get(f"{tr}|{sz}|throughput", 0)
                zl = z_stats.get(f"{tr}|{sz}|latency", 0)
                if zlink_only:
                    zt_s = format_throughput(sz, zt)
                    zl_s = f"{zl:8.2f} us"
                    print(
                        f"| {f'{sz}B':<{size_w}} | {'Throughput':<{metric_w}} | {zt_s:>{val_w}} |"
                    )
                    print(
                        f"| {f'{sz}B':<{size_w}} | {'Latency':<{metric_w}} | {zl_s:>{val_w}} |"
                    )
                else:
                    st = s_stats.get(f"{tr}|{sz}|throughput", 0)
                    td = ((zt - st) / st * 100) if st > 0 else 0
                    sl = s_stats.get(f"{tr}|{sz}|latency", 0)
                    ld = ((sl - zl) / sl * 100) if sl > 0 else 0
                    st_s = format_throughput(sz, st)
                    zt_s = format_throughput(sz, zt)
                    sl_s = f"{sl:8.2f} us"
                    zl_s = f"{zl:8.2f} us"
                    print(
                        f"| {f'{sz}B':<{size_w}} | {'Throughput':<{metric_w}} | {st_s:>{val_w}} | {zt_s:>{val_w}} | {td:>+7.2f}% |"
                    )
                    print(
                        f"| {f'{sz}B':<{size_w}} | {'Latency':<{metric_w}} | {sl_s:>{val_w}} | {zl_s:>{val_w}} | {ld:>+7.2f}% |"
                    )

    if all_failures:
        print("\n## Failures")
        for pattern, lib_name, tr, sz, reason in all_failures:
            print(f"- {pattern} {lib_name} {tr} {sz}B: {reason}")

if __name__ == "__main__":
    main()
