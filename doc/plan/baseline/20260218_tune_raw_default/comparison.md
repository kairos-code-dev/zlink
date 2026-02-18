# STREAM Echo Comparison

- expected_rows: 2
- actual_rows: 2
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 1024 | zlink | 135357.30 | 264.37 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 1024 | zlink | 49454.43 | 70572.57 | 97342.26 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 1024 | zlink | 2/2 |
