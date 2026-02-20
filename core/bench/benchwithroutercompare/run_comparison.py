#!/usr/bin/env python3
"""Router compare benchmark: zlink vs libzmq vs gRPC.

Runs 1:1 echo and N:1 echo tests with all three libraries,
then prints a combined comparison table.
"""
import json
import os
import platform
import socket
import subprocess
import sys
import time
from datetime import datetime, timezone

IS_WINDOWS = os.name == "nt"
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""
SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", ".."))
BUILD_CONFIG_DIRS = ("Release", "Debug", "RelWithDebInfo", "MinSizeRel")

COMMON_PY_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "common"))
if COMMON_PY_DIR not in sys.path:
    sys.path.insert(0, COMMON_PY_DIR)

from multi_e2e_metrics import (
    format_latency,
    format_throughput,
    latency_diff_str,
    load_cached_stats,
    parse_result,
    parse_results,
    throughput_diff_str,
    update_stats_with_median,
)

DEFAULT_MSG_SIZES = [64, 256, 1024, 65536, 131072, 262144]
LIBRARIES = ["libzmq", "zlink", "grpc"]

BINARIES = {
    "libzmq": ("bench_rc_zmq_server", "bench_rc_zmq_client"),
    "zlink": ("bench_rc_zlink_server", "bench_rc_zlink_client"),
    "grpc": ("bench_rc_grpc_server", "bench_rc_grpc_client"),
}


def parse_env_list(name, cast_fn):
    raw = os.environ.get(name)
    if not raw:
        return None
    out = []
    for p in raw.split(","):
        p = p.strip()
        if not p:
            continue
        try:
            out.append(cast_fn(p))
        except ValueError:
            continue
    return out or None


def normalize_build_dir(path):
    if not path:
        return path
    abs_path = os.path.abspath(path)
    if not os.path.isdir(abs_path):
        base = os.path.basename(abs_path)
        if base in BUILD_CONFIG_DIRS:
            return abs_path
        if base == "bin":
            return os.path.join(abs_path, "Release") if IS_WINDOWS else abs_path
        return runtime_bin_dir_from_cmake_dir(abs_path)

    base = os.path.basename(abs_path)
    if base in BUILD_CONFIG_DIRS:
        return abs_path
    if base == "bin":
        return os.path.join(abs_path, "Release") if IS_WINDOWS else abs_path

    bin_dir = os.path.join(abs_path, "bin")
    if os.path.isdir(bin_dir):
        return os.path.join(bin_dir, "Release") if IS_WINDOWS else bin_dir

    if os.path.isfile(os.path.join(abs_path, "CMakeCache.txt")):
        return runtime_bin_dir_from_cmake_dir(abs_path)

    return abs_path


def runtime_bin_dir_from_cmake_dir(cmake_build_dir):
    bin_dir = os.path.join(cmake_build_dir, "bin")
    return os.path.join(bin_dir, "Release") if IS_WINDOWS else bin_dir


def derive_cmake_build_dir(runtime_build_dir):
    if not runtime_build_dir:
        return ""
    abs_path = os.path.abspath(runtime_build_dir)
    if os.path.isfile(os.path.join(abs_path, "CMakeCache.txt")):
        return abs_path
    base = os.path.basename(abs_path)
    if base in BUILD_CONFIG_DIRS:
        parent = os.path.dirname(abs_path)
        if os.path.basename(parent) == "bin":
            return os.path.dirname(parent)
        return os.path.dirname(abs_path)
    if base == "bin":
        return os.path.dirname(abs_path)
    parent = os.path.dirname(abs_path)
    if os.path.basename(parent) == "bin":
        return os.path.dirname(parent)
    return abs_path


def derive_zlink_lib_dir(build_dir):
    build_root = build_dir
    base = os.path.basename(build_root)
    if base in BUILD_CONFIG_DIRS:
        bin_root = os.path.dirname(build_root)
        if os.path.basename(bin_root) == "bin":
            build_root = os.path.dirname(bin_root)
    elif base == "bin":
        build_root = os.path.dirname(build_root)
    return os.path.abspath(os.path.join(build_root, "lib"))


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
        os.path.join(ROOT_DIR, "core", "build", f"{platform_tag}-{arch_tag}", "bin", "Release"),
        os.path.join(ROOT_DIR, "core", "build", "bin"),
    ]
    return next((p for p in possible_paths if os.path.exists(p)), possible_paths[0])


def find_free_port(base_port):
    for port in range(base_port, base_port + 2000):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                s.bind(("127.0.0.1", port))
                return port
            except OSError:
                continue
    raise RuntimeError("no free port")


def get_env_for_lib(base_env, lib_name, build_dir):
    env = base_env.copy()
    libzmq_lib_dir = os.path.join(SCRIPT_DIR, "libzmq", "libzmq_dist", "linux-x64", "lib")
    grpc_lib_dir = os.path.join(SCRIPT_DIR, "grpc", "grpc_dist", "linux-x64", "lib")
    zlink_lib_dir = derive_zlink_lib_dir(build_dir)

    if lib_name == "zlink":
        env["LD_LIBRARY_PATH"] = f"{zlink_lib_dir}:{env.get('LD_LIBRARY_PATH', '')}"
    elif lib_name == "libzmq":
        env["LD_LIBRARY_PATH"] = f"{libzmq_lib_dir}:{env.get('LD_LIBRARY_PATH', '')}"
    elif lib_name == "grpc":
        env["LD_LIBRARY_PATH"] = f"{grpc_lib_dir}:{env.get('LD_LIBRARY_PATH', '')}"
    return env


def run_single_test(build_dir, lib_name, clients, msg_sizes, duration,
                    settle_ms, drain_ms, inflight, base_env):
    """Run server+client for a single library and return parsed results."""
    server_bin, client_bin = BINARIES[lib_name]

    port = find_free_port(29200)
    env = get_env_for_lib(base_env, lib_name, build_dir)
    env["BENCH_PORT"] = str(port)
    env["BENCH_CLIENTS"] = str(clients)
    env["BENCH_INFLIGHT"] = str(inflight)
    env["BENCH_DURATION_SECONDS"] = str(duration)
    env["BENCH_SETTLE_MS"] = str(settle_ms)
    env["BENCH_DRAIN_MS"] = str(drain_ms)
    env["BENCH_MSG_SIZES"] = ",".join(str(s) for s in msg_sizes)

    server_cmd = [os.path.join(build_dir, server_bin + EXE_SUFFIX)]
    client_cmd = [os.path.join(build_dir, client_bin + EXE_SUFFIX), lib_name]

    server_proc = None
    try:
        server_proc = subprocess.Popen(
            server_cmd,
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        time.sleep(max(1.0, settle_ms / 250.0))

        timeout_sec = max(60, duration * len(msg_sizes) + 40)
        result = subprocess.run(
            client_cmd,
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout_sec,
        )
        if result.returncode != 0:
            err_msg = (result.stderr or "").strip()
            if err_msg:
                err_msg = err_msg.splitlines()[-1].strip()
            return None, f"client_rc_{result.returncode}:{err_msg}" if err_msg else f"client_rc_{result.returncode}"

        parsed_map = {}
        for size in msg_sizes:
            parsed = parse_result(result.stdout, "ROUTER_ECHO", "tcp", size)
            if parsed and "throughput" in parsed and "latency" in parsed:
                parsed_map[size] = parsed
        if not parsed_map:
            return None, "no_data"
        return parsed_map, ""
    except subprocess.TimeoutExpired:
        return None, "timeout"
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


def ensure_binary(build_dir, name):
    return os.path.exists(os.path.join(build_dir, name + EXE_SUFFIX))


def parse_args():
    usage = (
        "Usage: run_comparison.py [options]\n\n"
        "Options:\n"
        "  --runs N              Iterations per config (default: 5)\n"
        "  --clients N           N:1 test client count (default: 100)\n"
        "  --build-dir PATH      Build directory\n"
        "  --zlink-only          Run only zlink, use cache for zmq/grpc\n"
        "  --refresh-cache       Refresh baseline cache\n"
        "  --run-cooldown-ms N   Sleep between runs (default: 3000)\n"
    )

    num_runs = 5
    clients = 100
    zlink_only = False
    refresh_cache = False
    build_dir = ""
    run_cooldown_ms = int(os.environ.get("BENCH_RUN_COOLDOWN_MS", "3000"))

    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg in ("-h", "--help"):
            print(usage)
            sys.exit(0)
        elif arg == "--runs":
            i += 1
            num_runs = int(sys.argv[i])
        elif arg == "--clients":
            i += 1
            clients = int(sys.argv[i])
        elif arg == "--build-dir":
            i += 1
            build_dir = sys.argv[i]
        elif arg == "--zlink-only":
            zlink_only = True
        elif arg == "--refresh-cache":
            refresh_cache = True
        elif arg == "--run-cooldown-ms":
            i += 1
            run_cooldown_ms = max(0, int(sys.argv[i]))
        i += 1

    if num_runs < 1:
        print("Error: --runs must be >= 1", file=sys.stderr)
        sys.exit(1)

    return num_runs, clients, zlink_only, refresh_cache, build_dir, run_cooldown_ms


def load_cache(path):
    if not os.path.exists(path):
        return {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def save_cache(path, data):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, sort_keys=True, ensure_ascii=True)


def print_comparison_table(title, msg_sizes, stats_by_lib):
    """Print a comparison table for one scenario."""
    print(f"\n### {title}")
    print("| Size | Metric | libzmq | zlink | gRPC | zlk vs zmq | zlk vs grpc |")
    print("|------|--------|--------|-------|------|------------|-------------|")

    for size in msg_sizes:
        key_t = f"{size}|throughput"
        key_l = f"{size}|latency"

        zmq_t = stats_by_lib.get("libzmq", {}).get(key_t)
        zlk_t = stats_by_lib.get("zlink", {}).get(key_t)
        grpc_t = stats_by_lib.get("grpc", {}).get(key_t)

        zmq_l = stats_by_lib.get("libzmq", {}).get(key_l)
        zlk_l = stats_by_lib.get("zlink", {}).get(key_l)
        grpc_l = stats_by_lib.get("grpc", {}).get(key_l)

        td_zmq = throughput_diff_str(zmq_t, zlk_t)
        td_grpc = throughput_diff_str(grpc_t, zlk_t)
        ld_zmq = latency_diff_str(zmq_l, zlk_l)
        ld_grpc = latency_diff_str(grpc_l, zlk_l)

        zmq_t_s = format_throughput(zmq_t) if zmq_t is not None else "N/A"
        zlk_t_s = format_throughput(zlk_t) if zlk_t is not None else "N/A"
        grpc_t_s = format_throughput(grpc_t) if grpc_t is not None else "N/A"
        zmq_l_s = format_latency(zmq_l) if zmq_l is not None else "N/A"
        zlk_l_s = format_latency(zlk_l) if zlk_l is not None else "N/A"
        grpc_l_s = format_latency(grpc_l) if grpc_l is not None else "N/A"

        print(f"| {size}B | Throughput | {zmq_t_s} | {zlk_t_s} | {grpc_t_s} | {td_zmq} | {td_grpc} |")
        print(f"| {size}B | Latency | {zmq_l_s} | {zlk_l_s} | {grpc_l_s} | {ld_zmq} | {ld_grpc} |")


def run_scenario(build_dir, libs, clients, msg_sizes, runs, duration,
                 settle_ms, drain_ms, inflight, run_cooldown_ms, base_env,
                 cache, zlink_only, refresh_cache):
    """Run all libraries for a given client count. Returns stats_by_lib dict."""
    stats_by_lib = {}
    all_failures = []
    cache_updated = False

    for lib in libs:
        cache_key = f"{lib}|{clients}"
        cached = cache.get(cache_key)

        need_run = True
        if lib != "zlink" and zlink_only and not refresh_cache and cached:
            need_run = False

        values_by_size = {size: [] for size in msg_sizes}

        if need_run:
            for i in range(runs):
                print(f"    {lib} clients={clients} run {i+1}/{runs}", flush=True)
                parsed_map, err = run_single_test(
                    build_dir, lib, clients, msg_sizes, duration,
                    settle_ms, drain_ms, inflight, base_env,
                )
                if not parsed_map:
                    all_failures.append((lib, clients, err))
                    continue
                for size in msg_sizes:
                    parsed = parsed_map.get(size)
                    if parsed:
                        values_by_size[size].append(parsed)
                if run_cooldown_ms > 0 and i + 1 < runs:
                    time.sleep(run_cooldown_ms / 1000.0)

            lib_stats = {}
            for size in msg_sizes:
                key_prefix = f"{size}"
                vals = values_by_size.get(size, [])
                if vals:
                    update_stats_with_median(lib_stats, key_prefix, vals)
            stats_by_lib[lib] = lib_stats

            if lib != "zlink":
                cache[cache_key] = {k: float(v) for k, v in lib_stats.items()}
                cache_updated = True
        else:
            lib_stats = {}
            if cached:
                lib_stats = {k: float(v) for k, v in cached.items()}
            stats_by_lib[lib] = lib_stats

    return stats_by_lib, all_failures, cache_updated


def main():
    num_runs, multi_clients, zlink_only, refresh_cache, build_dir_arg, run_cooldown_ms = parse_args()

    global_build_dir = resolve_linux_paths()
    build_dir = normalize_build_dir(build_dir_arg) if build_dir_arg else normalize_build_dir(global_build_dir)

    platform_tag, arch_tag = platform_arch_tag()
    cache_file = os.path.join(SCRIPT_DIR, f"rc_cache_{platform_tag}-{arch_tag}.json")

    # Check available binaries
    available_libs = []
    for lib in LIBRARIES:
        server_bin, client_bin = BINARIES[lib]
        if ensure_binary(build_dir, server_bin) and ensure_binary(build_dir, client_bin):
            available_libs.append(lib)
        else:
            print(f"Warning: {lib} binaries not found in {build_dir}, skipping")

    if not available_libs:
        print("Error: no benchmark binaries found", file=sys.stderr)
        return 2

    msg_sizes = parse_env_list("BENCH_MSG_SIZES", int) or DEFAULT_MSG_SIZES
    duration = int(os.environ.get("BENCH_DURATION_SECONDS", "5"))
    settle_ms = int(os.environ.get("BENCH_SETTLE_MS", "500"))
    drain_ms = int(os.environ.get("BENCH_DRAIN_MS", "300"))
    inflight = int(os.environ.get("BENCH_INFLIGHT", "1"))

    cache = load_cache(cache_file)
    base_env = os.environ.copy()
    all_failures = []
    cache_updated = False

    print("\n## Router Compare Benchmark Results")

    # Phase 1: 1:1 Echo
    print("\n  > Phase 1: 1:1 Echo")
    stats_1to1, failures_1, updated_1 = run_scenario(
        build_dir, available_libs, 1, msg_sizes, num_runs,
        duration, settle_ms, drain_ms, inflight, run_cooldown_ms,
        base_env, cache, zlink_only, refresh_cache,
    )
    all_failures.extend(failures_1)
    cache_updated = cache_updated or updated_1

    # Phase 2: N:1 Echo
    print(f"\n  > Phase 2: {multi_clients}:1 Echo")
    stats_nto1, failures_n, updated_n = run_scenario(
        build_dir, available_libs, multi_clients, msg_sizes, num_runs,
        duration, settle_ms, drain_ms, inflight, run_cooldown_ms,
        base_env, cache, zlink_only, refresh_cache,
    )
    all_failures.extend(failures_n)
    cache_updated = cache_updated or updated_n

    # Phase 3: Combined output
    print_comparison_table(
        "1:1 Echo (clients=1)", msg_sizes, stats_1to1,
    )
    print_comparison_table(
        f"{multi_clients}:1 Echo (clients={multi_clients})", msg_sizes, stats_nto1,
    )

    if cache_updated:
        cache["meta"] = {
            "build_dir": build_dir,
            "updated_at_utc": datetime.now(timezone.utc).isoformat(),
        }
        save_cache(cache_file, cache)
        print(f"\nSaved cache: {cache_file}")

    if all_failures:
        print("\n## Failures")
        for f in all_failures:
            lib, clients, reason = f
            print(f"  - {lib} clients={clients}: {reason}")

    return 1 if all_failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
