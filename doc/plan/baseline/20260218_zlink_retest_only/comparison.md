# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 356790.80 | 43.55 |
| 1024 | zlink | 315723.00 | 616.65 |
| 65536 | zlink | 69883.40 | 8735.42 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 26153.35 | 30647.55 | 35029.64 |
| 1024 | zlink | 29413.45 | 34117.92 | 38827.80 |
| 65536 | zlink | 75277.37 | 93995.02 | 4465530.62 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
| 1024 | zlink | 3/3 |
| 65536 | zlink | 3/3 |
