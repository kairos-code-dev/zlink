# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 65536 | zlink | 60484.00 | 7560.50 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 65536 | zlink | 107690.04 | 126852.63 | 3906489.02 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 65536 | zlink | 3/3 |
