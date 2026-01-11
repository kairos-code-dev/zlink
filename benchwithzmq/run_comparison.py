#!/usr/bin/env python3
import subprocess
import os
import sys
import statistics
import json

# OS Detection
IS_WINDOWS = os.name == 'nt'
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""

# Configuration
if IS_WINDOWS:
    # Windows typically builds in a Release/Debug subfolder
    BUILD_DIR = "build/windows-x64/benchwithzmq/Release"
    LIBZMQ_LIB_DIR = os.path.abspath("benchwithzmq/libzmq/libzmq_dist/bin")
else:
    # Linux typically builds directly in the target folder
    # Support multiple possible build locations
    possible_paths = ["build/linux-x64/bin", "build/benchwithzmq", "build/linux-x64/benchwithzmq"]
    BUILD_DIR = next((p for p in possible_paths if os.path.exists(p)), "build/linux-x64/bin")
    LIBZMQ_LIB_DIR = os.path.abspath("benchwithzmq/libzmq/libzmq_dist/lib")

NUM_RUNS = 10 
CACHE_FILE = "benchwithzmq/libzmq_cache.json"

# Settings for loop
TRANSPORTS = ["tcp", "inproc"]
if not IS_WINDOWS:
    TRANSPORTS.append("ipc") # IPC is more stable for benchmarks on Linux

MSG_SIZES = [64, 256, 1024, 65536, 131072, 262144]

env = os.environ.copy()
if IS_WINDOWS:
    env["PATH"] = f"{LIBZMQ_LIB_DIR};{env.get('PATH', '')}"
else:
    env["LD_LIBRARY_PATH"] = f"{LIBZMQ_LIB_DIR}:{env.get('LD_LIBRARY_PATH', '')}"

def run_single_test(binary_name, lib_name, transport, size):
    """Runs a single binary for one specific config."""
    binary_path = os.path.join(BUILD_DIR, binary_name + EXE_SUFFIX)
    try:
        # Args: [lib_name] [transport] [size]
        # Use taskset on Linux to pin to CPU 1 for reduced variance
        if IS_WINDOWS:
            cmd = [binary_path, lib_name, transport, str(size)]
        else:
            cmd = ["taskset", "-c", "1", binary_path, lib_name, transport, str(size)]
        result = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=60)
        if result.returncode != 0: return []
        
        parsed = []
        for line in result.stdout.splitlines():
            if line.startswith("RESULT,"):
                p = line.split(",")
                if len(p) >= 7:
                    parsed.append({"metric": p[5], "value": float(p[6])})
        return parsed
    except Exception as e:
        # print(f"Error running {binary_name}: {e}")
        return []

def collect_data(binary_name, lib_name, pattern_name):
    print(f"  > Benchmarking {lib_name} for {pattern_name}...")
    final_stats = {} # (tr, size, metric) -> avg_value
    
    for tr in TRANSPORTS:
        for sz in MSG_SIZES:
            print(f"    Testing {tr} | {sz}B: ", end="", flush=True)
            metrics_raw = {} # metric_name -> list of values
            
            for i in range(NUM_RUNS):
                print(f"{i+1} ", end="", flush=True)
                results = run_single_test(binary_name, lib_name, tr, sz)
                for r in results:
                    m = r['metric']
                    if m not in metrics_raw: metrics_raw[m] = []
                    metrics_raw[m].append(r['value'])
            
            for m, vals in metrics_raw.items():
                if len(vals) >= 3:
                    sorted_v = sorted(vals)
                    avg = statistics.mean(sorted_v[1:-1]) # Drop min/max
                else:
                    avg = statistics.mean(vals) if vals else 0
                final_stats[f"{tr}|{sz}|{m}"] = avg
            print("Done")
    return final_stats

def main():
    refresh = "--refresh-libzmq" in sys.argv
    p_req = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else "ALL"
    
    # Check if any target binary exists
    check_bin = os.path.join(BUILD_DIR, "comp_zlink_pair" + EXE_SUFFIX)
    if not os.path.exists(check_bin):
        print(f"Error: Binaries not found at {BUILD_DIR}.")
        print("Please build the project first.")
        return

    cache = {}
    if os.path.exists(CACHE_FILE):
        try:
            with open(CACHE_FILE, 'r') as f:
                cache = json.load(f)
        except: pass

    comparisons = [
        ("comp_std_zmq_pair", "comp_zlink_pair", "PAIR"),
        ("comp_std_zmq_pubsub", "comp_zlink_pubsub", "PUBSUB"),
        ("comp_std_zmq_dealer_dealer", "comp_zlink_dealer_dealer", "DEALER_DEALER"),
        ("comp_std_zmq_dealer_router", "comp_zlink_dealer_router", "DEALER_ROUTER"),
        ("comp_std_zmq_router_router", "comp_zlink_router_router", "ROUTER_ROUTER"),
    ]

    for std_bin, zlk_bin, p_name in comparisons:
        if p_req != "ALL" and p_name != p_req: continue
        
        print(f"\n## PATTERN: {p_name}")
        
        if refresh or p_name not in cache:
            s_stats = collect_data(std_bin, "libzmq", p_name)
            cache[p_name] = s_stats
            with open(CACHE_FILE, 'w') as f: json.dump(cache, f, indent=2)
        else:
            print(f"  [libzmq] Using cached baseline.")
            s_stats = cache[p_name]

        z_stats = collect_data(zlk_bin, "zlink", p_name)

        # Print Table
        for tr in TRANSPORTS:
            print(f"\n### Transport: {tr}")
            print(f"| Size | Metric | Standard libzmq | zlink | Diff (%) |")
            print(f"|------|--------|-----------------|-------|----------|")
            for sz in MSG_SIZES:
                st = s_stats.get(f"{tr}|{sz}|throughput", 0)
                zt = z_stats.get(f"{tr}|{sz}|throughput", 0)
                td = ((zt - st) / st * 100) if st > 0 else 0
                sl = s_stats.get(f"{tr}|{sz}|latency", 0)
                zl = z_stats.get(f"{tr}|{sz}|latency", 0)
                ld = ((sl - zl) / sl * 100) if sl > 0 else 0
                print(f"| {sz}B | Throughput | {st/1e6:6.2f} M/s | {zt/1e6:6.2f} M/s | {td:>+7.2f}% |")
                print(f"| | Latency | {sl:8.2f} us | {zl:8.2f} us | {ld:>+7.2f}% (inv) |")

if __name__ == "__main__":
    main()
