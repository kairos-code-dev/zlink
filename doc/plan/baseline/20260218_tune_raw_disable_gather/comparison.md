# STREAM Echo Comparison

- expected_rows: 2
- actual_rows: 2
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 1024 | zlink | 137094.70 | 267.76 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 1024 | zlink | 49198.12 | 72725.30 | 95512.74 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 1024 | zlink | 2/2 |
