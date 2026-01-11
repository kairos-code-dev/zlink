
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
| 64B | Throughput |   5.48 M/s |   5.47 M/s |   -0.15% |
| | Latency |    33.11 us |    33.22 us |   -0.34% (inv) |
| 256B | Throughput |   3.03 M/s |   3.00 M/s |   -0.89% |
| | Latency |    31.82 us |    33.23 us |   -4.42% (inv) |
| 1024B | Throughput |   1.32 M/s |   1.32 M/s |   +0.53% |
| | Latency |    31.61 us |    32.48 us |   -2.74% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.07 M/s |   -8.61% |
| | Latency |    53.05 us |    54.58 us |   -2.87% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.04 M/s |   -8.88% |
| | Latency |    64.61 us |    70.03 us |   -8.39% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.02 M/s |   -7.19% |
| | Latency |    85.72 us |    92.44 us |   -7.83% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.95 M/s |   5.88 M/s |   -1.31% |
| | Latency |     0.07 us |     0.07 us |   +0.00% (inv) |
| 256B | Throughput |   5.38 M/s |   5.45 M/s |   +1.38% |
| | Latency |     0.08 us |     0.08 us |   -1.64% (inv) |
| 1024B | Throughput |   3.34 M/s |   3.27 M/s |   -2.24% |
| | Latency |     0.09 us |     0.09 us |   -2.78% (inv) |
| 65536B | Throughput |   0.15 M/s |   0.14 M/s |   -5.29% |
| | Latency |     1.97 us |     2.00 us |   -1.39% (inv) |
| 131072B | Throughput |   0.08 M/s |   0.08 M/s |   -2.00% |
| | Latency |     3.52 us |     3.58 us |   -1.81% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.04 M/s |  -17.46% |
| | Latency |     6.86 us |     7.25 us |   -5.68% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.51 M/s |   5.46 M/s |   -1.01% |
| | Latency |    29.04 us |    32.14 us |  -10.68% (inv) |
| 256B | Throughput |   3.19 M/s |   3.00 M/s |   -5.88% |
| | Latency |    30.12 us |    33.24 us |  -10.35% (inv) |
| 1024B | Throughput |   1.51 M/s |   1.48 M/s |   -2.02% |
| | Latency |    29.77 us |    31.52 us |   -5.87% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.06 M/s |  -15.45% |
| | Latency |    47.65 us |    51.88 us |   -8.87% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |  -15.10% |
| | Latency |    61.18 us |    63.03 us |   -3.02% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |  -13.62% |
| | Latency |    81.24 us |    84.65 us |   -4.20% (inv) |

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
| 64B | Throughput |   5.37 M/s |   5.47 M/s |   +1.99% |
| | Latency |     0.19 us |     0.18 us |   +2.67% (inv) |
| 256B | Throughput |   2.32 M/s |   2.39 M/s |   +3.17% |
| | Latency |     0.43 us |     0.42 us |   +2.33% (inv) |
| 1024B | Throughput |   0.96 M/s |   0.94 M/s |   -1.25% |
| | Latency |     1.02 us |     1.06 us |   -3.42% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.06 M/s |   -9.73% |
| | Latency |    15.12 us |    16.77 us |  -10.94% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   -7.91% |
| | Latency |    24.27 us |    25.84 us |   -6.45% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   -9.41% |
| | Latency |    41.83 us |    46.20 us |  -10.45% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.30 M/s |   5.42 M/s |   +2.39% |
| | Latency |     0.19 us |     0.18 us |   +2.63% (inv) |
| 256B | Throughput |   4.27 M/s |   4.28 M/s |   +0.26% |
| | Latency |     0.24 us |     0.23 us |   +0.53% (inv) |
| 1024B | Throughput |   1.98 M/s |   1.76 M/s |  -11.45% |
| | Latency |     0.51 us |     0.57 us |  -12.32% (inv) |
| 65536B | Throughput |   0.15 M/s |   0.14 M/s |   -2.68% |
| | Latency |     6.79 us |     7.00 us |   -3.04% (inv) |
| 131072B | Throughput |   0.08 M/s |   0.08 M/s |   +2.54% |
| | Latency |    12.68 us |    12.18 us |   +3.93% (inv) |
| 262144B | Throughput |   0.06 M/s |   0.06 M/s |   +0.27% |
| | Latency |    16.96 us |    16.98 us |   -0.08% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.34 M/s |   5.36 M/s |   +0.50% |
| | Latency |     0.19 us |     0.19 us |   +0.66% (inv) |
| 256B | Throughput |   2.52 M/s |   2.40 M/s |   -4.60% |
| | Latency |     0.40 us |     0.42 us |   -4.39% (inv) |
| 1024B | Throughput |   1.03 M/s |   1.01 M/s |   -2.33% |
| | Latency |     0.96 us |     1.00 us |   -4.18% (inv) |
| 65536B | Throughput |   0.06 M/s |   0.06 M/s |   -0.82% |
| | Latency |    16.41 us |    16.55 us |   -0.85% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.03 M/s |  -13.38% |
| | Latency |    27.65 us |    31.57 us |  -14.14% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   -5.37% |
| | Latency |    47.41 us |    50.02 us |   -5.51% (inv) |

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
| 64B | Throughput |   5.41 M/s |   5.42 M/s |   +0.15% |
| | Latency |    32.51 us |    33.36 us |   -2.61% (inv) |
| 256B | Throughput |   2.98 M/s |   2.96 M/s |   -0.66% |
| | Latency |    32.87 us |    31.97 us |   +2.73% (inv) |
| 1024B | Throughput |   1.32 M/s |   1.29 M/s |   -2.44% |
| | Latency |    33.19 us |    33.39 us |   -0.60% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   +2.42% |
| | Latency |    55.14 us |    56.63 us |   -2.70% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   +5.96% |
| | Latency |    70.52 us |    67.89 us |   +3.72% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   +8.99% |
| | Latency |    98.71 us |    90.31 us |   +8.51% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.71 M/s |   5.70 M/s |   -0.11% |
| | Latency |     0.07 us |     0.07 us |   +1.72% (inv) |
| 256B | Throughput |   5.17 M/s |   5.04 M/s |   -2.51% |
| | Latency |     0.08 us |     0.08 us |   +0.00% (inv) |
| 1024B | Throughput |   3.19 M/s |   3.28 M/s |   +2.71% |
| | Latency |     0.10 us |     0.10 us |   +1.23% (inv) |
| 65536B | Throughput |   0.16 M/s |   0.17 M/s |   +3.96% |
| | Latency |     2.02 us |     2.03 us |   -0.68% (inv) |
| 131072B | Throughput |   0.11 M/s |   0.12 M/s |  +16.42% |
| | Latency |     3.58 us |     3.60 us |   -0.49% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   +6.61% |
| | Latency |     7.07 us |     7.02 us |   +0.64% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.50 M/s |   5.57 M/s |   +1.22% |
| | Latency |    31.85 us |    30.32 us |   +4.79% (inv) |
| 256B | Throughput |   3.09 M/s |   3.10 M/s |   +0.35% |
| | Latency |    31.07 us |    30.82 us |   +0.82% (inv) |
| 1024B | Throughput |   1.45 M/s |   1.48 M/s |   +1.98% |
| | Latency |    32.54 us |    31.36 us |   +3.63% (inv) |
| 65536B | Throughput |   0.06 M/s |   0.05 M/s |  -19.14% |
| | Latency |    54.83 us |    54.72 us |   +0.19% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |  +14.70% |
| | Latency |    68.71 us |    62.94 us |   +8.39% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   +6.46% |
| | Latency |    83.60 us |    85.44 us |   -2.19% (inv) |

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
| 64B | Throughput |   5.15 M/s |   5.08 M/s |   -1.33% |
| | Latency |    35.26 us |    42.92 us |  -21.74% (inv) |
| 256B | Throughput |   2.87 M/s |   2.86 M/s |   -0.47% |
| | Latency |    34.73 us |    51.98 us |  -49.67% (inv) |
| 1024B | Throughput |   1.27 M/s |   1.17 M/s |   -7.60% |
| | Latency |    34.90 us |    46.96 us |  -34.56% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   +2.00% |
| | Latency |    61.26 us |   100.80 us |  -64.54% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   +8.85% |
| | Latency |    70.65 us |   103.48 us |  -46.46% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   +8.83% |
| | Latency |    98.35 us |    91.19 us |   +7.28% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.08 M/s |   5.07 M/s |   -0.19% |
| | Latency |     0.11 us |     0.11 us |   +1.10% (inv) |
| 256B | Throughput |   3.89 M/s |   3.41 M/s |  -12.24% |
| | Latency |     0.12 us |     0.12 us |   -3.26% (inv) |
| 1024B | Throughput |   2.51 M/s |   2.58 M/s |   +2.69% |
| | Latency |     0.12 us |     0.13 us |   -4.04% (inv) |
| 65536B | Throughput |   0.13 M/s |   0.13 M/s |   +4.17% |
| | Latency |     1.98 us |     1.99 us |   -0.51% (inv) |
| 131072B | Throughput |   0.07 M/s |   0.09 M/s |  +23.55% |
| | Latency |     3.78 us |     3.76 us |   +0.46% (inv) |
| 262144B | Throughput |   0.04 M/s |   0.04 M/s |   -1.68% |
| | Latency |     7.20 us |     7.27 us |   -0.87% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.24 M/s |   5.04 M/s |   -3.84% |
| | Latency |    39.07 us |    44.59 us |  -14.14% (inv) |
| 256B | Throughput |   2.92 M/s |   2.94 M/s |   +0.76% |
| | Latency |    37.69 us |    43.83 us |  -16.29% (inv) |
| 1024B | Throughput |   1.44 M/s |   1.28 M/s |  -11.52% |
| | Latency |    39.94 us |    53.11 us |  -32.97% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.06 M/s |  -20.99% |
| | Latency |    61.22 us |    69.97 us |  -14.29% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   -8.58% |
| | Latency |    66.29 us |    76.19 us |  -14.93% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |  -13.07% |
| | Latency |    95.09 us |    92.19 us |   +3.05% (inv) |

## PATTERN: ROUTER_ROUTER
  [libzmq] Using cached baseline.
  > Benchmarking zlink for ROUTER_ROUTER...
    Testing tcp | 64B: 1 2 3 4 5 6 7 