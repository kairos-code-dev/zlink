# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 369510.60 | 45.11 |
| 1024 | zlink | 342765.80 | 669.46 |
| 65536 | zlink | 74306.40 | 9288.30 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 25476.21 | 29161.44 | 32848.09 |
| 1024 | zlink | 27306.87 | 30874.28 | 34242.51 |
| 65536 | zlink | 77946.49 | 92055.26 | 3924038.92 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 1/1 |
| 1024 | zlink | 1/1 |
| 65536 | zlink | 1/1 |
