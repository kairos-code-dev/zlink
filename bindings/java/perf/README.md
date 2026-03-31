# Java PERF

Policy-aligned perf suite for `bindings/java`.

This tree is the official Java binding performance surface. It must stay aligned
with:

- `bindings/README.md` perf policy
- `doc/perf/PERF_POLICY.md`
- `doc/perf/PERF_SINGLE_TEST_POLICY.md`
- `doc/perf/PERF_MULTI_TEST_POLICY.md`

The goal is to measure binding-layer cost with comparable messaging scenarios,
not to provide demo code or hide the hot path behind a complex harness.

## Entrypoints

- `perf/run_benchmarks.sh` or `perf/single/run_benchmarks.sh`
- `perf/run_benchmarks_multi.sh` or `perf/multi/run_benchmarks.sh`

Each suite builds and runs the Java binding perf entrypoints directly. There is
no shared cross-binding runner.

## Patterns

Single suite patterns are split into separate files:

- `PAIR`
- `PUBSUB`
- `DEALER_DEALER`
- `DEALER_ROUTER`
- `ROUTER_ROUTER`
- `SPOT`

Multi suite patterns are split into separate files:

- `MULTI_DEALER_DEALER`
- `MULTI_DEALER_ROUTER`
- `MULTI_ROUTER_ROUTER`
- `MULTI_PUBSUB`
- `MULTI_SPOT`
- `MULTI_STREAM`

This keeps each messaging pattern visible in its own source file, as required
by the bindings perf policy.

## Layout

- `perf/common/` shared Java perf utility
- `perf/single/Zlink.BindingBench/` single-suite sources
- `perf/multi/Zlink.BindingBench.Multi/` multi-suite sources
- `perf/results/single/{tmp,report,baseline}`
- `perf/results/multi/{tmp,report,baseline}`

The runnable Gradle subprojects are:

- `:perf-single`
- `:perf-multi`

## Verification

Each suite provides a directly executable entry and writes a report under
`perf/results/`.

- Single: callback receive path only
- Multi: `recv` and `callback` modes, with callback limited to policy-supported
  patterns

Use the wrapper scripts for normal verification so the documented execution path
matches the real measurement flow.

## Smoke

```bash
./perf/run_benchmarks.sh --pattern PAIR --transports tcp --msg-sizes 64 --warmup 1 --duration 1
./perf/run_benchmarks_multi.sh --pattern MULTI_SPOT --recv callback --transports tcp --msg-sizes 64 --clients 4 --warmup 1 --duration 1
```
