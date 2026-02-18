# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 150194.00 | 18.33 |
| 1024 | zlink | 138636.00 | 270.77 |
| 65536 | zlink | 36807.00 | 4600.88 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 44062.71 | 68524.62 | 84894.53 |
| 1024 | zlink | 49493.84 | 70543.59 | 101904.44 |
| 65536 | zlink | 117575.79 | 148083.58 | 5385806.87 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
| 1024 | zlink | 3/3 |
| 65536 | zlink | 3/3 |
