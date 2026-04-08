# Core PERF STREAM Client

Shared client code for the multi `STREAM` benchmark path.

Source of truth:

- [PERF_POLICY.md](../../../../doc/perf/PERF_POLICY.md)
- [PERF_MULTI_TEST_POLICY.md](../../../../doc/perf/PERF_MULTI_TEST_POLICY.md)

Key files:

- `perf_stream_client_options.hpp`: CLI parsing and per-case result helpers
- `perf_stream_client_session.hpp`: async session, `ready -> active` phase handling
- `perf_stream_bench_client.hpp`: orchestrator, connect scheduling, result reporting
- `stream_client.hpp`: transport client used for stop-token delivery

The shared client reports the Tier 1 metrics only:
`throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99`.

Build:

```bash
cmake --build core/build --target perf_stream_client -j$(nproc)
```

Example:

```bash
core/build/bin/perf_stream_client \
  --pattern STREAM \
  --transport tcp \
  --endpoint tcp://127.0.0.1:15557 \
  --sizes 64 \
  --ccu 10000 \
  --duration 5 \
  --io-threads 4 \
  --send-stop-token 1
```

`--transport` supports `tcp`, `tls`, `ws`, and `wss`.
