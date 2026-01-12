#!/usr/bin/env python3
import subprocess
import os
import sys
import math

# Configuration
BUILD_DIR = "build-bench-asio/bin"
ZLINK_LIB_DIR = os.path.abspath("build-bench-asio/lib")
LIBZMQ_LIB_DIR = os.path.abspath("benchwithzmq/libzmq/libzmq_dist/lib")

# Base environment
base_env = os.environ.copy()

def run_benchmark(binary_name):
    """Runs a benchmark binary and returns parsed results."""
    binary_path = os.path.join(BUILD_DIR, binary_name)
    if not os.path.exists(binary_path):
        print(f"Error: Binary {binary_path} not found. Did you build?")
        return []

    # Use appropriate library path based on binary type
    env = base_env.copy()
    if binary_name.startswith("comp_zlink"):
        env["LD_LIBRARY_PATH"] = f"{ZLINK_LIB_DIR}:{env.get('LD_LIBRARY_PATH', '')}"
    else:
        env["LD_LIBRARY_PATH"] = f"{LIBZMQ_LIB_DIR}:{env.get('LD_LIBRARY_PATH', '')}"

    print(f"Running {binary_name}...", file=sys.stderr)
    try:
        # Run binary and capture stdout
        result = subprocess.run(
            [binary_path],
            env=env,
            capture_output=True,
            text=True,
            timeout=600 # 10 min timeout
        )
        if result.returncode != 0:
            print(f"Error running {binary_name}: {result.stderr}", file=sys.stderr)
            return []
        
        parsed_results = []
        for line in result.stdout.splitlines():
            if line.startswith("RESULT,"):
                # Format: RESULT,lib_type,pattern,transport,size,metric,value
                parts = line.split(",")
                if len(parts) >= 7:
                    parsed_results.append({
                        "lib": parts[1],
                        "pattern": parts[2],
                        "transport": parts[3],
                        "size": int(parts[4]),
                        "metric": parts[5],
                        "value": float(parts[6])
                    })
        return parsed_results

    except Exception as e:
        print(f"Exception running {binary_name}: {e}", file=sys.stderr)
        return []

def format_throughput(val):
    if val >= 1_000_000:
        return f"{val/1_000_000:.2f} M/s"
    elif val >= 1_000:
        return f"{val/1_000:.2f} K/s"
    return f"{val:.0f} /s"

def format_size(size):
    if size >= 1024*1024:
        return f"{size//(1024*1024)} MB"
    if size >= 1024:
        return f"{size//1024} KB"
    return f"{size} B"

def main():
    # 1. Define comparisons
    comparisons = [
        ("comp_std_zmq_pair", "comp_zlink_pair", "PAIR"),
        ("comp_std_zmq_router", "comp_zlink_router", "ROUTER"),
        ("comp_std_zmq_pubsub", "comp_zlink_pubsub", "PUBSUB"),
    ]

    for std_bin, zlk_bin, pattern_name in comparisons:
        # Run both benchmarks
        print(f"\nComparing {pattern_name} pattern...")
        
        # Run 3 times and average? The prompt asked for 3 runs average.
        def run_n_times(bin_name, n=3):
            all_runs = []
            for i in range(n):
                print(f"  Run {i+1}/{n} for {bin_name}...")
                results = run_benchmark(bin_name)
                if not results: return []
                all_runs.append(results)
            
            # Average results
            if not all_runs: return []
            avg_results = []
            num_metrics = len(all_runs[0])
            for i in range(num_metrics):
                base = all_runs[0][i].copy()
                values = [run[i]['value'] for run in all_runs]
                base['value'] = sum(values) / len(values)
                avg_results.append(base)
            return avg_results

        std_results = run_n_times(std_bin)
        zlk_results = run_n_times(zlk_bin)

        if not std_results or not zlk_results:
            continue

        # Organize by key: (transport, size, metric) -> value
        data = {}
        
        for r in std_results:
            key = (r['transport'], r['size'], r['metric'])
            if key not in data: data[key] = {}
            data[key]['std'] = r['value']
            
        for r in zlk_results:
            key = (r['transport'], r['size'], r['metric'])
            if key not in data: data[key] = {}
            data[key]['zlk'] = r['value']

        # Print Table
        transports = sorted(list(set(r['transport'] for r in std_results + zlk_results)))
        
        for transport in transports:
            print(f"\n================================================================================================")
            print(f"Pattern: {pattern_name} | Transport: {transport}")
            print(f"================================================================================================")
            print(f"{ 'Size':<10} | {'Metric':<12} | {'Standard libzmq':<18} | {'zlink':<18} | {'Diff (%)':<15}")
            print(f"-----------|--------------|--------------------|--------------------|----------------")

            # Get sizes present for this transport
            sizes = sorted(list(set(k[1] for k in data.keys() if k[0] == transport)))

            for size in sizes:
                # Throughput
                t_key = (transport, size, 'throughput')
                if t_key in data:
                    std_val = data[t_key].get('std', 0)
                    zlk_val = data[t_key].get('zlk', 0)
                    diff = ((zlk_val - std_val) / std_val * 100) if std_val > 0 else 0
                    print(f"{format_size(size):<10} | {'Throughput':<12} | {format_throughput(std_val):<18} | {format_throughput(zlk_val):<18} | {diff:>+7.2f}%")

                # Latency
                l_key = (transport, size, 'latency')
                if l_key in data:
                    std_val = data[l_key].get('std', 0)
                    zlk_val = data[l_key].get('zlk', 0)
                    # For latency, lower is better, so diff is inverted
                    diff = ((std_val - zlk_val) / std_val * 100) if std_val > 0 else 0
                    print(f"{ '':<10} | {'Latency':<12} | {std_val:<12.2f} us | {zlk_val:<12.2f} us | {diff:>+7.2f}% (inv)")
            print(f"================================================================================================")

if __name__ == "__main__":
    main()
