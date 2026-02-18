# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 205432.80 | 25.08 |
| 1024 | zlink | 189885.80 | 370.87 |
| 65536 | zlink | 46343.60 | 5792.95 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 47696.07 | 53306.99 | 72433.44 |
| 1024 | zlink | 50990.11 | 55462.81 | 73083.88 |
| 65536 | zlink | 180412.54 | 344468.19 | 883490.23 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
| 1024 | zlink | 3/3 |
| 65536 | zlink | 3/3 |
