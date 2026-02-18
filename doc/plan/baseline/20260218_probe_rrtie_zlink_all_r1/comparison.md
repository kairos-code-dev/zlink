# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 337423.40 | 41.19 |
| 1024 | zlink | 312406.20 | 610.17 |
| 65536 | zlink | 72722.20 | 9090.27 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 27198.78 | 29419.65 | 32515.06 |
| 1024 | zlink | 29748.52 | 33092.62 | 35979.23 |
| 65536 | zlink | 78844.57 | 95543.76 | 4076614.48 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 1/1 |
| 1024 | zlink | 1/1 |
| 65536 | zlink | 1/1 |
