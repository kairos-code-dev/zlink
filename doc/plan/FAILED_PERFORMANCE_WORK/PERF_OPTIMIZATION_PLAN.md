# Performance Optimization Plan (ASIO Core)

## Goal
Improve TCP/IPC throughput and latency regressions observed after ASIO-only
transition while preserving correctness and test coverage.

## Constraints / Notes
- System profiling tools (`perf`, `strace`) are not available in this WSL
  environment (no sudo). Use `gprof` and internal benchmarks.
- Benchmarks live under `benchwithzmq/` and are the current source of truth
  for regressions.
- A profiling knob was added to benchmarks:
  - `BENCH_MSG_COUNT=<int>` overrides the throughput loop count.

## Current Signal (from `benchwithzmq/BENCHMARK_RESULTS.md`)
- TCP/IPC show consistent regressions in large message sizes and dealer/router
  patterns.
- Inproc is generally close to parity, so hot path is likely in transport +
  encoder/decoder interaction rather than message routing alone.

## Profiling Approach (Tooling Available)
### WSL perf limitations
- WSL kernel often lacks hardware counters; `perf` may report
  `<not supported>` for cycles/instructions. Use `task-clock` sampling or
  fall back to `gprof` and targeted instrumentation.

### 1) Gprof build
```bash
cmake -S . -B build-prof \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_BENCHMARKS=ON \
  -DWITH_BOOST_ASIO=ON \
  -DZMQ_CXX_STANDARD=20 \
  -DCMAKE_CXX_FLAGS=-pg \
  -DCMAKE_C_FLAGS=-pg \
  -DCMAKE_EXE_LINKER_FLAGS=-pg

cmake --build build-prof --target comp_zlink_dealer_dealer
```

### 2) Targeted bench run (example)
```bash
cd build-prof/bin
taskset -c 1 BENCH_MSG_COUNT=5000 ./comp_zlink_dealer_dealer zlink tcp 1024
gprof ./comp_zlink_dealer_dealer gmon.out | head -n 40
```

### 3) Adjust runtime for meaningful samples
- Increase `BENCH_MSG_COUNT` until the run is >5–10s so gprof collects useful
  samples. (In WSL with `-pg`, this can require larger values.)
- Pin to a single CPU core using `taskset -c 1` to reduce variance.

## Suspected Hotspots (Code Review + Bench Signal)
### Write path copy
`src/asio/asio_engine.cpp`
- `process_output()` always copies encoder output into `_write_buffer`
  via `memcpy`.
- This adds a full copy on every write batch, especially costly for
  larger message sizes (TCP/IPC regressions match this).

### Read path memmove
`src/asio/asio_engine.cpp`
- `start_async_read()` uses `memmove` to shift partial data to the start of
  the decoder buffer.
- For large messages with partial reads, this can repeatedly move large blocks.

### Decoder input staging
`src/asio/asio_engine.cpp`
- When `input_in_decoder_buffer == false`, data is copied into the decoder
  buffer before decoding.
- Minor for steady-state, but can affect handshake/transition phases.

### WS/TLS transport staging
`src/asio/ws_transport.cpp`, `src/asio/ssl_transport.cpp`
- WebSocket transport copies frame data into `_frame_buffer`, then into
  the caller buffer.
- TLS and WS paths are less implicated in TCP/IPC regressions but should be
  reviewed after TCP/IPC improvements.

## Improvement Plan (Phased)

### Phase 1 — Measurement & Guardrails
1) Establish stable profiling runs with `gprof` using `BENCH_MSG_COUNT`.
2) Add runbook commands to `doc/plan/` (this doc).
3) Record baseline results (TCP/IPC, 1024B and 65536B for DEALER/DEALER and
   DEALER/ROUTER).

**Acceptance**
- Reproducible `gprof` output showing real time in hot functions.
- Benchmark output stored in `benchwithzmq/benchmark_result.txt`.

### Phase 2 — Remove Write-Copy in Hot Path (Low Risk, High Impact)
Goal: eliminate `memcpy` in `process_output()` for steady-state writes.

Options:
1) **Direct send from encoder buffer**
   - Keep `_outpos/_outsize` until `async_write_some` completes.
   - Ensure encoder is not called again until write completion.
   - Requires a small state machine change to avoid overwriting encoder buffer.

2) **Scatter/gather**
   - Use `boost::asio::buffer` array to avoid intermediate copy
     (only if encoder can provide stable buffers).

**Risks**
- Buffer lifetime; must not call `encode()` until write completes.

**Acceptance**
- Throughput improves on TCP/IPC at large sizes by >5%.
- No regression in tests.

### Phase 3 — Reduce Read-Side Memmove
Goal: minimize or eliminate `memmove` of partial reads.

Options:
1) **Ring buffer or two-segment buffer**
   - Read into tail, decode using two segments without moving.
2) **Decoder accepts offset buffer**
   - Allow decoder to process from an offset pointer and preserve partial bytes.

**Risks**
- Decoder invariants and handshake transitions.

**Acceptance**
- Reduced CPU time in read path (gprof/top).
- Latency regression reduced for large TCP/IPC sizes.

### Phase 4 — Transport-Level Optimizations
1) Tune `SO_SNDBUF` / `SO_RCVBUF` defaults for TCP/IPC.
2) Consider `boost::asio::async_write` for full buffers to avoid partial writes
   and reduce overhead.

**Acceptance**
- TCP/IPC throughput improves without increasing latency.

### Phase 5 — WS/TLS Path Review (If Needed)
Only if regressions remain in WS/WSS/TLS:
- Reduce frame buffer copies in `ws_transport_t`.
- Ensure TLS read/write uses large buffers to reduce syscalls.

## Validation Checklist
1) `./build.sh` must pass.
2) `benchwithzmq/run_benchmarks.sh --pattern DEALER_DEALER` improves TCP/IPC.
3) No regression in PAIR/PUBSUB/ROUTER patterns.

## Next Action
Start Phase 1 with gprof collection on:
- `DEALER_DEALER tcp 1024B`
- `DEALER_ROUTER tcp 1024B`
- `DEALER_DEALER ipc 1024B`
