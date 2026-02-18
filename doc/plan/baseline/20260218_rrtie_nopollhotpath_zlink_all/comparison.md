# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 362402.20 | 44.24 |
| 1024 | zlink | 341860.40 | 667.70 |
| 65536 | zlink | 75991.00 | 9498.88 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 25890.04 | 28986.39 | 31792.32 |
| 1024 | zlink | 27245.41 | 31346.04 | 33603.93 |
| 65536 | zlink | 73303.72 | 96509.97 | 3614383.02 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
| 1024 | zlink | 3/3 |
| 65536 | zlink | 3/3 |
