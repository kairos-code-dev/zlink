# STREAM Performance Iteration Report (shared io_context)

Date: 2026-02-18
Workspace: `/home/hep7/project/kairos/zlink-perf-wt`

## What changed
- `ctx_t` now creates one shared Boost.Asio `io_context` for I/O threads and keeps it alive with a work guard.
- `asio_poller_t` can use shared `io_context` from `ctx_t` (fallback to owned context remains for standalone poller tests).
- STREAM engine default selection on TCP listener/connecter changed to `asio_stream_engine_t` unless explicitly disabled by env:
  - `ZLINK_TCP_STREAM_ENGINE=raw` (or `0/off/false`)
  - `ZLINK_TCP_STREAM_LISTENER_ENGINE=raw`
  - `ZLINK_TCP_STREAM_CONNECTER_ENGINE=raw`

## Benchmark command
```bash
RESULT_DIR=doc/plan/baseline/20260217_sharedctx_cpp \
./core/tests/scenario/stream/run_stream_compare.sh \
  --stack cppserver --size all --ccu 10000 --duration 5 --repeats 3 \
  --inflight 1 --client-io-threads 4 --server-io-threads 2

RESULT_DIR=doc/plan/baseline/20260217_sharedctx_zlink \
./core/tests/scenario/stream/run_stream_compare.sh \
  --stack zlink --size all --ccu 10000 --duration 5 --repeats 3 \
  --inflight 1 --client-io-threads 4 --server-io-threads 2
```

## Median results
| size | cppserver throughput (msg/s) | zlink throughput (msg/s) | zlink / cppserver | zlink throughput vs previous zlink baseline* |
|---:|---:|---:|---:|---:|
| 64 | 382,128.4 | 205,432.8 | 53.76% | +36.78% |
| 1024 | 337,266.4 | 189,885.8 | 56.30% | +36.97% |
| 65536 | 68,719.6 | 46,343.6 | 67.44% | +25.91% |

\* previous zlink baseline: `doc/plan/baseline/20260218_native_zlink_all_rerun2/summary.json`

## Latency notes (zlink vs previous zlink baseline)
- 64: p95 `68,524.62us -> 53,306.99us` (-22.21%), p99 `84,894.53us -> 72,433.44us` (-14.68%)
- 1024: p95 `70,543.59us -> 55,462.81us` (-21.38%), p99 `101,904.44us -> 73,083.88us` (-28.28%)
- 65536: p95 `148,083.58us -> 344,468.19us` (+132.62%), p99 `5,385,806.87us -> 883,490.23us` (-83.60%)

## Artifacts
- cppserver run: `doc/plan/baseline/20260217_sharedctx_cpp`
- zlink run: `doc/plan/baseline/20260217_sharedctx_zlink`

## Conclusion
- Throughput improved significantly over the previous zlink baseline on all 3 sizes.
- Termination condition (`zlink >= cppserver` on 64/1024/65536) is **not met yet**.

## Additional experiment (rejected)
- Tried removing routing-id frame metadata propagation in `stream.cpp` hot path.
- Result (`doc/plan/baseline/20260218_sharedctx_iter2_zlink_nometa`) improved 64/1024 slightly but regressed 65536 throughput/latency, so it was not kept as final.
- Tried read/write-drain bias in `asio_stream_engine` to reduce read-ahead pressure.
- Result (`doc/plan/baseline/20260218_sharedctx_iter3_zlink_read_write_drain`) improved 64/1024 slightly but regressed 65536 throughput, so it was not kept as final.
