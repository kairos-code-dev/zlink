# STREAM Echo Comparison

- expected_rows: 15
- actual_rows: 15
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 1024 | asio | 212603.80 | 415.24 |
| 1024 | cppserver | 320108.20 | 625.21 |
| 1024 | dotnet | 292401.40 | 571.10 |
| 1024 | zlink | 299738.60 | 585.43 |
| 1024 | cgdk10 | 243492.00 | 475.57 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 1024 | asio | 281153.58 | 331283.22 | 5365624.12 |
| 1024 | cppserver | 187345.61 | 255447.90 | 5061312.50 |
| 1024 | dotnet | 347861.48 | 393330.43 | 739078.56 |
| 1024 | zlink | 280938.96 | 378444.88 | 2982211.38 |
| 1024 | cgdk10 | 390608.91 | 758860.56 | 1251712.94 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 1024 | asio | 3/3 |
| 1024 | cppserver | 3/3 |
| 1024 | dotnet | 3/3 |
| 1024 | zlink | 3/3 |
| 1024 | cgdk10 | 3/3 |
