# STREAM Echo Comparison

- expected_rows: 1
- actual_rows: 1
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 334256.80 | 40.80 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 27634.08 | 32677.97 | 36857.47 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 1/1 |
