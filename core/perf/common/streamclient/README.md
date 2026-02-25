# Core PERF Shared STREAM Client

`core/perf/common/streamclient/bench_stream_client.cpp` is the shared raw STREAM
client used for STREAM benchmark client-side traffic generation.

## Build

```bash
./core/perf/common/streamclient/build.sh
```

Binary output:

- `core/perf/common/streamclient/build/bench_stream_client`

## Example (single-style count mode)

```bash
core/perf/common/streamclient/build/bench_stream_client \
  --pattern STREAM \
  --transport tcp \
  --endpoint tcp://127.0.0.1:15557 \
  --sizes 64 \
  --warmup-count 1000 \
  --lat-count 500 \
  --msg-count 10000 \
  --print-perf-result 2
```

## Example (multi-style duration mode)

```bash
core/perf/common/streamclient/build/bench_stream_client \
  --pattern MULTI_STREAM \
  --transport tcp \
  --endpoint tcp://127.0.0.1:15557 \
  --sizes 64 \
  --ccu 1000 \
  --warmup 3 \
  --duration 5 \
  --inflight 1 \
  --io-threads 4 \
  --print-perf-result 2 \
  --send-stop-token 1
```

`--transport` supports `tcp,tls,ws,wss`.
No `UNSUPPORTED` fallback is emitted; failed connect/handshake returns non-zero.
