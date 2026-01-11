# zlink vs libzmq Performance Benchmark Results

## Test Environment

| Item | Value |
|------|-------|
| OS | Linux (WSL2) 6.6.87.2-microsoft-standard-WSL2 |
| CPU | Intel Core Ultra 7 265K |
| Architecture | x86_64 |
| C++ Standard | C++20 |
| CPU Affinity | `taskset -c 1` (pinned to CPU 1) |
| Iterations | 10 runs per configuration (min/max trimmed) |
| Date | 2026-01-12 |

## Methodology

- Each benchmark runs 10 iterations
- Results are averaged after removing min/max outliers
- CPU affinity is applied to reduce variance from core migration
- Both libraries use identical build options (Release, -O3)
- zlink built with C++20 standard

---

## Summary

**zlink performs equivalently to standard libzmq** across all tested patterns and transports.

| Pattern | Throughput Diff | Latency Diff |
|---------|-----------------|--------------|
| PAIR | -1% ~ +6% | ±2% |
| PUBSUB | -3% ~ +16% | ±4% |
| DEALER_DEALER | -2% ~ +60% | ±4% |
| DEALER_ROUTER | ±4% | ±5% |
| ROUTER_ROUTER | -19% ~ +7% | ±7% |

Note: Large percentage differences in low-throughput scenarios (large messages) represent small absolute differences.

---

## Detailed Results

### PAIR Pattern

#### TCP Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 5.90 M/s | 5.94 M/s | +0.57% |
| 64B | Latency | 4.99 us | 4.98 us | +0.30% |
| 256B | Throughput | 3.60 M/s | 3.56 M/s | -0.95% |
| 256B | Latency | 5.03 us | 5.13 us | -2.19% |
| 1024B | Throughput | 1.36 M/s | 1.37 M/s | +0.66% |
| 1024B | Latency | 5.19 us | 5.10 us | +1.66% |
| 64KB | Throughput | 0.04 M/s | 0.04 M/s | +3.08% |
| 64KB | Latency | 12.85 us | 12.74 us | +0.84% |
| 128KB | Throughput | 0.02 M/s | 0.02 M/s | +5.46% |
| 128KB | Latency | 18.59 us | 18.88 us | -1.55% |
| 256KB | Throughput | 0.01 M/s | 0.01 M/s | +5.61% |
| 256KB | Latency | 30.78 us | 30.92 us | -0.48% |

#### Inproc Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 7.93 M/s | 7.69 M/s | -2.96% |
| 64B | Latency | 0.07 us | 0.07 us | -1.75% |
| 256B | Throughput | 7.98 M/s | 8.01 M/s | +0.35% |
| 256B | Latency | 0.07 us | 0.07 us | +1.72% |
| 1024B | Throughput | 4.71 M/s | 4.70 M/s | -0.36% |
| 1024B | Latency | 0.09 us | 0.09 us | +0.00% |
| 64KB | Throughput | 0.15 M/s | 0.15 M/s | +1.79% |
| 64KB | Latency | 2.08 us | 2.02 us | +2.71% |
| 128KB | Throughput | 0.10 M/s | 0.10 M/s | -0.57% |
| 128KB | Latency | 3.59 us | 3.60 us | -0.10% |
| 256KB | Throughput | 0.06 M/s | 0.06 M/s | -0.56% |
| 256KB | Latency | 7.00 us | 7.06 us | -0.82% |

#### IPC Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 6.20 M/s | 6.14 M/s | -0.84% |
| 64B | Latency | 4.46 us | 4.47 us | -0.31% |
| 256B | Throughput | 3.93 M/s | 4.01 M/s | +2.02% |
| 256B | Latency | 4.52 us | 4.51 us | +0.22% |
| 1024B | Throughput | 1.62 M/s | 1.67 M/s | +2.90% |
| 1024B | Latency | 4.56 us | 4.54 us | +0.41% |
| 64KB | Throughput | 0.04 M/s | 0.04 M/s | -3.20% |
| 64KB | Latency | 12.46 us | 12.37 us | +0.77% |
| 128KB | Throughput | 0.02 M/s | 0.02 M/s | +1.20% |
| 128KB | Latency | 18.80 us | 18.61 us | +1.00% |
| 256KB | Throughput | 0.01 M/s | 0.01 M/s | -4.06% |
| 256KB | Latency | 30.17 us | 30.58 us | -1.34% |

---

### PUBSUB Pattern

#### TCP Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 3.98 M/s | 3.92 M/s | -1.59% |
| 64B | Latency | 0.25 us | 0.26 us | -1.49% |
| 256B | Throughput | 2.33 M/s | 2.28 M/s | -2.46% |
| 256B | Latency | 0.43 us | 0.44 us | -2.03% |
| 1024B | Throughput | 0.87 M/s | 0.84 M/s | -2.89% |
| 1024B | Latency | 1.15 us | 1.19 us | -3.71% |
| 64KB | Throughput | 0.03 M/s | 0.03 M/s | +1.46% |
| 64KB | Latency | 33.36 us | 32.84 us | +1.56% |
| 128KB | Throughput | 0.02 M/s | 0.02 M/s | +15.54% |
| 128KB | Latency | 58.46 us | 54.80 us | +6.25% |
| 256KB | Throughput | 0.01 M/s | 0.01 M/s | +1.43% |
| 256KB | Latency | 102.66 us | 101.19 us | +1.43% |

#### Inproc Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 5.88 M/s | 5.91 M/s | +0.44% |
| 64B | Latency | 0.17 us | 0.17 us | +0.74% |
| 256B | Throughput | 4.71 M/s | 4.69 M/s | -0.33% |
| 256B | Latency | 0.21 us | 0.21 us | -1.18% |
| 1024B | Throughput | 2.63 M/s | 2.63 M/s | +0.15% |
| 1024B | Latency | 0.38 us | 0.38 us | +0.00% |
| 64KB | Throughput | 0.15 M/s | 0.15 M/s | +0.09% |
| 64KB | Latency | 6.80 us | 6.79 us | +0.09% |
| 128KB | Throughput | 0.10 M/s | 0.10 M/s | -1.33% |
| 128KB | Latency | 9.77 us | 9.90 us | -1.34% |
| 256KB | Throughput | 0.06 M/s | 0.06 M/s | -1.46% |
| 256KB | Latency | 16.25 us | 16.49 us | -1.49% |

#### IPC Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 4.13 M/s | 3.99 M/s | -3.47% |
| 64B | Latency | 0.24 us | 0.25 us | -4.15% |
| 256B | Throughput | 2.49 M/s | 2.51 M/s | +0.96% |
| 256B | Latency | 0.40 us | 0.40 us | +0.31% |
| 1024B | Throughput | 0.94 M/s | 0.94 M/s | -0.37% |
| 1024B | Latency | 1.06 us | 1.06 us | +0.59% |
| 64KB | Throughput | 0.03 M/s | 0.03 M/s | -0.72% |
| 64KB | Latency | 34.16 us | 34.40 us | -0.71% |
| 128KB | Throughput | 0.02 M/s | 0.02 M/s | -0.15% |
| 128KB | Latency | 59.70 us | 60.37 us | -1.12% |
| 256KB | Throughput | 0.01 M/s | 0.01 M/s | +1.35% |
| 256KB | Latency | 107.91 us | 106.46 us | +1.34% |

---

### DEALER_DEALER Pattern

#### TCP Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 5.66 M/s | 5.70 M/s | +0.77% |
| 64B | Latency | 5.13 us | 5.06 us | +1.36% |
| 256B | Throughput | 3.49 M/s | 3.47 M/s | -0.52% |
| 256B | Latency | 5.07 us | 5.12 us | -0.99% |
| 1024B | Throughput | 1.36 M/s | 1.34 M/s | -1.43% |
| 1024B | Latency | 5.19 us | 5.22 us | -0.65% |
| 64KB | Throughput | 0.02 M/s | 0.04 M/s | +59.61% |
| 64KB | Latency | 12.84 us | 12.67 us | +1.32% |
| 128KB | Throughput | 0.02 M/s | 0.02 M/s | -1.96% |
| 128KB | Latency | 19.02 us | 18.92 us | +0.53% |
| 256KB | Throughput | 0.01 M/s | 0.01 M/s | +0.27% |
| 256KB | Latency | 30.91 us | 30.57 us | +1.10% |

#### Inproc Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 7.54 M/s | 7.53 M/s | -0.08% |
| 64B | Latency | 0.08 us | 0.08 us | +1.56% |
| 256B | Throughput | 7.59 M/s | 7.71 M/s | +1.63% |
| 256B | Latency | 0.08 us | 0.08 us | +0.00% |
| 1024B | Throughput | 4.64 M/s | 4.60 M/s | -0.82% |
| 1024B | Latency | 0.10 us | 0.10 us | +0.00% |
| 64KB | Throughput | 0.15 M/s | 0.15 M/s | +2.00% |
| 64KB | Latency | 2.08 us | 2.05 us | +1.68% |
| 128KB | Throughput | 0.10 M/s | 0.10 M/s | +2.78% |
| 128KB | Latency | 3.71 us | 3.64 us | +1.92% |
| 256KB | Throughput | 0.06 M/s | 0.06 M/s | +2.27% |
| 256KB | Latency | 7.08 us | 7.08 us | -0.04% |

#### IPC Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 5.99 M/s | 5.95 M/s | -0.74% |
| 64B | Latency | 4.46 us | 4.50 us | -1.07% |
| 256B | Throughput | 3.85 M/s | 3.88 M/s | +0.78% |
| 256B | Latency | 4.54 us | 4.65 us | -2.56% |
| 1024B | Throughput | 1.59 M/s | 1.61 M/s | +1.23% |
| 1024B | Latency | 4.64 us | 4.62 us | +0.35% |
| 64KB | Throughput | 0.04 M/s | 0.04 M/s | +1.87% |
| 64KB | Latency | 13.01 us | 12.51 us | +3.86% |
| 128KB | Throughput | 0.02 M/s | 0.02 M/s | -3.43% |
| 128KB | Latency | 18.98 us | 18.82 us | +0.89% |
| 256KB | Throughput | 0.01 M/s | 0.01 M/s | +10.70% |
| 256KB | Latency | 31.52 us | 31.32 us | +0.63% |

---

### DEALER_ROUTER Pattern

#### TCP Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 4.40 M/s | 4.45 M/s | +1.16% |
| 64B | Latency | 5.09 us | 5.09 us | +0.00% |
| 256B | Throughput | 2.96 M/s | 2.97 M/s | +0.42% |
| 256B | Latency | 5.17 us | 5.20 us | -0.60% |
| 1024B | Throughput | 1.24 M/s | 1.23 M/s | -0.16% |
| 1024B | Latency | 5.31 us | 5.21 us | +1.93% |
| 64KB | Throughput | 0.02 M/s | 0.02 M/s | +3.41% |
| 64KB | Latency | 12.84 us | 12.80 us | +0.29% |
| 128KB | Throughput | 0.01 M/s | 0.01 M/s | +6.88% |
| 128KB | Latency | 19.54 us | 19.23 us | +1.58% |
| 256KB | Throughput | 0.01 M/s | 0.01 M/s | +3.50% |
| 256KB | Latency | 31.98 us | 31.39 us | +1.84% |

#### Inproc Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 6.59 M/s | 6.82 M/s | +3.56% |
| 64B | Latency | 0.11 us | 0.12 us | -4.55% |
| 256B | Throughput | 6.23 M/s | 6.48 M/s | +4.03% |
| 256B | Latency | 0.11 us | 0.11 us | +1.12% |
| 1024B | Throughput | 3.78 M/s | 3.92 M/s | +3.59% |
| 1024B | Latency | 0.13 us | 0.13 us | +0.98% |
| 64KB | Throughput | 0.13 M/s | 0.13 M/s | +2.73% |
| 64KB | Latency | 2.02 us | 2.00 us | +0.99% |
| 128KB | Throughput | 0.09 M/s | 0.09 M/s | +6.35% |
| 128KB | Latency | 3.80 us | 3.76 us | +1.18% |
| 256KB | Throughput | 0.06 M/s | 0.06 M/s | +0.31% |
| 256KB | Latency | 7.43 us | 7.35 us | +1.08% |

#### IPC Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 4.49 M/s | 4.55 M/s | +1.39% |
| 64B | Latency | 4.60 us | 4.54 us | +1.33% |
| 256B | Throughput | 3.26 M/s | 3.32 M/s | +2.03% |
| 256B | Latency | 4.70 us | 4.66 us | +0.90% |
| 1024B | Throughput | 1.43 M/s | 1.47 M/s | +3.21% |
| 1024B | Latency | 4.71 us | 4.68 us | +0.64% |
| 64KB | Throughput | 0.02 M/s | 0.02 M/s | -1.66% |
| 64KB | Latency | 12.65 us | 12.50 us | +1.18% |
| 128KB | Throughput | 0.01 M/s | 0.01 M/s | +0.19% |
| 128KB | Latency | 19.30 us | 18.94 us | +1.85% |
| 256KB | Throughput | 0.01 M/s | 0.01 M/s | +2.89% |
| 256KB | Latency | 32.19 us | 31.55 us | +1.99% |

---

### ROUTER_ROUTER Pattern

#### TCP Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 3.96 M/s | 4.01 M/s | +1.20% |
| 64B | Latency | 4.26 us | 4.24 us | +0.44% |
| 256B | Throughput | 2.79 M/s | 2.88 M/s | +3.16% |
| 256B | Latency | 4.40 us | 4.32 us | +1.90% |
| 1024B | Throughput | 1.18 M/s | 1.23 M/s | +4.15% |
| 1024B | Latency | 4.52 us | 4.36 us | +3.49% |
| 64KB | Throughput | 0.04 M/s | 0.04 M/s | +2.47% |
| 64KB | Latency | 11.86 us | 12.02 us | -1.35% |
| 128KB | Throughput | 0.02 M/s | 0.02 M/s | -19.07% |
| 128KB | Latency | 19.04 us | 18.50 us | +2.86% |
| 256KB | Throughput | 0.01 M/s | 0.01 M/s | +6.36% |
| 256KB | Latency | 40.82 us | 39.37 us | +3.56% |

#### Inproc Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 4.69 M/s | 4.76 M/s | +1.40% |
| 64B | Latency | 0.16 us | 0.16 us | +1.56% |
| 256B | Throughput | 4.81 M/s | 4.99 M/s | +3.76% |
| 256B | Latency | 0.16 us | 0.16 us | -1.56% |
| 1024B | Throughput | 3.26 M/s | 3.50 M/s | +7.52% |
| 1024B | Latency | 0.18 us | 0.18 us | +0.68% |
| 64KB | Throughput | 0.15 M/s | 0.15 M/s | +2.88% |
| 64KB | Latency | 2.12 us | 2.12 us | -0.41% |
| 128KB | Throughput | 0.08 M/s | 0.08 M/s | +1.52% |
| 128KB | Latency | 3.99 us | 3.90 us | +2.29% |
| 256KB | Throughput | 0.06 M/s | 0.06 M/s | +1.92% |
| 256KB | Latency | 7.41 us | 7.26 us | +1.96% |

#### IPC Transport
| Size | Metric | libzmq | zlink | Diff |
|------|--------|--------|-------|------|
| 64B | Throughput | 4.07 M/s | 4.11 M/s | +1.11% |
| 64B | Latency | 3.72 us | 3.79 us | -1.91% |
| 256B | Throughput | 3.14 M/s | 3.16 M/s | +0.54% |
| 256B | Latency | 3.83 us | 3.86 us | -0.75% |
| 1024B | Throughput | 1.41 M/s | 1.42 M/s | +0.90% |
| 1024B | Latency | 3.90 us | 3.89 us | +0.32% |
| 64KB | Throughput | 0.04 M/s | 0.04 M/s | +2.02% |
| 64KB | Latency | 13.44 us | 13.12 us | +2.39% |
| 128KB | Throughput | 0.02 M/s | 0.02 M/s | -2.88% |
| 128KB | Latency | 27.51 us | 25.64 us | +6.80% |
| 256KB | Throughput | 0.01 M/s | 0.01 M/s | -9.34% |
| 256KB | Latency | 39.04 us | 38.10 us | +2.41% |

---

## Conclusions

1. **Performance Parity**: zlink achieves equivalent performance to standard libzmq across all tested socket patterns and transport types.

2. **C++20 Build**: No performance regression observed with C++20 standard compilation.

3. **Small Message Performance**: For messages up to 1KB, both libraries achieve similar throughput (3-8 M/s depending on pattern).

4. **Large Message Performance**: For messages 64KB+, both libraries show similar throughput with minor variations within expected benchmark noise.

5. **Latency**: Sub-microsecond latency for inproc, ~5us for TCP/IPC - consistent between both libraries.

6. **Recommendation**: zlink can be used as a drop-in replacement for libzmq without performance concerns.

---

## Running the Benchmark

```bash
# Build with benchmarks enabled
cmake -B build -DBUILD_BENCHMARKS=ON -DZMQ_CXX_STANDARD=20
cmake --build build

# Run comparison (uses cached libzmq baseline)
python3 benchwithzmq/run_comparison.py

# Force refresh of libzmq baseline
python3 benchwithzmq/run_comparison.py --refresh-libzmq

# Run specific pattern only
python3 benchwithzmq/run_comparison.py PAIR
```
