# bindings/c/perf

This directory contains the standalone C benchmark runner and comparison tools.

## Core Runtime Rule

`run_benchmarks_multi.sh` evaluates the runtime from `core/build`.
It does not read `build_cpp_release`, so performance results are meaningful only
after rebuilding `core/build`.

```bash
cmake --build core/build
./bindings/c/perf/run_benchmarks_multi.sh --pattern ROUTER_ROUTER_REQREP
```

Before running benchmarks, the script prints the resolved `libzlink.so` path.
If any file under `core/src` or `core/include` is newer than the resolved
runtime library, the script stops immediately and asks for a `core/build`
rebuild.

## Cross-Binding Handshake Reference

`bindings/c/perf` is the canonical benchmark handshake for all binding-language
perf runners. When C, C++, .NET, Java, Rust, Go, Node, or Python perf results
are compared, their runner/process orchestration must match the C perf
handshake before the numbers are treated as comparable.

The fixed reference surface is:

- runner and benchmark process stdin/stdout tokens (`READY`, `CLIENT_READY`,
  `CLIENT_DONE`, `START`, `PHASE_ACTIVE`, `STOP`, `RESULT`);
- raw socket connection gates based on the same C perf `CONNECTION_READY`
  meaning, plus the C perf `CLIENT_READY` / `START` start barrier for the
  one-way raw patterns that use it;
- active window, stop/drain, timeout, fail, skip, and unsupported semantics;
- RESULT line metric names and anchor points.

`PHASE_ACTIVE,<msg_size>` is a C runner compatibility token for some one-way
paths. Benchmark processes must not require it as an extra active gate; the
required active gate is the pattern-specific C handshake documented in
`doc/perf`. Only patterns that use the C runner `CLIENT_READY` / `START`
barrier may require `START,<msg_size>`.

If a binding cannot implement this handshake through its public API, add the
missing public binding API or mark that perf combination `UNSUPPORTED`. Do not
use private binding members, reflection, raw native handles, sleeps, or
language-specific warmup gates to make a result line comparable.

Handshake changes are made in this order: update `bindings/c/perf` first, update
`doc/perf/PERF_POLICY.md` and the suite policy, then port the same contract to
other binding perf runners.

## Socket Sizing Policy

The default single and multi benchmark paths use context auto-HWM.
Both runners default the context auto-HWM profile to `balanced`. Pass
`--auto-hwm-profile compact`, `--auto-hwm-profile low_latency`,
`--auto-hwm-profile balanced`, or `--auto-hwm-profile throughput` to select a
different profile for a run. The same value can be supplied through
`PERF_SINGLE_CTX_AUTO_HWM_PROFILE`, `PERF_MULTI_CTX_AUTO_HWM_PROFILE`, or the
shared `PERF_CTX_AUTO_HWM_PROFILE`.
Runner defaults do not inject numeric `SNDHWM`, `RCVHWM`, `SNDBUF`, or
`RCVBUF` into benchmark sockets. If you need a manual override for debug,
set `PERF_SINGLE_ALLOW_MANUAL_SOCKET_OVERRIDES=1` or
`PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES=1` (or the shared
`PERF_ALLOW_MANUAL_SOCKET_OVERRIDES=1`) and then pass explicit
`PERF_SINGLE_*` / `PERF_MULTI_*` socket values. The runner option `--buf 128k`
is a shortcut that sets both send and receive socket buffers to the same value;
`--sndbuf` and `--rcvbuf` are still available when each direction needs a
different value.

Multi benchmarks set the auto-HWM message unit from the current message size
before each run:

| Socket family | Message unit used by perf |
|---------------|---------------------------|
| raw multi perf sockets | `msg_size` |

A visible `MsgUnit(B)=4096` row is only valid for a 4096 byte test case.
If a 64 byte or 1024 byte run prints `4096`, the run is not a valid comparison
because the effective slot budget and socket buffers are different from the
target message size.

All single benchmarks use one context I/O thread by default. Pass
`--io-threads` or set `PERF_IO_THREADS` only when intentionally running a
non-baseline diagnostic.

The auto-HWM detail table is printed after the result rows. It uses the cached
runtime snapshots and includes the applied HWM and socket buffers.

OOM must not be avoided by lowering the official workload's send rate, payload
size, client count, active duration, or concurrency. Each message-size phase
updates the context auto-HWM message unit and recalculates auto-HWM. Normal
burst control belongs to auto-HWM and public-API backpressure. If memory keeps
growing without `EAGAIN`, or phase/process teardown does not release queued
messages, treat that behavior as a Core bug and rerun the same workload after
the fix.

The bounded reservoirs used for p95 and p99 protect measurement metadata only.
They do not change queue HWM, send timing, receive count, or mean-latency
aggregation.

## Core 10.0.0 Spot Patterns

Single supports `SPOT_PUBSUB`. Multi supports `SPOT_PUBSUB`, `SPOT_REQREP`,
and `SPOT_SENDSEND`.

For multi Spot, `--clients N` means N peer processes. Each peer process owns one
MeshNode and one entry Spot and connects only to the hub MeshNode. The hub owns
one MeshNode and one entry Spot, so the measured topology is one hub MeshNode
managing N peer connections. The peer processes do not connect to one another.

Each peer context uses one I/O thread by default because it owns one MeshNode
and one hub connection. This is the baseline topology, not an OOM throttle.
Set `PERF_MULTI_SPOT_NODE_IO_THREADS` explicitly for an I/O-thread comparison.
The hub and every peer recalculate context auto-HWM from the current message
size before each active phase.

`MULTI_ROUTER_ROUTER_ONEWAY` is the direction-matched baseline for
`MULTI_SPOT_PUBSUB`. Its hub owns one context and one ROUTER socket. Each of
the N peer processes owns one context and one ROUTER socket with one I/O
thread. The hub sends each active record once to every peer, and throughput is
the sum of the active records received by all peers.

Use `run_spot_paired_gate.py` for the formal Spot-to-ROUTER comparison. It
alternates Spot-first and ROUTER-first order for each cell, fixes both roles to
one I/O thread, computes five-run medians, and fails a cell when throughput is
below 90% or mean, p95, or p99 latency exceeds 1.25 times the matched ROUTER
result.

```bash
python3 bindings/c/perf/run_spot_paired_gate.py
```

The tool rejects a stale or changing `core/build` runtime and writes the
runtime path, runtime SHA-256, source-tree SHA-256, raw runs, and cell verdicts
under `bindings/c/perf/results/multi/paired/`. Before measuring, it always
rebuilds the selected benchmark targets in the official `bindings/c/build`
directory; this prevents `--reuse-build` from accepting stale harness binaries.
Use `--dry-run` to inspect the exact matrix without building or measuring.
`--perf-record` records descendant process stacks when the host provides Linux
`perf`. Use `--time-verbose` to record low-overhead GNU `time -v` process-tree
CPU time, maximum resident set size, faults, and context switches next to each
raw run. Those aggregate measurements can identify CPU or memory pressure, but
they do not replace a call-stack profile.

## Auto-HWM Profile Sweep

Use `--auto-hwm-profile` or `PERF_CTX_AUTO_HWM_PROFILE` with benchmark message
sizes to exercise the per-connection auto-HWM policy.
The recommended sweep axes are:

| Axis | Values |
|------|--------|
| profile | `low_latency`, `balanced`, `throughput` |
| message sizes | `64`, `1024`, `4096`, `65536` bytes |
| patterns | `DEALER_ROUTER_SENDSEND`, `DEALER_ROUTER_REQREP`, `ROUTER_ROUTER_SENDSEND`, `ROUTER_ROUTER_REQREP`, `ROUTER_ROUTER_ONEWAY`, `PUBSUB`, `STREAM` |

Profile guidance after the auto-HWM message-unit change:

| Profile | Use case |
|---------|----------|
| `compact` | smallest queue depth and 128 KiB socket-buffer floor for memory-sensitive runs |
| `low_latency` | lower per-connection queue depth for small latency-focused tests |
| `balanced` | default profile for ordinary service messages |
| `throughput` | larger per-connection queue depth for high throughput sweeps |

If calculated HWM values are too small for the target traffic shape, first
select a larger profile. Use `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES` when the
benchmark context's effective message unit differs from the socket-type default.
Use raw socket `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` only for a socket-specific
override inside that context.

## One-Way Latency Caveat

One-way patterns measure throughput during the active phase. For large one-way
payloads, that phase intentionally saturates the sender and receiver queues, so
active-phase message timestamps mostly describe queue residence time rather than
service latency.

The runner reports the active-phase latency values in both the markdown table
and `RESULT` data. This keeps the C runner and binding runners on the same
single-pass measurement contract for each pattern, transport, size, and run.
