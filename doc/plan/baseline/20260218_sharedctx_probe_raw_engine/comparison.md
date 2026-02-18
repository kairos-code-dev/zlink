# STREAM Echo Comparison

- expected_rows: 3
- actual_rows: 3
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | zlink | 234367.20 | 28.61 |
| 1024 | zlink | 216757.20 | 423.35 |
| 65536 | zlink | 67313.20 | 8414.15 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | zlink | 40848.68 | 46342.05 | 85614.83 |
| 1024 | zlink | 45303.97 | 49609.09 | 69876.04 |
| 65536 | zlink | 134920.84 | 164628.24 | 938663.58 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | zlink | 1/1 |
| 1024 | zlink | 1/1 |
| 65536 | zlink | 1/1 |
