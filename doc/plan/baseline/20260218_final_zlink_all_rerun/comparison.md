# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 368504.80 | 44.98 |
| 1024 | zlink | 345685.40 | 675.17 |
| 65536 | zlink | 76531.40 | 9566.42 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 25599.20 | 28495.40 | 31114.82 |
| 1024 | zlink | 27445.85 | 30456.46 | 31894.64 |
| 65536 | zlink | 69661.28 | 90209.88 | 4693456.41 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
| 1024 | zlink | 3/3 |
| 65536 | zlink | 3/3 |
