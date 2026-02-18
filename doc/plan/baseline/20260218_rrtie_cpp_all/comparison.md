# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | cppserver | 365124.20 | 44.57 |
| 1024 | cppserver | 331927.00 | 648.29 |
| 65536 | cppserver | 70516.80 | 8814.60 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | cppserver | 19172.58 | 28134.82 | 30222.54 |
| 1024 | cppserver | 19892.20 | 30608.14 | 33187.79 |
| 65536 | cppserver | 37149.97 | 44669.14 | 5343373.15 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | cppserver | 3/3 |
| 1024 | cppserver | 3/3 |
| 65536 | cppserver | 3/3 |
