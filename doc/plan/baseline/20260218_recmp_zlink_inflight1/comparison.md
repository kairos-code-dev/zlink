# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 351457.60 | 42.90 |
| 1024 | zlink | 324456.80 | 633.70 |
| 65536 | zlink | 73145.80 | 9143.23 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 26764.33 | 29190.22 | 30705.47 |
| 1024 | zlink | 28962.26 | 32357.36 | 40907.03 |
| 65536 | zlink | 77718.81 | 96435.76 | 4023777.84 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
| 1024 | zlink | 3/3 |
| 65536 | zlink | 3/3 |
