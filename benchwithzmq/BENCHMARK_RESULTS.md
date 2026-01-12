# zlink vs libzmq Performance Benchmark Results

## Test Environment

| Item | Value |
|------|-------|
| OS | Linux (WSL2) 6.6.87.2-microsoft-standard-WSL2 |
| CPU | Intel Core Ultra 7 265K |
| Architecture | x86_64 |
| C++ Standard | C++20 |
| CPU Affinity | `taskset -c 1` (pinned to CPU 1) |
| Iterations | 3 runs per configuration (min/max trimmed) |
| Date | 2026-01-12 |

## Methodology

- Command: `python3 benchwithzmq/run_comparison.py ALL --refresh-libzmq`
- Each configuration runs 3 iterations; min/max are dropped (median kept).
- Throughput diff = `(zlink - libzmq) / libzmq`.
- Latency diff uses inverse `(libzmq - zlink) / libzmq` (positive means lower latency for zlink).

## Summary

- Throughput diff range: -38.88% to +21.51% across all patterns/transports/sizes.
- Latency inverse diff range: -32.73% to +21.05% (positive means lower latency for zlink).
- Results captured on WSL2; expect variance across runs and hosts.

## Detailed Results

### PAIR Pattern

#### TCP Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.60 M/s |   5.63 M/s |   +0.49% |
| | Latency |     5.21 us |     5.78 us |  -10.94% (inv) |
| 256B | Throughput |   3.39 M/s |   3.20 M/s |   -5.77% |
| | Latency |     5.38 us |     5.81 us |   -7.99% (inv) |
| 1024B | Throughput |   1.29 M/s |   1.26 M/s |   -2.38% |
| | Latency |     5.36 us |     5.90 us |  -10.07% (inv) |
| 65536B | Throughput |   0.04 M/s |   0.03 M/s |  -11.10% |
| | Latency |    13.17 us |    14.88 us |  -12.98% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |   +3.84% |
| | Latency |    20.82 us |    25.46 us |  -22.29% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |  -14.83% |
| | Latency |    30.98 us |    41.12 us |  -32.73% (inv) |

#### Inproc Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   7.52 M/s |   7.69 M/s |   +2.24% |
| | Latency |     0.07 us |     0.07 us |   +0.00% (inv) |
| 256B | Throughput |   7.92 M/s |   7.92 M/s |   +0.00% |
| | Latency |     0.07 us |     0.07 us |   +0.00% (inv) |
| 1024B | Throughput |   4.52 M/s |   4.71 M/s |   +4.20% |
| | Latency |     0.09 us |     0.09 us |   +0.00% (inv) |
| 65536B | Throughput |   0.15 M/s |   0.14 M/s |   -5.96% |
| | Latency |     2.07 us |     2.06 us |   +0.48% (inv) |
| 131072B | Throughput |   0.10 M/s |   0.10 M/s |   +2.43% |
| | Latency |     3.68 us |     3.73 us |   -1.36% (inv) |
| 262144B | Throughput |   0.06 M/s |   0.06 M/s |   +4.09% |
| | Latency |     7.11 us |     7.12 us |   -0.14% (inv) |

#### IPC Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   6.07 M/s |   5.88 M/s |   -3.01% |
| | Latency |     4.62 us |     5.33 us |  -15.37% (inv) |
| 256B | Throughput |   3.78 M/s |   3.64 M/s |   -3.47% |
| | Latency |     4.68 us |     5.55 us |  -18.59% (inv) |
| 1024B | Throughput |   1.62 M/s |   1.43 M/s |  -11.61% |
| | Latency |     4.53 us |     5.46 us |  -20.53% (inv) |
| 65536B | Throughput |   0.04 M/s |   0.04 M/s |   +2.00% |
| | Latency |    12.77 us |    13.76 us |   -7.75% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |   -7.46% |
| | Latency |    19.13 us |    21.60 us |  -12.91% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |   -5.00% |
| | Latency |    30.79 us |    33.34 us |   -8.28% (inv) |

### PUBSUB Pattern

#### TCP Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   3.95 M/s |   4.12 M/s |   +4.24% |
| | Latency |     0.25 us |     0.24 us |   +4.00% (inv) |
| 256B | Throughput |   2.33 M/s |   2.41 M/s |   +3.62% |
| | Latency |     0.43 us |     0.41 us |   +4.65% (inv) |
| 1024B | Throughput |   0.85 M/s |   0.89 M/s |   +4.79% |
| | Latency |     1.18 us |     1.13 us |   +4.24% (inv) |
| 65536B | Throughput |   0.03 M/s |   0.03 M/s |   +5.91% |
| | Latency |    37.81 us |    35.70 us |   +5.58% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |  -11.57% |
| | Latency |    53.18 us |    60.14 us |  -13.09% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |   -5.84% |
| | Latency |   106.28 us |   103.99 us |   +2.15% (inv) |

#### Inproc Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.34 M/s |   6.49 M/s |  +21.51% |
| | Latency |     0.19 us |     0.15 us |  +21.05% (inv) |
| 256B | Throughput |   4.83 M/s |   4.65 M/s |   -3.76% |
| | Latency |     0.21 us |     0.22 us |   -4.76% (inv) |
| 1024B | Throughput |   2.62 M/s |   2.58 M/s |   -1.55% |
| | Latency |     0.38 us |     0.39 us |   -2.63% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.15 M/s |   +3.10% |
| | Latency |     6.96 us |     6.75 us |   +3.02% (inv) |
| 131072B | Throughput |   0.10 M/s |   0.09 M/s |   -7.45% |
| | Latency |    10.02 us |    10.83 us |   -8.08% (inv) |
| 262144B | Throughput |   0.06 M/s |   0.06 M/s |   -0.74% |
| | Latency |    16.64 us |    16.82 us |   -1.08% (inv) |

#### IPC Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   4.12 M/s |   4.03 M/s |   -2.12% |
| | Latency |     0.24 us |     0.25 us |   -4.17% (inv) |
| 256B | Throughput |   2.54 M/s |   2.36 M/s |   -6.98% |
| | Latency |     0.39 us |     0.42 us |   -7.69% (inv) |
| 1024B | Throughput |   0.94 M/s |   0.85 M/s |   -9.24% |
| | Latency |     1.07 us |     1.17 us |   -9.35% (inv) |
| 65536B | Throughput |   0.03 M/s |   0.03 M/s |   -6.87% |
| | Latency |    31.43 us |    33.75 us |   -7.38% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |  +13.23% |
| | Latency |    64.08 us |    56.59 us |  +11.69% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |   -6.81% |
| | Latency |    93.90 us |   100.77 us |   -7.32% (inv) |

### DEALER_DEALER Pattern

#### TCP Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.71 M/s |   5.53 M/s |   -3.12% |
| | Latency |     5.16 us |     5.75 us |  -11.43% (inv) |
| 256B | Throughput |   3.50 M/s |   3.27 M/s |   -6.64% |
| | Latency |     5.08 us |     5.84 us |  -14.96% (inv) |
| 1024B | Throughput |   1.36 M/s |   1.25 M/s |   -7.71% |
| | Latency |     5.16 us |     5.86 us |  -13.57% (inv) |
| 65536B | Throughput |   0.03 M/s |   0.04 M/s |  +16.38% |
| | Latency |    13.00 us |    15.12 us |  -16.31% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |   -4.31% |
| | Latency |    19.20 us |    23.59 us |  -22.86% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |  +18.23% |
| | Latency |    31.81 us |    40.60 us |  -27.63% (inv) |

#### Inproc Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   7.54 M/s |   7.49 M/s |   -0.62% |
| | Latency |     0.08 us |     0.08 us |   +0.00% (inv) |
| 256B | Throughput |   7.68 M/s |   7.71 M/s |   +0.38% |
| | Latency |     0.08 us |     0.08 us |   +0.00% (inv) |
| 1024B | Throughput |   4.55 M/s |   4.72 M/s |   +3.89% |
| | Latency |     0.10 us |     0.09 us |  +10.00% (inv) |
| 65536B | Throughput |   0.15 M/s |   0.15 M/s |   +0.79% |
| | Latency |     2.04 us |     2.07 us |   -1.47% (inv) |
| 131072B | Throughput |   0.10 M/s |   0.10 M/s |   +3.85% |
| | Latency |     3.72 us |     3.64 us |   +2.15% (inv) |
| 262144B | Throughput |   0.06 M/s |   0.06 M/s |   +2.86% |
| | Latency |     7.43 us |     7.02 us |   +5.52% (inv) |

#### IPC Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.68 M/s |   5.59 M/s |   -1.66% |
| | Latency |     4.63 us |     5.54 us |  -19.65% (inv) |
| 256B | Throughput |   3.71 M/s |   3.39 M/s |   -8.64% |
| | Latency |     4.85 us |     5.72 us |  -17.94% (inv) |
| 1024B | Throughput |   1.55 M/s |   1.38 M/s |  -10.92% |
| | Latency |     4.79 us |     5.71 us |  -19.21% (inv) |
| 65536B | Throughput |   0.04 M/s |   0.04 M/s |   -2.32% |
| | Latency |    13.11 us |    14.03 us |   -7.02% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |  +17.01% |
| | Latency |    19.86 us |    21.01 us |   -5.79% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |   +1.75% |
| | Latency |    31.86 us |    34.53 us |   -8.38% (inv) |

### DEALER_ROUTER Pattern

#### TCP Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   4.26 M/s |   4.34 M/s |   +2.05% |
| | Latency |     5.34 us |     5.83 us |   -9.18% (inv) |
| 256B | Throughput |   2.89 M/s |   2.84 M/s |   -1.73% |
| | Latency |     5.34 us |     5.74 us |   -7.49% (inv) |
| 1024B | Throughput |   1.22 M/s |   1.18 M/s |   -4.01% |
| | Latency |     5.44 us |     5.85 us |   -7.54% (inv) |
| 65536B | Throughput |   0.02 M/s |   0.02 M/s |   +8.34% |
| | Latency |    13.24 us |    14.76 us |  -11.48% (inv) |
| 131072B | Throughput |   0.01 M/s |   0.01 M/s |   +1.49% |
| | Latency |    19.16 us |    24.30 us |  -26.83% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |   -3.79% |
| | Latency |    32.86 us |    40.89 us |  -24.44% (inv) |

#### Inproc Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   6.32 M/s |   5.55 M/s |  -12.26% |
| | Latency |     0.11 us |     0.12 us |   -9.09% (inv) |
| 256B | Throughput |   6.38 M/s |   6.40 M/s |   +0.31% |
| | Latency |     0.11 us |     0.12 us |   -9.09% (inv) |
| 1024B | Throughput |   3.85 M/s |   3.38 M/s |  -12.12% |
| | Latency |     0.13 us |     0.14 us |   -7.69% (inv) |
| 65536B | Throughput |   0.12 M/s |   0.13 M/s |   +9.92% |
| | Latency |     2.05 us |     2.06 us |   -0.49% (inv) |
| 131072B | Throughput |   0.09 M/s |   0.09 M/s |   +7.75% |
| | Latency |     3.83 us |     3.88 us |   -1.31% (inv) |
| 262144B | Throughput |   0.06 M/s |   0.05 M/s |   -9.63% |
| | Latency |     7.23 us |     7.64 us |   -5.67% (inv) |

#### IPC Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   4.41 M/s |   4.35 M/s |   -1.37% |
| | Latency |     4.62 us |     5.69 us |  -23.16% (inv) |
| 256B | Throughput |   3.26 M/s |   2.98 M/s |   -8.46% |
| | Latency |     4.73 us |     5.75 us |  -21.56% (inv) |
| 1024B | Throughput |   1.47 M/s |   1.26 M/s |  -14.47% |
| | Latency |     4.86 us |     5.62 us |  -15.64% (inv) |
| 65536B | Throughput |   0.02 M/s |   0.02 M/s |   +3.66% |
| | Latency |    12.40 us |    13.99 us |  -12.82% (inv) |
| 131072B | Throughput |   0.01 M/s |   0.01 M/s |   -3.78% |
| | Latency |    19.38 us |    20.65 us |   -6.55% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |   -4.52% |
| | Latency |    32.01 us |    34.88 us |   -8.97% (inv) |

### ROUTER_ROUTER Pattern

#### TCP Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   3.86 M/s |   3.92 M/s |   +1.55% |
| | Latency |     4.37 us |     4.43 us |   -1.37% (inv) |
| 256B | Throughput |   2.86 M/s |   2.67 M/s |   -6.39% |
| | Latency |     4.42 us |     4.59 us |   -3.85% (inv) |
| 1024B | Throughput |   1.14 M/s |   1.09 M/s |   -4.14% |
| | Latency |     4.47 us |     4.65 us |   -4.03% (inv) |
| 65536B | Throughput |   0.04 M/s |   0.04 M/s |   +2.22% |
| | Latency |    11.96 us |    13.87 us |  -15.97% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |   -1.06% |
| | Latency |    18.91 us |    23.17 us |  -22.53% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |  -38.88% |
| | Latency |    40.23 us |    49.72 us |  -23.59% (inv) |

#### Inproc Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   4.64 M/s |   4.30 M/s |   -7.35% |
| | Latency |     0.16 us |     0.16 us |   +0.00% (inv) |
| 256B | Throughput |   4.85 M/s |   4.74 M/s |   -2.16% |
| | Latency |     0.16 us |     0.17 us |   -6.25% (inv) |
| 1024B | Throughput |   3.36 M/s |   3.47 M/s |   +3.25% |
| | Latency |     0.18 us |     0.19 us |   -5.56% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.14 M/s |   -1.79% |
| | Latency |     2.15 us |     2.14 us |   +0.47% (inv) |
| 131072B | Throughput |   0.10 M/s |   0.10 M/s |   -1.76% |
| | Latency |     3.91 us |     3.97 us |   -1.53% (inv) |
| 262144B | Throughput |   0.06 M/s |   0.06 M/s |   -5.94% |
| | Latency |     7.41 us |     7.90 us |   -6.61% (inv) |

#### IPC Transport
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   3.95 M/s |   4.00 M/s |   +1.31% |
| | Latency |     4.00 us |     4.68 us |  -17.00% (inv) |
| 256B | Throughput |   3.08 M/s |   2.92 M/s |   -5.19% |
| | Latency |     4.06 us |     4.71 us |  -16.01% (inv) |
| 1024B | Throughput |   1.38 M/s |   1.24 M/s |   -9.90% |
| | Latency |     3.97 us |     4.93 us |  -24.18% (inv) |
| 65536B | Throughput |   0.04 M/s |   0.04 M/s |   -2.37% |
| | Latency |    14.63 us |    17.56 us |  -20.03% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |   -2.24% |
| | Latency |    28.89 us |    29.78 us |   -3.08% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |  -11.55% |
| | Latency |    39.21 us |    49.45 us |  -26.12% (inv) |

## Running the Benchmark

```bash
# Build with benchmarks enabled
cmake -B build-bench-asio -DBUILD_BENCHMARKS=ON -DWITH_BOOST_ASIO=ON -DZMQ_CXX_STANDARD=20
cmake --build build-bench-asio --target comp_zlink_pair comp_zlink_pubsub comp_zlink_dealer_dealer comp_zlink_dealer_router comp_zlink_router_router

# Run comparison (uses cached libzmq baseline)
python3 benchwithzmq/run_comparison.py ALL

# Force refresh of libzmq baseline
python3 benchwithzmq/run_comparison.py ALL --refresh-libzmq

# Run a single pattern only
python3 benchwithzmq/run_comparison.py PAIR
```
