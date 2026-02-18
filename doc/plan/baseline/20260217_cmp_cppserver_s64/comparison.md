# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | cppserver | 390650.20 | 47.69 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | cppserver | 18865.85 | 26802.95 | 31951.77 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | cppserver | 3/3 |
