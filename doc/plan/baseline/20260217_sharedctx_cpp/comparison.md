# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | cppserver | 382128.40 | 46.65 |
| 1024 | cppserver | 337266.40 | 658.72 |
| 65536 | cppserver | 68719.60 | 8589.95 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | cppserver | 19358.10 | 27741.16 | 30407.14 |
| 1024 | cppserver | 20877.35 | 30832.67 | 33526.80 |
| 65536 | cppserver | 32838.91 | 42635.92 | 5407252.63 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | cppserver | 3/3 |
| 1024 | cppserver | 3/3 |
| 65536 | cppserver | 3/3 |
