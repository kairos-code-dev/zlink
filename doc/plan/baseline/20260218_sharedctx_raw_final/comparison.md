# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 235475.20 | 28.74 |
| 1024 | zlink | 213750.00 | 417.48 |
| 65536 | zlink | 66709.20 | 8338.65 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 40936.11 | 45839.39 | 64764.31 |
| 1024 | zlink | 45932.11 | 49296.27 | 69612.74 |
| 65536 | zlink | 135404.13 | 163303.71 | 806615.30 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
| 1024 | zlink | 3/3 |
| 65536 | zlink | 3/3 |
