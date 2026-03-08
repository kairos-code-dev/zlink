# single/GATEWAY fails with native runtime when queue metrics use `router_peers()`

## Summary

`bindings/cpp/perf/single/src/perf_gateway.cpp` was aligned to the documented single policy:

- send: blocking send once
- recv: blocking recv + same-iteration nonblocking drain
- no retry budget / fallback / cap in hot path

The benchmark works when linked against the local build lib (`build-cpp-perf-v4/lib/libzlink.so.5`), but fails with the distributed native runtime (`bindings/cpp/native/linux-x86_64/libzlink.so.5`).

The failure is triggered when queue metrics sample the public service-instance peer APIs:

- `gateway_t::router_peers()`
- `receiver_t::router_peers()`

In the native runtime, calling those peer enumeration APIs before/while the benchmark loop causes the first `gateway.send()` to fail. The same repro passes against the local build lib.

## Affected version

- runtime used by `run_policy_bench.py`: `bindings/cpp/native/linux-x86_64/libzlink.so.5`
- reported version: `4.0.1`
- local build lib used for comparison: `build-cpp-perf-v4/lib/libzlink.so.5`

## Visible symptom

Runner result:

```text
python3 bindings/perf/run_policy_bench.py --binding cpp --suite single --pattern GATEWAY --runs 1 --msg-sizes 64 --transports tcp
```

Actual result:

```text
Failures
- GATEWAY current tcp 64B: no_data
```

Binary stdout only prints queue metrics and never prints throughput/latency:

```text
RESULT,current,GATEWAY,tcp,64,snd_pending_max,0.00
RESULT,current,GATEWAY,tcp,64,rcv_pending_max,0.00
RESULT,current,GATEWAY,tcp,64,rcv_pending_end,0.00
```

## Control comparison

The exact same benchmark logic succeeds with the local build lib:

```bash
bindings/cpp/perf/single/build/cpp_perf_gateway current tcp 64
```

Output:

```text
RESULT,current,GATEWAY,tcp,64,throughput,17208.40
RESULT,current,GATEWAY,tcp,64,bandwidth,1.10
RESULT,current,GATEWAY,tcp,64,latency,0.06
RESULT,current,GATEWAY,tcp,64,latency_p95,0.08
RESULT,current,GATEWAY,tcp,64,latency_p99,0.10
RESULT,current,GATEWAY,tcp,64,snd_pending_max,492.00
RESULT,current,GATEWAY,tcp,64,rcv_pending_max,0.00
RESULT,current,GATEWAY,tcp,64,rcv_pending_end,0.00
```

`ldd` shows why this differs:

- `cpp_perf_gateway` without override loads `build-cpp-perf-v4/lib/libzlink.so.5`
- runner/native path loads `bindings/cpp/native/linux-x86_64/libzlink.so.5`

## Minimal repro

The following minimal repro is enough to trigger the issue with the native runtime:

1. create `gateway_t` + `receiver_t`
2. wait until discovery/gateway connection is ready
3. call `gateway_t::router_peers()` / `receiver_t::router_peers()` to prepare queue sampling
4. send one stamped payload with `gateway.send()`

With the native runtime, the first send fails.

Observed stderr from the repro linked to `bindings/cpp/native/linux-x86_64/libzlink.so.5`:

```text
send fail errno=156384763
```

The same source linked to `build-cpp-perf-v4/lib/libzlink.so.5` succeeds and reports:

```text
received=17442
```

## Repro commands

Native runtime repro:

```bash
g++ -std=c++17 \
  -I/home/hep7/project/kairos/zlink/bindings/cpp/include \
  /tmp/test_single_gateway_service_probe.cpp \
  -L/home/hep7/project/kairos/zlink/bindings/cpp/native/linux-x86_64 \
  -Wl,-rpath,/home/hep7/project/kairos/zlink/bindings/cpp/native/linux-x86_64 \
  -lzlink -lpthread \
  -o /tmp/test_single_gateway_service_probe && \
  /tmp/test_single_gateway_service_probe
```

Build-lib comparison:

```bash
g++ -std=c++17 \
  -I/home/hep7/project/kairos/zlink/bindings/cpp/include \
  /tmp/test_single_gateway_service_probe.cpp \
  -L/home/hep7/project/kairos/zlink/build-cpp-perf-v4/lib \
  -Wl,-rpath,/home/hep7/project/kairos/zlink/build-cpp-perf-v4/lib \
  -lzlink -lpthread \
  -o /tmp/test_single_gateway_service_probe_build && \
  /tmp/test_single_gateway_service_probe_build
```

## Expected

- `gateway_t::router_peers()` and `receiver_t::router_peers()` should be safe to use for queue sampling in single-threaded perf code.
- They should not perturb subsequent `gateway.send()` calls.
- The distributed native runtime should behave the same as the local build lib.

## Actual

- Native runtime: peer enumeration perturbs the gateway send path and `single/GATEWAY` collapses to `no_data`.
- Local build lib: same code path works.

## Impact

- `bindings/cpp/perf` single suite cannot pass completely under the shipped native runtime.
- The remaining blocker is `GATEWAY`; the rest of the single patterns pass.
- This is not hidden by a fallback or retry; the benchmark fails directly.

## Current status in cpp perf

The benchmark code remains aligned to the documented policy. No workaround was added to bypass the failure.

If a temporary workaround is needed, it would mean dropping or weakening queue metric sampling for `single/GATEWAY`, which would hide the runtime issue and reduce metric fidelity. That workaround was intentionally not applied.


## Fixed status

- Re-tested on `4.0.2` native runtime on 2026-03-07.
- `CHANGELOG.md` explicitly states:
  - `Fixed zlink_gateway_router_peers() so peer enumeration no longer forces the Gateway into pollable/raw mode.`
- Re-test results:
  - `single/GATEWAY` native direct run now prints throughput/latency again.
  - split minimal repros with `gateway.router_peers()` only and `receiver.router_peers()` only both pass.
- Conclusion: the `4.0.1` runtime regression appears fixed in `4.0.2`.
