# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 360408.40 | 44.00 |
| 1024 | zlink | 336581.20 | 657.39 |
| 65536 | zlink | 70128.40 | 8766.05 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 26247.24 | 28749.39 | 29996.70 |
| 1024 | zlink | 27842.14 | 31552.12 | 37707.99 |
| 65536 | zlink | 71919.33 | 110088.76 | 5025034.35 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 1/1 |
| 1024 | zlink | 1/1 |
| 65536 | zlink | 1/1 |
