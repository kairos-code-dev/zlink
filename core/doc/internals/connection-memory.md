[English](connection-memory.md) | [한국어](connection-memory.ko.md)

# Per-Connection Memory

This document explains what core allocates when a single transport connection
(TCP/TLS/WS/IPC) is established, when each allocation happens, and the design
decisions that determine the sizes. In deployments that hold thousands to tens
of thousands of server-to-server connections, the fixed per-connection cost
dominates process memory, so understanding this structure is a prerequisite
for choosing tuning points.

For user-facing capacity planning (measured numbers per pattern, sizing
guidance), see
[Memory planning for large connection counts](../guide/10-performance.md#8-memory-planning-for-large-connection-counts)
in the performance guide. The measurement experiments and raw data behind this
structure live in `core/study/connection-memory-study.ko.md`.

## 1. The allocation chain of one connection

When a connection is accepted (or connected), objects are created in this
order:

```text
listener accept / connecter completion
  └─ asio_zmp_engine_t created      (asio_raw_engine_t for raw STREAM)
       └─ session_base_t created
            └─ engine plug → protocol handshake
                 ├─ encoder/decoder allocated right after the HELLO exchange
                 └─ engine_ready → pipepair()
                      = 2 pipe_t + 2 ypipes (socket ↔ session queues)
```

### 1.1 Per-item allocations (ZMP connection, default options, x86-64)

| Item | Allocated when | Size formula | Default (B) |
|------|----------------|--------------|-------------|
| ypipe queue chunks ×2 | pipepair constructor (eager) | `session_pipe_granularity(64) × sizeof(msg_t)(64) + 16` ×2 | 8,224 |
| ZMP decoder buffer | right after HELLO receipt (mid-handshake) | `in_batch_size(8192) + 8 + ceil(8192/41) × 40` | 16,200 |
| ZMP encoder buffer | right after HELLO receipt (mid-handshake) | `out_batch_size(8192)` | 8,192 |
| handshake read_buffer | first handshake read (lazy) | `handshake_read_buffer_size` | 512 |
| asio_zmp_engine_t object | on accept | includes 3×1,040 handler_allocator + options_t copy (936) + HELLO 272×2 + rid 256 | 5,856 |
| pipe_t objects ×2 | pipepair | embeds stream packet state and two msg_t | 1,280 |
| session_base_t object | on accept | includes options_t copy (936) | 1,216 |
| ypipe_t objects ×2 / transport / metadata / ROUTER routing entry etc. | various | | ~1,300 |
| **Total** | | | **≈ 43 KB** |

Raw STREAM connections differ. The STREAM socket shrinks its batch sizes in
its constructor (`in_batch_size = 4,160`, `out_batch_size = 4,096`), so the
decoder is 4,208 B and the encoder 4,096 B, and because the raw engine creates
its decoder at plug time, the handshake read_buffer is never allocated. There
are no HELLO/rid buffers either.

Allocation and actual residency (RSS) are not the same. Malloc'd pages commit
only when touched, so an idle connection's RSS is below the total and grows
toward it as traffic passes through. The library keeps a connection's buffers
and chunks for the connection's lifetime, so RSS does not fall back to the
idle value after traffic stops (confirmed by measurement) — which is why
capacity planning must use the post-traffic residual value (see the guide
document).

## 2. Design decisions

### 2.1 Session pipes use a smaller chunk (`session_pipe_granularity = 64`)

The ypipe queue allocates memory in chunks. The default
`message_pipe_granularity = 256` assumes a small number of high-throughput
pipes (inproc in particular) and makes each chunk 16,400 B. Transport
connection pipes are a different situation:

- Every connection eagerly allocates one pipepair (two chunks) at creation.
  With tens of thousands of connections this fixed cost dominates — this is
  the primary rationale.
- On SPOT mesh internal sockets (`mesh-pub`/`mesh-xsub`/`external-router`),
  the auto-HWM connection-count buckets reduce the per-pipe budget to 16–32
  messages at high connection counts, so a 256-slot chunk is 8–16× larger
  than the depth the pipe is even allowed to use.

`pipepair()` takes a `session_pipe_` argument and uses
`session_pipe_granularity = 64` (chunk 4,112 B ≈ one page) for session↔socket
pipes; inproc pipes use 256. A pipe records its selected granularity in
`pipe_t::_session_pipe`. On reconnect, `hiccup()` uses this value to recreate
the inpipe at the same size, so session pipes continue to use 64.

Smaller chunks can increase allocation and release activity when queues run
deep, but the yqueue spare-chunk cache absorbs the steady flow. The tcp/1024
throughput and latency conditions are verified by the measurements in study
document §6.8.

### 2.2 The handshake read_buffer is small and lazily allocated

The engine's `_pipeline.read_buffer` is a read target only while no decoder
exists during the protocol handshake. The first handshake read acquires
`handshake_read_buffer_size = 512` B
(`select_handshake_read_buffer()`). ZMP HELLO parsing is incremental (bytes
are accumulated into the `_hello_recv` buffer), so a small buffer does not
affect correctness; it can add a few reads during the handshake. Once the
decoder exists, new reads go into the decoder buffer, or the pending buffer
pool for a STREAM socket under backpressure. Raw engines create their decoder
at plug time and do not allocate a handshake read buffer.

When a data frame arrives with the handshake tail, the residual bytes
(`_insize > 0`) point into this buffer until they are consumed. The engine
keeps the buffer alive until all residual input has been consumed.

## 3. How auto-HWM relates to memory

auto-HWM is a cap, not a preallocation. It does not affect idle memory; it
only limits how many messages a pipe may queue under load. Growing connection
counts shrink the per-pipe budget through buckets to limit total exposure,
with hysteresis on bucket transitions. See
[socket option defaults](socket-option-defaults.md) for the full policy.

Two known limits remain as follow-up design items: enforcement is
message-count based, so messages larger than the message unit (4 KiB) can
exceed the byte budget, and there is no global (per-context) byte budget
(study document §6.7).

## 4. Where the code lives

| What | Where |
|------|-------|
| granularity constants | `src/runtime/utils/config.hpp` (`message_pipe_granularity`, `session_pipe_granularity`) |
| pipepair and granularity selection | `src/runtime/core/pipe.cpp` (`pipepair`, `pipe_t::hiccup`) |
| session pipe creation sites | `src/runtime/core/session_base.cpp` (`engine_ready`), `src/runtime/sockets/common/socket_base_endpoint.cpp` (connect path) |
| handshake buffer | `src/runtime/engine/asio/asio_engine.hpp` (`handshake_read_buffer_size`), `asio_engine.cpp` (`select_handshake_read_buffer`) |
| codec buffer sizes | `src/runtime/core/options.cpp` (`in/out_batch_size`), `src/runtime/sockets/stream/stream.cpp` (STREAM overrides) |
| decoder allocation formula | `src/runtime/protocol/decoder_allocators.cpp` |
