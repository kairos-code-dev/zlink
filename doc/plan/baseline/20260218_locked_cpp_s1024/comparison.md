# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 1024 | cppserver | 363004.20 | 708.99 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 1024 | cppserver | 20196.32 | 28693.00 | 30307.56 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 1024 | cppserver | 3/3 |
