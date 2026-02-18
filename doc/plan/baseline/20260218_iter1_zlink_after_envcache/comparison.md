# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 149397.20 | 18.24 |
| 1024 | zlink | 140090.00 | 273.61 |
| 65536 | zlink | 37394.40 | 4674.30 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 44057.43 | 67613.89 | 97558.28 |
| 1024 | zlink | 46533.04 | 71603.51 | 100639.91 |
| 65536 | zlink | 132654.73 | 162684.18 | 5325580.22 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
| 1024 | zlink | 3/3 |
| 65536 | zlink | 3/3 |
