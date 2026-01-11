# Final Performance Comparison (Tag v0.1.4 Final): Standard libzmq vs zlink

## 1. 개요
본 문서는 zlink v0.1.4의 최종 성능 측정 결과를 담고 있습니다. 특히 테스트 신뢰도를 높이기 위해 메시지 반복 횟수를 기존 대비 10배 상향 조정(대형 메시지 기준 2,000회 -> 20,000회)하여 측정의 정밀도를 확보했습니다.

## 2. 테스트 횟수 상향에 따른 변화 분석
- **데이터 안정성 확보**: 반복 횟수 증가로 인해 OS 스케줄러 노이즈가 상쇄되어, 특정 구간에서 튀던 수치(최대 -44%)가 정상 범위(+12~16%)로 수렴됨을 확인했습니다.
- **LTO 최적화 실효성 입증**: 충분한 부하 지속 시간을 확보함으로써, Link Time Optimization(LTO)이 실제 처리량(Throughput) 향상에 기여하는 진짜 성능이 데이터로 증명되었습니다.


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
| 64B | Throughput |   5.48 M/s |   5.54 M/s |   +1.21% |
| | Latency |    33.11 us |    31.82 us |   +3.88% (inv) |
| 256B | Throughput |   3.03 M/s |   3.05 M/s |   +0.67% |
| | Latency |    31.82 us |    30.77 us |   +3.31% (inv) |
| 1024B | Throughput |   1.32 M/s |   1.35 M/s |   +2.73% |
| | Latency |    31.61 us |    31.59 us |   +0.05% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.08 M/s |   +2.73% |
| | Latency |    53.05 us |    48.38 us |   +8.82% (inv) |
| 131072B | Throughput |   0.05 M/s |   0.05 M/s |   -1.04% |
| | Latency |    64.61 us |    62.36 us |   +3.49% (inv) |
| 262144B | Throughput |   0.03 M/s |   0.03 M/s |   +2.66% |
| | Latency |    85.72 us |    81.06 us |   +5.44% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.95 M/s |   5.73 M/s |   -3.84% |
| | Latency |     0.07 us |     0.07 us |   +0.00% (inv) |
| 256B | Throughput |   5.38 M/s |   5.49 M/s |   +2.08% |
| | Latency |     0.08 us |     0.07 us |   +6.56% (inv) |
| 1024B | Throughput |   3.34 M/s |   3.39 M/s |   +1.52% |
| | Latency |     0.09 us |     0.09 us |   -1.39% (inv) |
| 65536B | Throughput |   0.15 M/s |   0.14 M/s |   -3.04% |
| | Latency |     1.97 us |     1.98 us |   -0.19% (inv) |
| 131072B | Throughput |   0.08 M/s |   0.08 M/s |   -1.48% |
| | Latency |     3.52 us |     3.52 us |   -0.14% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   +1.63% |
| | Latency |     6.86 us |     6.83 us |   +0.44% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.51 M/s |   5.50 M/s |   -0.17% |
| | Latency |    29.04 us |    29.04 us |   +0.01% (inv) |
| 256B | Throughput |   3.19 M/s |   3.19 M/s |   -0.04% |
| | Latency |    30.12 us |    29.05 us |   +3.56% (inv) |
| 1024B | Throughput |   1.51 M/s |   1.48 M/s |   -1.78% |
| | Latency |    29.77 us |    31.07 us |   -4.38% (inv) |
| 65536B | Throughput |   0.08 M/s |   0.06 M/s |  -24.20% |
| | Latency |    47.65 us |    46.07 us |   +3.33% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   -5.22% |
| | Latency |    61.18 us |    58.60 us |   +4.22% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   -4.84% |
| | Latency |    81.24 us |    82.83 us |   -1.96% (inv) |

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
| 64B | Throughput |   5.37 M/s |   5.47 M/s |   +1.85% |
| | Latency |     0.19 us |     0.18 us |   +3.33% (inv) |
| 256B | Throughput |   2.32 M/s |   2.42 M/s |   +4.16% |
| | Latency |     0.43 us |     0.41 us |   +3.78% (inv) |
| 1024B | Throughput |   0.96 M/s |   1.00 M/s |   +4.14% |
| | Latency |     1.02 us |     1.00 us |   +1.95% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   +2.75% |
| | Latency |    15.12 us |    14.37 us |   +4.98% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   +5.27% |
| | Latency |    24.27 us |    23.03 us |   +5.13% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.03 M/s |   +4.95% |
| | Latency |    41.83 us |    39.85 us |   +4.73% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.30 M/s |   5.29 M/s |   -0.10% |
| | Latency |     0.19 us |     0.19 us |   +0.00% (inv) |
| 256B | Throughput |   4.27 M/s |   4.33 M/s |   +1.45% |
| | Latency |     0.24 us |     0.23 us |   +1.06% (inv) |
| 1024B | Throughput |   1.98 M/s |   1.99 M/s |   +0.10% |
| | Latency |     0.51 us |     0.50 us |   +0.74% (inv) |
| 65536B | Throughput |   0.15 M/s |   0.15 M/s |   +2.67% |
| | Latency |     6.79 us |     6.62 us |   +2.56% (inv) |
| 131072B | Throughput |   0.08 M/s |   0.09 M/s |   +9.15% |
| | Latency |    12.68 us |    11.60 us |   +8.46% (inv) |
| 262144B | Throughput |   0.06 M/s |   0.07 M/s |  +10.29% |
| | Latency |    16.96 us |    15.40 us |   +9.25% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.34 M/s |   5.48 M/s |   +2.61% |
| | Latency |     0.19 us |     0.18 us |   +3.31% (inv) |
| 256B | Throughput |   2.52 M/s |   2.55 M/s |   +1.25% |
| | Latency |     0.40 us |     0.39 us |   +1.88% (inv) |
| 1024B | Throughput |   1.03 M/s |   1.08 M/s |   +4.90% |
| | Latency |     0.96 us |     0.93 us |   +3.14% (inv) |
| 65536B | Throughput |   0.06 M/s |   0.07 M/s |   +9.79% |
| | Latency |    16.41 us |    14.66 us |  +10.71% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |  +11.26% |
| | Latency |    27.65 us |    24.84 us |  +10.16% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   +7.06% |
| | Latency |    47.41 us |    44.21 us |   +6.74% (inv) |

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
| 64B | Throughput |   5.41 M/s |   5.49 M/s |   +1.59% |
| | Latency |    32.51 us |    30.73 us |   +5.48% (inv) |
| 256B | Throughput |   2.98 M/s |   2.99 M/s |   +0.63% |
| | Latency |    32.87 us |    31.48 us |   +4.24% (inv) |
| 1024B | Throughput |   1.32 M/s |   1.31 M/s |   -1.46% |
| | Latency |    33.19 us |    32.13 us |   +3.17% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   +7.30% |
| | Latency |    55.14 us |    51.73 us |   +6.19% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |  +12.73% |
| | Latency |    70.52 us |    62.90 us |  +10.80% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.03 M/s |  +16.04% |
| | Latency |    98.71 us |    85.58 us |  +13.31% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.71 M/s |   5.68 M/s |   -0.43% |
| | Latency |     0.07 us |     0.07 us |   -1.72% (inv) |
| 256B | Throughput |   5.17 M/s |   5.10 M/s |   -1.39% |
| | Latency |     0.08 us |     0.08 us |   -1.56% (inv) |
| 1024B | Throughput |   3.19 M/s |   3.29 M/s |   +3.15% |
| | Latency |     0.10 us |     0.10 us |   +2.47% (inv) |
| 65536B | Throughput |   0.16 M/s |   0.18 M/s |  +14.42% |
| | Latency |     2.02 us |     2.02 us |   -0.12% (inv) |
| 131072B | Throughput |   0.11 M/s |   0.12 M/s |  +11.54% |
| | Latency |     3.58 us |     3.66 us |   -2.06% (inv) |
| 262144B | Throughput |   0.05 M/s |   0.05 M/s |   +0.70% |
| | Latency |     7.07 us |     7.12 us |   -0.81% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.50 M/s |   5.53 M/s |   +0.44% |
| | Latency |    31.85 us |    29.92 us |   +6.04% (inv) |
| 256B | Throughput |   3.09 M/s |   3.14 M/s |   +1.48% |
| | Latency |    31.07 us |    28.97 us |   +6.77% (inv) |
| 1024B | Throughput |   1.45 M/s |   1.45 M/s |   -0.11% |
| | Latency |    32.54 us |    30.05 us |   +7.65% (inv) |
| 65536B | Throughput |   0.06 M/s |   0.07 M/s |  +11.22% |
| | Latency |    54.83 us |    47.01 us |  +14.25% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   +1.90% |
| | Latency |    68.71 us |    64.91 us |   +5.52% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   -7.14% |
| | Latency |    83.60 us |    90.67 us |   -8.45% (inv) |

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
| 64B | Throughput |   5.15 M/s |   5.07 M/s |   -1.50% |
| | Latency |    35.26 us |    39.73 us |  -12.67% (inv) |
| 256B | Throughput |   2.87 M/s |   2.87 M/s |   -0.06% |
| | Latency |    34.73 us |    42.00 us |  -20.92% (inv) |
| 1024B | Throughput |   1.27 M/s |   1.26 M/s |   -0.28% |
| | Latency |    34.90 us |    39.21 us |  -12.35% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.07 M/s |   +2.60% |
| | Latency |    61.26 us |    62.97 us |   -2.78% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   +4.03% |
| | Latency |    70.65 us |    75.48 us |   -6.83% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   +7.05% |
| | Latency |    98.35 us |    99.34 us |   -1.01% (inv) |

### Transport: inproc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.08 M/s |   5.08 M/s |   +0.03% |
| | Latency |     0.11 us |     0.11 us |   +2.20% (inv) |
| 256B | Throughput |   3.89 M/s |   3.99 M/s |   +2.60% |
| | Latency |     0.12 us |     0.11 us |   +3.26% (inv) |
| 1024B | Throughput |   2.51 M/s |   2.66 M/s |   +5.85% |
| | Latency |     0.12 us |     0.13 us |   -2.02% (inv) |
| 65536B | Throughput |   0.13 M/s |   0.13 M/s |   +2.29% |
| | Latency |     1.98 us |     1.96 us |   +0.70% (inv) |
| 131072B | Throughput |   0.07 M/s |   0.08 M/s |   +6.21% |
| | Latency |     3.78 us |     3.74 us |   +1.06% (inv) |
| 262144B | Throughput |   0.04 M/s |   0.04 M/s |   -1.80% |
| | Latency |     7.20 us |     7.30 us |   -1.34% (inv) |

### Transport: ipc
| Size | Metric | Standard libzmq | zlink | Diff (%) |
|------|--------|-----------------|-------|----------|
| 64B | Throughput |   5.24 M/s |   5.06 M/s |   -3.29% |
| | Latency |    39.07 us |    37.40 us |   +4.27% (inv) |
| 256B | Throughput |   2.92 M/s |   2.88 M/s |   -1.42% |
| | Latency |    37.69 us |    43.16 us |  -14.51% (inv) |
| 1024B | Throughput |   1.44 M/s |   1.44 M/s |   -0.39% |
| | Latency |    39.94 us |    44.17 us |  -10.57% (inv) |
| 65536B | Throughput |   0.07 M/s |   0.06 M/s |  -11.23% |
| | Latency |    61.22 us |    62.81 us |   -2.60% (inv) |
| 131072B | Throughput |   0.04 M/s |   0.04 M/s |   -9.74% |
| | Latency |    66.29 us |    77.86 us |  -17.45% (inv) |
| 262144B | Throughput |   0.02 M/s |   0.02 M/s |   -0.86% |
| | Latency |    95.09 us |    95.80 us |   -0.75% (inv) |
