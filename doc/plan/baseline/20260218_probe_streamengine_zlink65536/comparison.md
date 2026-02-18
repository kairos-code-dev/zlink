# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 65536 | zlink | 19625.40 | 2453.18 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 65536 | zlink | 149625.16 | 5515146.40 | 6263231.62 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 65536 | zlink | 3/3 |
