# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | cppserver | 328330.20 | 40.08 |
| 1024 | cppserver | 330697.40 | 645.89 |
| 65536 | cppserver | 69409.20 | 8676.15 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | cppserver | 20330.96 | 29474.43 | 45128.86 |
| 1024 | cppserver | 20103.43 | 30136.44 | 32485.19 |
| 65536 | cppserver | 39813.86 | 47602.97 | 5459040.16 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | cppserver | 3/3 |
| 1024 | cppserver | 3/3 |
| 65536 | cppserver | 3/3 |
