# bindings/c/perf

This directory contains the standalone C benchmark runner and comparison tools.

## Core Runtime Rule

`run_benchmarks_multi.sh` evaluates the runtime from `core/build`.
It does not read `build_cpp_release`, so performance results are meaningful only
after rebuilding `core/build`.

```bash
cmake --build core/build
./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_REQREP
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
  `CLIENT_DONE`, `START`, `PHASE_ACTIVE`, `STOP`, `RESULT`, and the SPOT
  control tokens);
- raw socket connection gates based on the same C perf `CONNECTION_READY`
  meaning, plus the C perf `CLIENT_READY` / `START` start barrier for the
  one-way raw patterns that use it;
- SPOT-family direct control handshake (`CONNECTED` progress payload,
  `DATA_ENDPOINT` for routed SPOT echo variants, plus `READY_COUNT` -> direct
  `START` start barrier);
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

Multi benchmarks set `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` from the current
message size before each run for raw benchmark sockets:

| Socket family | Message unit used by perf |
|---------------|---------------------------|
| raw multi perf sockets | `msg_size` |

SPOT node and SPOT handle paths do not set this common socket option. The C API
treats `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` as a raw socket option, so SPOT perf
uses the core's SPOT defaults instead of writing a per-handle message unit.

All single benchmarks use one context I/O thread by default. SPOT does not have
a pattern-specific I/O thread default; pass `--io-threads` or set
`PERF_IO_THREADS` only when intentionally running a non-baseline diagnostic.

For non-SPOT patterns, the auto-HWM detail table is printed after the result
rows. It uses the cached runtime snapshots and includes the applied HWM and
socket buffers.

SPOT output uses `zlink_spot_node_internal_sockets_snapshot()` and prints only
actual snapshot rows where `auto_hwm_visible == 1`. The default tables are
`Auto-HWM spotnode` for node-owned sockets and `Auto-HWM spot handles` for
per-spot handle sockets. Shared routers, mesh sockets, and peer-control sockets
belong to the SpotNode table. A per-spot handle normally exposes only its
handle-owned PUB/SUB data sockets, so router rows are not expected in
`Auto-HWM spot handles`. Each row includes `Size(B)`, `MsgUnit(B)`, the internal
socket name, the public socket type, role, HWM, and effective buffers. If a
SpotNode mode does not create a socket group, that group is absent from the perf
output.

## Auto-HWM Profile Sweep

Use `--auto-hwm-profile` or `PERF_CTX_AUTO_HWM_PROFILE` with benchmark message
sizes to exercise the per-connection auto-HWM policy.
The recommended sweep axes are:

| Axis | Values |
|------|--------|
| profile | `low_latency`, `balanced`, `throughput` |
| message sizes | `64`, `1024`, `4096`, `65536` bytes |
| patterns | `DEALER_ROUTER`, `PUBSUB`, `SPOT`, `SPOT_REQREP`, `SPOT_SENDSEND`, `STREAM` |

Profile guidance after the auto-HWM message-unit change:

| Profile | Use case |
|---------|----------|
| `compact` | smallest queue depth and 128 KiB socket-buffer floor for memory-sensitive runs |
| `low_latency` | lower per-connection queue depth for small latency-focused tests |
| `balanced` | default profile for ordinary service messages |
| `throughput` | larger per-connection queue depth for high throughput sweeps |

If calculated HWM values are too small for the target traffic shape, first
select a larger profile. Use `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` only when the
raw benchmark socket's effective message unit differs from the socket-type
default.

## SPOT One-Way Latency

`MULTI_SPOT` measures throughput during the active phase. For large one-way
payloads, that phase intentionally saturates the sender and receiver queues, so
active-phase message timestamps mostly describe queue residence time rather than
service latency.

The runner reports the active-phase latency values in both the markdown table
and `RESULT` data. This keeps the C runner and binding runners on the same
single-pass measurement contract for each pattern, transport, size, and run.
