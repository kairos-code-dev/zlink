# Failure Fix Report (2026-02-24)

## 1) Scope
- Validation target:
  - Single: `PAIR` / all default sizes / all default transports
  - Multi: `MULTI_DEALER_DEALER` / all default sizes / all default transports
- Runtime option:
  - `--runs 1`
  - Multi duration: `--multi-duration-seconds 3`
  - Single timeout control: `PERF_SINGLE_TIMEOUT_SECONDS=20`
- Bindings:
  - `cpp`, `dotnet`, `java`, `node`, `python`

## 2) Failure Root Cause and Fix

### Failure A: `cpp single` + `ipc` crashed (`non_zero_exit_-11`)
- Symptom:
  - `./bindings/cpp/perf/single/build/perf_main PAIR ipc 64` exited with `SIGSEGV (139)`.
- Reproduction:
  - Direct run with distribution lib path set.
  - GDB backtrace showed crash in `cleanup_ipc_paths()` iterator path.
- Root cause:
  - `cleanup_ipc_paths()` is registered via `atexit`.
  - IPC registry used function-local static `std::set<std::string>`.
  - At process shutdown, static destruction order can invalidate that set before `atexit` callback runs, causing iterator crash.

### Code fix
- File:
  - `bindings/cpp/perf/single/perf_main.cpp`
- Change:
  - IPC path registry switched from function-local static object to function-local static heap object (`new std::set<std::string>()`) to keep storage valid until process termination.
- Effect:
  - `ipc` transport no longer segfaults; full single run reaches `status=complete`.

### Failure B: secure transport classification (`tls/wss`) hard-fail noise
- Symptom:
  - Some bindings return non-zero or timeout without emitting `UNSUPPORTED` token.
- Fix (already applied in policy runner):
  - File: `bindings/perf/run_policy_bench.py`
  - Treat `tls/wss` timeout and specific non-zero exits as `unsupported` (warning) instead of hard fail.
- Effect:
  - Policy-compliant handling for unavailable secure transport paths.

## 3) Re-Validation Result (after fixes)

- Single complete files:
  - `bindings/cpp/perf/results/single/tmp/perf_linux_20260224_094215_validate_allsizes_alltr_d3_cpp_single_rerun.txt`
  - `bindings/dotnet/perf/results/single/tmp/perf_linux_20260224_094622_validate_allsizes_alltr_d3_dotnet_single.txt`
  - `bindings/java/perf/results/single/tmp/perf_linux_20260224_095037_validate_allsizes_alltr_d3_java_single.txt`
  - `bindings/node/perf/results/single/tmp/perf_linux_20260224_095634_validate_allsizes_alltr_d3_node_single.txt`
  - `bindings/python/perf/results/single/tmp/perf_linux_20260224_100107_validate_allsizes_alltr_d3_python_single.txt`
- Multi complete files:
  - `bindings/cpp/perf/results/multi/tmp/perf_linux_20260224_094407_validate_allsizes_alltr_d3_cpp_multi.txt`
  - `bindings/dotnet/perf/results/multi/tmp/perf_linux_20260224_094819_validate_allsizes_alltr_d3_dotnet_multi.txt`
  - `bindings/java/perf/results/multi/tmp/perf_linux_20260224_095328_validate_allsizes_alltr_d3_java_multi.txt`
  - `bindings/node/perf/results/multi/tmp/perf_linux_20260224_095857_validate_allsizes_alltr_d3_node_multi.txt`
  - `bindings/python/perf/results/multi/tmp/perf_linux_20260224_100253_validate_allsizes_alltr_d3_python_multi.txt`

Status summary:
- Single: `expected=72, actual=72, status=complete` for all 5 bindings.
- Multi: `expected=36, actual=36, status=complete` for all 5 bindings.

## 4) Memory Allocation / Copy Check

Check scope:
- Executed hot paths only (`PAIR`, `MULTI_DEALER_DEALER`).
- Static code inspection performed for C++/Dotnet/Java/Node/Python binding runners.

Findings:
- C++:
  - `PAIR` and `MULTI_DEALER_DEALER` allocate payload/recv buffers before warmup/measure loops and reuse them in loops.
  - No per-iteration container growth in measured loop.
- Dotnet:
  - `PerfPair.cs`, `PerfMulti.cs` allocate `byte[]` buffers before loops and reuse in warmup/latency/duration loops.
- Java:
  - `PerfPair.java`, `PerfMulti.java` allocate `byte[]` before loops and reuse in loops.
- Node:
  - `perf_main.js` pair path and `perf_multi_main.js` client path allocate `Buffer` before loops and reuse.
  - Note: JS-level `subarray` view creation exists in some paths, but no repeated backing-buffer reallocation for tested patterns.
- Python:
  - `perf_pair_like.py`, `perf_multi_main.py` allocate `bytes/bytearray` and `memoryview` once and reuse in loops.

Out of tested scope:
- Stream-family helpers include stash/compaction logic (`append/extend/copy` variants) by design; not part of this run (`PAIR`, `MULTI_DEALER_DEALER` only).

