# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 1024 | zlink | 147735.20 | 288.55 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 1024 | zlink | 35303.76 | 56097.78 | 96501.48 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 1024 | zlink | 3/3 |
