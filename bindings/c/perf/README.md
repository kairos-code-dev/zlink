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
`Auto-HWM spotnode` for node-owned sockets and `Auto-HWM spot` for per-spot
sockets. Each row includes `Size(B)`, `MsgUnit(B)`, the internal socket name,
the public socket type, role, connection counts, HWM, and effective buffers.
If a SpotNode mode does not create a socket group, that group is absent from
the perf output.

## Context Budget Tiers

Use `PERF_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB` for context memory sweeps.
The recommended sweep axes are:

| Axis | Values |
|------|--------|
| context memory | `128`, `256`, `512`, `1024` MiB, plus `2048` MiB for the 100-client SPOT_SENDSEND 64 KiB edge case |
| message sizes | `64`, `1024`, `4096`, `65536` bytes |
| patterns | `DEALER_ROUTER`, `PUBSUB`, `SPOT`, `SPOT_REQREP`, `SPOT_SENDSEND`, `STREAM` |

Budget tier guidance after the auto-HWM message-unit change:

| Tier | Recommended budget | Use case |
|------|--------------------|----------|
| small | `128` MiB | up to about 100 clients with mostly small payloads |
| medium | `256` MiB | up to about 1000 clients with ordinary service messages |
| large | `512` MiB or `1024` MiB | high client counts or a moderate share of large payloads |
| xlarge | `2048` MiB | 100-client SPOT_SENDSEND with 64 KiB payloads; 1024 MiB leaves per-spot HWM below the target concurrency in the local sweep |

If calculated HWM values are too small for the target concurrency, increase the
context budget before lowering `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`.
