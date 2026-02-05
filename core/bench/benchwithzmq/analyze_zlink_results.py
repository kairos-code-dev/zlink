#!/usr/bin/env python3
"""Analyze zlink-only benchmark results"""

import csv
import statistics
from collections import defaultdict
from datetime import datetime

def load_results(filename):
    """Load CSV results"""
    data = defaultdict(lambda: defaultdict(list))

    with open(filename, 'r', encoding='utf-8-sig') as f:
        reader = csv.DictReader(f)
        for row in reader:
            pattern = row['Pattern']
            size = int(row['MsgSize'])
            metric = row['Metric']
            value = float(row['Value'])

            key = (pattern, size, metric)
            data[key]['values'].append(value)

    return data

def calculate_stats(values):
    """Calculate statistics"""
    mean = statistics.mean(values)
    stdev = statistics.stdev(values) if len(values) > 1 else 0
    minimum = min(values)
    maximum = max(values)
    cv = (stdev / mean * 100) if mean > 0 else 0

    return {
        'mean': mean,
        'stdev': stdev,
        'min': minimum,
        'max': maximum,
        'cv': cv,
        'count': len(values)
    }

def format_number(value):
    """Format with thousands separator"""
    return f"{value:,.2f}"

def generate_markdown(data):
    """Generate markdown report"""
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    md = f"""# zlink Benchmark Results (C++20 Build)

**Test Date:** {now}
**Platform:** Windows x64
**Compiler:** Visual Studio 2022 (MSVC 19.44.35222.0) with C++20 standard
**Transport:** TCP
**Iterations:** 10 runs per benchmark
**Build Configuration:** Minimal build (CURVE disabled, no libsodium)

## Results Summary (Mean ± Std Dev)

### 64-byte Messages

| Pattern | Throughput (msg/s) | Latency (μs) | CV (Throughput) | CV (Latency) |
|---------|-------------------|--------------|-----------------|--------------|
"""

    patterns_64 = ['PAIR', 'PUBSUB', 'DEALER_DEALER', 'DEALER_ROUTER', 'ROUTER_ROUTER']

    for pattern in patterns_64:
        thr_key = (pattern, 64, 'throughput')
        lat_key = (pattern, 64, 'latency')

        if thr_key in data and lat_key in data:
            thr_stats = calculate_stats(data[thr_key]['values'])
            lat_stats = calculate_stats(data[lat_key]['values'])

            md += f"| **{pattern.replace('_', '/')}** | "
            md += f"{format_number(thr_stats['mean'])} ± {format_number(thr_stats['stdev'])} | "
            md += f"{lat_stats['mean']:.2f} ± {lat_stats['stdev']:.2f} | "
            md += f"{thr_stats['cv']:.2f}% | "
            md += f"{lat_stats['cv']:.2f}% |\n"

    md += "\n### 1024-byte Messages\n\n"
    md += "| Pattern | Throughput (msg/s) | Latency (μs) | CV (Throughput) | CV (Latency) |\n"
    md += "|---------|-------------------|--------------|-----------------|--------------|"
    md += "\n"

    patterns_1024 = ['PAIR', 'PUBSUB']

    for pattern in patterns_1024:
        thr_key = (pattern, 1024, 'throughput')
        lat_key = (pattern, 1024, 'latency')

        if thr_key in data and lat_key in data:
            thr_stats = calculate_stats(data[thr_key]['values'])
            lat_stats = calculate_stats(data[lat_key]['values'])

            md += f"| **{pattern}** | "
            md += f"{format_number(thr_stats['mean'])} ± {format_number(thr_stats['stdev'])} | "
            md += f"{lat_stats['mean']:.2f} ± {lat_stats['stdev']:.2f} | "
            md += f"{thr_stats['cv']:.2f}% | "
            md += f"{lat_stats['cv']:.2f}% |\n"

    md += "\n## Detailed Results\n\n"
    md += "### All Measurements (Min/Max/Mean)\n\n"
    md += "| Pattern | Size | Metric | Min | Max | Mean | Std Dev |\n"
    md += "|---------|------|--------|-----|-----|------|---------|" + "\n"

    all_keys = sorted(data.keys())
    for key in all_keys:
        pattern, size, metric = key
        stats = calculate_stats(data[key]['values'])

        md += f"| {pattern.replace('_', '/')} | {size} | {metric} | "

        if metric == 'throughput':
            md += f"{format_number(stats['min'])} | "
            md += f"{format_number(stats['max'])} | "
            md += f"{format_number(stats['mean'])} | "
            md += f"{format_number(stats['stdev'])} |\n"
        else:
            md += f"{stats['min']:.2f} | "
            md += f"{stats['max']:.2f} | "
            md += f"{stats['mean']:.2f} | "
            md += f"{stats['stdev']:.2f} |\n"

    md += "\n## Analysis\n\n"
    md += "### C++20 Build Performance\n\n"
    md += "Built with Visual Studio 2022 MSVC 19.44 using C++20 standard:\n"
    md += "- Modern C++ features enabled\n"
    md += "- CURVE encryption disabled for minimal footprint\n"
    md += "- Optimized Release build (/O2)\n\n"

    md += "### Measurement Stability\n\n"
    md += "Coefficient of Variation (CV) indicates measurement consistency:\n"
    md += "- **Excellent** (<5%): Very stable and reproducible\n"
    md += "- **Good** (5-10%): Acceptably stable\n"
    md += "- **Moderate** (10-30%): Some variance, typical for TCP benchmarks\n"
    md += "- **High** (>30%): High variance, multiple factors affecting performance\n\n"

    md += "### Performance Characteristics\n\n"
    md += "Pattern-specific observations:\n"
    md += "- **PAIR**: Direct one-to-one communication\n"
    md += "- **PUBSUB**: Publisher-subscriber pattern\n"
    md += "- **DEALER/DEALER**: Load-balanced request-reply\n"
    md += "- **DEALER/ROUTER**: Asynchronous request-reply\n"
    md += "- **ROUTER/ROUTER**: Advanced routing pattern\n\n"

    md += "---\n"
    md += "Generated by zlink benchmark suite (C++20 build)\n"

    return md

if __name__ == '__main__':
    print("Loading zlink results...")
    data = load_results('zlink_results.csv')

    print("Calculating statistics...")

    print("Generating markdown report...")
    markdown = generate_markdown(data)

    output_file = 'zlink_BENCHMARK_RESULTS.md'
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(markdown)

    print(f"Report generated: {output_file}")
    print(f"\nTotal measurements: {len(data)} combinations × 10 samples each")
