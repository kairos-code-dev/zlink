# STREAM Echo Comparison

- expected_rows: 9
- actual_rows: 9
- row_mismatch: False
- invalid_pass_rows: 0

## Throughput Median (PASS rows)

| size | stack | msg/s | MiB/s |
|---:|---|---:|---:|
| 64 | cppserver | 377011.40 | 46.02 |
| 1024 | cppserver | 338302.20 | 660.75 |
| 65536 | cppserver | 70339.60 | 8792.45 |

## Latency Median (PASS rows)

| size | stack | p50_us | p95_us | p99_us |
|---:|---|---:|---:|---:|
| 64 | cppserver | 19031.25 | 28274.49 | 31123.80 |
| 1024 | cppserver | 20312.81 | 31639.47 | 35610.74 |
| 65536 | cppserver | 34954.41 | 42104.81 | 5382514.71 |

## PASS/FAIL

| size | stack | pass/total |
|---:|---|---:|
| 64 | cppserver | 3/3 |
| 1024 | cppserver | 3/3 |
| 65536 | cppserver | 3/3 |
