#!/usr/bin/env python3
import json
import os
import platform
import re
import socket
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone

IS_WINDOWS = os.name == "nt"
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""
SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", "..", ".."))
BUILD_CONFIG_DIRS = ("Release", "Debug", "RelWithDebInfo", "MinSizeRel")

DEFAULT_PATTERNS = [
    "MULTI_DEALER_DEALER",
    "MULTI_DEALER_ROUTER",
    "MULTI_ROUTER_ROUTER",
    "MULTI_ROUTER_ROUTER_POLL",
    "MULTI_PUBSUB",
    "MULTI_STREAM",
]


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
    build_dir = next((p for p in possible_paths if os.path.exists(p)), possible_paths[0])

    linux_dist_dir = os.path.join(
        ROOT_DIR,
        "core",
        "bench",
        "benchwithzmq",
        "libzmq",
        "libzmq_dist",
        "linux-x64",
        "lib",
    )
    default_dist_dir = os.path.join(
        ROOT_DIR,
        "core",
        "bench",
        "benchwithzmq",
        "libzmq",
        "libzmq_dist",
        "lib",
    )
    libzmq_lib_dir = os.path.abspath(linux_dist_dir if os.path.exists(linux_dist_dir) else default_dist_dir)
    env_libzmq_dir = os.environ.get("BENCH_LIBZMQ_LIB_DIR")
    if env_libzmq_dir:
        libzmq_lib_dir = os.path.abspath(env_libzmq_dir)

    return build_dir, libzmq_lib_dir


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
    usage = (
        "Usage: multi_e2e/run_comparison.py [PATTERN] [options]\n\n"
        "Options:\n"
        "  --zlink-only            Run only zlink and compare with cached libzmq\n"
        "  --refresh-std-cache     Refresh libzmq cache\n"
        "  --std-cache-file PATH   Override cache file path\n"
        "  --runs N                Iterations per config (default: 5)\n"
        "  --build-dir PATH        Build directory\n"
        "  --run-cooldown-ms N     Sleep between runs (default: 3000)\n"
        "  --pin-cpu               Enable taskset pinning on Linux\n"
    )

    pattern = "ALL"
    num_runs = 5
    zlink_only = False
    refresh_std_cache = False
    std_cache_file = ""
    build_dir = ""
    run_cooldown_ms = int(os.environ.get("BENCH_MULTI_RUN_COOLDOWN_MS", "3000"))
    pin_cpu = False

    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg in ("-h", "--help"):
            print(usage)
            sys.exit(0)
        if arg == "--zlink-only":
            zlink_only = True
        elif arg == "--refresh-std-cache":
            refresh_std_cache = True
        elif arg == "--std-cache-file":
            i += 1
            std_cache_file = sys.argv[i]
        elif arg == "--runs":
            i += 1
            num_runs = int(sys.argv[i])
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

    if num_runs < 1:
        print("Error: --runs must be >= 1", file=sys.stderr)
        sys.exit(1)

    return pattern, num_runs, zlink_only, refresh_std_cache, std_cache_file, build_dir, run_cooldown_ms, pin_cpu


def parse_result(stdout, transport, msg_size, pattern):
    out = {}
    for line in stdout.splitlines():
        if not line.startswith("RESULT,"):
            continue
        p = line.split(",")
        if len(p) < 7:
            continue
        if p[2].strip() != pattern:
            continue
        if p[3].strip() != transport:
            continue
        try:
            size = int(p[4])
        except ValueError:
            continue
        if size != msg_size:
            continue
        metric = p[5].strip().lower()
        if metric not in ("throughput", "latency"):
            continue
        try:
            out[metric] = float(p[6])
        except ValueError:
            continue
    return out


def get_env_for_lib(base_env, lib_name, libzmq_lib_dir, zlink_lib_dir):
    env = base_env.copy()
    if IS_WINDOWS:
        env["PATH"] = f"{libzmq_lib_dir};{env.get('PATH', '')}"
    else:
        if lib_name == "zlink":
            env["LD_LIBRARY_PATH"] = f"{zlink_lib_dir}:{env.get('LD_LIBRARY_PATH', '')}"
        else:
            env["LD_LIBRARY_PATH"] = f"{libzmq_lib_dir}:{env.get('LD_LIBRARY_PATH', '')}"
    return env


def ensure_runtime_binaries(build_dir):
    required = [
        "comp_std_zmq_multi_e2e_server",
        "comp_std_zmq_multi_e2e_client",
        "comp_zlink_multi_e2e_server",
        "comp_zlink_multi_e2e_client",
    ]
    missing = [
        name
        for name in required
        if not os.path.exists(os.path.join(build_dir, name + EXE_SUFFIX))
    ]
    return required, missing


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
                          base_env):
    port = find_free_port(29100)
    env = base_env.copy()
    env["BENCH_MULTI_E2E_PATTERN"] = pattern
    env["BENCH_MULTI_E2E_PORT"] = str(port)
    env["BENCH_MULTI_CLIENTS"] = str(clients)
    env["BENCH_MULTI_INFLIGHT"] = str(inflight)
    env["BENCH_MULTI_DURATION_SECONDS"] = str(duration)
    env["BENCH_MULTI_SETTLE_MS"] = str(settle_ms)
    env["BENCH_MULTI_DRAIN_MS"] = str(drain_ms)
    env["BENCH_IO_THREADS"] = str(io_threads)

    server_cmd = [
        os.path.join(build_dir, server_bin + EXE_SUFFIX),
        lib_name,
        transport,
        str(msg_size),
    ]
    client_cmd = [
        os.path.join(build_dir, client_bin + EXE_SUFFIX),
        lib_name,
        transport,
        str(msg_size),
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
        parsed = parse_result(result.stdout, transport, msg_size, pattern)
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


def format_t(v):
    return f"{v/1e3:6.2f} Kmsg/s"


def main():
    global_build_dir, libzmq_lib_dir = (
        os.path.join(ROOT_DIR, "core", "build", "windows-x64", "bin", "Release"),
        os.path.join(ROOT_DIR, "core", "bench", "benchwithzmq", "libzmq", "libzmq_dist", "windows-x64", "bin"),
    ) if IS_WINDOWS else resolve_linux_paths()

    pattern_req, runs, zlink_only, refresh_std_cache, std_cache_file, build_dir_arg, run_cooldown_ms, pin_cpu = parse_args()

    build_dir = normalize_build_dir(build_dir_arg) if build_dir_arg else normalize_build_dir(global_build_dir)
    zlink_lib_dir = derive_zlink_lib_dir(build_dir)

    platform_tag, arch_tag = platform_arch_tag()
    cache_file = os.path.abspath(std_cache_file) if std_cache_file else os.path.join(
        SCRIPT_DIR,
        f"std_zmq_multi_e2e_cache_{platform_tag}-{arch_tag}.json",
    )

    required, missing = ensure_runtime_binaries(build_dir)
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
        zlink_lib_dir = derive_zlink_lib_dir(build_dir)
        _, missing = ensure_runtime_binaries(build_dir)
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

    requested_transports = parse_env_list("BENCH_TRANSPORTS", str) or ["tcp"]
    requested_transports = [t.strip().lower() for t in requested_transports if t]
    unsupported_transports = [t for t in requested_transports if t != "tcp"]
    if unsupported_transports:
        uniq = []
        for t in unsupported_transports:
            if t not in uniq:
                uniq.append(t)
        print("Note: benchwithzmq multi_e2e supports only tcp; skipping: "
              + ",".join(uniq))

    transports = [t for t in requested_transports if t == "tcp"]
    if not transports:
        print("No tcp transport requested; nothing to run.")
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
    all_failures = []
    cache_updated = False

    for pattern in patterns:
        print(f"\n## PATTERN: {pattern}")

        clients_matrix = (
            global_clients_override
            if global_clients_override
            else (default_stream_clients if pattern == "MULTI_STREAM" else default_non_stream_clients)
        )

        std_stats = {}
        zlk_stats = {}

        cache_key = pattern
        cached = cache_patterns.get(cache_key)

        for transport in transports:
            for clients in clients_matrix:
                print(f"  > Config clients={clients} inflight={inflight} transport={transport}")
                for size in msg_sizes:
                    key_prefix = f"{transport}|{clients}|{size}"

                    std_values = []
                    zlk_values = []

                    if not zlink_only:
                        pass

                    need_std = (not zlink_only) or refresh_std_cache or not cached
                    if need_std:
                        for i in range(runs):
                            print(f"    libzmq {size}B run {i+1}/{runs}", flush=True)
                            env_std = get_env_for_lib(base_env, "libzmq", libzmq_lib_dir, zlink_lib_dir)
                            parsed, err = run_single_stack_test(
                                build_dir,
                                "comp_std_zmq_multi_e2e_server",
                                "comp_std_zmq_multi_e2e_client",
                                "libzmq",
                                pattern,
                                transport,
                                size,
                                clients,
                                inflight,
                                duration,
                                settle_ms,
                                drain_ms,
                                io_threads,
                                env_std,
                            )
                            if not parsed:
                                all_failures.append((pattern, "libzmq", transport, clients, size, err))
                                continue
                            std_values.append(parsed)
                            if run_cooldown_ms > 0 and i + 1 < runs:
                                time.sleep(run_cooldown_ms / 1000.0)
                    elif cached:
                        ck = cached.get(f"{key_prefix}|throughput")
                        cl = cached.get(f"{key_prefix}|latency")
                        if ck is not None and cl is not None:
                            std_stats[f"{key_prefix}|throughput"] = float(ck)
                            std_stats[f"{key_prefix}|latency"] = float(cl)

                    for i in range(runs):
                        print(f"    zlink  {size}B run {i+1}/{runs}", flush=True)
                        env_zlk = get_env_for_lib(base_env, "zlink", libzmq_lib_dir, zlink_lib_dir)
                        parsed, err = run_single_stack_test(
                            build_dir,
                            "comp_zlink_multi_e2e_server",
                            "comp_zlink_multi_e2e_client",
                            "zlink",
                            pattern,
                            transport,
                            size,
                            clients,
                            inflight,
                            duration,
                            settle_ms,
                            drain_ms,
                            io_threads,
                            env_zlk,
                        )
                        if not parsed:
                            all_failures.append((pattern, "zlink", transport, clients, size, err))
                            continue
                        zlk_values.append(parsed)
                        if run_cooldown_ms > 0 and i + 1 < runs:
                            time.sleep(run_cooldown_ms / 1000.0)

                    if std_values:
                        std_stats[f"{key_prefix}|throughput"] = statistics.median(v["throughput"] for v in std_values)
                        std_stats[f"{key_prefix}|latency"] = statistics.median(v["latency"] for v in std_values)
                    if zlk_values:
                        zlk_stats[f"{key_prefix}|throughput"] = statistics.median(v["throughput"] for v in zlk_values)
                        zlk_stats[f"{key_prefix}|latency"] = statistics.median(v["latency"] for v in zlk_values)

        if not zlink_only or refresh_std_cache or not cached:
            cache_patterns[cache_key] = {k: float(v) for k, v in std_stats.items()}
            cache_updated = True
        elif cached and not std_stats:
            std_stats = {k: float(v) for k, v in cached.items()}

        for transport in transports:
            print(f"\n### Transport: {transport}")
            print("| Clients | Size | Metric | Standard libzmq | zlink | Diff (%) |")
            print("|---------|------|--------|-----------------|-------|----------|")
            for clients in clients_matrix:
                for size in msg_sizes:
                    key_prefix = f"{transport}|{clients}|{size}"
                    std_t = std_stats.get(f"{key_prefix}|throughput")
                    std_l = std_stats.get(f"{key_prefix}|latency")
                    zlk_t = zlk_stats.get(f"{key_prefix}|throughput")
                    zlk_l = zlk_stats.get(f"{key_prefix}|latency")

                    if std_t and zlk_t and std_t > 0:
                        td = (zlk_t - std_t) / std_t * 100.0
                        td_s = f"{td:+.2f}%"
                    else:
                        td_s = "N/A"
                    if std_l and zlk_l and std_l > 0:
                        ld = (std_l - zlk_l) / std_l * 100.0
                        ld_s = f"{ld:+.2f}%"
                    else:
                        ld_s = "N/A"

                    std_t_s = format_t(std_t) if std_t is not None else "N/A"
                    zlk_t_s = format_t(zlk_t) if zlk_t is not None else "N/A"
                    std_l_s = f"{std_l:8.2f} us" if std_l is not None else "N/A"
                    zlk_l_s = f"{zlk_l:8.2f} us" if zlk_l is not None else "N/A"

                    print(f"| {clients} | {size}B | Throughput | {std_t_s} | {zlk_t_s} | {td_s} |")
                    print(f"| {clients} | {size}B | Latency | {std_l_s} | {zlk_l_s} | {ld_s} |")

    if cache_updated:
        cache["meta"] = {
            "build_dir": build_dir,
            "libzmq_lib_dir": libzmq_lib_dir,
            "updated_at_utc": datetime.now(timezone.utc).isoformat(),
        }
        save_cache(cache_file, cache)
        print(f"Saved libzmq cache: {cache_file}")

    if all_failures:
        print("\n## Failures")
        for f in all_failures:
            p, libn, tr, clients, sz, reason = f
            print(f"- {p} {libn} {tr} clients={clients} {sz}B: {reason}")

    return 1 if all_failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
