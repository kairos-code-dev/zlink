# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | cppserver | 396579.20 | 48.41 |
| 1024 | cppserver | 358260.40 | 699.73 |
| 65536 | cppserver | 69257.20 | 8657.15 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | cppserver | 24180.82 | 26155.26 | 27797.30 |
| 1024 | cppserver | 20854.94 | 28885.09 | 30750.73 |
| 65536 | cppserver | 40197.12 | 48910.08 | 5454582.88 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | cppserver | 3/3 |
| 1024 | cppserver | 3/3 |
| 65536 | cppserver | 3/3 |
