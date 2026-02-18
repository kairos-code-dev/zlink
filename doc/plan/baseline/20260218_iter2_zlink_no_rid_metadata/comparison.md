# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 151588.40 | 18.50 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 43995.59 | 68301.84 | 88126.30 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
