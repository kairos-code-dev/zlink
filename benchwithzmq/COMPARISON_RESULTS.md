# Performance Comparison (Tag v0.1.4): Standard libzmq vs zlink (Clean Build)


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
| 64B | Throughput |   5.41 M/s |   5.61 M/s |   +3.60% |
| | Latency |    37.70 us |    31.08 us |  +17.56% (inv) |
| 256B | Throughput |   3.09 M/s |   2.96 M/s |   -4.02% |
| | Latency |    30.14 us |    33.38 us |  -10.72% (inv) |
| 1024B | Throughput |   1.36 M/s |   1.32 M/s |   -3.27% |
| | Latency |    31.51 us |    30.20 us |   +4.15% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   +1.09% |
| | Latency |    49.59 us |    49.21 us |   +0.77% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.05 M/s |   -2.89% |
| | Latency |    62.15 us |    63.70 us |   -2.49% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   +0.30% |
| | Latency |    84.23 us |    82.50 us |   +2.05% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.99 M/s |   5.93 M/s |   -1.00% |
| | Latency |     0.07 us |     0.07 us |   -1.79% (inv) |
| 256B | Throughput |   5.16 M/s |   5.25 M/s |   +1.87% |
| | Latency |     0.08 us |     0.08 us |   -1.67% (inv) |
| 1024B | Throughput |   3.40 M/s |   3.22 M/s |   -5.39% |
| | Latency |     0.09 us |     0.09 us |   +0.00% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.13 M/s |   -8.85% |
| | Latency |     1.98 us |     2.01 us |   -1.58% (inv) |
| 131072B | Throughput |   0.09 M/s |   0.08 M/s |   -5.99% |
| | Latency |     3.49 us |     3.55 us |   -1.83% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   -4.53% |
| | Latency |     6.85 us |     6.95 us |   -1.44% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.63 M/s |   5.50 M/s |   -2.36% |
| | Latency |    30.19 us |    28.96 us |   +4.04% (inv) |
| 256B | Throughput |   3.14 M/s |   3.09 M/s |   -1.46% |
| | Latency |    28.30 us |    30.22 us |   -6.77% (inv) |
| 1024B | Throughput |   1.52 M/s |   1.42 M/s |   -6.68% |
| | Latency |    28.75 us |    28.70 us |   +0.18% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   -0.99% |
| | Latency |    45.97 us |    47.38 us |   -3.05% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   -0.68% |
| | Latency |    58.02 us |    57.94 us |   +0.15% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   -6.39% |
| | Latency |    79.94 us |    81.92 us |   -2.48% (inv) |

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
| 64B | Throughput |   5.54 M/s |   5.49 M/s |   -0.92% |
| | Latency |     0.18 us |     0.18 us |   -0.69% (inv) |
| 256B | Throughput |   2.59 M/s |   2.40 M/s |   -7.34% |
| | Latency |     0.39 us |     0.42 us |   -8.12% (inv) |
| 1024B | Throughput |   0.99 M/s |   0.96 M/s |   -2.87% |
| | Latency |     1.01 us |     1.04 us |   -2.96% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   +0.50% |
| | Latency |    13.92 us |    13.72 us |   +1.42% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.05 M/s |   -2.90% |
| | Latency |    20.60 us |    21.24 us |   -3.10% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   -6.04% |
| | Latency |    36.43 us |    37.85 us |   -3.88% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.57 M/s |   5.48 M/s |   -1.59% |
| | Latency |     0.18 us |     0.18 us |   -2.11% (inv) |
| 256B | Throughput |   4.44 M/s |   4.47 M/s |   +0.65% |
| | Latency |     0.23 us |     0.22 us |   +0.56% (inv) |
| 1024B | Throughput |   2.15 M/s |   2.10 M/s |   -2.43% |
| | Latency |     0.47 us |     0.48 us |   -1.60% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.15 M/s |   +7.27% |
| | Latency |     7.44 us |     6.80 us |   +8.54% (inv) |
| 131072B | Throughput |   0.08 M/s |   0.09 M/s |   +4.34% |
| | Latency |    12.01 us |    11.46 us |   +4.53% (inv) |
| 262144B | Throughput |   0.06 M/s |   0.07 M/s |   +5.80% |
| | Latency |    15.88 us |    14.97 us |   +5.70% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.43 M/s |   5.40 M/s |   -0.61% |
| | Latency |     0.18 us |     0.19 us |   -2.05% (inv) |
| 256B | Throughput |   2.68 M/s |   2.66 M/s |   -0.50% |
| | Latency |     0.37 us |     0.38 us |   -0.67% (inv) |
| 1024B | Throughput |   1.00 M/s |   1.03 M/s |   +2.86% |
| | Latency |     1.00 us |     0.97 us |   +2.86% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   +1.12% |
| | Latency |    14.12 us |    13.95 us |   +1.24% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   -0.40% |
| | Latency |    22.77 us |    22.86 us |   -0.40% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   -3.27% |
| | Latency |    42.26 us |    43.68 us |   -3.37% (inv) |

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
| 64B | Throughput |   5.47 M/s |   5.44 M/s |   -0.59% |
| | Latency |    32.24 us |    31.87 us |   +1.14% (inv) |
| 256B | Throughput |   3.05 M/s |   3.02 M/s |   -1.25% |
| | Latency |    30.64 us |    31.86 us |   -3.99% (inv) |
| 1024B | Throughput |   1.32 M/s |   1.35 M/s |   +2.14% |
| | Latency |    32.19 us |    31.95 us |   +0.75% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   -3.93% |
| | Latency |    50.74 us |    51.56 us |   -1.61% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.05 M/s |   -8.05% |
| | Latency |    62.17 us |    62.93 us |   -1.22% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   -7.39% |
| | Latency |    83.40 us |    82.05 us |   +1.62% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.75 M/s |   5.73 M/s |   -0.41% |
| | Latency |     0.07 us |     0.07 us |   -3.57% (inv) |
| 256B | Throughput |   5.04 M/s |   4.91 M/s |   -2.55% |
| | Latency |     0.08 us |     0.08 us |   -3.33% (inv) |
| 1024B | Throughput |   3.26 M/s |   3.29 M/s |   +1.08% |
| | Latency |     0.10 us |     0.09 us |   +3.85% (inv) |
| 65536B | Throughput |   0.19 M/s |   0.16 M/s |  -19.84% |
| | Latency |     1.98 us |     2.03 us |   -2.65% (inv) |
| 131072B | Throughput |   0.13 M/s |   0.14 M/s |   +7.93% |
| | Latency |     3.58 us |     3.59 us |   -0.21% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   -7.56% |
| | Latency |     6.92 us |     7.00 us |   -1.21% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.57 M/s |   5.48 M/s |   -1.76% |
| | Latency |    29.43 us |    29.76 us |   -1.12% (inv) |
| 256B | Throughput |   3.15 M/s |   3.12 M/s |   -1.08% |
| | Latency |    29.44 us |    30.72 us |   -4.34% (inv) |
| 1024B | Throughput |   1.52 M/s |   1.50 M/s |   -1.60% |
| | Latency |    29.27 us |    29.72 us |   -1.54% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   -5.79% |
| | Latency |    48.24 us |    47.32 us |   +1.91% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.04 M/s |   -3.33% |
| | Latency |    60.96 us |    57.93 us |   +4.97% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   -4.60% |
| | Latency |    84.09 us |    83.33 us |   +0.91% (inv) |

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
| 64B | Throughput |   5.08 M/s |   5.16 M/s |   +1.58% |
| | Latency |    46.92 us |    47.11 us |   -0.40% (inv) |
| 256B | Throughput |   2.86 M/s |   2.88 M/s |   +0.79% |
| | Latency |    41.78 us |    42.81 us |   -2.46% (inv) |
| 1024B | Throughput |   1.27 M/s |   1.31 M/s |   +3.02% |
| | Latency |    42.76 us |    40.99 us |   +4.14% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.07 M/s |   -5.67% |
| | Latency |    68.01 us |    73.99 us |   -8.80% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.05 M/s |   -0.29% |
| | Latency |    74.22 us |   104.65 us |  -40.99% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   +3.96% |
| | Latency |    98.21 us |   108.00 us |   -9.97% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.19 M/s |   5.09 M/s |   -1.81% |
| | Latency |     0.11 us |     0.11 us |   -1.14% (inv) |
| 256B | Throughput |   4.04 M/s |   4.11 M/s |   +1.87% |
| | Latency |     0.11 us |     0.11 us |   +1.12% (inv) |
| 1024B | Throughput |   2.68 M/s |   2.85 M/s |   +6.29% |
| | Latency |     0.12 us |     0.12 us |   -2.06% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.14 M/s |   +5.56% |
| | Latency |     1.97 us |     1.94 us |   +1.52% (inv) |
| 131072B | Throughput |   0.08 M/s |   0.09 M/s |   +4.15% |
| | Latency |     3.71 us |     3.69 us |   +0.64% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   +3.77% |
| | Latency |     7.12 us |     7.05 us |   +0.93% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.18 M/s |   5.21 M/s |   +0.64% |
| | Latency |    39.08 us |    38.81 us |   +0.69% (inv) |
| 256B | Throughput |   2.97 M/s |   2.97 M/s |   -0.07% |
| | Latency |    47.12 us |    42.89 us |   +8.97% (inv) |
| 1024B | Throughput |   1.45 M/s |   1.40 M/s |   -3.58% |
| | Latency |    39.33 us |    40.72 us |   -3.52% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.08 M/s |   +1.22% |
| | Latency |    55.06 us |    93.89 us |  -70.51% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   -1.47% |
| | Latency |    87.18 us |   103.26 us |  -18.45% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   +2.03% |
| | Latency |    94.61 us |    94.27 us |   +0.36% (inv) |
