# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 65536 | zlink | 37110.80 | 4638.85 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 65536 | zlink | 134064.89 | 153960.84 | 5359579.91 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 65536 | zlink | 3/3 |
