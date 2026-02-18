# STREAM Performance Report (Native-optimized build)

## Scope
- Repository: `/home/hep7/project/kairos/zlink-perf-wt`
- Script: `core/tests/scenario/stream/run_stream_compare.sh`
- Condition: same benchmark script/parameters, zlink core tuning only
- Date: 2026-02-18

## Command Parameters
- `--ccu 10000 --duration 5 --repeats 3 --inflight 1 --client-io-threads 4 --server-io-threads 2`
- Sizes: `64`, `1024`, `65536`

## Result Directories
- cppserver: `doc/plan/baseline/20260218_native_cpp_all_rerun2`
- zlink: `doc/plan/baseline/20260218_native_zlink_all_rerun2`

## Median Comparison
| size | stack | throughput(msg/s) | p95(us) | p99(us) |
|---|---|---:|---:|---:|
| 64 | cppserver | 396,579.2 | 26,155.26 | 27,797.30 |
| 64 | zlink | 150,194.0 | 68,524.62 | 84,894.53 |
| 1024 | cppserver | 358,260.4 | 28,885.09 | 30,750.73 |
| 1024 | zlink | 138,636.0 | 70,543.59 | 101,904.44 |
| 65536 | cppserver | 69,257.2 | 48,910.08 | 5,454,582.88 |
| 65536 | zlink | 36,807.0 | 148,083.58 | 5,385,806.87 |

## Delta (zlink vs cppserver)
| size | throughput ratio | p95 delta | p99 delta |
|---|---:|---:|---:|
| 64 | 37.87% | +161.99% | +205.41% |
| 1024 | 38.70% | +144.22% | +231.39% |
| 65536 | 53.15% | +202.77% | -1.26% |

## Termination Condition Check
Target condition:
- zlink throughput/latency must be equal or better than cppserver for all of `64`, `1024`, `65536`.

Current status:
- **Not met** (all 3 sizes still below cppserver throughput; p95 also behind all sizes).

## Notes
- Build was reconfigured with native optimization options enabled (`ENABLE_NATIVE_OPTIMIZATIONS=ON`, `ENABLE_NATIVE_TUNE=ON`) and rebuilt before this run.
- Several core-only tuning attempts were tested in this cycle; no change closed the gap to the target condition.
