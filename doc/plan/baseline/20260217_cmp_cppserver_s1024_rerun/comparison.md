# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 1024 | cppserver | 351254.60 | 686.04 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 1024 | cppserver | 20048.73 | 29368.80 | 32717.28 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 1024 | cppserver | 3/3 |
