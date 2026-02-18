# STREAM Echo Comparison

- expected_rows: 1
- actual_rows: 1
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 1024 | zlink | 183687.00 | 358.76 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 1024 | zlink | 51071.62 | 66892.82 | 77553.18 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 1024 | zlink | 1/1 |
