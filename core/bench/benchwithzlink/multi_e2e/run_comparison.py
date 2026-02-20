#!/usr/bin/env python3
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
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", "..", ".."))
BUILD_CONFIG_DIRS = ("Release", "Debug", "RelWithDebInfo", "MinSizeRel")
COMMON_PY_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", "common"))
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

DEFAULT_PATTERNS = [
    "MULTI_DEALER_DEALER",
    "MULTI_DEALER_ROUTER",
    "MULTI_ROUTER_ROUTER",
    "MULTI_PUBSUB",
    "MULTI_STREAM",
    "MULTI_GATEWAY",
    "MULTI_SPOT",
]
RTT_PATTERNS = {
    "MULTI_DEALER_DEALER",
    "MULTI_DEALER_ROUTER",
    "MULTI_ROUTER_ROUTER",
    "MULTI_STREAM",
}


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
        if os.path.basename(os.path.dirname(abs_path)) == "bin":
            return abs_path
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


def resolve_default_build_dir():
    platform_tag, arch_tag = platform_arch_tag()
    candidates = [
        os.path.join(ROOT_DIR, "core", "build", f"{platform_tag}-{arch_tag}", "bin"),
        os.path.join(ROOT_DIR, "core", "build", f"{platform_tag}-{arch_tag}", "bin", "Release"),
        os.path.join(ROOT_DIR, "core", "build", "bin"),
    ]
    return next((p for p in candidates if os.path.exists(p)), candidates[0])


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


def derive_baseline_lib_dir():
    platform_tag, arch_tag = platform_arch_tag()
    if IS_WINDOWS:
        return os.path.abspath(
            os.path.join(
                ROOT_DIR,
                "core",
                "bench",
                "benchwithzlink",
                "baseline",
                "zlink_dist",
                f"{platform_tag}-{arch_tag}",
                "bin",
            )
        )
    return os.path.abspath(
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


def run_cmake_configure(cmake_build_dir):
    source_dir = ROOT_DIR if os.path.isfile(os.path.join(ROOT_DIR, "CMakeLists.txt")) else os.path.join(ROOT_DIR, "core")
    cmd = [
        "cmake",
        "-S",
        source_dir,
        "-B",
        cmake_build_dir,
        "-DBUILD_SHARED=ON",
        "-DBUILD_BENCHMARKS=ON",
        "-DZLINK_BUILD_BENCH_ZMQ=ON",
        "-DZLINK_BUILD_BENCH_ZLINK=ON",
        "-DZLINK_BUILD_BENCH_BEAST=OFF",
    ]
    print(f"  > Configuring benchmark build in {cmake_build_dir}", flush=True)
    print(f"  > Configure command: {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd).returncode


def run_cmake_build(cmake_build_dir, targets):
    cmd = ["cmake", "--build", cmake_build_dir]
    if IS_WINDOWS:
        cmd.extend(["--config", "Release"])
    if targets:
        cmd.append("--target")
        cmd.extend(targets)
    print(f"  > Auto-building missing benchmark binaries in {cmake_build_dir}", flush=True)
    print(f"  > Build command: {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd).returncode


def parse_args():
    pattern = "ALL"
    runs = 5
    refresh_baseline = False
    current_only = False
    build_dir = ""
    run_cooldown_ms = int(os.environ.get("BENCH_MULTI_RUN_COOLDOWN_MS", "3000"))
    pin_cpu = False

    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg in ("-h", "--help"):
            print(
                "Usage: multi_e2e/run_comparison.py [PATTERN] [options]\n"
                "  --refresh-baseline\n"
                "  --current-only | --zlink-only\n"
                "  --runs N\n"
                "  --build-dir PATH\n"
                "  --run-cooldown-ms N\n"
                "  --pin-cpu\n"
            )
            sys.exit(0)
        if arg in ("--refresh-baseline", "--refresh-libzlink"):
            refresh_baseline = True
        elif arg in ("--current-only", "--zlink-only"):
            current_only = True
        elif arg == "--runs":
            i += 1
            runs = int(sys.argv[i])
        elif arg == "--build-dir":
            i += 1
            build_dir = sys.argv[i]
        elif arg == "--run-cooldown-ms":
            i += 1
            run_cooldown_ms = max(0, int(sys.argv[i]))
        elif arg == "--pin-cpu":
            pin_cpu = True
        elif not arg.startswith("--") and pattern == "ALL":
            pattern = arg.upper()
        i += 1

    if runs < 1:
        print("Error: --runs must be >= 1", file=sys.stderr)
        sys.exit(1)

    return pattern, runs, refresh_baseline, current_only, build_dir, run_cooldown_ms, pin_cpu


def ensure_runtime_binaries(build_dir, current_only):
    required = [
        "comp_current_multi_e2e_server",
        "comp_current_multi_e2e_client",
    ]
    if not current_only:
        required.extend([
            "comp_baseline_multi_e2e_server",
            "comp_baseline_multi_e2e_client",
        ])
    missing = [
        name
        for name in required
        if not os.path.exists(os.path.join(build_dir, name + EXE_SUFFIX))
    ]
    return required, missing


def get_env(base_env, lib_name, baseline_lib_dir, current_lib_dir):
    env = base_env.copy()
    if IS_WINDOWS:
        if lib_name == "baseline":
            env["PATH"] = f"{baseline_lib_dir};{env.get('PATH', '')}"
        else:
            env["PATH"] = f"{current_lib_dir};{env.get('PATH', '')}"
    else:
        if lib_name == "baseline":
            env["LD_LIBRARY_PATH"] = f"{baseline_lib_dir}:{env.get('LD_LIBRARY_PATH', '')}"
        else:
            env["LD_LIBRARY_PATH"] = f"{current_lib_dir}:{env.get('LD_LIBRARY_PATH', '')}"
    return env


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


def run_single_stack_test(build_dir,
                          server_bin,
                          client_bin,
                          lib_name,
                          pattern,
                          transport,
                          msg_size,
                          clients,
                          inflight,
                          duration,
                          settle_ms,
                          drain_ms,
                          io_threads,
                          base_env,
                          msg_sizes_override=None):
    port = find_free_port(29300)
    env = base_env.copy()
    env["BENCH_MULTI_E2E_PATTERN"] = pattern
    env["BENCH_MULTI_E2E_PORT"] = str(port)
    env["BENCH_MULTI_CLIENTS"] = str(clients)
    env["BENCH_MULTI_INFLIGHT"] = str(inflight)
    env["BENCH_MULTI_DURATION_SECONDS"] = str(duration)
    env["BENCH_MULTI_SETTLE_MS"] = str(settle_ms)
    env["BENCH_MULTI_DRAIN_MS"] = str(drain_ms)
    env["BENCH_IO_THREADS"] = str(io_threads)
    if msg_sizes_override:
        env["BENCH_MULTI_E2E_MSG_SIZES"] = ",".join(str(int(v)) for v in msg_sizes_override)
    elif "BENCH_MULTI_E2E_MSG_SIZES" in env:
        del env["BENCH_MULTI_E2E_MSG_SIZES"]

    size_arg = int(msg_size)
    server_cmd = [
        os.path.join(build_dir, server_bin + EXE_SUFFIX),
        lib_name,
        transport,
        str(size_arg),
    ]
    client_cmd = [
        os.path.join(build_dir, client_bin + EXE_SUFFIX),
        lib_name,
        transport,
        str(size_arg),
    ]

    server_proc = None
    try:
        server_proc = subprocess.Popen(
            server_cmd,
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        time.sleep(float(max(1, settle_ms // 250)))

        timeout_sec = max(60, duration + 40)
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
            if err_msg:
                return None, f"client_rc_{result.returncode}:{err_msg}"
            return None, f"client_rc_{result.returncode}"
        if msg_sizes_override:
            parsed_map = parse_results(result.stdout, pattern, transport)
            filtered = {}
            for size in msg_sizes_override:
                parsed = parsed_map.get(int(size))
                if not parsed:
                    continue
                if "throughput" not in parsed or "latency" not in parsed:
                    continue
                filtered[int(size)] = parsed
            if not filtered:
                return None, "no_data"
            return filtered, ""
        parsed = parse_result(result.stdout, pattern, transport, int(msg_size))
        if "throughput" not in parsed or "latency" not in parsed:
            return None, "no_data"
        return parsed, ""
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
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, sort_keys=True, ensure_ascii=True)


def main():
    pattern_req, runs, refresh_baseline, current_only, build_dir_arg, run_cooldown_ms, pin_cpu = parse_args()

    default_build = resolve_default_build_dir()
    build_dir = normalize_build_dir(build_dir_arg) if build_dir_arg else normalize_build_dir(default_build)
    current_lib_dir = derive_current_lib_dir(build_dir)
    baseline_lib_dir = derive_baseline_lib_dir()

    platform_tag, arch_tag = platform_arch_tag()
    cache_file = os.path.join(
        SCRIPT_DIR,
        f"baseline_multi_e2e_cache_{platform_tag}-{arch_tag}.json",
    )

    required, missing = ensure_runtime_binaries(build_dir, current_only)
    if missing:
        cmake_build_dir = derive_cmake_build_dir(build_dir)
        if not cmake_build_dir:
            print(f"Error: missing benchmark binaries in {build_dir}", file=sys.stderr)
            for m in missing:
                print(f"  - {m}{EXE_SUFFIX}", file=sys.stderr)
            return 2

        rc = run_cmake_configure(cmake_build_dir)
        if rc != 0:
            print(f"Error: auto-configure failed with exit code {rc}", file=sys.stderr)
            return rc
        rc = run_cmake_build(cmake_build_dir, required)
        if rc != 0:
            print(f"Error: auto-build failed with exit code {rc}", file=sys.stderr)
            return rc

        build_dir = normalize_build_dir(runtime_bin_dir_from_cmake_dir(cmake_build_dir))
        current_lib_dir = derive_current_lib_dir(build_dir)
        _, missing = ensure_runtime_binaries(build_dir, current_only)
        if missing:
            print(f"Error: missing benchmark binaries in {build_dir}", file=sys.stderr)
            for m in missing:
                print(f"  - {m}{EXE_SUFFIX}", file=sys.stderr)
            return 2

    base_env = os.environ.copy()
    if pin_cpu and not IS_WINDOWS:
        base_env["BENCH_TASKSET"] = "1"

    patterns = list(DEFAULT_PATTERNS)
    if pattern_req != "ALL":
        req = [p.strip().upper() for p in pattern_req.split(",") if p.strip()]
        invalid = [p for p in req if p not in patterns]
        if invalid:
            print("Error: unsupported pattern(s): " + ", ".join(invalid), file=sys.stderr)
            return 2
        patterns = req

    requested_transports = parse_env_list("BENCH_TRANSPORTS", str) or [
        "tcp", "tls", "ws", "wss"
    ]
    requested_transports = [t.strip().lower() for t in requested_transports if t]
    allowed_transports = ("tcp", "tls", "ws", "wss")
    invalid_transports = [t for t in requested_transports
                          if t not in allowed_transports]
    if invalid_transports:
        uniq = []
        for t in invalid_transports:
            if t not in uniq:
                uniq.append(t)
        print("Error: unsupported transport(s): " + ",".join(uniq),
              file=sys.stderr)
        print("Allowed transports: tcp,tls,ws,wss", file=sys.stderr)
        return 2

    transports = []
    for t in requested_transports:
        if t not in transports:
            transports.append(t)
    if not transports:
        print("No transports requested")
        return 0

    msg_sizes = parse_env_list("BENCH_MSG_SIZES", int) or [64, 256, 1024, 65536, 131072, 262144]
    default_non_stream_clients = parse_env_list("BENCH_MULTI_E2E_NON_STREAM_CLIENTS", int) or [100, 1000]
    default_stream_clients = parse_env_list("BENCH_MULTI_E2E_STREAM_CLIENTS", int) or [10000]
    global_clients_override = parse_env_list("BENCH_MULTI_CLIENTS", int)

    inflight = int(os.environ.get("BENCH_MULTI_INFLIGHT", "1"))
    duration = int(os.environ.get("BENCH_MULTI_DURATION_SECONDS", "5"))
    settle_ms = int(os.environ.get("BENCH_MULTI_SETTLE_MS", "500"))
    drain_ms = int(os.environ.get("BENCH_MULTI_DRAIN_MS", "300"))
    io_threads = int(os.environ.get("BENCH_IO_THREADS", "4"))

    cache = load_cache(cache_file)
    cache_patterns = cache.setdefault("patterns", {})
    cache_updated = False
    all_failures = []

    for pattern in patterns:
        print(f"\n## PATTERN: {pattern}")

        clients_matrix = (
            global_clients_override
            if global_clients_override
            else (default_stream_clients if pattern == "MULTI_STREAM" else default_non_stream_clients)
        )

        baseline_stats = {}
        current_stats = {}

        cached = cache_patterns.get(pattern)

        for transport in transports:
            for clients in clients_matrix:
                print(f"  > Config clients={clients} inflight={inflight} transport={transport}")

                use_size_reuse = pattern in RTT_PATTERNS
                if use_size_reuse:
                    size_csv = ",".join(str(s) for s in msg_sizes)
                    need_baseline = (not current_only) or refresh_baseline or not cached
                    baseline_values_by_size = {size: [] for size in msg_sizes}
                    current_values_by_size = {size: [] for size in msg_sizes}

                    if need_baseline:
                        for i in range(runs):
                            print(f"    baseline sizes[{size_csv}] run {i+1}/{runs}", flush=True)
                            env_baseline = get_env(base_env, "baseline", baseline_lib_dir, current_lib_dir)
                            parsed_map, err = run_single_stack_test(
                                build_dir,
                                "comp_baseline_multi_e2e_server",
                                "comp_baseline_multi_e2e_client",
                                "baseline",
                                pattern,
                                transport,
                                msg_sizes[0],
                                clients,
                                inflight,
                                duration,
                                settle_ms,
                                drain_ms,
                                io_threads,
                                env_baseline,
                                msg_sizes_override=msg_sizes,
                            )
                            if not parsed_map:
                                all_failures.append((pattern, "baseline", transport, clients, 0, err))
                                continue
                            for size in msg_sizes:
                                parsed = parsed_map.get(size)
                                if not parsed:
                                    all_failures.append((pattern, "baseline", transport, clients, size, "missing_metric"))
                                    continue
                                baseline_values_by_size[size].append(parsed)
                            if run_cooldown_ms > 0 and i + 1 < runs:
                                time.sleep(run_cooldown_ms / 1000.0)
                    elif cached:
                        for size in msg_sizes:
                            key_prefix = f"{transport}|{clients}|{size}"
                            load_cached_stats(baseline_stats, cached, key_prefix)

                    for i in range(runs):
                        print(f"    current  sizes[{size_csv}] run {i+1}/{runs}", flush=True)
                        env_current = get_env(base_env, "current", baseline_lib_dir, current_lib_dir)
                        parsed_map, err = run_single_stack_test(
                            build_dir,
                            "comp_current_multi_e2e_server",
                            "comp_current_multi_e2e_client",
                            "current",
                            pattern,
                            transport,
                            msg_sizes[0],
                            clients,
                            inflight,
                            duration,
                            settle_ms,
                            drain_ms,
                            io_threads,
                            env_current,
                            msg_sizes_override=msg_sizes,
                        )
                        if not parsed_map:
                            all_failures.append((pattern, "current", transport, clients, 0, err))
                            continue
                        for size in msg_sizes:
                            parsed = parsed_map.get(size)
                            if not parsed:
                                all_failures.append((pattern, "current", transport, clients, size, "missing_metric"))
                                continue
                            current_values_by_size[size].append(parsed)
                        if run_cooldown_ms > 0 and i + 1 < runs:
                            time.sleep(run_cooldown_ms / 1000.0)

                    for size in msg_sizes:
                        key_prefix = f"{transport}|{clients}|{size}"
                        baseline_values = baseline_values_by_size.get(size, [])
                        current_values = current_values_by_size.get(size, [])
                        update_stats_with_median(baseline_stats, key_prefix, baseline_values)
                        update_stats_with_median(current_stats, key_prefix, current_values)
                    continue

                for size in msg_sizes:
                    key_prefix = f"{transport}|{clients}|{size}"

                    baseline_values = []
                    current_values = []

                    need_baseline = (not current_only) or refresh_baseline or not cached
                    if need_baseline:
                        for i in range(runs):
                            print(f"    baseline {size}B run {i+1}/{runs}", flush=True)
                            env_baseline = get_env(base_env, "baseline", baseline_lib_dir, current_lib_dir)
                            parsed, err = run_single_stack_test(
                                build_dir,
                                "comp_baseline_multi_e2e_server",
                                "comp_baseline_multi_e2e_client",
                                "baseline",
                                pattern,
                                transport,
                                size,
                                clients,
                                inflight,
                                duration,
                                settle_ms,
                                drain_ms,
                                io_threads,
                                env_baseline,
                            )
                            if not parsed:
                                all_failures.append((pattern, "baseline", transport, clients, size, err))
                                continue
                            baseline_values.append(parsed)
                            if run_cooldown_ms > 0 and i + 1 < runs:
                                time.sleep(run_cooldown_ms / 1000.0)
                    elif cached:
                        load_cached_stats(baseline_stats, cached, key_prefix)

                    for i in range(runs):
                        print(f"    current  {size}B run {i+1}/{runs}", flush=True)
                        env_current = get_env(base_env, "current", baseline_lib_dir, current_lib_dir)
                        parsed, err = run_single_stack_test(
                            build_dir,
                            "comp_current_multi_e2e_server",
                            "comp_current_multi_e2e_client",
                            "current",
                            pattern,
                            transport,
                            size,
                            clients,
                            inflight,
                            duration,
                            settle_ms,
                            drain_ms,
                            io_threads,
                            env_current,
                        )
                        if not parsed:
                            all_failures.append((pattern, "current", transport, clients, size, err))
                            continue
                        current_values.append(parsed)
                        if run_cooldown_ms > 0 and i + 1 < runs:
                            time.sleep(run_cooldown_ms / 1000.0)

                    update_stats_with_median(baseline_stats, key_prefix, baseline_values)
                    update_stats_with_median(current_stats, key_prefix, current_values)

        if not current_only or refresh_baseline or not cached:
            cache_patterns[pattern] = {k: float(v) for k, v in baseline_stats.items()}
            cache_updated = True
        elif cached and not baseline_stats:
            baseline_stats = {k: float(v) for k, v in cached.items()}

        for transport in transports:
            print(f"\n### Transport: {transport}")
            print("| Clients | Size | Metric | baseline | current | Diff (%) |")
            print("|---------|------|--------|----------|---------|----------|")
            for clients in clients_matrix:
                for size in msg_sizes:
                    key_prefix = f"{transport}|{clients}|{size}"
                    bt = baseline_stats.get(f"{key_prefix}|throughput")
                    bl = baseline_stats.get(f"{key_prefix}|latency")
                    ct = current_stats.get(f"{key_prefix}|throughput")
                    cl = current_stats.get(f"{key_prefix}|latency")

                    td_s = throughput_diff_str(bt, ct)
                    ld_s = latency_diff_str(bl, cl)

                    bt_s = format_throughput(bt) if bt is not None else "N/A"
                    ct_s = format_throughput(ct) if ct is not None else "N/A"
                    bl_s = format_latency(bl) if bl is not None else "N/A"
                    cl_s = format_latency(cl) if cl is not None else "N/A"

                    print(f"| {clients} | {size}B | Throughput | {bt_s} | {ct_s} | {td_s} |")
                    print(f"| {clients} | {size}B | Latency | {bl_s} | {cl_s} | {ld_s} |")

    if cache_updated:
        cache["meta"] = {
            "build_dir": build_dir,
            "baseline_lib_dir": baseline_lib_dir,
            "current_lib_dir": current_lib_dir,
            "updated_at_utc": datetime.now(timezone.utc).isoformat(),
        }
        save_cache(cache_file, cache)
        print(f"Saved baseline cache: {cache_file}")

    if all_failures:
        print("\n## Failures")
        for f in all_failures:
            p, libn, tr, clients, sz, reason = f
            print(f"- {p} {libn} {tr} clients={clients} {sz}B: {reason}")

    return 1 if all_failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
