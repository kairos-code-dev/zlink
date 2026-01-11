# Final Performance Comparison (v0.1.4): Standard libzmq vs zlink (Optimized)


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
| 64B | Throughput |   5.52 M/s |   5.52 M/s |   +0.15% |
| | Latency |    33.95 us |    35.32 us |   -4.04% (inv) |
| 256B | Throughput |   2.95 M/s |   3.03 M/s |   +2.91% |
| | Latency |    34.09 us |    31.62 us |   +7.22% (inv) |
| 1024B | Throughput |   1.29 M/s |   1.34 M/s |   +3.68% |
| | Latency |    31.09 us |    31.89 us |   -2.59% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   +0.30% |
| | Latency |    49.77 us |    50.47 us |   -1.41% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.05 M/s |   +2.50% |
| | Latency |    64.77 us |    63.06 us |   +2.64% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   -1.97% |
| | Latency |    82.04 us |    83.73 us |   -2.07% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.91 M/s |   5.97 M/s |   +0.96% |
| | Latency |     0.07 us |     0.07 us |   +1.75% (inv) |
| 256B | Throughput |   5.24 M/s |   5.17 M/s |   -1.40% |
| | Latency |     0.07 us |     0.07 us |   -1.75% (inv) |
| 1024B | Throughput |   3.26 M/s |   3.26 M/s |   +0.04% |
| | Latency |     0.09 us |     0.09 us |   -1.37% (inv) |
| 65536B | Throughput |   0.13 M/s |   0.14 M/s |   +4.32% |
| | Latency |     2.02 us |     1.98 us |   +2.16% (inv) |
| 131072B | Throughput |   0.08 M/s |   0.08 M/s |   +4.05% |
| | Latency |     3.55 us |     3.50 us |   +1.27% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   -1.04% |
| | Latency |     6.88 us |     6.84 us |   +0.56% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.51 M/s |   5.53 M/s |   +0.38% |
| | Latency |    28.90 us |    29.21 us |   -1.05% (inv) |
| 256B | Throughput |   3.11 M/s |   3.20 M/s |   +2.95% |
| | Latency |    29.65 us |    29.65 us |   +0.01% (inv) |
| 1024B | Throughput |   1.47 M/s |   1.54 M/s |   +4.85% |
| | Latency |    28.78 us |    29.68 us |   -3.14% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   -3.49% |
| | Latency |    48.16 us |    47.04 us |   +2.32% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   -0.47% |
| | Latency |    60.89 us |    59.17 us |   +2.84% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   +0.40% |
| | Latency |    81.77 us |    86.91 us |   -6.29% (inv) |

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
| 64B | Throughput |   5.47 M/s |   5.53 M/s |   +1.04% |
| | Latency |     0.18 us |     0.18 us |   +0.69% (inv) |
| 256B | Throughput |   2.51 M/s |   2.48 M/s |   -1.09% |
| | Latency |     0.40 us |     0.40 us |   -1.25% (inv) |
| 1024B | Throughput |   0.96 M/s |   0.95 M/s |   -1.17% |
| | Latency |     1.04 us |     1.06 us |   -1.20% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   +5.88% |
| | Latency |    14.32 us |    13.44 us |   +6.19% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.04 M/s |  -10.17% |
| | Latency |    20.12 us |    22.42 us |  -11.43% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   -3.37% |
| | Latency |    36.58 us |    37.81 us |   -3.37% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.68 M/s |   5.50 M/s |   -3.16% |
| | Latency |     0.18 us |     0.18 us |   -2.84% (inv) |
| 256B | Throughput |   4.37 M/s |   4.19 M/s |   -4.26% |
| | Latency |     0.23 us |     0.24 us |   -4.92% (inv) |
| 1024B | Throughput |   2.06 M/s |   2.11 M/s |   +2.22% |
| | Latency |     0.48 us |     0.47 us |   +2.06% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.14 M/s |   -0.04% |
| | Latency |     7.24 us |     7.30 us |   -0.90% (inv) |
| 131072B | Throughput |   0.09 M/s |   0.08 M/s |   -6.16% |
| | Latency |    11.49 us |    12.21 us |   -6.26% (inv) |
| 262144B | Throughput |   0.07 M/s |   0.06 M/s |   -6.84% |
| | Latency |    15.25 us |    16.47 us |   -7.97% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.45 M/s |   4.44 M/s |  -18.62% |
| | Latency |     0.18 us |     0.22 us |  -22.60% (inv) |
| 256B | Throughput |   2.60 M/s |   2.48 M/s |   -4.57% |
| | Latency |     0.38 us |     0.40 us |   -4.89% (inv) |
| 1024B | Throughput |   1.03 M/s |   0.98 M/s |   -4.96% |
| | Latency |     0.97 us |     1.02 us |   -5.28% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   +0.24% |
| | Latency |    14.29 us |    14.26 us |   +0.23% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   -2.16% |
| | Latency |    23.68 us |    24.20 us |   -2.19% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   +4.59% |
| | Latency |    45.36 us |    43.36 us |   +4.42% (inv) |

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
| 64B | Throughput |   5.40 M/s |   5.46 M/s |   +1.06% |
| | Latency |    30.33 us |    31.49 us |   -3.84% (inv) |
| 256B | Throughput |   2.98 M/s |   2.99 M/s |   +0.40% |
| | Latency |    32.13 us |    32.49 us |   -1.10% (inv) |
| 1024B | Throughput |   1.34 M/s |   1.29 M/s |   -3.81% |
| | Latency |    32.98 us |    33.14 us |   -0.47% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   +2.28% |
| | Latency |    52.42 us |    50.97 us |   +2.78% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.05 M/s |   +4.03% |
| | Latency |    66.83 us |    63.90 us |   +4.39% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   +0.54% |
| | Latency |    85.07 us |    83.84 us |   +1.45% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.92 M/s |   5.79 M/s |   -2.27% |
| | Latency |     0.07 us |     0.07 us |   -5.36% (inv) |
| 256B | Throughput |   4.99 M/s |   4.97 M/s |   -0.47% |
| | Latency |     0.08 us |     0.08 us |   +0.00% (inv) |
| 1024B | Throughput |   3.15 M/s |   3.28 M/s |   +4.16% |
| | Latency |     0.09 us |     0.10 us |   -5.33% (inv) |
| 65536B | Throughput |   0.15 M/s |   0.17 M/s |  +12.08% |
| | Latency |     1.99 us |     2.03 us |   -2.26% (inv) |
| 131072B | Throughput |   0.12 M/s |   0.12 M/s |   +3.31% |
| | Latency |     3.58 us |     3.61 us |   -0.73% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   +3.37% |
| | Latency |     6.99 us |     7.01 us |   -0.21% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.63 M/s |   5.45 M/s |   -3.09% |
| | Latency |    29.05 us |    29.37 us |   -1.12% (inv) |
| 256B | Throughput |   3.15 M/s |   3.14 M/s |   -0.36% |
| | Latency |    29.15 us |    29.36 us |   -0.70% (inv) |
| 1024B | Throughput |   1.53 M/s |   1.52 M/s |   -0.40% |
| | Latency |    30.55 us |    29.38 us |   +3.85% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   -2.64% |
| | Latency |    49.06 us |    47.21 us |   +3.76% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.04 M/s |   -4.99% |
| | Latency |    60.04 us |    59.91 us |   +0.23% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   -6.47% |
| | Latency |    80.68 us |    84.14 us |   -4.29% (inv) |

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
| 64B | Throughput |   5.21 M/s |   5.21 M/s |   -0.08% |
| | Latency |    43.13 us |    54.08 us |  -25.38% (inv) |
| 256B | Throughput |   2.89 M/s |   2.80 M/s |   -2.94% |
| | Latency |    46.93 us |    45.26 us |   +3.57% (inv) |
| 1024B | Throughput |   1.23 M/s |   1.17 M/s |   -4.56% |
| | Latency |    43.64 us |    54.86 us |  -25.73% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.07 M/s |   -7.07% |
| | Latency |    71.45 us |    87.71 us |  -22.75% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.05 M/s |   +2.48% |
| | Latency |    85.78 us |    95.04 us |  -10.79% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   -2.64% |
| | Latency |    95.32 us |    94.26 us |   +1.11% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.18 M/s |   5.16 M/s |   -0.36% |
| | Latency |     0.11 us |     0.11 us |   +0.00% (inv) |
| 256B | Throughput |   3.90 M/s |   4.00 M/s |   +2.73% |
| | Latency |     0.11 us |     0.12 us |   -2.22% (inv) |
| 1024B | Throughput |   2.61 M/s |   2.73 M/s |   +4.31% |
| | Latency |     0.12 us |     0.12 us |   +0.00% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.14 M/s |   -2.81% |
| | Latency |     1.97 us |     1.94 us |   +1.84% (inv) |
| 131072B | Throughput |   0.09 M/s |   0.08 M/s |   -7.34% |
| | Latency |     3.71 us |     3.72 us |   -0.24% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   +2.63% |
| | Latency |     7.23 us |     7.24 us |   -0.09% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.10 M/s |   5.13 M/s |   +0.62% |
| | Latency |    41.30 us |    42.41 us |   -2.70% (inv) |
| 256B | Throughput |   2.99 M/s |   2.89 M/s |   -3.36% |
| | Latency |    38.99 us |    45.44 us |  -16.54% (inv) |
| 1024B | Throughput |   1.32 M/s |   1.34 M/s |   +1.34% |
| | Latency |    48.96 us |    49.56 us |   -1.22% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.06 M/s |  -14.95% |
| | Latency |    86.08 us |    64.17 us |  +25.45% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   -2.49% |
| | Latency |    89.92 us |    77.01 us |  +14.35% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |  -12.92% |
| | Latency |    94.55 us |    92.88 us |   +1.76% (inv) |
