# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 65536 | cppserver | 73707.40 | 9213.42 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 65536 | cppserver | 28296.38 | 38438.17 | 5447234.30 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 65536 | cppserver | 3/3 |
