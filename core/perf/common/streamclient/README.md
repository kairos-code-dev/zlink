# Core PERF STREAM Client

This directory keeps only the multi STREAM client.

## Files

- `perf_stream_client.cpp`: entrypoint + runner (`main()`, `perf_stream_client_run`)
- `perf_stream_common.hpp`: shared constants (`inline constexpr`), protocol helpers (len32be encode/decode), time, parse utilities
- `perf_stream_arg_reader.hpp`: `arg_reader_t` CLI helper class
- `perf_stream_bench_client_iface.hpp`: `bench_client_iface_t` pure virtual interface (session/bench contract)
- `perf_stream_client_options.hpp`: `client_options_t`, `case_metrics_t`, option parsing, result reporting helpers
- `perf_stream_client_session.hpp`: async constants (`inline constexpr`), `phase_mode_t`, `resize_latch_t`, `client_session_t`
- `perf_stream_bench_client.hpp`: `loopback_bind_plan_t` + helpers, `bench_client_t` async benchmark orchestrator
- `stream_client.hpp`: `stream_client_t` header-only len32be transport client (tcp/tls/ws/wss)

## Architecture Overview

```
main()
  └─ perf_stream_client_run()
       ├─ parse_options()
       └─ bench_client_t::run()        ← async multi-connection path
            ├─ io_context + worker threads
            └─ client_session_t (×CCU) ← Boost.Asio async I/O
```

## Include Dependency Graph

```
perf_stream_client.cpp
  ├─ perf_stream_bench_client.hpp        (bench_client_t, loopback plan)
  │    ├─ perf_stream_client_options.hpp  (options, metrics, parse, helpers)
  │    │    ├─ perf_stream_arg_reader.hpp (arg_reader_t)
  │    │    ├─ perf_stream_common.hpp     (protocol utils, shared constants)
  │    │    └─ stream_client.hpp          (transport abstraction)
  │    ├─ perf_stream_client_session.hpp  (session, async constants, latch)
  │    │    ├─ perf_stream_bench_client_iface.hpp  (interface)
  │    │    └─ perf_stream_common.hpp
  │    └─ perf_stream_common.hpp
  └─ perf_stream_client_options.hpp
```

## Class Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│  perf_stream_client.cpp                                          │
│                                                                  │
│  ┌─────────────────┐     ┌──────────────────────────────────┐    │
│  │ client_options_t │     │ bench_client_t                   │    │
│  │                  │────▶│   : bench_client_iface_t         │    │
│  │ transport        │     │                                  │    │
│  │ pattern          │     │ io_context + worker threads      │    │
│  │ host, port       │     │ sessions: [client_session_t ×N]  │    │
│  │ ccu              │     │ connected_sessions               │    │
│  │ sizes[]          │     │ loopback_bind_plan               │    │
│  │ warmup, duration │     │ rtt_samples_bits (atomic ring)   │    │
│  │ io_threads       │     │                                  │    │
│  │ ...              │     │ run()                            │    │
│  └─────────────────┘     │ schedule_connects()              │    │
│                           │ on_connect_result()              │    │
│                           │ allow_send()                     │    │
│                           │ on_recv_done()                   │    │
│                           │ run_case() → run_window()        │    │
│                           └──────────┬───────────────────────┘    │
│                                      │ owns N instances          │
│                           ┌──────────▼───────────────────────┐   │
│                           │ client_session_t                  │   │
│                           │   → bench_client_iface_t &owner   │   │
│                           │                                   │   │
│                           │ tcp::socket (raw)                 │   │
│                           │ strand (serialized dispatch)      │   │
│                           │                                   │   │
│                           │ begin_connect()                   │   │
│                           │   → do_connect() → on_connect()   │   │
│                           │   → retry with timer on failure   │   │
│                           │                                   │   │
│                           │ start_traffic()                   │   │
│                           │   → maybe_send_more()             │   │
│                           │   → send_one() [async_write]      │   │
│                           │   → on_write()                    │   │
│                           │   → start_read_header()           │   │
│                           │   → on_read_header()              │   │
│                           │   → start_read_payload()          │   │
│                           │   → on_read_payload()             │   │
│                           │   → maybe_send_more() (loop)      │   │
│                           └───────────────────────────────────┘   │
│                                                                  │
├──────────────────────────────────────────────────────────────────┤
│  stream_client.hpp (header-only)                                 │
│                                                                  │
│  ┌──────────────────────────────────────────────────┐            │
│  │ stream_client_t                                   │            │
│  │                                                   │            │
│  │ Transport abstraction (synchronous Boost.Asio).   │            │
│  │ Supports: tcp, tls, ws, wss                       │            │
│  │ Used for stop token delivery.                     │            │
│  │                                                   │            │
│  │ connect()                                         │            │
│  │ send_payload(payload)   → [len32be header]+data   │            │
│  │ recv_payload(out, size) ← [len32be header]+data   │            │
│  │ close()                                           │            │
│  │                                                   │            │
│  │ Internal:                                         │            │
│  │   write_frame_bytes()                             │            │
│  │   read_frame_bytes()                              │            │
│  │     ├─ tcp/tls: read_exact_tcp_like() (4B + N)    │            │
│  │     └─ ws/wss:  read_ws_message_bytes()           │            │
│  │                 + ws_pending_frame reassembly      │            │
│  │                                                   │            │
│  │ Sockets (one active per mode):                    │            │
│  │   tcp_socket  → tcp::socket                       │            │
│  │   tls_socket  → ssl::stream<tcp::socket>          │            │
│  │   ws_socket   → beast::websocket::stream<tcp>     │            │
│  │   wss_socket  → beast::websocket::stream<ssl>     │            │
│  └──────────────────────────────────────────────────┘            │
└──────────────────────────────────────────────────────────────────┘
```

## Wire Protocol

Fixed `len32be` framing on all transports:

```
┌──────────────────┬──────────────────────────────┐
│  4 bytes (BE)    │  N bytes                      │
│  payload length  │  payload data                 │
└──────────────────┴──────────────────────────────┘
```

- Every `send_payload()` writes `[4-byte big-endian length][payload]`.
- Every `recv_payload()` reads the same framing.
- For ws/wss: len32be frames are packed inside WebSocket binary messages.
  Multiple len32be frames may arrive in a single WS message, so the
  receiver buffers partial data in `ws_pending_frame` and reassembles.
- `--pattern` is kept for result labeling/runner routing, not for framing
  mode switching.
- Payload size range: 16 ~ 4 MiB (`k_stream_min_chunk_size` ~ `k_stream_max_chunk_size`).

## Benchmark Lifecycle

```
1. Parse CLI options
       │
2. Create io_context + N worker threads (--io-threads)
       │
3. Create CCU client_session_t instances
       │
4. Batched connect (k_connect_batch=1024 at a time)
   ┌─────────────────────────────────────────────┐
   │ do_connect() ──▶ async_connect()            │
   │   ├─ success: report, apply socket tuning   │
   │   └─ failure: retry after 25ms              │
   │       └─ give up after 90s timeout          │
   └─────────────────────────────────────────────┘
       │
5. For each message size in --sizes:
       │
   ├─ 5a. set_chunk_size() on all connected sessions
   │       (with resize_latch_t barrier, 30s timeout)
   │
   ├─ 5b. Warmup window (--warmup seconds, no metrics)
   │       └─ run_window(warmup, measure=false)
   │
   ├─ 5c. Measure window (--duration seconds, collect metrics)
   │       └─ run_window(duration, measure=true)
   │           ├─ kick_phase_for_connected() starts traffic
   │           ├─ each session: send_one() → async_write
   │           │   → on_write() → start_read_header()
   │           │   → on_read_header() → start_read_payload()
   │           │   → on_read_payload() → maybe_send_more()
   │           └─ sleep(duration), then stop
   │
   ├─ 5d. Drain (--drain-ms, wait for in-flight ops)
   │
   ├─ 5e. Collect & report metrics
   │       └─ throughput, latency percentiles (p50/p95/p99)
   │
   └─ 5f. Size transition drain (--size-transition-drain-ms)
       │
6. Shutdown all sessions, join worker threads
       │
7. (optional) Send stop token to server
```

## Latency Sampling

When `--latency-sample-rate N` is set, every N-th message embeds timing
data in the first 16 bytes of payload:

```
┌───────────────┬───────────────┬──────────────┐
│ 8 bytes (BE)  │ 8 bytes (BE)  │ remaining    │
│ sequence num  │ sent_ns       │ (ignored)    │
└───────────────┴───────────────┴──────────────┘
```

The echo server reflects payload unchanged. On receive, the client reads
back `seq` and `sent_ns`, computes RTT = `now_ns - sent_ns`, and stores
the sample in an atomic ring buffer (`k_rtt_sample_capacity` = 1M entries).
Samples are stored as bit-cast `double → uint64_t` in `std::atomic<uint64_t>[]`
to ensure thread-safe write from I/O threads and safe read from the main thread.

## Loopback Port Sharding

When the server endpoint is loopback (127.x.x.x) and CCU exceeds the
OS ephemeral port range, the client auto-shards source addresses across
multiple loopback IPs (127.0.0.1, 127.0.0.2, ...) to avoid port
exhaustion. The required shard count is:

```
shards = ceil(ccu / usable_ephemeral_ports)
```

Ephemeral port range is read from `/proc/sys/net/ipv4/ip_local_port_range`.

## CLI Options

| Option | Default | Description |
|--------|---------|-------------|
| `--transport` | `tcp` | Transport protocol: `tcp`, `tls`, `ws`, `wss` |
| `--pattern` | `STREAM` | Label for result output routing |
| `--endpoint` | — | Full endpoint URI (e.g., `tcp://127.0.0.1:15557`). Overrides `--host`/`--port`/`--transport`. |
| `--host` | `127.0.0.1` | Server host |
| `--port` | `38001` | Server port |
| `--ccu` | `1000` | Concurrent connections |
| `--sizes` | `64,1024,65536` | Comma-separated payload sizes (bytes) |
| `--runs` | `1` | Number of benchmark runs per size |
| `--warmup` | `2` | Warmup duration (seconds) |
| `--duration` | `10` | Measurement duration (seconds) |
| `--drain-ms` | `300` | Post-measurement drain wait (ms) |
| `--size-transition-drain-ms` | `300` | Drain between size transitions (ms) |
| `--io-threads` | `4` | I/O worker thread count |
| `--latency-sample-rate` | `0` | Sample every N-th message for latency (0=disabled) |
| `--print-perf-result` | `0` | Output format: 0=detailed, 1=both, 2=CSV only |
| `--send-stop-token` | `0` | Send stop token to server after benchmark |
| `--stop-token` | `__zlink_perf_stop__` | Custom stop token string |

## Entrypoint

- binary: `perf_stream_client`

## Build

Quick local build:

```bash
./core/perf/common/streamclient/build.sh
```

Output:

- `core/perf/common/streamclient/build/perf_stream_client`

Full CMake target:

```bash
cmake --build core/build --target perf_stream_client -j$(nproc)
```

## Example

```bash
core/build/bin/perf_stream_client \
  --pattern MULTI_STREAM \
  --transport tcp \
  --endpoint tcp://127.0.0.1:15557 \
  --sizes 64 \
  --ccu 1000 \
  --warmup 3 \
  --duration 5 \
  --io-threads 4 \
  --print-perf-result 2 \
  --send-stop-token 1
```

`--transport` supports `tcp,tls,ws,wss`.
