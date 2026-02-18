# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 65536 | cppserver | 72226.80 | 9028.35 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 65536 | cppserver | 27653.47 | 36922.54 | 5466564.00 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 65536 | cppserver | 3/3 |
