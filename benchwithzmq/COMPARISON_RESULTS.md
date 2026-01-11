# Performance Comparison (Tag v0.1.4 Final): Standard libzmq vs zlink (C++11 + LTO)


## PATTERN: PAIR
  [libzmq] Using cached baseline.
  > Benchmarking zlink for PAIR...
    Testing tcp | 64B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 256B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 1024B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 65536B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 131072B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 262144B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 64B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 256B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 1024B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 65536B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 131072B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 262144B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 64B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 256B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 1024B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 65536B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 131072B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 262144B: 1 2 3 4 5 6 7 8 9 10 Done

### Transport: tcp
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.52 M/s |   5.57 M/s |   +1.00% |
| | Latency |    33.95 us |    31.78 us |   +6.41% (inv) |
| 256B | Throughput |   2.95 M/s |   3.05 M/s |   +3.40% |
| | Latency |    34.09 us |    30.41 us |  +10.80% (inv) |
| 1024B | Throughput |   1.29 M/s |   1.31 M/s |   +1.89% |
| | Latency |    31.09 us |    31.47 us |   -1.24% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   +2.26% |
| | Latency |    49.77 us |    50.99 us |   -2.46% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.05 M/s |   -0.86% |
| | Latency |    64.77 us |    62.31 us |   +3.79% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   -3.83% |
| | Latency |    82.04 us |    83.23 us |   -1.46% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.91 M/s |   5.98 M/s |   +1.20% |
| | Latency |     0.07 us |     0.07 us |   +1.75% (inv) |
| 256B | Throughput |   5.24 M/s |   5.21 M/s |   -0.71% |
| | Latency |     0.07 us |     0.07 us |   -1.75% (inv) |
| 1024B | Throughput |   3.26 M/s |   3.36 M/s |   +2.94% |
| | Latency |     0.09 us |     0.09 us |   +1.37% (inv) |
| 65536B | Throughput |   0.13 M/s |   0.14 M/s |   +5.39% |
| | Latency |     2.02 us |     1.99 us |   +1.55% (inv) |
| 131072B | Throughput |   0.08 M/s |   0.08 M/s |  +11.25% |
| | Latency |     3.55 us |     3.53 us |   +0.60% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   +3.46% |
| | Latency |     6.88 us |     6.88 us |   -0.02% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.51 M/s |   5.53 M/s |   +0.31% |
| | Latency |    28.90 us |    29.63 us |   -2.52% (inv) |
| 256B | Throughput |   3.11 M/s |   3.19 M/s |   +2.45% |
| | Latency |    29.65 us |    29.49 us |   +0.54% (inv) |
| 1024B | Throughput |   1.47 M/s |   1.49 M/s |   +1.17% |
| | Latency |    28.78 us |    29.35 us |   -1.99% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   -0.28% |
| | Latency |    48.16 us |    47.54 us |   +1.28% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.05 M/s |   +3.71% |
| | Latency |    60.89 us |    59.02 us |   +3.08% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   +0.90% |
| | Latency |    81.77 us |    83.03 us |   -1.54% (inv) |

## PATTERN: PUBSUB
  [libzmq] Using cached baseline.
  > Benchmarking zlink for PUBSUB...
    Testing tcp | 64B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 256B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 1024B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 65536B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 131072B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 262144B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 64B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 256B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 1024B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 65536B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 131072B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 262144B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 64B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 256B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 1024B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 65536B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 131072B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 262144B: 1 2 3 4 5 6 7 8 9 10 Done

### Transport: tcp
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.47 M/s |   5.54 M/s |   +1.25% |
| | Latency |     0.18 us |     0.18 us |   +0.69% (inv) |
| 256B | Throughput |   2.51 M/s |   2.53 M/s |   +0.60% |
| | Latency |     0.40 us |     0.40 us |   +0.63% (inv) |
| 1024B | Throughput |   0.96 M/s |   0.96 M/s |   -0.25% |
| | Latency |     1.04 us |     1.05 us |   -0.36% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.08 M/s |   +6.42% |
| | Latency |    14.32 us |    13.37 us |   +6.64% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.05 M/s |   -4.24% |
| | Latency |    20.12 us |    21.02 us |   -4.52% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   -0.70% |
| | Latency |    36.58 us |    36.78 us |   -0.56% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.68 M/s |   5.61 M/s |   -1.27% |
| | Latency |     0.18 us |     0.18 us |   -1.42% (inv) |
| 256B | Throughput |   4.37 M/s |   4.37 M/s |   +0.01% |
| | Latency |     0.23 us |     0.23 us |   +0.00% (inv) |
| 1024B | Throughput |   2.06 M/s |   2.06 M/s |   +0.07% |
| | Latency |     0.48 us |     0.49 us |   -0.26% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.14 M/s |   +0.68% |
| | Latency |     7.24 us |     7.01 us |   +3.19% (inv) |
| 131072B | Throughput |   0.09 M/s |   0.08 M/s |  -13.10% |
| | Latency |    11.49 us |    13.35 us |  -16.24% (inv) |
| 262144B | Throughput |   0.07 M/s |   0.06 M/s |   -2.91% |
| | Latency |    15.25 us |    15.71 us |   -2.99% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.45 M/s |   5.43 M/s |   -0.47% |
| | Latency |     0.18 us |     0.18 us |   -1.37% (inv) |
| 256B | Throughput |   2.60 M/s |   2.60 M/s |   -0.11% |
| | Latency |     0.38 us |     0.39 us |   -0.33% (inv) |
| 1024B | Throughput |   1.03 M/s |   0.99 M/s |   -4.09% |
| | Latency |     0.97 us |     1.01 us |   -4.25% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   +3.45% |
| | Latency |    14.29 us |    13.86 us |   +3.06% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   +3.04% |
| | Latency |    23.68 us |    22.99 us |   +2.90% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   +1.27% |
| | Latency |    45.36 us |    44.79 us |   +1.26% (inv) |

## PATTERN: DEALER_DEALER
  [libzmq] Using cached baseline.
  > Benchmarking zlink for DEALER_DEALER...
    Testing tcp | 64B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 256B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 1024B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 65536B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 131072B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 262144B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 64B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 256B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 1024B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 65536B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 131072B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 262144B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 64B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 256B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 1024B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 65536B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 131072B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 262144B: 1 2 3 4 5 6 7 8 9 10 Done

### Transport: tcp
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.40 M/s |   5.48 M/s |   +1.48% |
| | Latency |    30.33 us |    33.40 us |  -10.14% (inv) |
| 256B | Throughput |   2.98 M/s |   2.93 M/s |   -1.38% |
| | Latency |    32.13 us |    33.46 us |   -4.12% (inv) |
| 1024B | Throughput |   1.34 M/s |   1.31 M/s |   -2.39% |
| | Latency |    32.98 us |    33.33 us |   -1.06% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   -7.11% |
| | Latency |    52.42 us |    49.61 us |   +5.36% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.05 M/s |  +10.07% |
| | Latency |    66.83 us |    64.01 us |   +4.21% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.02 M/s |   -7.08% |
| | Latency |    85.07 us |    89.70 us |   -5.44% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.92 M/s |   5.76 M/s |   -2.77% |
| | Latency |     0.07 us |     0.08 us |  -14.29% (inv) |
| 256B | Throughput |   4.99 M/s |   4.95 M/s |   -0.93% |
| | Latency |     0.08 us |     0.08 us |   +0.00% (inv) |
| 1024B | Throughput |   3.15 M/s |   3.23 M/s |   +2.70% |
| | Latency |     0.09 us |     0.10 us |   -6.67% (inv) |
| 65536B | Throughput |   0.15 M/s |   0.16 M/s |   +6.15% |
| | Latency |     1.99 us |     2.02 us |   -1.76% (inv) |
| 131072B | Throughput |   0.12 M/s |   0.11 M/s |  -10.05% |
| | Latency |     3.58 us |     3.63 us |   -1.33% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   +3.90% |
| | Latency |     6.99 us |     6.99 us |   +0.05% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.63 M/s |   5.64 M/s |   +0.18% |
| | Latency |    29.05 us |    30.86 us |   -6.23% (inv) |
| 256B | Throughput |   3.15 M/s |   3.20 M/s |   +1.79% |
| | Latency |    29.15 us |    29.09 us |   +0.20% (inv) |
| 1024B | Throughput |   1.53 M/s |   1.50 M/s |   -1.79% |
| | Latency |    30.55 us |    30.11 us |   +1.44% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   -4.91% |
| | Latency |    49.06 us |    46.68 us |   +4.85% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.04 M/s |  -13.56% |
| | Latency |    60.04 us |    61.04 us |   -1.66% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |  -15.20% |
| | Latency |    80.68 us |    82.61 us |   -2.39% (inv) |

## PATTERN: DEALER_ROUTER
  [libzmq] Using cached baseline.
  > Benchmarking zlink for DEALER_ROUTER...
    Testing tcp | 64B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 256B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 1024B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 65536B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 131072B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing tcp | 262144B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 64B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 256B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 1024B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 65536B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 131072B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing inproc | 262144B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 64B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 256B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 1024B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 65536B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 131072B: 1 2 3 4 5 6 7 8 9 10 Done
    Testing ipc | 262144B: 1 2 3 4 5 6 7 8 9 10 Done

### Transport: tcp
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.21 M/s |   5.07 M/s |   -2.76% |
| | Latency |    43.13 us |    49.03 us |  -13.69% (inv) |
| 256B | Throughput |   2.89 M/s |   2.85 M/s |   -1.30% |
| | Latency |    46.93 us |    43.87 us |   +6.53% (inv) |
| 1024B | Throughput |   1.23 M/s |   1.18 M/s |   -4.16% |
| | Latency |    43.64 us |    42.74 us |   +2.05% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.07 M/s |   -6.86% |
| | Latency |    71.45 us |    87.94 us |  -23.08% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.04 M/s |   -1.37% |
| | Latency |    85.78 us |   102.41 us |  -19.39% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   -3.95% |
| | Latency |    95.32 us |   122.02 us |  -28.01% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.18 M/s |   5.12 M/s |   -1.01% |
| | Latency |     0.11 us |     0.11 us |   +0.00% (inv) |
| 256B | Throughput |   3.90 M/s |   4.04 M/s |   +3.67% |
| | Latency |     0.11 us |     0.11 us |   -1.11% (inv) |
| 1024B | Throughput |   2.61 M/s |   2.70 M/s |   +3.29% |
| | Latency |     0.12 us |     0.13 us |   -3.06% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.13 M/s |   -5.09% |
| | Latency |     1.97 us |     1.95 us |   +0.95% (inv) |
| 131072B | Throughput |   0.09 M/s |   0.08 M/s |   -5.01% |
| | Latency |     3.71 us |     3.72 us |   -0.24% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   +3.29% |
| | Latency |     7.23 us |     7.29 us |   -0.85% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.10 M/s |   5.14 M/s |   +0.89% |
| | Latency |    41.30 us |    41.39 us |   -0.24% (inv) |
| 256B | Throughput |   2.99 M/s |   2.99 M/s |   +0.15% |
| | Latency |    38.99 us |    45.28 us |  -16.13% (inv) |
| 1024B | Throughput |   1.32 M/s |   1.40 M/s |   +5.83% |
| | Latency |    48.96 us |    43.18 us |  +11.81% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   -6.97% |
| | Latency |    86.08 us |   126.52 us |  -46.98% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   -0.74% |
| | Latency |    89.92 us |    76.07 us |  +15.40% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |  -10.04% |
| | Latency |    94.55 us |    96.25 us |   -1.80% (inv) |
