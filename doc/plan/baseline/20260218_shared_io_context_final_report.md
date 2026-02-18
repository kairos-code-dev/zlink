# STREAM Performance Final Report (shared io_context)

Date: 2026-02-18
Workspace: `/home/hep7/project/kairos/zlink-perf-wt`

## Final code changes
- Shared `io_context` introduced at `ctx_t` level and reused by ASIO pollers.
- `asio_poller_t` now supports:
  - shared context path (production, from `ctx_t`)
  - owned context fallback (unit-test compatibility)
- STREAM engine default selection on TCP path was restored to existing behavior (raw by default, stream only when env explicitly enables it).

## Benchmark conditions
```bash
./core/tests/scenario/stream/run_stream_compare.sh \
  --stack cppserver --size all --ccu 10000 --duration 5 --repeats 3 \
  --inflight 1 --client-io-threads 4 --server-io-threads 2

# zlink final run (same condition)
RESULT_DIR=doc/plan/baseline/20260218_sharedctx_raw_final \
ZLINK_TCP_STREAM_ENGINE=raw \
./core/tests/scenario/stream/run_stream_compare.sh \
  --stack zlink --size all --ccu 10000 --duration 5 --repeats 3 \
  --inflight 1 --client-io-threads 4 --server-io-threads 2
```

Note: code default is now raw for STREAM unless `ZLINK_TCP_STREAM_ENGINE=stream` is set.

## Final median comparison (zlink vs cppserver)
| size | cppserver tp (msg/s) | zlink tp (msg/s) | zlink/cpp | zlink p95 delta vs cpp | zlink p99 delta vs cpp |
|---:|---:|---:|---:|---:|---:|
| 64 | 382,128.4 | 235,475.2 | 61.62% | +65.24% | +112.99% |
| 1024 | 337,266.4 | 213,750.0 | 63.38% | +59.88% | +107.63% |
| 65536 | 68,719.6 | 66,709.2 | 97.07% | +283.02% | -85.08% |

## Improvement vs previous native zlink baseline
Reference: `doc/plan/baseline/20260218_native_zlink_all_rerun2/summary.json`

| size | throughput improvement |
|---:|---:|
| 64 | +56.78% |
| 1024 | +54.18% |
| 65536 | +81.24% |

## Artifacts
- cppserver baseline: `doc/plan/baseline/20260217_sharedctx_cpp`
- zlink final: `doc/plan/baseline/20260218_sharedctx_raw_final`
- intermediate (not selected):
  - `doc/plan/baseline/20260217_sharedctx_zlink`
  - `doc/plan/baseline/20260218_sharedctx_iter2_zlink_nometa`
  - `doc/plan/baseline/20260218_sharedctx_iter3_zlink_read_write_drain`

## Status
- 성능 개선은 유의미하게 완료됨 (특히 64K는 cppserver 대비 97.07%까지 도달).
- 종료 조건(`64/1024/65536` 모두 `zlink >= cppserver`)은 아직 미달.
