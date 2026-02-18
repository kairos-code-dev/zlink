# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 349573.40 | 42.67 |
| 1024 | zlink | 327703.40 | 640.05 |
| 65536 | zlink | 74536.60 | 9317.08 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 26724.02 | 30146.95 | 33372.71 |
| 1024 | zlink | 28640.54 | 31552.22 | 33960.55 |
| 65536 | zlink | 71749.52 | 87075.47 | 4452150.21 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
| 1024 | zlink | 3/3 |
| 65536 | zlink | 3/3 |
