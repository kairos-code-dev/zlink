# zlink vs libzlink Performance Benchmark Results

## Test Environment

| Item | Value |
|------|-------|
| OS | Linux (WSL2) 6.6.87.2-microsoft-standard-WSL2 |
| CPU | Intel Core Ultra 7 265K |
| Architecture | x86_64 |
| C++ Standard | C++20 |
| CPU Affinity | `taskset -c 1` (pinned to CPU 1) |
| Iterations | 3 runs per configuration (min/max trimmed) |
| Date | 2026-01-13 |

## Methodology

- Command: `benchwithzlink-baseline/run_benchmarks.sh --with-libzlink --runs 3`
- Each configuration runs 3 iterations; min/max are dropped (median kept).
- Throughput diff = `(zlink - libzlink) / libzlink`.
- Latency diff uses inverse `(libzlink - zlink) / libzlink` (positive means lower latency for zlink).

## Summary

- Throughput diff range: -29.20% to +41.37% across all patterns/transports/sizes.
- Latency inverse diff range: -46.45% to +29.70% (positive means lower latency for zlink).
- Results captured on WSL2; expect variance across runs and hosts.

## Detailed Results

## PATTERN: PAIR
  > Benchmarking libzlink for PAIR...
    Testing tcp | 64B: 1 2 3 Done
    Testing tcp | 256B: 1 2 3 Done
    Testing tcp | 1024B: 1 2 3 Done
    Testing tcp | 65536B: 1 2 3 Done
    Testing tcp | 131072B: 1 2 3 Done
    Testing tcp | 262144B: 1 2 3 Done
    Testing inproc | 64B: 1 2 3 Done
    Testing inproc | 256B: 1 2 3 Done
    Testing inproc | 1024B: 1 2 3 Done
    Testing inproc | 65536B: 1 2 3 Done
    Testing inproc | 131072B: 1 2 3 Done
    Testing inproc | 262144B: 1 2 3 Done
    Testing ipc | 64B: 1 2 3 Done
    Testing ipc | 256B: 1 2 3 Done
    Testing ipc | 1024B: 1 2 3 Done
    Testing ipc | 65536B: 1 2 3 Done
    Testing ipc | 131072B: 1 2 3 Done
    Testing ipc | 262144B: 1 2 3 Done
  > Benchmarking zlink for PAIR...
    Testing tcp | 64B: 1 2 3 Done
    Testing tcp | 256B: 1 2 3 Done
    Testing tcp | 1024B: 1 2 3 Done
    Testing tcp | 65536B: 1 2 3 Done
    Testing tcp | 131072B: 1 2 3 Done
    Testing tcp | 262144B: 1 2 3 Done
    Testing inproc | 64B: 1 2 3 Done
    Testing inproc | 256B: 1 2 3 Done
    Testing inproc | 1024B: 1 2 3 Done
    Testing inproc | 65536B: 1 2 3 Done
    Testing inproc | 131072B: 1 2 3 Done
    Testing inproc | 262144B: 1 2 3 Done
    Testing ipc | 64B: 1 2 3 Done
    Testing ipc | 256B: 1 2 3 Done
    Testing ipc | 1024B: 1 2 3 Done
    Testing ipc | 65536B: 1 2 3 Done
    Testing ipc | 131072B: 1 2 3 Done
    Testing ipc | 262144B: 1 2 3 Done

### Transport: tcp
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   6.07 M/s |   5.72 M/s |   -5.87% |
| | Latency |     4.84 us |     5.57 us |  -15.08% (inv) |
| 256B | Throughput |   3.67 M/s |   3.29 M/s |  -10.39% |
| | Latency |     4.86 us |     5.46 us |  -12.35% (inv) |
| 1024B | Throughput |   1.39 M/s |   1.20 M/s |  -13.58% |
| | Latency |     4.97 us |     5.67 us |  -14.08% (inv) |
| 65536B | Throughput |   0.04 M/s |   0.03 M/s |   -9.97% |
| | Latency |    12.21 us |    15.25 us |  -24.90% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |   -9.89% |
| | Latency |    18.21 us |    25.29 us |  -38.88% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |   -9.22% |
| | Latency |    31.91 us |    42.33 us |  -32.65% (inv) |

### Transport: inproc
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   8.33 M/s |   8.22 M/s |   -1.35% |
| | Latency |     0.07 us |     0.07 us |   +0.00% (inv) |
| 256B | Throughput |   8.52 M/s |   8.57 M/s |   +0.65% |
| | Latency |     0.07 us |     0.07 us |   +0.00% (inv) |
| 1024B | Throughput |   4.96 M/s |   4.79 M/s |   -3.49% |
| | Latency |     0.09 us |     0.09 us |   +0.00% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.15 M/s |   +6.99% |
| | Latency |     1.94 us |     1.95 us |   -0.52% (inv) |
| 131072B | Throughput |   0.10 M/s |   0.10 M/s |   +2.21% |
| | Latency |     3.48 us |     3.36 us |   +3.45% (inv) |
| 262144B | Throughput |   0.06 M/s |   0.06 M/s |   -1.37% |
| | Latency |     6.57 us |     6.58 us |   -0.15% (inv) |

### Transport: ipc
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   6.38 M/s |   6.34 M/s |   -0.51% |
| | Latency |     4.23 us |     4.55 us |   -7.57% (inv) |
| 256B | Throughput |   4.15 M/s |   3.85 M/s |   -7.25% |
| | Latency |     4.40 us |     4.69 us |   -6.59% (inv) |
| 1024B | Throughput |   1.59 M/s |   1.53 M/s |   -3.78% |
| | Latency |     4.49 us |     4.77 us |   -6.24% (inv) |
| 65536B | Throughput |   0.04 M/s |   0.03 M/s |   -4.40% |
| | Latency |    11.88 us |    14.26 us |  -20.03% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |   -4.33% |
| | Latency |    17.85 us |    24.00 us |  -34.45% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |   -5.80% |
| | Latency |    30.06 us |    41.59 us |  -38.36% (inv) |

## PATTERN: PUBSUB
  > Benchmarking libzlink for PUBSUB...
    Testing tcp | 64B: 1 2 3 Done
    Testing tcp | 256B: 1 2 3 Done
    Testing tcp | 1024B: 1 2 3 Done
    Testing tcp | 65536B: 1 2 3 Done
    Testing tcp | 131072B: 1 2 3 Done
    Testing tcp | 262144B: 1 2 3 Done
    Testing inproc | 64B: 1 2 3 Done
    Testing inproc | 256B: 1 2 3 Done
    Testing inproc | 1024B: 1 2 3 Done
    Testing inproc | 65536B: 1 2 3 Done
    Testing inproc | 131072B: 1 2 3 Done
    Testing inproc | 262144B: 1 2 3 Done
    Testing ipc | 64B: 1 2 3 Done
    Testing ipc | 256B: 1 2 3 Done
    Testing ipc | 1024B: 1 2 3 Done
    Testing ipc | 65536B: 1 2 3 Done
    Testing ipc | 131072B: 1 2 3 Done
    Testing ipc | 262144B: 1 2 3 Done
  > Benchmarking zlink for PUBSUB...
    Testing tcp | 64B: 1 2 3 Done
    Testing tcp | 256B: 1 2 3 Done
    Testing tcp | 1024B: 1 2 3 Done
    Testing tcp | 65536B: 1 2 3 Done
    Testing tcp | 131072B: 1 2 3 Done
    Testing tcp | 262144B: 1 2 3 Done
    Testing inproc | 64B: 1 2 3 Done
    Testing inproc | 256B: 1 2 3 Done
    Testing inproc | 1024B: 1 2 3 Done
    Testing inproc | 65536B: 1 2 3 Done
    Testing inproc | 131072B: 1 2 3 Done
    Testing inproc | 262144B: 1 2 3 Done
    Testing ipc | 64B: 1 2 3 Done
    Testing ipc | 256B: 1 2 3 Done
    Testing ipc | 1024B: 1 2 3 Done
    Testing ipc | 65536B: 1 2 3 Done
    Testing ipc | 131072B: 1 2 3 Done
    Testing ipc | 262144B: 1 2 3 Done

### Transport: tcp
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   3.87 M/s |   3.94 M/s |   +2.05% |
| | Latency |     0.26 us |     0.25 us |   +3.85% (inv) |
| 256B | Throughput |   1.68 M/s |   2.26 M/s |  +34.65% |
| | Latency |     0.60 us |     0.44 us |  +26.67% (inv) |
| 1024B | Throughput |   0.61 M/s |   0.86 M/s |  +41.37% |
| | Latency |     1.65 us |     1.16 us |  +29.70% (inv) |
| 65536B | Throughput |   0.03 M/s |   0.03 M/s |  +18.25% |
| | Latency |    39.01 us |    32.99 us |  +15.43% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |   -1.70% |
| | Latency |    55.75 us |    56.71 us |   -1.72% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |  -11.64% |
| | Latency |    97.37 us |   110.19 us |  -13.17% (inv) |

### Transport: inproc
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.38 M/s |   5.88 M/s |   +9.15% |
| | Latency |     0.19 us |     0.17 us |  +10.53% (inv) |
| 256B | Throughput |   4.40 M/s |   4.97 M/s |  +13.00% |
| | Latency |     0.23 us |     0.20 us |  +13.04% (inv) |
| 1024B | Throughput |   2.38 M/s |   2.48 M/s |   +4.15% |
| | Latency |     0.42 us |     0.40 us |   +4.76% (inv) |
| 65536B | Throughput |   0.15 M/s |   0.14 M/s |   -8.70% |
| | Latency |     6.46 us |     7.08 us |   -9.60% (inv) |
| 131072B | Throughput |   0.10 M/s |   0.08 M/s |  -17.81% |
| | Latency |     9.75 us |    11.86 us |  -21.64% (inv) |
| 262144B | Throughput |   0.06 M/s |   0.06 M/s |   -2.18% |
| | Latency |    17.25 us |    17.63 us |   -2.20% (inv) |

### Transport: ipc
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   3.90 M/s |   4.05 M/s |   +3.78% |
| | Latency |     0.26 us |     0.25 us |   +3.85% (inv) |
| 256B | Throughput |   2.33 M/s |   2.41 M/s |   +3.73% |
| | Latency |     0.43 us |     0.41 us |   +4.65% (inv) |
| 1024B | Throughput |   0.89 M/s |   0.90 M/s |   +0.68% |
| | Latency |     1.12 us |     1.11 us |   +0.89% (inv) |
| 65536B | Throughput |   0.03 M/s |   0.03 M/s |   +4.07% |
| | Latency |    34.72 us |    33.36 us |   +3.92% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |  -16.53% |
| | Latency |    55.60 us |    66.61 us |  -19.80% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |   -5.07% |
| | Latency |   100.83 us |   106.22 us |   -5.35% (inv) |

## PATTERN: DEALER_DEALER
  > Benchmarking libzlink for DEALER_DEALER...
    Testing tcp | 64B: 1 2 3 Done
    Testing tcp | 256B: 1 2 3 Done
    Testing tcp | 1024B: 1 2 3 Done
    Testing tcp | 65536B: 1 2 3 Done
    Testing tcp | 131072B: 1 2 3 Done
    Testing tcp | 262144B: 1 2 3 Done
    Testing inproc | 64B: 1 2 3 Done
    Testing inproc | 256B: 1 2 3 Done
    Testing inproc | 1024B: 1 2 3 Done
    Testing inproc | 65536B: 1 2 3 Done
    Testing inproc | 131072B: 1 2 3 Done
    Testing inproc | 262144B: 1 2 3 Done
    Testing ipc | 64B: 1 2 3 Done
    Testing ipc | 256B: 1 2 3 Done
    Testing ipc | 1024B: 1 2 3 Done
    Testing ipc | 65536B: 1 2 3 Done
    Testing ipc | 131072B: 1 2 3 Done
    Testing ipc | 262144B: 1 2 3 Done
  > Benchmarking zlink for DEALER_DEALER...
    Testing tcp | 64B: 1 2 3 Done
    Testing tcp | 256B: 1 2 3 Done
    Testing tcp | 1024B: 1 2 3 Done
    Testing tcp | 65536B: 1 2 3 Done
    Testing tcp | 131072B: 1 2 3 Done
    Testing tcp | 262144B: 1 2 3 Done
    Testing inproc | 64B: 1 2 3 Done
    Testing inproc | 256B: 1 2 3 Done
    Testing inproc | 1024B: 1 2 3 Done
    Testing inproc | 65536B: 1 2 3 Done
    Testing inproc | 131072B: 1 2 3 Done
    Testing inproc | 262144B: 1 2 3 Done
    Testing ipc | 64B: 1 2 3 Done
    Testing ipc | 256B: 1 2 3 Done
    Testing ipc | 1024B: 1 2 3 Done
    Testing ipc | 65536B: 1 2 3 Done
    Testing ipc | 131072B: 1 2 3 Done
    Testing ipc | 262144B: 1 2 3 Done

### Transport: tcp
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   6.01 M/s |   5.70 M/s |   -5.05% |
| | Latency |     4.99 us |     5.37 us |   -7.62% (inv) |
| 256B | Throughput |   3.70 M/s |   3.31 M/s |  -10.62% |
| | Latency |     4.84 us |     5.27 us |   -8.88% (inv) |
| 1024B | Throughput |   1.38 M/s |   1.21 M/s |  -12.28% |
| | Latency |     4.89 us |     5.42 us |  -10.84% (inv) |
| 65536B | Throughput |   0.04 M/s |   0.03 M/s |   -3.89% |
| | Latency |    12.00 us |    16.25 us |  -35.42% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |   +6.90% |
| | Latency |    19.69 us |    25.44 us |  -29.20% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |  -29.20% |
| | Latency |    30.81 us |    45.12 us |  -46.45% (inv) |

### Transport: inproc
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   7.89 M/s |   7.53 M/s |   -4.56% |
| | Latency |     0.07 us |     0.08 us |  -14.29% (inv) |
| 256B | Throughput |   8.04 M/s |   8.03 M/s |   -0.03% |
| | Latency |     0.07 us |     0.08 us |  -14.29% (inv) |
| 1024B | Throughput |   4.75 M/s |   4.14 M/s |  -12.87% |
| | Latency |     0.09 us |     0.10 us |  -11.11% (inv) |
| 65536B | Throughput |   0.16 M/s |   0.13 M/s |  -16.62% |
| | Latency |     1.92 us |     1.96 us |   -2.08% (inv) |
| 131072B | Throughput |   0.10 M/s |   0.10 M/s |   -2.25% |
| | Latency |     3.54 us |     3.83 us |   -8.19% (inv) |
| 262144B | Throughput |   0.06 M/s |   0.06 M/s |   -5.41% |
| | Latency |     6.78 us |     7.33 us |   -8.11% (inv) |

### Transport: ipc
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   6.11 M/s |   6.00 M/s |   -1.84% |
| | Latency |     4.36 us |     4.60 us |   -5.50% (inv) |
| 256B | Throughput |   3.93 M/s |   3.71 M/s |   -5.58% |
| | Latency |     4.58 us |     4.76 us |   -3.93% (inv) |
| 1024B | Throughput |   1.48 M/s |   1.46 M/s |   -1.20% |
| | Latency |     4.57 us |     4.82 us |   -5.47% (inv) |
| 65536B | Throughput |   0.03 M/s |   0.04 M/s |   +6.43% |
| | Latency |    12.45 us |    14.15 us |  -13.65% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |   -3.90% |
| | Latency |    18.26 us |    23.99 us |  -31.38% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |  -12.18% |
| | Latency |    30.28 us |    42.39 us |  -39.99% (inv) |

## PATTERN: DEALER_ROUTER
  > Benchmarking libzlink for DEALER_ROUTER...
    Testing tcp | 64B: 1 2 3 Done
    Testing tcp | 256B: 1 2 3 Done
    Testing tcp | 1024B: 1 2 3 Done
    Testing tcp | 65536B: 1 2 3 Done
    Testing tcp | 131072B: 1 2 3 Done
    Testing tcp | 262144B: 1 2 3 Done
    Testing inproc | 64B: 1 2 3 Done
    Testing inproc | 256B: 1 2 3 Done
    Testing inproc | 1024B: 1 2 3 Done
    Testing inproc | 65536B: 1 2 3 Done
    Testing inproc | 131072B: 1 2 3 Done
    Testing inproc | 262144B: 1 2 3 Done
    Testing ipc | 64B: 1 2 3 Done
    Testing ipc | 256B: 1 2 3 Done
    Testing ipc | 1024B: 1 2 3 Done
    Testing ipc | 65536B: 1 2 3 Done
    Testing ipc | 131072B: 1 2 3 Done
    Testing ipc | 262144B: 1 2 3 Done
  > Benchmarking zlink for DEALER_ROUTER...
    Testing tcp | 64B: 1 2 3 Done
    Testing tcp | 256B: 1 2 3 Done
    Testing tcp | 1024B: 1 2 3 Done
    Testing tcp | 65536B: 1 2 3 Done
    Testing tcp | 131072B: 1 2 3 Done
    Testing tcp | 262144B: 1 2 3 Done
    Testing inproc | 64B: 1 2 3 Done
    Testing inproc | 256B: 1 2 3 Done
    Testing inproc | 1024B: 1 2 3 Done
    Testing inproc | 65536B: 1 2 3 Done
    Testing inproc | 131072B: 1 2 3 Done
    Testing inproc | 262144B: 1 2 3 Done
    Testing ipc | 64B: 1 2 3 Done
    Testing ipc | 256B: 1 2 3 Done
    Testing ipc | 1024B: 1 2 3 Done
    Testing ipc | 65536B: 1 2 3 Done
    Testing ipc | 131072B: 1 2 3 Done
    Testing ipc | 262144B: 1 2 3 Done

### Transport: tcp
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   4.47 M/s |   4.57 M/s |   +2.33% |
| | Latency |     5.05 us |     5.34 us |   -5.74% (inv) |
| 256B | Throughput |   3.10 M/s |   2.93 M/s |   -5.39% |
| | Latency |     4.95 us |     5.47 us |  -10.51% (inv) |
| 1024B | Throughput |   1.27 M/s |   1.23 M/s |   -3.15% |
| | Latency |     4.97 us |     5.52 us |  -11.07% (inv) |
| 65536B | Throughput |   0.02 M/s |   0.02 M/s |   +3.84% |
| | Latency |    11.61 us |    14.85 us |  -27.91% (inv) |
| 131072B | Throughput |   0.01 M/s |   0.01 M/s |   -7.10% |
| | Latency |    17.77 us |    24.85 us |  -39.84% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |   -0.99% |
| | Latency |    31.44 us |    42.80 us |  -36.13% (inv) |

### Transport: inproc
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   7.15 M/s |   5.89 M/s |  -17.57% |
| | Latency |     0.10 us |     0.11 us |  -10.00% (inv) |
| 256B | Throughput |   7.00 M/s |   5.75 M/s |  -17.91% |
| | Latency |     0.10 us |     0.11 us |  -10.00% (inv) |
| 1024B | Throughput |   4.27 M/s |   3.18 M/s |  -25.55% |
| | Latency |     0.12 us |     0.12 us |   +0.00% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.13 M/s |  -10.40% |
| | Latency |     1.85 us |     1.96 us |   -5.95% (inv) |
| 131072B | Throughput |   0.09 M/s |   0.08 M/s |  -11.99% |
| | Latency |     3.78 us |     3.95 us |   -4.50% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   +0.70% |
| | Latency |     6.85 us |     6.94 us |   -1.31% (inv) |

### Transport: ipc
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   4.69 M/s |   4.61 M/s |   -1.52% |
| | Latency |     4.43 us |     4.88 us |  -10.16% (inv) |
| 256B | Throughput |   3.48 M/s |   3.23 M/s |   -7.09% |
| | Latency |     4.38 us |     4.83 us |  -10.27% (inv) |
| 1024B | Throughput |   1.50 M/s |   1.39 M/s |   -7.44% |
| | Latency |     4.39 us |     4.95 us |  -12.76% (inv) |
| 65536B | Throughput |   0.02 M/s |   0.02 M/s |   +9.16% |
| | Latency |    12.09 us |    14.33 us |  -18.53% (inv) |
| 131072B | Throughput |   0.01 M/s |   0.01 M/s |   +6.14% |
| | Latency |    19.75 us |    24.94 us |  -26.28% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |   +7.48% |
| | Latency |    30.10 us |    41.14 us |  -36.68% (inv) |

## PATTERN: ROUTER_ROUTER
  > Benchmarking libzlink for ROUTER_ROUTER...
    Testing tcp | 64B: 1 2 3 Done
    Testing tcp | 256B: 1 2 3 Done
    Testing tcp | 1024B: 1 2 3 Done
    Testing tcp | 65536B: 1 2 3 Done
    Testing tcp | 131072B: 1 2 3 Done
    Testing tcp | 262144B: 1 2 3 Done
    Testing inproc | 64B: 1 2 3 Done
    Testing inproc | 256B: 1 2 3 Done
    Testing inproc | 1024B: 1 2 3 Done
    Testing inproc | 65536B: 1 2 3 Done
    Testing inproc | 131072B: 1 2 3 Done
    Testing inproc | 262144B: 1 2 3 Done
    Testing ipc | 64B: 1 2 3 Done
    Testing ipc | 256B: 1 2 3 Done
    Testing ipc | 1024B: 1 2 3 Done
    Testing ipc | 65536B: 1 2 3 Done
    Testing ipc | 131072B: 1 2 3 Done
    Testing ipc | 262144B: 1 2 3 Done
  > Benchmarking zlink for ROUTER_ROUTER...
    Testing tcp | 64B: 1 2 3 Done
    Testing tcp | 256B: 1 2 3 Done
    Testing tcp | 1024B: 1 2 3 Done
    Testing tcp | 65536B: 1 2 3 Done
    Testing tcp | 131072B: 1 2 3 Done
    Testing tcp | 262144B: 1 2 3 Done
    Testing inproc | 64B: 1 2 3 Done
    Testing inproc | 256B: 1 2 3 Done
    Testing inproc | 1024B: 1 2 3 Done
    Testing inproc | 65536B: 1 2 3 Done
    Testing inproc | 131072B: 1 2 3 Done
    Testing inproc | 262144B: 1 2 3 Done
    Testing ipc | 64B: 1 2 3 Done
    Testing ipc | 256B: 1 2 3 Done
    Testing ipc | 1024B: 1 2 3 Done
    Testing ipc | 65536B: 1 2 3 Done
    Testing ipc | 131072B: 1 2 3 Done
    Testing ipc | 262144B: 1 2 3 Done

### Transport: tcp
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   4.27 M/s |   4.15 M/s |   -2.80% |
| | Latency |     3.94 us |     4.19 us |   -6.35% (inv) |
| 256B | Throughput |   3.10 M/s |   2.81 M/s |   -9.50% |
| | Latency |     3.97 us |     4.26 us |   -7.30% (inv) |
| 1024B | Throughput |   1.27 M/s |   1.10 M/s |  -13.24% |
| | Latency |     4.05 us |     4.37 us |   -7.90% (inv) |
| 65536B | Throughput |   0.03 M/s |   0.04 M/s |   +6.56% |
| | Latency |    11.81 us |    13.45 us |  -13.89% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |   -3.05% |
| | Latency |    19.91 us |    23.23 us |  -16.68% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |  -16.28% |
| | Latency |    37.26 us |    53.36 us |  -43.21% (inv) |

### Transport: inproc
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   4.90 M/s |   4.88 M/s |   -0.36% |
| | Latency |     0.15 us |     0.15 us |   +0.00% (inv) |
| 256B | Throughput |   5.10 M/s |   5.22 M/s |   +2.23% |
| | Latency |     0.15 us |     0.16 us |   -6.67% (inv) |
| 1024B | Throughput |   3.26 M/s |   3.49 M/s |   +7.31% |
| | Latency |     0.19 us |     0.18 us |   +5.26% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.14 M/s |   +1.01% |
| | Latency |     2.06 us |     1.98 us |   +3.88% (inv) |
| 131072B | Throughput |   0.09 M/s |   0.10 M/s |   +4.44% |
| | Latency |     3.82 us |     3.84 us |   -0.52% (inv) |
| 262144B | Throughput |   0.06 M/s |   0.06 M/s |   +0.51% |
| | Latency |     7.23 us |     7.04 us |   +2.63% (inv) |

### Transport: ipc
| Size | Metric | Standard libzlink | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   4.15 M/s |   4.13 M/s |   -0.52% |
| | Latency |     3.71 us |     3.74 us |   -0.81% (inv) |
| 256B | Throughput |   3.26 M/s |   3.04 M/s |   -6.64% |
| | Latency |     3.67 us |     3.90 us |   -6.27% (inv) |
| 1024B | Throughput |   1.44 M/s |   1.25 M/s |  -12.70% |
| | Latency |     3.82 us |     3.94 us |   -3.14% (inv) |
| 65536B | Throughput |   0.04 M/s |   0.03 M/s |  -10.17% |
| | Latency |    12.56 us |    13.50 us |   -7.48% (inv) |
| 131072B | Throughput |   0.02 M/s |   0.02 M/s |   -9.30% |
| | Latency |    26.97 us |    24.90 us |   +7.68% (inv) |
| 262144B | Throughput |   0.01 M/s |   0.01 M/s |   -0.11% |
| | Latency |    37.35 us |    49.12 us |  -31.51% (inv) |
