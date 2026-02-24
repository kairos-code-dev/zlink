#!/usr/bin/env python3
import json
import os
import platform
import selectors
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
BENCH_ZMQ_ROOT = os.path.join(ROOT_DIR, "core", "bench", "with_zmq")
if not os.path.isdir(BENCH_ZMQ_ROOT):
    BENCH_ZMQ_ROOT = os.path.join(ROOT_DIR, "core", "bench", "benchwithzmq")


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
        os.path.join(
            ROOT_DIR, "core", "build", f"{platform_tag}-{arch_tag}", "bin", "Release"
        ),
        os.path.join(ROOT_DIR, "core", "build", "bin"),
    ]
    build_dir = next((p for p in possible_paths if os.path.exists(p)), possible_paths[0])

    linux_dist_dir = os.path.join(
        BENCH_ZMQ_ROOT,
        "libzmq",
        "libzmq_dist",
        "linux-x64",
        "lib",
    )
    default_dist_dir = os.path.join(
        BENCH_ZMQ_ROOT, "libzmq", "libzmq_dist", "lib"
    )
    libzmq_lib_dir = os.path.abspath(linux_dist_dir if os.path.exists(linux_dist_dir) else default_dist_dir)
    env_libzmq_dir = os.environ.get("BENCH_LIBZMQ_LIB_DIR")
    if env_libzmq_dir:
        libzmq_lib_dir = os.path.abspath(env_libzmq_dir)

    build_root = build_dir
    base = os.path.basename(build_root)
    if base in ("Release", "Debug", "RelWithDebInfo", "MinSizeRel"):
        bin_root = os.path.dirname(build_root)
        if os.path.basename(bin_root) == "bin":
            build_root = os.path.dirname(bin_root)
    elif base == "bin":
        build_root = os.path.dirname(build_root)
    zlink_lib_dir = os.path.abspath(os.path.join(build_root, "lib"))
    return build_dir, libzmq_lib_dir, zlink_lib_dir


def runtime_bin_dir_from_cmake_dir(cmake_build_dir):
    bin_dir = os.path.join(cmake_build_dir, "bin")
    if IS_WINDOWS:
        return os.path.join(bin_dir, "Release")
    return bin_dir


def normalize_build_dir(path):
    if not path:
        return path
    abs_path = os.path.abspath(path)
    if not os.path.isdir(abs_path):
        base = os.path.basename(abs_path)
        if base in BUILD_CONFIG_DIRS:
            return abs_path
        if base == "bin":
            if IS_WINDOWS:
                return os.path.join(abs_path, "Release")
            return abs_path
        if os.path.basename(os.path.dirname(abs_path)) == "bin":
            return abs_path
        return runtime_bin_dir_from_cmake_dir(abs_path)

    base = os.path.basename(abs_path)
    if base in BUILD_CONFIG_DIRS:
        return abs_path

    if base == "bin":
        if IS_WINDOWS:
            release_dir = os.path.join(abs_path, "Release")
            return release_dir
        return abs_path

    bin_dir = os.path.join(abs_path, "bin")
    if os.path.isdir(bin_dir):
        if IS_WINDOWS:
            release_dir = os.path.join(bin_dir, "Release")
            return release_dir
        return bin_dir

    if os.path.isfile(os.path.join(abs_path, "CMakeCache.txt")):
        return runtime_bin_dir_from_cmake_dir(abs_path)

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


def has_cmake_cache(path):
    return os.path.isfile(os.path.join(path, "CMakeCache.txt"))


def derive_cmake_build_dir(runtime_build_dir):
    if not runtime_build_dir:
        return ""

    abs_path = os.path.abspath(runtime_build_dir)
    if has_cmake_cache(abs_path):
        return abs_path

    base = os.path.basename(abs_path)
    if base in BUILD_CONFIG_DIRS:
        bin_root = os.path.dirname(abs_path)
        if os.path.basename(bin_root) == "bin":
            return os.path.dirname(bin_root)
        return os.path.dirname(abs_path)
    elif base == "bin":
        return os.path.dirname(abs_path)

    parent = os.path.dirname(abs_path)
    if os.path.basename(parent) == "bin":
        return os.path.dirname(parent)
    return abs_path


if IS_WINDOWS:
    BUILD_DIR = os.path.join(ROOT_DIR, "core", "build", "windows-x64", "bin", "Release")
    LIBZMQ_LIB_DIR = os.path.join(
        BENCH_ZMQ_ROOT,
        "libzmq",
        "libzmq_dist",
        "windows-x64",
        "bin",
    )
    env_libzmq_dir = os.environ.get("BENCH_LIBZMQ_BIN_DIR")
    if env_libzmq_dir:
        LIBZMQ_LIB_DIR = os.path.abspath(env_libzmq_dir)
    ZLINK_LIB_DIR = os.path.join(ROOT_DIR, "core", "build", "windows-x64", "bin", "Release")
else:
    BUILD_DIR, LIBZMQ_LIB_DIR, ZLINK_LIB_DIR = resolve_linux_paths()

DEFAULT_NUM_RUNS = 3
PATTERN_SEPARATOR = "==============================================================================="
_platform_tag, _arch_tag = platform_arch_tag()
DEFAULT_STD_CACHE_FILE = os.path.join(
    SCRIPT_DIR, f"std_zmq_multi_cache_{_platform_tag}-{_arch_tag}.json"
)


def parse_env_list(name, cast_fn):
    val = os.environ.get(name)
    if not val:
        return None
    out = []
    for part in val.split(","):
        part = part.strip()
        if not part:
            continue
        try:
            out.append(cast_fn(part))
        except ValueError:
            continue
    return out or None


_env_transports = parse_env_list("BENCH_TRANSPORTS", str)
TRANSPORTS = _env_transports if _env_transports else ["tcp"]

_env_sizes = parse_env_list("BENCH_MSG_SIZES", int)
MSG_SIZES = _env_sizes if _env_sizes else [64, 256, 1024, 65536, 131072, 262144]

FAIL_FAST = os.environ.get("BENCH_FAIL_FAST", "0") == "1"
SHOW_PREP = (
    os.environ.get("BENCH_STREAM_SHOW_PREP", "0") == "1"
    or os.environ.get("BENCH_DEBUG", "0") == "1"
)
base_env = os.environ.copy()


def parse_nonnegative_int_env(name, default_value):
    raw = os.environ.get(name)
    if raw is None:
        return default_value
    try:
        return max(0, int(raw))
    except ValueError:
        return default_value


DEFAULT_RUN_COOLDOWN_MS = parse_nonnegative_int_env(
    "BENCH_MULTI_RUN_COOLDOWN_MS", 3000
)


def select_comparisons(comparisons, pattern_req):
    selected = []
    for item in comparisons:
        if pattern_req != "ALL" and item[2] != pattern_req:
            continue
        selected.append(item)
    return selected


def expected_runtime_binaries(selected_comparisons, include_std):
    names = []
    for std_bin, zlk_bin, _ in selected_comparisons:
        names.append(zlk_bin)
        if include_std:
            names.append(std_bin)
    return names


def collect_missing_binaries(runtime_bin_dir, names):
    missing = []
    for name in names:
        bin_path = os.path.join(runtime_bin_dir, name + EXE_SUFFIX)
        if not os.path.exists(bin_path):
            missing.append(name)
    return sorted(set(missing))


def collect_multi_build_targets(comparisons, include_std):
    targets = []
    for std_bin, zlk_bin, _ in comparisons:
        if include_std and std_bin not in targets:
            targets.append(std_bin)
        if zlk_bin not in targets:
            targets.append(zlk_bin)
    return targets


def run_cmake_build(cmake_build_dir, targets):
    cmd = ["cmake", "--build", cmake_build_dir]
    if IS_WINDOWS:
        cmd.extend(["--config", "Release"])
    if targets:
        cmd.append("--target")
        cmd.extend(targets)

    print(
        f"  > Auto-building missing benchmark binaries in {cmake_build_dir}",
        flush=True,
    )
    print(f"  > Build command: {' '.join(cmd)}", flush=True)
    try:
        return subprocess.run(cmd).returncode
    except FileNotFoundError:
        print("Error: cmake not found in PATH", file=sys.stderr)
        return 127


def detect_cmake_source_dir(cmake_build_dir):
    cache_path = os.path.join(cmake_build_dir, "CMakeCache.txt")
    if os.path.isfile(cache_path):
        try:
            with open(cache_path, "r", encoding="utf-8") as cache_file:
                for line in cache_file:
                    if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="):
                        source_dir = line.split("=", 1)[1].strip()
                        if source_dir:
                            return source_dir
        except OSError:
            pass

    root_cmake = os.path.join(ROOT_DIR, "CMakeLists.txt")
    if os.path.isfile(root_cmake):
        return ROOT_DIR
    return os.path.join(ROOT_DIR, "core")


def run_cmake_configure(cmake_build_dir):
    source_dir = detect_cmake_source_dir(cmake_build_dir)
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

    print(
        f"  > Configuring benchmark build in {cmake_build_dir}",
        flush=True,
    )
    print(f"  > Configure command: {' '.join(cmd)}", flush=True)
    try:
        return subprocess.run(cmd).returncode
    except FileNotFoundError:
        print("Error: cmake not found in PATH", file=sys.stderr)
        return 127


def get_env_for_lib(lib_name):
    env = base_env.copy()
    env["BENCH_MSG_SIZES"] = ",".join(str(sz) for sz in MSG_SIZES)
    if IS_WINDOWS:
        env["PATH"] = f"{LIBZMQ_LIB_DIR};{env.get('PATH', '')}"
    else:
        if lib_name == "zlink":
            env["LD_LIBRARY_PATH"] = f"{ZLINK_LIB_DIR}:{env.get('LD_LIBRARY_PATH', '')}"
        else:
            env["LD_LIBRARY_PATH"] = f"{LIBZMQ_LIB_DIR}:{env.get('LD_LIBRARY_PATH', '')}"
    return env


def parse_result_line(line, transport, expected_sizes):
    if not line.startswith("RESULT,"):
        return None
    p = line.split(",")
    if len(p) < 7:
        return None
    try:
        line_transport = p[3]
        line_size = int(p[4])
        metric = p[5].strip().lower()
        value = float(p[6])
    except ValueError:
        return None
    if line_transport != transport or line_size not in expected_sizes:
        return None
    if metric not in ("throughput", "latency"):
        return None
    return line_transport, line_size, metric, value


def parse_prep_line(line, transport, expected_sizes):
    if not line.startswith("PREP,"):
        return None
    p = line.split(",")
    if len(p) < 9:
        return None
    try:
        line_transport = p[3]
        line_size = int(p[4])
        connect_ms = float(p[6])
        ready_ms = float(p[8])
    except ValueError:
        return None
    if line_transport != transport or line_size not in expected_sizes:
        return None
    return line_size, connect_ms, ready_ms


def run_single_test(
    binary_name, lib_name, transport, pattern_name, progress_cb=None, metric_cb=None
):
    binary_path = os.path.join(BUILD_DIR, binary_name + EXE_SUFFIX)
    env = get_env_for_lib(lib_name)
    env["BENCH_MULTI_PATTERN"] = pattern_name
    fallback_size = MSG_SIZES[0] if MSG_SIZES else 64
    timeout_override = parse_nonnegative_int_env("BENCH_MULTI_TIMEOUT_SECONDS", 0)
    duration_seconds = 5
    warmup_seconds = 3
    try:
        duration_seconds = max(1, int(os.environ.get("BENCH_MULTI_DURATION_SECONDS", "5")))
    except ValueError:
        duration_seconds = 5
    try:
        warmup_seconds = max(0, int(os.environ.get("BENCH_MULTI_WARMUP_SECONDS", "3")))
    except ValueError:
        warmup_seconds = 3
    if timeout_override > 0:
        timeout_sec = timeout_override
    else:
        timeout_sec = max(
            600,
            (warmup_seconds + duration_seconds) * max(1, len(MSG_SIZES)) * 8 + 120,
        )

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

        parsed = {}
        expected_sizes = set(MSG_SIZES)
        expected_metric_count = len(expected_sizes) * 2
        outcome = {
            "parsed": parsed,
            "timed_out": False,
            "returncode": 0,
            "error": "",
        }
        if IS_WINDOWS:
            result = subprocess.run(
                cmd, env=env, capture_output=True, text=True, timeout=timeout_sec
            )
            for line in result.stdout.splitlines():
                prep_line = parse_prep_line(line, transport, expected_sizes)
                if prep_line and progress_cb is not None:
                    line_size, connect_ms, ready_ms = prep_line
                    progress_cb(line_size, connect_ms, ready_ms)
                parsed_line = parse_result_line(line, transport, expected_sizes)
                if not parsed_line:
                    continue
                line_transport, line_size, metric, value = parsed_line
                parsed[f"{line_transport}|{line_size}|{metric}"] = value
                if metric_cb is not None:
                    metric_cb(line_size, metric, value)
                if progress_cb is not None and metric == "throughput":
                    progress_cb(line_size)
            outcome["returncode"] = result.returncode
            return outcome

        proc = subprocess.Popen(
            cmd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        deadline = time.monotonic() + timeout_sec
        selector = selectors.DefaultSelector()
        if proc.stdout is not None:
            selector.register(proc.stdout, selectors.EVENT_READ)

        timed_out = False
        process_done = False
        while True:
            if process_done:
                break
            now = time.monotonic()
            if now >= deadline:
                timed_out = True
                break

            events = selector.select(timeout=min(1.0, deadline - now))
            if not events:
                if proc.poll() is not None:
                    break
                continue

            for key, _ in events:
                line = key.fileobj.readline()
                if not line:
                    if proc.poll() is not None:
                        process_done = True
                        break
                    continue
                prep_line = parse_prep_line(line, transport, expected_sizes)
                if prep_line and progress_cb is not None:
                    line_size, connect_ms, ready_ms = prep_line
                    progress_cb(line_size, connect_ms, ready_ms)
                parsed_line = parse_result_line(line, transport, expected_sizes)
                if not parsed_line:
                    continue
                line_transport, line_size, metric, value = parsed_line
                parsed[f"{line_transport}|{line_size}|{metric}"] = value
                if metric_cb is not None:
                    metric_cb(line_size, metric, value)
                if progress_cb is not None and metric == "throughput":
                    progress_cb(line_size)

                if expected_metric_count > 0 and len(parsed) >= expected_metric_count:
                    if proc.poll() is None:
                        proc.terminate()
                        try:
                            proc.wait(timeout=2.0)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            proc.wait()
                    selector.close()
                    outcome["returncode"] = 0
                    return outcome

        if timed_out:
            proc.kill()
            rc = proc.wait()
            selector.close()
            outcome["timed_out"] = True
            outcome["returncode"] = rc
            return outcome

        selector.close()

        if proc.stdout is not None:
            tail = proc.stdout.read()
            if tail:
                for line in tail.splitlines():
                    prep_line = parse_prep_line(line, transport, expected_sizes)
                    if prep_line and progress_cb is not None:
                        line_size, connect_ms, ready_ms = prep_line
                        progress_cb(line_size, connect_ms, ready_ms)
                    parsed_line = parse_result_line(line, transport, expected_sizes)
                    if not parsed_line:
                        continue
                    line_transport, line_size, metric, value = parsed_line
                    parsed[f"{line_transport}|{line_size}|{metric}"] = value
                    if metric_cb is not None:
                        metric_cb(line_size, metric, value)
                    if progress_cb is not None and metric == "throughput":
                        progress_cb(line_size)

                    if expected_metric_count > 0 and len(parsed) >= expected_metric_count:
                        if proc.poll() is None:
                            proc.terminate()
                            try:
                                proc.wait(timeout=2.0)
                            except subprocess.TimeoutExpired:
                                proc.kill()
                                proc.wait()
                        outcome["returncode"] = 0
                        return outcome

        rc = proc.wait()
        outcome["returncode"] = rc
        return outcome
    except subprocess.TimeoutExpired:
        return {
            "parsed": {},
            "timed_out": True,
            "returncode": -1,
            "error": "timeout_expired",
        }
    except Exception as exc:
        return {
            "parsed": {},
            "timed_out": False,
            "returncode": -1,
            "error": f"exception_{type(exc).__name__}",
        }


def collect_data(
    binary_name, lib_name, pattern_name, num_runs, transports, run_cooldown_ms
):
    print(f"  > Benchmarking {lib_name} for {pattern_name}...")
    final_stats = {}
    failures = []

    for tr in transports:
        size_tag = ",".join(f"{sz}B" for sz in MSG_SIZES)
        print(f"    Testing {tr} | {size_tag}: ", end="", flush=True)
        if tr != "tcp":
            print("Skipped (unsupported_transport(libzmq_tcp_only))")
            continue
        metrics_raw = {}
        failed_runs = 0
        expected_keys = []
        for sz in MSG_SIZES:
            expected_keys.append(f"{tr}|{sz}|throughput")
            expected_keys.append(f"{tr}|{sz}|latency")

        for i in range(num_runs):
            print(f"{i+1} ", end="", flush=True)
            seen_sizes = set()
            live_metrics = {}
            reported_sizes = set()
            has_next_run = (i + 1) < num_runs

            def maybe_cooldown():
                if run_cooldown_ms <= 0 or not has_next_run:
                    return
                print(f"[cooldown={run_cooldown_ms}ms] ", end="", flush=True)
                time.sleep(run_cooldown_ms / 1000.0)

            def on_size_done(size, connect_ms=None, ready_ms=None):
                if size in seen_sizes:
                    return
                seen_sizes.add(size)
                if (
                    SHOW_PREP
                    and connect_ms is not None
                    and ready_ms is not None
                ):
                    print(
                        f"{size}B(c={connect_ms:.0f}ms,r={ready_ms:.0f}ms) ",
                        end="",
                        flush=True,
                    )
                else:
                    print(f"{size}B ", end="", flush=True)

            def on_metric(size, metric, value):
                bucket = live_metrics.setdefault(size, {})
                bucket[metric] = value
                if size in reported_sizes:
                    return
                if "throughput" in bucket and "latency" in bucket:
                    print(
                        f"{size}B[t={bucket['throughput']/1e3:.2f}K,l={bucket['latency']:.2f}us] ",
                        end="",
                        flush=True,
                    )
                    reported_sizes.add(size)

            run_outcome = run_single_test(
                binary_name, lib_name, tr, pattern_name, on_size_done, on_metric
            )
            results = run_outcome.get("parsed", {})
            rc = run_outcome.get("returncode", 0)
            timed_out = bool(run_outcome.get("timed_out", False))
            run_error = run_outcome.get("error", "")
            missing = [key for key in expected_keys if key not in results]

            if timed_out:
                failed_runs += 1
                failures.append((pattern_name, lib_name, tr, 0, "timeout"))
                if FAIL_FAST:
                    print("aborting")
                    return final_stats, failures
                maybe_cooldown()
                continue

            if run_error:
                failed_runs += 1
                failures.append((pattern_name, lib_name, tr, 0, run_error))
                if FAIL_FAST:
                    print("aborting")
                    return final_stats, failures
                maybe_cooldown()
                continue

            if not results:
                failed_runs += 1
                reason = f"no_data_rc_{rc}" if rc not in (0, None) else "no_data"
                failures.append((pattern_name, lib_name, tr, 0, reason))
                if FAIL_FAST:
                    print("aborting")
                    return final_stats, failures
                maybe_cooldown()
                continue

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
                    print("aborting")
                    return final_stats, failures

            for key, value in results.items():
                metrics_raw.setdefault(key, []).append(value)
            maybe_cooldown()

        for key, vals in metrics_raw.items():
            final_stats[key] = statistics.median(vals) if vals else 0

        if failed_runs:
            print(f"(failures={failed_runs}) ", end="", flush=True)
        print("Done")
    return final_stats, failures


def load_std_cache(path):
    if not path or not os.path.exists(path):
        return {}

    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def save_std_cache(path, cache_data):
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(cache_data, f, indent=2, sort_keys=True, ensure_ascii=True)


def sanitize_metric_map(raw):
    out = {}
    if not isinstance(raw, dict):
        return out
    for k, v in raw.items():
        try:
            out[str(k)] = float(v)
        except Exception:
            continue
    return out


def sanitize_failures(raw):
    if not isinstance(raw, list):
        return []
    out = []
    for item in raw:
        if isinstance(item, (list, tuple)):
            out.append(tuple(item))
        else:
            out.append((str(item),))
    return out


def get_cached_std_entry(cache_data, pattern_name):
    patterns = cache_data.get("patterns")
    if not isinstance(patterns, dict):
        return None
    entry = patterns.get(pattern_name)
    if not isinstance(entry, dict):
        return None
    std_data = sanitize_metric_map(entry.get("data", {}))
    std_fail = sanitize_failures(entry.get("failures", []))
    return std_data, std_fail


def has_valid_cached_metrics(std_data):
    if not isinstance(std_data, dict):
        return False
    for tr in TRANSPORTS:
        for sz in MSG_SIZES:
            k_t = f"{tr}|{sz}|throughput"
            k_l = f"{tr}|{sz}|latency"
            t = metric_or_none(std_data, k_t)
            l = metric_or_none(std_data, k_l)
            if t is None or l is None:
                return False
            if t <= 0.0 or l <= 0.0:
                return False
    return True


def update_cached_std_entry(cache_data, pattern_name, std_data, std_fail):
    patterns = cache_data.setdefault("patterns", {})
    patterns[pattern_name] = {
        "data": sanitize_metric_map(std_data),
        "failures": [list(x) for x in std_fail],
        "updated_at_utc": datetime.now(timezone.utc).isoformat(),
        "transports": list(TRANSPORTS),
        "msg_sizes": list(MSG_SIZES),
    }


def format_throughput(msgs_per_sec):
    return f"{msgs_per_sec/1e3:6.2f} Kmsg/s"


def metric_or_none(metric_map, key):
    if key not in metric_map:
        return None
    try:
        return float(metric_map[key])
    except Exception:
        return None


def print_pattern_report(std_data, zlk_data, std_available=True):
    size_w = 6
    metric_w = 10
    val_w = 16
    diff_w = 9

    for tr in TRANSPORTS:
        print(f"\n### Transport: {tr}")
        print(
            f"| {'Size':<{size_w}} | {'Metric':<{metric_w}} | {'Standard libzmq':>{val_w}} | {'zlink':>{val_w}} | {'Diff (%)':>{diff_w}} |"
        )
        print(
            f"|{'-' * (size_w + 2)}|{'-' * (metric_w + 2)}|{'-' * (val_w + 2)}|{'-' * (val_w + 2)}|{'-' * (diff_w + 2)}|"
        )

        for sz in MSG_SIZES:
            k_t = f"{tr}|{sz}|throughput"
            k_l = f"{tr}|{sz}|latency"
            zlk_t = metric_or_none(zlk_data, k_t)
            zlk_l = metric_or_none(zlk_data, k_l)

            zlk_t_s = format_throughput(zlk_t) if zlk_t is not None else "N/A"
            zlk_l_s = f"{zlk_l:8.2f} us" if zlk_l is not None else "N/A"
            if std_available:
                std_t = metric_or_none(std_data, k_t)
                std_l = metric_or_none(std_data, k_l)
                std_t_s = format_throughput(std_t) if std_t is not None else "N/A"
                std_l_s = f"{std_l:8.2f} us" if std_l is not None else "N/A"
                if std_t is not None and zlk_t is not None and std_t > 0:
                    t_diff = (zlk_t - std_t) / std_t * 100.0
                    t_diff_s = f"{t_diff:>+7.2f}%"
                else:
                    t_diff_s = "N/A"
                if std_l is not None and zlk_l is not None and std_l > 0:
                    l_diff = (std_l - zlk_l) / std_l * 100.0
                    l_diff_s = f"{l_diff:>+7.2f}%"
                else:
                    l_diff_s = "N/A"
            else:
                std_t_s = "N/A"
                std_l_s = "N/A"
                t_diff_s = "N/A"
                l_diff_s = "N/A"

            print(
                f"| {f'{sz}B':<{size_w}} | {'Throughput':<{metric_w}} | {std_t_s:>{val_w}} | {zlk_t_s:>{val_w}} | {t_diff_s:>{diff_w}} |"
            )
            print(
                f"| {f'{sz}B':<{size_w}} | {'Latency':<{metric_w}} | {std_l_s:>{val_w}} | {zlk_l_s:>{val_w}} | {l_diff_s:>{diff_w}} |"
            )


def normalize_pattern_name(raw):
    if raw is None:
        return None
    token = str(raw).strip().upper()
    if not token:
        return None
    if token == "ALL":
        return "ALL"
    if token.startswith("MULTI_"):
        token = token[len("MULTI_") :]
    elif token.startswith("MULT_"):
        token = token[len("MULT_") :]

    aliases = {
        "DEALER_DEALER": "MULTI_DEALER_DEALER",
        "DEALER_ROUTER": "MULTI_DEALER_ROUTER",
        "ROUTER_ROUTER": "MULTI_ROUTER_ROUTER",
        "PUBSUB": "MULTI_PUBSUB",
        "STREAM": "MULTI_STREAM",
    }
    return aliases.get(token)


def parse_args():
    usage = (
        "Usage: multi/run_comparison.py [PATTERN] [options]\n\n"
        "Options:\n"
        "  --zlink-only            Re-measure only zlink (use cached libzmq when available)\n"
        "  --refresh-std-cache     Refresh libzmq cache with current measurements\n"
        "  --std-cache-file PATH   libzmq cache file path (default: platform cache in multi/)\n"
        "  --runs N                Iterations per configuration (default: 3)\n"
        "  --run-cooldown-ms N     Sleep between runs (default: BENCH_MULTI_RUN_COOLDOWN_MS)\n"
        "  --build-dir PATH        Build directory\n"
        "  --pin-cpu               Pin CPU core during benchmarks (Linux taskset)\n"
        "  -h, --help              Show this help\n"
        "\n"
        "PATTERN:\n"
        "  dealer_dealer | dealer_router | router_router | pubsub | stream\n"
        "  (legacy MULTI_* names are also accepted)\n"
        "  Missing benchmark binaries are auto-built via cmake --build.\n"
        "\n"
        "Env:\n"
        "  BENCH_TRANSPORTS=tcp\n"
        "  BENCH_MSG_SIZES=1024\n"
        "  BENCH_MULTI_CLIENTS=100\n"
        "  BENCH_MULTI_INFLIGHT=30  (per-client, global=clients*inflight)\n"
        "  BENCH_MULTI_HWM=100000\n"
        "  BENCH_MULTI_SNDTIMEO_MS=5000\n"
        "  BENCH_MULTI_RCVTIMEO_MS=5000\n"
        "  BENCH_MULTI_MONITOR_HWM=100000\n"
        "  BENCH_MULTI_CONNECT_READY_TIMEOUT_MS=5000\n"
        "  BENCH_MULTI_WARMUP_SECONDS=3\n"
        "  BENCH_MULTI_DURATION_SECONDS=5\n"
        "  BENCH_MULTI_SETTLE_MS=500\n"
        "  BENCH_MULTI_DRAIN_MS=300  (pattern exceptions) / 0 (default)\n"
        "  BENCH_MULTI_SIZE_TRANSITION_DRAIN_MS=300\n"
        "  BENCH_MULTI_RECV_BATCH=64\n"
        "  BENCH_MULTI_SEND_WORKERS=auto\n"
        "  BENCH_MULTI_SEND_BACKOFF_US=20\n"
        "  BENCH_MULTI_RUN_COOLDOWN_MS=3000\n"
        "  BENCH_IO_THREADS=4\n"
        "  BENCH_MULTI_TIMEOUT_SECONDS=600\n"
    )

    pattern = "ALL"
    num_runs = DEFAULT_NUM_RUNS
    run_cooldown_ms = DEFAULT_RUN_COOLDOWN_MS
    build_dir = ""
    zlink_only = False
    pin_cpu = False
    refresh_std_cache = False
    std_cache_file = ""

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
            if i + 1 >= len(sys.argv):
                print("Error: --std-cache-file requires a value", file=sys.stderr)
                sys.exit(1)
            std_cache_file = sys.argv[i + 1]
            i += 1
        elif arg == "--pin-cpu":
            pin_cpu = True
        elif arg == "--runs":
            if i + 1 >= len(sys.argv):
                print("Error: --runs requires a value", file=sys.stderr)
                sys.exit(1)
            num_runs = int(sys.argv[i + 1])
            i += 1
        elif arg.startswith("--runs="):
            num_runs = int(arg.split("=", 1)[1])
        elif arg == "--run-cooldown-ms":
            if i + 1 >= len(sys.argv):
                print("Error: --run-cooldown-ms requires a value", file=sys.stderr)
                sys.exit(1)
            run_cooldown_ms = max(0, int(sys.argv[i + 1]))
            i += 1
        elif arg.startswith("--run-cooldown-ms="):
            run_cooldown_ms = max(0, int(arg.split("=", 1)[1]))
        elif arg == "--build-dir":
            if i + 1 >= len(sys.argv):
                print("Error: --build-dir requires a value", file=sys.stderr)
                sys.exit(1)
            build_dir = sys.argv[i + 1]
            i += 1
        elif not arg.startswith("--") and pattern == "ALL":
            normalized = normalize_pattern_name(arg)
            if not normalized:
                print(
                    f"Error: unsupported pattern '{arg}'. "
                    "Use router_router (or MULTI_ROUTER_ROUTER).",
                    file=sys.stderr,
                )
                sys.exit(1)
            pattern = normalized
        i += 1

    if num_runs < 1:
        print("Error: --runs must be >= 1", file=sys.stderr)
        sys.exit(1)

    return (
        pattern,
        num_runs,
        run_cooldown_ms,
        build_dir,
        zlink_only,
        pin_cpu,
        refresh_std_cache,
        std_cache_file,
    )


def main():
    global BUILD_DIR, ZLINK_LIB_DIR, base_env

    (
        pattern_req,
        num_runs,
        run_cooldown_ms,
        build_dir,
        zlink_only,
        pin_cpu,
        refresh_std_cache,
        std_cache_file,
    ) = parse_args()
    BUILD_DIR = normalize_build_dir(build_dir) if build_dir else normalize_build_dir(BUILD_DIR)
    ZLINK_LIB_DIR = derive_zlink_lib_dir(BUILD_DIR)
    cache_file = os.path.abspath(std_cache_file) if std_cache_file else DEFAULT_STD_CACHE_FILE
    std_cache = load_std_cache(cache_file)

    if pin_cpu:
        base_env["BENCH_TASKSET"] = "1"

    if zlink_only and refresh_std_cache:
        print(
            "Info: --zlink-only with --refresh-std-cache will refresh libzmq baselines first.",
            flush=True,
        )

    comparisons = [
        ("comp_std_zmq_multi_dealer_dealer", "comp_zlink_multi_dealer_dealer", "MULTI_DEALER_DEALER"),
        ("comp_std_zmq_multi_dealer_router", "comp_zlink_multi_dealer_router", "MULTI_DEALER_ROUTER"),
        ("comp_std_zmq_multi_router_router", "comp_zlink_multi_router_router", "MULTI_ROUTER_ROUTER"),
        ("comp_std_zmq_multi_pubsub", "comp_zlink_multi_pubsub", "MULTI_PUBSUB"),
        ("comp_std_zmq_multi_stream", "comp_zlink_multi_stream", "MULTI_STREAM"),
    ]

    supported = sorted({p for _, _, p in comparisons})
    if pattern_req != "ALL" and pattern_req not in supported:
        print(
            f"Error: unsupported pattern '{pattern_req}'. Supported: {', '.join(supported)}",
            file=sys.stderr,
        )
        return 2

    selected_comparisons = select_comparisons(comparisons, pattern_req)
    missing_cache_patterns = []
    for _, _, p_name in selected_comparisons:
        cached_entry = get_cached_std_entry(std_cache, p_name)
        if cached_entry is None or not has_valid_cached_metrics(cached_entry[0]):
            missing_cache_patterns.append(p_name)
    need_std_baseline = (not zlink_only) or refresh_std_cache or bool(
        missing_cache_patterns
    )

    expected_bins = expected_runtime_binaries(selected_comparisons, need_std_baseline)
    missing = collect_missing_binaries(BUILD_DIR, expected_bins)

    if missing:
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
            for name in missing:
                print(f"  - {name}{EXE_SUFFIX}", file=sys.stderr)
            return 2

        configure_rc = run_cmake_configure(cmake_build_dir)
        if configure_rc != 0:
            print(
                f"Error: auto-configure failed with exit code {configure_rc}",
                file=sys.stderr,
            )
            return configure_rc

        BUILD_DIR = normalize_build_dir(runtime_bin_dir_from_cmake_dir(cmake_build_dir))
        ZLINK_LIB_DIR = derive_zlink_lib_dir(BUILD_DIR)
        build_targets = collect_multi_build_targets(
            selected_comparisons, need_std_baseline
        )
        build_rc = run_cmake_build(cmake_build_dir, build_targets)
        if build_rc != 0:
            print(
                f"Error: auto-build failed with exit code {build_rc}",
                file=sys.stderr,
            )
            return build_rc

        missing = collect_missing_binaries(BUILD_DIR, expected_bins)

    if missing:
        print(f"Error: missing benchmark binaries in {BUILD_DIR}:", file=sys.stderr)
        for name in missing:
            print(f"  - {name}{EXE_SUFFIX}", file=sys.stderr)
        return 2

    any_failure = False
    all_failures = []
    cache_updated = False

    printed_pattern = False
    for std_bin, zlk_bin, p_name in comparisons:
        if pattern_req != "ALL" and p_name != pattern_req:
            continue

        if printed_pattern:
            print("")
            print(PATTERN_SEPARATOR)
            print("")
        else:
            print("")
        print(f"## PATTERN: {p_name}")
        printed_pattern = True

        std_data = {}
        std_fail = []
        zlk_data = {}
        zlk_fail = []
        cached = get_cached_std_entry(std_cache, p_name)
        cached_valid = cached is not None and has_valid_cached_metrics(cached[0])
        if zlink_only:
            if refresh_std_cache or not cached_valid:
                reason = (
                    "refresh requested"
                    if refresh_std_cache
                    else "baseline missing/invalid"
                )
                print(
                    f"  > Cached libzmq {reason} for {p_name}; "
                    "measuring and caching libzmq baseline now."
                )
                std_data, std_fail = collect_data(
                    std_bin,
                    "libzmq",
                    p_name,
                    num_runs,
                    TRANSPORTS,
                    run_cooldown_ms,
                )
                update_cached_std_entry(std_cache, p_name, std_data, std_fail)
                cache_updated = True
            else:
                std_data, std_fail = cached
                print(f"  > Using cached libzmq for {p_name} from {cache_file}")
            zlk_data, zlk_fail = collect_data(
                zlk_bin,
                "zlink",
                p_name,
                num_runs,
                TRANSPORTS,
                run_cooldown_ms,
            )
        else:
            std_data, std_fail = collect_data(
                std_bin,
                "libzmq",
                p_name,
                num_runs,
                TRANSPORTS,
                run_cooldown_ms,
            )
            if refresh_std_cache or cached is None:
                update_cached_std_entry(std_cache, p_name, std_data, std_fail)
                cache_updated = True
            zlk_data, zlk_fail = collect_data(
                zlk_bin,
                "zlink",
                p_name,
                num_runs,
                TRANSPORTS,
                run_cooldown_ms,
            )

        print_pattern_report(std_data, zlk_data, std_available=True)
        all_failures.extend(std_fail)
        all_failures.extend(zlk_fail)

        if std_fail or zlk_fail:
            any_failure = True

    if cache_updated:
        std_cache["meta"] = {
            "build_dir": BUILD_DIR,
            "libzmq_lib_dir": LIBZMQ_LIB_DIR,
            "platform_tag": _platform_tag,
            "arch_tag": _arch_tag,
            "updated_at_utc": datetime.now(timezone.utc).isoformat(),
        }
        save_std_cache(cache_file, std_cache)
        print(f"Saved libzmq cache: {cache_file}")

    if all_failures:
        print("\n## Failures")
        for failure in all_failures:
            if len(failure) >= 5:
                pattern, lib_name, tr, sz, reason = failure[:5]
                print(f"- {pattern} {lib_name} {tr} {sz}B: {reason}")
            else:
                print(f"- {failure}")
    return 1 if any_failure else 0


if __name__ == "__main__":
    raise SystemExit(main())
