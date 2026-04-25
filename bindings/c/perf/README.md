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
