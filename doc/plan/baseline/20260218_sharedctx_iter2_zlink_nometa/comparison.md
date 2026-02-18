# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 207643.20 | 25.35 |
| 1024 | zlink | 196608.00 | 384.00 |
| 65536 | zlink | 44923.20 | 5615.40 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 47046.95 | 52920.66 | 63873.39 |
| 1024 | zlink | 49444.11 | 54350.25 | 64096.59 |
| 65536 | zlink | 182760.48 | 382424.71 | 1127395.89 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
| 1024 | zlink | 3/3 |
| 65536 | zlink | 3/3 |
