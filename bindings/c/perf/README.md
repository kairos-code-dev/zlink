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

## Socket Sizing Policy

The default single and multi benchmark paths use context auto-HWM.
Runner defaults do not inject numeric `SNDHWM`, `RCVHWM`, `SNDBUF`, or
`RCVBUF` into benchmark sockets. If you need a manual override for debug,
set `PERF_SINGLE_ALLOW_MANUAL_SOCKET_OVERRIDES=1` or
`PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES=1` (or the shared
`PERF_ALLOW_MANUAL_SOCKET_OVERRIDES=1`) and then pass explicit
`PERF_SINGLE_*` / `PERF_MULTI_*` socket values.

Multi benchmarks set `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` from the current
message size before each run:

| Socket family | Message unit used by perf |
|---------------|---------------------------|
| all multi perf sockets | `msg_size` |

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

Use `PERF_CTX_AUTO_HWM_PROFILE` and benchmark message sizes to exercise the
per-connection auto-HWM policy.
The recommended sweep axes are:

| Axis | Values |
|------|--------|
| profile | `low_latency`, `balanced`, `throughput` |
| message sizes | `64`, `1024`, `4096`, `65536` bytes |
| patterns | `DEALER_ROUTER`, `PUBSUB`, `SPOT`, `SPOT_REQREP`, `SPOT_SENDSEND`, `STREAM` |

Profile guidance after the auto-HWM message-unit change:

| Profile | Use case |
|---------|----------|
| `low_latency` | lower per-connection queue depth for small latency-focused tests |
| `balanced` | default profile for ordinary service messages |
| `throughput` | larger per-connection queue depth for high throughput sweeps |

If calculated HWM values are too small for the target traffic shape, first
select a larger profile. Use `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` only when the
benchmark's effective message unit differs from the socket-type default.
