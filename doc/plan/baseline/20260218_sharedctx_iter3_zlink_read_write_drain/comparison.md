# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 208592.80 | 25.46 |
| 1024 | zlink | 195910.00 | 382.64 |
| 65536 | zlink | 44942.80 | 5617.85 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 47243.15 | 52751.55 | 63672.20 |
| 1024 | zlink | 49632.73 | 55938.86 | 68885.91 |
| 65536 | zlink | 178506.54 | 399818.58 | 1186541.12 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
| 1024 | zlink | 3/3 |
| 65536 | zlink | 3/3 |
