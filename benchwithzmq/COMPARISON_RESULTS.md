
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
| 64B | Throughput |   5.52 M/s |   5.57 M/s |   +0.93% |
| | Latency |    33.95 us |    30.95 us |   +8.84% (inv) |
| 256B | Throughput |   2.95 M/s |   3.05 M/s |   +3.30% |
| | Latency |    34.09 us |    31.06 us |   +8.87% (inv) |
| 1024B | Throughput |   1.29 M/s |   1.30 M/s |   +1.02% |
| | Latency |    31.09 us |    31.09 us |   +0.01% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   +3.63% |
| | Latency |    49.77 us |    52.66 us |   -5.81% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.05 M/s |   -0.98% |
| | Latency |    64.77 us |    65.59 us |   -1.27% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   -1.62% |
| | Latency |    82.04 us |    88.86 us |   -8.31% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.91 M/s |   5.97 M/s |   +0.93% |
| | Latency |     0.07 us |     0.07 us |   +1.75% (inv) |
| 256B | Throughput |   5.24 M/s |   5.29 M/s |   +0.95% |
| | Latency |     0.07 us |     0.07 us |   +0.00% (inv) |
| 1024B | Throughput |   3.26 M/s |   3.34 M/s |   +2.46% |
| | Latency |     0.09 us |     0.09 us |   +1.37% (inv) |
| 65536B | Throughput |   0.13 M/s |   0.13 M/s |   +1.46% |
| | Latency |     2.02 us |     2.04 us |   -0.87% (inv) |
| 131072B | Throughput |   0.08 M/s |   0.08 M/s |   +8.97% |
| | Latency |     3.55 us |     3.52 us |   +0.85% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   +2.72% |
| | Latency |     6.88 us |     6.88 us |   -0.04% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.51 M/s |   5.53 M/s |   +0.34% |
| | Latency |    28.90 us |    29.81 us |   -3.12% (inv) |
| 256B | Throughput |   3.11 M/s |   3.13 M/s |   +0.76% |
| | Latency |    29.65 us |    29.62 us |   +0.13% (inv) |
| 1024B | Throughput |   1.47 M/s |   1.48 M/s |   +0.51% |
| | Latency |    28.78 us |    29.66 us |   -3.07% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   -0.90% |
| | Latency |    48.16 us |    49.49 us |   -2.77% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   +1.23% |
| | Latency |    60.89 us |    58.35 us |   +4.18% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   +4.40% |
| | Latency |    81.77 us |    81.12 us |   +0.79% (inv) |

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
| 64B | Throughput |   5.47 M/s |   5.54 M/s |   +1.12% |
| | Latency |     0.18 us |     0.18 us |   +0.69% (inv) |
| 256B | Throughput |   2.51 M/s |   2.46 M/s |   -1.85% |
| | Latency |     0.40 us |     0.41 us |   -1.57% (inv) |
| 1024B | Throughput |   0.96 M/s |   0.94 M/s |   -2.00% |
| | Latency |     1.04 us |     1.06 us |   -1.92% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.08 M/s |   +8.58% |
| | Latency |    14.32 us |    13.08 us |   +8.67% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.05 M/s |   -6.33% |
| | Latency |    20.12 us |    21.54 us |   -7.08% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   -5.31% |
| | Latency |    36.58 us |    38.61 us |   -5.55% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.68 M/s |   5.65 M/s |   -0.62% |
| | Latency |     0.18 us |     0.18 us |   +0.00% (inv) |
| 256B | Throughput |   4.37 M/s |   4.32 M/s |   -1.26% |
| | Latency |     0.23 us |     0.23 us |   -1.64% (inv) |
| 1024B | Throughput |   2.06 M/s |   2.10 M/s |   +2.05% |
| | Latency |     0.48 us |     0.47 us |   +2.06% (inv) |
| 65536B | Throughput |   0.14 M/s |   0.14 M/s |   +3.64% |
| | Latency |     7.24 us |     7.02 us |   +3.07% (inv) |
| 131072B | Throughput |   0.09 M/s |   0.08 M/s |   -5.00% |
| | Latency |    11.49 us |    12.13 us |   -5.63% (inv) |
| 262144B | Throughput |   0.07 M/s |   0.06 M/s |  -11.08% |
| | Latency |    15.25 us |    17.22 us |  -12.88% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.45 M/s |   5.46 M/s |   +0.19% |
| | Latency |     0.18 us |     0.18 us |   +0.00% (inv) |
| 256B | Throughput |   2.60 M/s |   2.58 M/s |   -0.76% |
| | Latency |     0.38 us |     0.39 us |   -0.65% (inv) |
| 1024B | Throughput |   1.03 M/s |   0.97 M/s |   -5.82% |
| | Latency |     0.97 us |     1.03 us |   -6.05% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   +5.84% |
| | Latency |    14.29 us |    13.51 us |   +5.50% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   -0.34% |
| | Latency |    23.68 us |    23.76 us |   -0.33% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   +4.94% |
| | Latency |    45.36 us |    43.19 us |   +4.79% (inv) |

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
    Testing inproc | 256B: 1 2 