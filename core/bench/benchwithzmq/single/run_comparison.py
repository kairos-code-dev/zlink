#!/usr/bin/env python3
import json
import os
import platform
import statistics
import subprocess
import sys
from datetime import datetime, timezone

IS_WINDOWS = os.name == "nt"
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""
SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", "..", ".."))
BUILD_CONFIG_DIRS = ("Release", "Debug", "RelWithDebInfo", "MinSizeRel")


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
        ROOT_DIR, "core", "bench", "benchwithzmq", "libzmq", "libzmq_dist", "lib"
    )
    libzmq_lib_dir = os.path.abspath(
        linux_dist_dir if os.path.exists(linux_dist_dir) else default_dist_dir
    )
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
            return os.path.join(abs_path, "Release")
        return abs_path

    bin_dir = os.path.join(abs_path, "bin")
    if os.path.isdir(bin_dir):
        if IS_WINDOWS:
            return os.path.join(bin_dir, "Release")
        return bin_dir

    if os.path.isfile(os.path.join(abs_path, "CMakeCache.txt")):
        return runtime_bin_dir_from_cmake_dir(abs_path)

    return abs_path


def runtime_bin_dir_from_cmake_dir(cmake_build_dir):
    bin_dir = os.path.join(cmake_build_dir, "bin")
    if IS_WINDOWS:
        return os.path.join(bin_dir, "Release")
    return bin_dir


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
    if base == "bin":
        return os.path.dirname(abs_path)

    parent = os.path.dirname(abs_path)
    if os.path.basename(parent) == "bin":
        return os.path.dirname(parent)
    return abs_path


def expected_runtime_binaries(selected_comparisons):
    names = []
    for std_bin, zlk_bin, _ in selected_comparisons:
        names.append(std_bin)
        names.append(zlk_bin)
    return names


def collect_missing_binaries(runtime_bin_dir, names):
    missing = []
    for name in names:
        bin_path = os.path.join(runtime_bin_dir, name + EXE_SUFFIX)
        if not os.path.exists(bin_path):
            missing.append(name)
    return sorted(set(missing))


def collect_single_build_targets(comparisons):
    targets = []
    for std_bin, zlk_bin, _ in comparisons:
        if std_bin not in targets:
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


if IS_WINDOWS:
    BUILD_DIR = os.path.join(ROOT_DIR, "core", "build", "windows-x64", "bin", "Release")
    LIBZMQ_LIB_DIR = os.path.join(
        ROOT_DIR,
        "core",
        "bench",
        "benchwithzmq",
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

DEFAULT_NUM_RUNS = 1
_platform_tag, _arch_tag = platform_arch_tag()
DEFAULT_STD_CACHE_FILE = os.path.join(
    SCRIPT_DIR, f"std_zmq_single_cache_{_platform_tag}-{_arch_tag}.json"
)
DEFAULT_MSG_SIZES = [64, 256, 1024, 65536, 131072, 262144]
if IS_WINDOWS:
    DEFAULT_TRANSPORTS = ["tcp", "inproc"]
else:
    DEFAULT_TRANSPORTS = ["tcp", "inproc", "ipc"]


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
TRANSPORTS = _env_transports if _env_transports else list(DEFAULT_TRANSPORTS)

_env_sizes = parse_env_list("BENCH_MSG_SIZES", int)
MSG_SIZES = _env_sizes if _env_sizes else list(DEFAULT_MSG_SIZES)

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


def parse_result_line(line, transport, expected_size):
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
    if line_transport != transport or line_size != expected_size:
        return None
    if metric not in ("throughput", "latency"):
        return None
    return line_transport, line_size, metric, value


def run_single_test(binary_name, lib_name, transport, size):
    binary_path = os.path.join(BUILD_DIR, binary_name + EXE_SUFFIX)
    env = get_env_for_lib(lib_name)
    timeout_sec = max(60, int(os.environ.get("BENCH_SINGLE_TIMEOUT_SECONDS", "120")))

    try:
        if IS_WINDOWS:
            cmd = [binary_path, lib_name, transport, str(size)]
        elif os.environ.get("BENCH_TASKSET") == "1":
            cmd = ["taskset", "-c", "1", binary_path, lib_name, transport, str(size)]
        else:
            cmd = [binary_path, lib_name, transport, str(size)]

        result = subprocess.run(
            cmd, env=env, capture_output=True, text=True, timeout=timeout_sec
        )
        parsed = {}
        for line in result.stdout.splitlines():
            parsed_line = parse_result_line(line, transport, size)
            if not parsed_line:
                continue
            line_transport, line_size, metric, value = parsed_line
            parsed[f"{line_transport}|{line_size}|{metric}"] = value
        return {
            "parsed": parsed,
            "timed_out": False,
            "returncode": result.returncode,
            "error": "",
        }
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


def collect_data(binary_name, lib_name, pattern_name, num_runs, transports):
    print(f"  > Benchmarking {lib_name} for {pattern_name}...")
    final_stats = {}
    failures = []

    for tr in transports:
        for sz in MSG_SIZES:
            print(f"    Testing {tr} | {sz}B: ", end="", flush=True)
            metric_buckets = {"throughput": [], "latency": []}
            failed_runs = 0
            expected_throughput = f"{tr}|{sz}|throughput"
            expected_latency = f"{tr}|{sz}|latency"

            for i in range(num_runs):
                print(f"{i+1} ", end="", flush=True)
                run_outcome = run_single_test(binary_name, lib_name, tr, sz)
                results = run_outcome.get("parsed", {})
                rc = run_outcome.get("returncode", 0)
                timed_out = bool(run_outcome.get("timed_out", False))
                run_error = run_outcome.get("error", "")

                if timed_out:
                    failed_runs += 1
                    failures.append((pattern_name, lib_name, tr, sz, "timeout"))
                    continue
                if run_error:
                    failed_runs += 1
                    failures.append((pattern_name, lib_name, tr, sz, run_error))
                    continue
                if not results:
                    failed_runs += 1
                    reason = f"no_data_rc_{rc}" if rc not in (0, None) else "no_data"
                    failures.append((pattern_name, lib_name, tr, sz, reason))
                    continue

                missing = []
                if expected_throughput not in results:
                    missing.append("throughput")
                if expected_latency not in results:
                    missing.append("latency")
                if missing:
                    failed_runs += 1
                    suffix = f"_rc_{rc}" if rc not in (0, None) else ""
                    for metric in missing:
                        failures.append(
                            (pattern_name, lib_name, tr, sz, f"missing_{metric}{suffix}")
                        )
                    continue

                metric_buckets["throughput"].append(results[expected_throughput])
                metric_buckets["latency"].append(results[expected_latency])

            if metric_buckets["throughput"]:
                final_stats[expected_throughput] = statistics.median(
                    metric_buckets["throughput"]
                )
            if metric_buckets["latency"]:
                final_stats[expected_latency] = statistics.median(
                    metric_buckets["latency"]
                )

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


def print_pattern_report(std_data, zlk_data, transports):
    size_w = 6
    metric_w = 10
    val_w = 16
    diff_w = 9

    for tr in transports:
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

            std_t = metric_or_none(std_data, k_t)
            std_l = metric_or_none(std_data, k_l)
            zlk_t = metric_or_none(zlk_data, k_t)
            zlk_l = metric_or_none(zlk_data, k_l)

            std_t_s = format_throughput(std_t) if std_t is not None else "N/A"
            std_l_s = f"{std_l:8.2f} us" if std_l is not None else "N/A"
            zlk_t_s = format_throughput(zlk_t) if zlk_t is not None else "N/A"
            zlk_l_s = f"{zlk_l:8.2f} us" if zlk_l is not None else "N/A"

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

            print(
                f"| {f'{sz}B':<{size_w}} | {'Throughput':<{metric_w}} | {std_t_s:>{val_w}} | {zlk_t_s:>{val_w}} | {t_diff_s:>{diff_w}} |"
            )
            print(
                f"| {f'{sz}B':<{size_w}} | {'Latency':<{metric_w}} | {std_l_s:>{val_w}} | {zlk_l_s:>{val_w}} | {l_diff_s:>{diff_w}} |"
            )


def normalize_pattern_name(raw):
    token = str(raw).strip().upper()
    if not token:
        return None
    if token == "ALL":
        return "ALL"
    if token.startswith("SINGLE_"):
        token = token[len("SINGLE_") :]
    aliases = {
        "PAIR": "PAIR",
        "PUBSUB": "PUBSUB",
        "DEALER_DEALER": "DEALER_DEALER",
        "DEALER_ROUTER": "DEALER_ROUTER",
        "ROUTER_ROUTER": "ROUTER_ROUTER",
        "ROUTER_ROUTER_POLL": "ROUTER_ROUTER_POLL",
        "STREAM": "STREAM",
    }
    return aliases.get(token)


def parse_list_arg(raw, normalize_fn):
    out = []
    for part in str(raw).split(","):
        normalized = normalize_fn(part)
        if not normalized:
            return None
        out.append(normalized)
    return out


def usage_text():
    return (
        "Usage: single/run_comparison.py [options]\n\n"
        "Options:\n"
        "  --patterns LIST         Pattern list (default: ALL)\n"
        "  --pattern NAME          Alias for one pattern\n"
        "  --runs N                Iterations per configuration (default: 1)\n"
        "  --transport LIST        Transport list (default: tcp,inproc,ipc)\n"
        "  --transports LIST       Alias for --transport\n"
        "  --msg-sizes LIST        Message sizes (default: 64,256,1024,65536,131072,262144)\n"
        "  --build-dir PATH        Build directory\n"
        "  --pin-cpu               Pin benchmarks to CPU core 1 (Linux taskset)\n"
        "  -h, --help              Show this help\n"
    )


def parse_args():
    patterns = ["ALL"]
    num_runs = DEFAULT_NUM_RUNS
    transports = list(TRANSPORTS)
    msg_sizes = list(MSG_SIZES)
    build_dir = ""
    pin_cpu = False

    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg in ("-h", "--help"):
            print(usage_text())
            sys.exit(0)
        if arg in ("--patterns", "--pattern"):
            if i + 1 >= len(sys.argv):
                print(f"Error: {arg} requires a value", file=sys.stderr)
                sys.exit(1)
            parsed = parse_list_arg(sys.argv[i + 1], normalize_pattern_name)
            if not parsed:
                print(f"Error: invalid pattern list '{sys.argv[i + 1]}'", file=sys.stderr)
                sys.exit(1)
            patterns = parsed
            i += 1
        elif arg == "--runs":
            if i + 1 >= len(sys.argv):
                print("Error: --runs requires a value", file=sys.stderr)
                sys.exit(1)
            num_runs = int(sys.argv[i + 1])
            i += 1
        elif arg.startswith("--runs="):
            num_runs = int(arg.split("=", 1)[1])
        elif arg in ("--transport", "--transports"):
            if i + 1 >= len(sys.argv):
                print(f"Error: {arg} requires a value", file=sys.stderr)
                sys.exit(1)
            parsed = parse_env_list_value(sys.argv[i + 1], str)
            if not parsed:
                print(f"Error: invalid transport list '{sys.argv[i + 1]}'", file=sys.stderr)
                sys.exit(1)
            transports = parsed
            i += 1
        elif arg == "--msg-sizes":
            if i + 1 >= len(sys.argv):
                print("Error: --msg-sizes requires a value", file=sys.stderr)
                sys.exit(1)
            parsed = parse_env_list_value(sys.argv[i + 1], int)
            if not parsed:
                print(f"Error: invalid msg size list '{sys.argv[i + 1]}'", file=sys.stderr)
                sys.exit(1)
            msg_sizes = parsed
            i += 1
        elif arg == "--build-dir":
            if i + 1 >= len(sys.argv):
                print("Error: --build-dir requires a value", file=sys.stderr)
                sys.exit(1)
            build_dir = sys.argv[i + 1]
            i += 1
        elif arg == "--pin-cpu":
            pin_cpu = True
        else:
            print(f"Error: unknown option '{arg}'", file=sys.stderr)
            sys.exit(1)
        i += 1

    if num_runs < 1:
        print("Error: --runs must be >= 1", file=sys.stderr)
        sys.exit(1)
    return patterns, num_runs, transports, msg_sizes, build_dir, pin_cpu


def parse_env_list_value(raw, cast_fn):
    out = []
    for part in str(raw).split(","):
        part = part.strip()
        if not part:
            continue
        try:
            out.append(cast_fn(part))
        except ValueError:
            return None
    return out or None


def main():
    global BUILD_DIR, ZLINK_LIB_DIR, TRANSPORTS, MSG_SIZES
    global base_env

    patterns_req, num_runs, transports, msg_sizes, build_dir, pin_cpu = parse_args()

    if build_dir:
        BUILD_DIR = normalize_build_dir(build_dir)
        ZLINK_LIB_DIR = derive_zlink_lib_dir(BUILD_DIR)
    else:
        BUILD_DIR = normalize_build_dir(BUILD_DIR)
        ZLINK_LIB_DIR = derive_zlink_lib_dir(BUILD_DIR)

    TRANSPORTS = transports
    MSG_SIZES = msg_sizes

    if pin_cpu:
        base_env["BENCH_TASKSET"] = "1"

    comparisons = [
        ("comp_std_zmq_pair", "comp_zlink_pair", "PAIR"),
        ("comp_std_zmq_pubsub", "comp_zlink_pubsub", "PUBSUB"),
        ("comp_std_zmq_dealer_dealer", "comp_zlink_dealer_dealer", "DEALER_DEALER"),
        ("comp_std_zmq_dealer_router", "comp_zlink_dealer_router", "DEALER_ROUTER"),
        ("comp_std_zmq_router_router", "comp_zlink_router_router", "ROUTER_ROUTER"),
        (
            "comp_std_zmq_router_router_poll",
            "comp_zlink_router_router_poll",
            "ROUTER_ROUTER_POLL",
        ),
        ("comp_std_zmq_stream", "comp_zlink_stream", "STREAM"),
    ]

    if len(patterns_req) == 1 and patterns_req[0] == "ALL":
        selected = comparisons
    else:
        req_set = set(patterns_req)
        selected = [item for item in comparisons if item[2] in req_set]
        if len(selected) != len(req_set):
            supported = sorted([p for _, _, p in comparisons])
            print(
                f"Error: unsupported pattern in {patterns_req}. Supported: {', '.join(supported)}",
                file=sys.stderr,
            )
            return 2

    expected_bins = expected_runtime_binaries(selected)
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
        build_targets = collect_single_build_targets(selected)
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
        for name in sorted(set(missing)):
            print(f"  - {name}{EXE_SUFFIX}", file=sys.stderr)
        return 2

    cache_file = os.path.abspath(
        os.environ.get("BENCH_STD_CACHE_FILE", "") or DEFAULT_STD_CACHE_FILE
    )
    std_cache = load_std_cache(cache_file)
    cache_updated = False

    all_failures = []
    any_failure = False

    for std_bin, zlk_bin, pattern_name in selected:
        print(f"\n## PATTERN: {pattern_name}")
        std_data, std_fail = collect_data(std_bin, "libzmq", pattern_name, num_runs, TRANSPORTS)
        update_cached_std_entry(std_cache, pattern_name, std_data, std_fail)
        cache_updated = True
        zlk_data, zlk_fail = collect_data(zlk_bin, "zlink", pattern_name, num_runs, TRANSPORTS)
        print_pattern_report(std_data, zlk_data, TRANSPORTS)

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
        for pattern, lib_name, tr, sz, reason in all_failures:
            print(f"- {pattern} {lib_name} {tr} {sz}B: {reason}")

    return 1 if any_failure else 0


if __name__ == "__main__":
    raise SystemExit(main())
