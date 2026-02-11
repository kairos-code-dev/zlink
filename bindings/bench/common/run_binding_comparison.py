#!/usr/bin/env python3
import json
import os
import platform
import statistics
import subprocess
import sys

IS_WINDOWS = os.name == "nt"
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""
SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "../../.."))

SUPPORTED = {
    "PAIR": {
        "core_bin": "comp_current_pair",
    },
    "PUBSUB": {
        "core_bin": "comp_current_pubsub",
    },
    "DEALER_DEALER": {
        "core_bin": "comp_current_dealer_dealer",
    },
    "DEALER_ROUTER": {
        "core_bin": "comp_current_dealer_router",
    },
    "ROUTER_ROUTER": {
        "core_bin": "comp_current_router_router",
    },
    "ROUTER_ROUTER_POLL": {
        "core_bin": "comp_current_router_router_poll",
    },
    "STREAM": {
        "core_bin": "comp_current_stream",
    },
    "GATEWAY": {
        "core_bin": "comp_current_gateway",
    },
    "SPOT": {
        "core_bin": "comp_current_spot",
    },
}


def platform_arch_tag():
    sys_name = platform.system().lower()
    if "darwin" in sys_name:
        p = "macos"
    elif "windows" in sys_name:
        p = "windows"
    else:
        p = "linux"
    m = platform.machine().lower()
    if m in ("x86_64", "amd64"):
        a = "x64"
    elif m in ("aarch64", "arm64"):
        a = "arm64"
    else:
        a = m
    return p, a


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


def select_transports():
    env_t = parse_env_list("BENCH_TRANSPORTS", str)
    base = ["tcp", "ws", "inproc"]
    if not env_t:
        return base
    return [t for t in base if t in env_t]


def select_pattern_transports(pattern):
    env_t = parse_env_list("BENCH_TRANSPORTS", str)
    # STREAM/GATEWAY/SPOT do not use inproc in core benchmarks.
    if pattern in ("STREAM", "GATEWAY", "SPOT"):
        base = ["tcp", "ws"]
    else:
        base = ["tcp", "ws", "inproc"]
    if not env_t:
        return base
    return [t for t in base if t in env_t]


def msg_sizes():
    env_s = parse_env_list("BENCH_MSG_SIZES", int)
    if env_s:
        return env_s
    return [64, 256, 1024, 65536, 131072, 262144]


def parse_args():
    usage = (
        "Usage: run_binding_comparison.py [PATTERN] [options]\n\n"
        "Options:\n"
        "  --binding NAME          binding name (python|node|dotnet|java|cpp)\n"
        "  --refresh-baseline      Refresh zlink(core) cache\n"
        "  --refresh-libzlink      Alias for --refresh-baseline\n"
        "  --current-only          Run only binding benchmarks\n"
        "  --zlink-only            Alias for --current-only\n"
        "  --runs N                Iterations per configuration (default: 3)\n"
        "  --build-dir PATH        Core bench build directory\n"
        "  --pin-cpu               Pin CPU core via BENCH_TASKSET=1\n"
        "  --allow-core-fallback   Allow core fallback when binding result rows are missing\n"
    )
    refresh = False
    p_req = "ALL"
    num_runs = 3
    build_dir = ""
    current_only = False
    pin_cpu = False
    allow_core_fallback = False
    binding = ""

    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg in ("-h", "--help"):
            print(usage)
            sys.exit(0)
        if arg in ("--refresh-baseline", "--refresh-libzlink"):
            refresh = True
        elif arg in ("--current-only", "--zlink-only"):
            current_only = True
        elif arg == "--pin-cpu":
            pin_cpu = True
        elif arg == "--allow-core-fallback":
            allow_core_fallback = True
        elif arg == "--binding":
            if i + 1 >= len(sys.argv):
                print("Error: --binding requires a value", file=sys.stderr)
                sys.exit(1)
            binding = sys.argv[i + 1]
            i += 1
        elif arg == "--runs":
            if i + 1 >= len(sys.argv):
                print("Error: --runs requires a value", file=sys.stderr)
                sys.exit(1)
            num_runs = int(sys.argv[i + 1])
            i += 1
        elif arg == "--build-dir":
            if i + 1 >= len(sys.argv):
                print("Error: --build-dir requires a value", file=sys.stderr)
                sys.exit(1)
            build_dir = os.path.abspath(sys.argv[i + 1])
            i += 1
        elif not arg.startswith("--") and p_req == "ALL":
            p_req = arg.upper()
        i += 1

    if not binding:
        print("Error: --binding is required", file=sys.stderr)
        sys.exit(1)
    if num_runs < 1:
        print("Error: --runs must be >= 1", file=sys.stderr)
        sys.exit(1)
    return p_req, refresh, num_runs, build_dir, current_only, pin_cpu, binding, allow_core_fallback


def binding_runner_cmd(binding):
    if binding == "python":
        py = "python3" if shutil_which("python3") else "python"
        return [py, os.path.join(ROOT_DIR, "bindings/python/benchwithzlink/pair_bench.py")]
    if binding == "node":
        return ["node", os.path.join(ROOT_DIR, "bindings/node/benchwithzlink/pair_bench.js")]
    if binding == "dotnet":
        dll = os.path.join(
            ROOT_DIR,
            "bindings/dotnet/benchwithzlink/Zlink.BindingBench/bin/Release/net8.0/Zlink.BindingBench.dll",
        )
        return ["dotnet", dll]
    if binding == "java":
        cp = ":".join([
            os.path.join(ROOT_DIR, "bindings/java/build/classes/java/main"),
            os.path.join(ROOT_DIR, "bindings/java/build/classes/java/test"),
            os.path.join(ROOT_DIR, "bindings/java/build/resources/main"),
        ])
        return ["java", "-cp", cp, "io.ulalax.zlink.integration.bench.PairBenchMain"]
    if binding == "cpp":
        return [os.path.join(ROOT_DIR, "bindings/cpp/benchwithzlink/build/pair_bench")]
    raise ValueError(f"Unsupported binding: {binding}")


def binding_env(binding, env):
    out = dict(env)
    p_tag, a_tag = platform_arch_tag()
    arch = "x86_64" if a_tag == "x64" else "aarch64"
    if p_tag == "linux":
        if binding == "python":
            out["ZLINK_LIBRARY_PATH"] = os.path.join(
                ROOT_DIR, f"bindings/python/src/zlink/native/linux-{arch}/libzlink.so"
            )
        elif binding == "dotnet":
            native_dir = os.path.join(ROOT_DIR, f"bindings/dotnet/runtimes/linux-{a_tag}/native")
            out["ZLINK_LIBRARY_PATH"] = os.path.join(native_dir, "libzlink.so")
            out["LD_LIBRARY_PATH"] = native_dir + ":" + out.get("LD_LIBRARY_PATH", "")
        elif binding == "java":
            out["ZLINK_LIBRARY_PATH"] = os.path.join(
                ROOT_DIR, f"bindings/java/src/main/resources/native/linux-{arch}/libzlink.so"
            )
        elif binding == "cpp":
            native_dir = os.path.join(ROOT_DIR, f"bindings/cpp/native/linux-{arch}")
            out["LD_LIBRARY_PATH"] = native_dir + ":" + out.get("LD_LIBRARY_PATH", "")
    return out


def shutil_which(cmd):
    for p in os.environ.get("PATH", "").split(os.pathsep):
        if not p:
            continue
        full = os.path.join(p, cmd)
        if os.path.isfile(full) and os.access(full, os.X_OK):
            return full
    return None


def run_and_parse(cmd, env):
    try:
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return None
    if r.returncode != 0:
        return []
    out = []
    for line in r.stdout.splitlines():
        if line.startswith("RESULT,"):
            p = line.split(",")
            if len(p) >= 7:
                out.append({"metric": p[5], "value": float(p[6])})
    return out


def collect_data(run_fn, num_runs, transports, sizes):
    final_stats = {}
    failures = []
    for tr in transports:
        for sz in sizes:
            print(f"    Testing {tr} | {sz}B: ", end="", flush=True)
            metrics_raw = {}
            failed_runs = 0
            for i in range(num_runs):
                print(f"{i+1} ", end="", flush=True)
                results = run_fn(tr, sz)
                if results is None:
                    failed_runs += 1
                    failures.append((tr, sz, "timeout"))
                    continue
                if not results:
                    failed_runs += 1
                    failures.append((tr, sz, "no_data"))
                    continue
                for row in results:
                    metrics_raw.setdefault(row["metric"], []).append(row["value"])
            for m, vals in metrics_raw.items():
                final_stats[f"{tr}|{sz}|{m}"] = statistics.median(vals) if vals else 0
            if failed_runs:
                print(f"(failures={failed_runs}) ", end="", flush=True)
            print("Done")
    return final_stats, failures


def format_thr(v):
    return f"{v/1e3:6.2f} Kmsg/s"


def default_core_build_dir():
    p, a = platform_arch_tag()
    if p == "windows":
        return os.path.join(ROOT_DIR, "core", "build", "windows-x64", "bin", "Release")
    return os.path.join(ROOT_DIR, "core", "build", f"{p}-{a}", "bin")


def normalize_build_dir(path: str) -> str:
    abs_path = os.path.abspath(path)
    if not os.path.isdir(abs_path):
        return abs_path
    candidates = [
        os.path.join(abs_path, "bin"),
        os.path.join(abs_path, "bin", "Release"),
        abs_path,
    ]
    for c in candidates:
        if os.path.exists(os.path.join(c, "comp_current_pair" + EXE_SUFFIX)):
            return c
    return abs_path


def main():
    p_req, refresh, num_runs, build_dir, current_only, pin_cpu, binding, allow_core_fallback = parse_args()
    sizes = msg_sizes()

    if p_req == "ALL":
        requested = list(SUPPORTED.keys())
    else:
        requested = [p.strip().upper() for p in p_req.split(",") if p.strip()]

    for p in requested:
        if p not in SUPPORTED:
            print(f"Skipping unsupported pattern for bindings benchmark: {p}")
    requested = [p for p in requested if p in SUPPORTED]
    if not requested:
        print("No supported patterns selected.", file=sys.stderr)
        sys.exit(1)

    core_build = normalize_build_dir(build_dir) if build_dir else normalize_build_dir(default_core_build_dir())

    env_base = os.environ.copy()
    if pin_cpu:
        env_base["BENCH_TASKSET"] = "1"

    platform_tag, arch_tag = platform_arch_tag()
    cache_file = os.path.join(ROOT_DIR, "bindings", binding, "benchwithzlink", f"zlink_cache_{platform_tag}-{arch_tag}.json")
    cache = {}
    if not current_only and os.path.exists(cache_file):
        try:
            with open(cache_file, "r", encoding="utf-8") as f:
                cache = json.load(f)
        except Exception:
            cache = {}

    bind_cmd_prefix = binding_runner_cmd(binding)
    fallback_hits = []

    all_failures = []

    for pattern in requested:
        transports = select_pattern_transports(pattern)
        if not transports:
            print(f"\n## PATTERN: {pattern}")
            print("  Skipping: no matching transports selected.")
            continue

        print(f"\n## PATTERN: {pattern}")
        core_bin = os.path.join(core_build, SUPPORTED[pattern]["core_bin"] + EXE_SUFFIX)
        if not os.path.exists(core_bin) and not IS_WINDOWS:
            alt = os.path.join(os.path.dirname(core_build), SUPPORTED[pattern]["core_bin"] + EXE_SUFFIX)
            if os.path.exists(alt):
                core_bin = alt
        if not os.path.exists(core_bin):
            print(f"Error: core benchmark binary not found: {core_bin}", file=sys.stderr)
            sys.exit(1)

        def run_core(tr, sz):
            cmd = [core_bin, "current", tr, str(sz)]
            if not IS_WINDOWS and env_base.get("BENCH_TASKSET") == "1":
                cmd = ["taskset", "-c", "1"] + cmd
            return run_and_parse(cmd, env_base)

        binding_env_vars = binding_env(binding, env_base)

        def run_binding(tr, sz):
            cmd = bind_cmd_prefix + [pattern, tr, str(sz)]
            if binding == "cpp":
                cmd.append(core_build)
            if not IS_WINDOWS and env_base.get("BENCH_TASKSET") == "1":
                cmd = ["taskset", "-c", "1"] + cmd
            parsed = run_and_parse(cmd, binding_env_vars)
            if parsed:
                return parsed
            if not allow_core_fallback:
                return parsed
            fb = run_core(tr, sz)
            if fb:
                fallback_hits.append((pattern, tr, sz))
                return fb
            return parsed

        if current_only:
            b_stats = {}
        else:
            if refresh or pattern not in cache:
                print("  > Benchmarking zlink(core current) for reference...")
                b_stats, failures = collect_data(run_core, num_runs, transports, sizes)
                all_failures.extend((pattern, "zlink", tr, sz, rsn) for tr, sz, rsn in failures)
                cache[pattern] = b_stats
                os.makedirs(os.path.dirname(cache_file), exist_ok=True)
                with open(cache_file, "w", encoding="utf-8") as f:
                    json.dump(cache, f, indent=2)
            else:
                print("  [zlink] Using cached reference.")
                b_stats = cache[pattern]

        print(f"  > Benchmarking binding ({binding})...")
        c_stats, failures = collect_data(run_binding, num_runs, transports, sizes)
        all_failures.extend((pattern, binding, tr, sz, rsn) for tr, sz, rsn in failures)

        for tr in transports:
            print(f"\n### Transport: {tr}")
            if current_only:
                print("| Size   | Metric     |          current |")
                print("|--------|------------|------------------|")
            else:
                print("| Size   | Metric     |          zlink |        binding |  Diff (%) |")
                print("|--------|------------|----------------|----------------|-----------|")
            for sz in sizes:
                ct = c_stats.get(f"{tr}|{sz}|throughput", 0)
                cl = c_stats.get(f"{tr}|{sz}|latency", 0)
                if current_only:
                    print(f"| {sz}B | Throughput | {format_thr(ct):>16} |")
                    print(f"| {sz}B | Latency    | {f'{cl:8.2f} us':>16} |")
                else:
                    bt = b_stats.get(f"{tr}|{sz}|throughput", 0)
                    bl = b_stats.get(f"{tr}|{sz}|latency", 0)
                    td = ((ct - bt) / bt * 100) if bt > 0 else 0
                    ld = ((bl - cl) / bl * 100) if bl > 0 else 0
                    print(f"| {sz}B | Throughput | {format_thr(bt):>14} | {format_thr(ct):>14} | {td:>+7.2f}% |")
                    print(f"| {sz}B | Latency    | {f'{bl:8.2f} us':>14} | {f'{cl:8.2f} us':>14} | {ld:>+7.2f}% |")

    if all_failures:
        print("\n## Failures")
        for p, lib, tr, sz, reason in all_failures:
            print(f"- {p} {lib} {tr} {sz}B: {reason}")
        sys.exit(2)
    if fallback_hits:
        print("\n## Fallbacks")
        print("Used core benchmark fallback where binding runner returned no RESULT rows:")
        for p, tr, sz in fallback_hits:
            print(f"- {p} {binding} {tr} {sz}B")


if __name__ == "__main__":
    main()
