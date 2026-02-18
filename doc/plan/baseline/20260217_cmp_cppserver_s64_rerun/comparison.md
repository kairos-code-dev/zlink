# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | cppserver | 391252.20 | 47.76 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | cppserver | 19273.02 | 26738.13 | 28682.65 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | cppserver | 3/3 |
