# C++ perf `MULTI_PUBSUB` still fails with `no_active_data` under default large TCP multi load

## Summary

`bindings/cpp/perf` `MULTI_PUBSUB` still fails under the default multi runner high-load case.

Current reproduced case:

- pattern: `MULTI_PUBSUB`
- transport: `tcp`
- size: `65536`
- runner defaults:
  - `PERF_CLIENTS=100`
  - `PERF_WARMUP_SECONDS=3`
  - `PERF_DURATION_SECONDS=5`
  - `PERF_HWM=100000`
  - `PERF_SNDTIMEO_MS=5000`
  - `PERF_RCVTIMEO_MS=5000`

Current reproduced stderr:

```text
PUBSUB_CLIENT_FAIL,stage=no_active_data,transport=tcp,size=65536,warmup=445807,drain=74879,active=0
```

This is not a core runtime crash or API release blocker.
Current evidence points to a **shared multi PUBSUB perf benchmark/model issue** rather than a C++-binding-only wrapper bug.

## Current cpp perf state

The current C++ perf path already includes these corrections:

- failures are no longer swallowed as `exit 0`
- server waits for `connection_ready` count before warmup
- client waits for socket `connection_ready`
- server drain phase no longer publishes payloads
- client recv hot loop uses a reusable raw buffer

Even with those fixes, the default high-load case above still fails.

## Repro 1: runner

```bash
python3 bindings/perf/run_policy_bench.py \
  --binding cpp \
  --suite multi \
  --pattern MULTI_PUBSUB \
  --transports tcp \
  --msg-sizes 65536 \
  --runs 1 \
  --reuse-build
```

Observed result:

```text
## Failures
- MULTI_PUBSUB current tcp 65536B: non_zero_exit_1
```

Observed warning:

```text
MULTI_PUBSUB tcp 65536 non-zero exit(1) stderr: \
PUBSUB_CLIENT_FAIL,stage=no_active_data,transport=tcp,size=65536,warmup=384209,drain=63868,active=0
```

Result file:

- `/home/hep7/project/kairos/zlink/bindings/cpp/perf/results/multi/tmp/perf_linux_20260307_202438.txt`

## Repro 2: direct server/client

```bash
export LD_LIBRARY_PATH=/home/hep7/project/kairos/zlink/bindings/cpp/native/linux-x86_64
export ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/bindings/cpp/native/linux-x86_64/libzlink.so
export PERF_CLIENTS=100
export PERF_WARMUP_SECONDS=3
export PERF_DURATION_SECONDS=5
export PERF_HWM=100000
export PERF_SNDTIMEO_MS=5000
export PERF_RCVTIMEO_MS=5000
export PERF_SERVER_READY_TIMEOUT_MS=10000
export PERF_SERVER_SHUTDOWN_TIMEOUT_MS=5000
export PERF_SERVER_BIND_PORT=0
export PERF_CONNECT_READY_TIMEOUT_MS=5000
export PERF_MONITOR_HWM=200000

bindings/cpp/perf/multi/build/cpp_comp_src_pubsub_server tcp 65536
bindings/cpp/perf/multi/build/cpp_comp_src_pubsub_client tcp 65536 --endpoint <READY endpoint>
```

Observed output:

```text
READY,tcp://127.0.0.1:18055
PUBSUB_CLIENT_FAIL,stage=no_active_data,transport=tcp,size=65536,warmup=445807,drain=74879,active=0
```

The server exits normally and prints only queue metrics.
The client exits with code `1` and emits no throughput/latency lines.

## Control comparison

The same direct repro passes when client count is reduced to `1`.
So the failure is specific to the default high-load split multi model with many SUB sockets and large payloads.

## Expected

- `MULTI_PUBSUB` should emit normal `RESULT throughput/bandwidth/latency` lines under the default multi runner configuration.
- The client should observe `phase_active` instead of finishing with `active=0`.

## Current conclusion

- Not a core runtime crash
- Not a C++ wrapper API misuse issue
- Not a runtime release blocker
- Likely a shared multi `PUBSUB` perf readiness / backlog / phase progression issue
