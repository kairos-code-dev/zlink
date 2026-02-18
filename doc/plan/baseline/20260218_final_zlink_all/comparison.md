# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 372251.60 | 45.44 |
| 1024 | zlink | 346414.40 | 676.59 |
| 65536 | zlink | 76755.00 | 9594.38 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 25546.45 | 28707.78 | 31090.65 |
| 1024 | zlink | 27277.30 | 30691.18 | 33235.35 |
| 65536 | zlink | 73692.00 | 94447.65 | 3976073.24 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 3/3 |
| 1024 | zlink | 3/3 |
| 65536 | zlink | 3/3 |
