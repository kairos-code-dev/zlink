# single PUBSUB `inproc` timeout under alternating send/recv load

## Summary

- Scope: `.NET` binding perf only revealed it, but the failure reproduces with a
  minimal `.NET` socket-level loop and does not depend on perf helper state.
- Affected path: single `PUBSUB`, `transport=inproc`, small payloads (`64B`,
  `256B`, `1024B` confirmed).
- Observed on runtime: `core/v4.0.1` / `libzlink.so:4.0.1`.
- Symptom: blocking `SUB.Receive(..., None)` throws
  `ZlinkException: Resource temporarily unavailable` during an alternating
  `PUB.Send -> SUB.Receive` loop, even after initial readiness succeeds.

## Repro

### 1. Perf binary repro

```bash
dotnet build bindings/dotnet/perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj -c Release
dotnet bindings/dotnet/perf/single/Zlink.BindingBench/bin/Release/net8.0/Zlink.BindingBench.dll PUBSUB inproc 64
```

Observed stderr:

```text
single_pubsub_error:Resource temporarily unavailable
```

Exit code:

```text
2
```

### 2. Runner repro

```bash
python3 bindings/perf/run_policy_bench.py \
  --binding dotnet \
  --suite single \
  --mode observe \
  --pattern PUBSUB \
  --msg-sizes 64,256,1024 \
  --transports inproc \
  --runs 1 \
  --reuse-build \
  --result
```

Observed failures:

```text
- PUBSUB current inproc 64B: non_zero_exit_2
- PUBSUB current inproc 256B: non_zero_exit_2
- PUBSUB current inproc 1024B: non_zero_exit_2
```

Result file:

```text
bindings/dotnet/perf/results/single/tmp/perf_linux_20260307_163850.txt
```

### 3. Minimal socket-level repro

This reproduces outside the perf binary with only `Context`, `PUB`, `SUB`,
`inproc://...`, `Subscribe("")`, and alternating `Send(None)` /
`Receive(None)` calls.

Observed output:

```text
ready=True
warmup-ok
ZlinkException:Resource temporarily unavailable
```

Key characteristics of the minimal repro:

- same-process `PUB` + `SUB`
- `inproc` endpoint
- `SUB` has `RcvTimeo=200`
- one message sent, one message received, repeated
- no application-level retry budget or extra helper logic

## Why this looks like runtime/core behavior

- Existing short `.NET` tests for `pubsub_basic(inproc)` pass, so basic setup is
  valid.
- Initial readiness also succeeds in the minimal repro.
- The failure appears only after sustained alternating send/recv load.
- The reproducer uses socket-level APIs directly and does not depend on
  perf-specific service wrappers or poller ownership rules.

This points at an `inproc PUB/SUB` delivery/timing issue under sustained load,
not a `.NET perf` state machine bug.

## Impact

- `.NET` single perf cannot currently satisfy the requested policy for
  `PUBSUB/inproc`:
  - `single = blocking send + blocking recv + nonblocking drain`
- Full `bindings/dotnet/perf/run_benchmarks.sh --reuse-build` cannot be marked
  complete while this case remains failing.

## Expected

- After initial readiness, repeated alternating
  `PUB.Send(None) -> SUB.Receive(None)` on `inproc` should not intermittently
  hit `EAGAIN` / timeout for the subscriber in this workload.

## Actual

- Subscriber eventually times out with `Resource temporarily unavailable`
  although the publisher continues sending in the same loop.

