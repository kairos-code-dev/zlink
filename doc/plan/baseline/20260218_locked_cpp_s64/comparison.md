# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | cppserver | 396111.80 | 48.35 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | cppserver | 18504.81 | 26630.14 | 29071.80 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | cppserver | 3/3 |
