# Python PERF

Policy-aligned perf suite for `bindings/python`.

This tree is the official Python binding performance surface. It must stay
aligned with:

- `bindings/README.md` perf policy
- `doc/perf/PERF_POLICY.md`
- `doc/perf/PERF_SINGLE_TEST_POLICY.md`
- `doc/perf/PERF_MULTI_TEST_POLICY.md`

The goal is to measure Python binding boundary cost on the canonical public
surface, not to hide extra work behind helper wrappers.

## Entrypoints

- `./perf/run_benchmarks.sh`
- `./perf/run_benchmarks_multi.sh`
- `./perf/single/run_benchmarks.sh`
- `./perf/multi/run_benchmarks.sh`

Top-level wrappers must stay thin and forward directly to the suite-specific
runner so the documented execution path matches the real measurement path.

## Layout

- `perf/single/`
- `perf/multi/`
- `perf/run_benchmarks.sh`
- `perf/run_benchmarks_multi.sh`

Each pattern keeps its own executable source file. This preserves visible
ownership of the hot path and avoids a shallow mega-runner that obscures
pattern-specific costs.

## Single Suite

Single-suite measurements use the recv path only and expose the official
shared CLI:

- `--pattern`
- `--duration`
- `--msg-sizes`
- `--transports`
- `--runs`
- `--results-dir`
- `--results-tag`

Patterns:

- `PAIR`
- `PUBSUB`
- `DEALER_DEALER`
- `DEALER_ROUTER`
- `ROUTER_ROUTER`
- `SPOT`

Policy transport matrix:

- `PAIR`: `tcp`, `tls`, `ws`, `wss`, `inproc`, `ipc`
- `PUBSUB`: `tcp`, `tls`, `ws`, `wss`, `inproc`, `ipc`
- `DEALER_DEALER`: `tcp`, `tls`, `ws`, `wss`, `inproc`, `ipc`
- `DEALER_ROUTER`: `tcp`, `tls`, `ws`, `wss`, `inproc`, `ipc`
- `ROUTER_ROUTER`: `tcp`, `tls`, `ws`, `wss`, `inproc`, `ipc`
- `SPOT`: `tcp`, `tls`, `ws`, `wss`

When a selected combination is outside the policy transport matrix, or the
runtime reports `protocol not supported`, the runner emits
`UNSUPPORTED,current,...` for that case. Other execution failures remain `fail`
so regressions are not hidden as unsupported cases.

SPOT measurements use the service-aware public surface:
`publish(topic, ...)` on the sender and `subscribe()` on the
receiver. The Python perf helpers no longer attach an external pub/sub pair to
`SpotNode`; they drive the benchmark through the `Spot` facade directly so the
perf path follows the current public contract.

## Multi Suite

The multi suite is process-isolated. The policy transport matrix for the multi
suite is `tcp`, `tls`, `ws`, `wss`. Combinations outside that matrix, or
runtimes that report `protocol not supported`, are reported as
`UNSUPPORTED,current,...`; other failures remain `fail`.

The multi runner exposes the same common CLI surface plus `--clients`.

Patterns:

- `MULTI_DEALER_DEALER`
- `MULTI_DEALER_ROUTER`
- `MULTI_ROUTER_ROUTER`
- `MULTI_PUBSUB`
- `MULTI_SPOT`
- `MULTI_SPOT_REQREP`
- `MULTI_SPOT_SENDSEND`
- `MULTI_STREAM`

Shared component contract:

- `MULTI_STREAM` client uses the shared core `perf_stream_client` path required
  by the perf policy and execution guide.

## Cost Model Rules

Python perf must measure the canonical binding path, so hot paths should avoid:

- hidden payload copies
- hidden list rebuilding beyond multipart shape requirements
- unnecessary UTF-8 encoding or decoding
- helper-specific fallback behavior that the real API does not use
- benchmark-only wrappers that materially change ownership or receive shape

Fastpath helpers may exist for verification, but they must stay clearly
separated from the default canonical perf path.

## Output

Each runnable pattern prints official-style:

```text
RESULT,current,...
```

lines so the output can be consumed by the same reporting flow as other binding
perf suites.

Runner output and saved reports include:

- `## Effective Options (start)`
- `RESULT,current,...` lines
- a markdown summary table
- result files under `results/{single|multi}/report/`
- filenames shaped as `perf_<lang>_<suite>_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt`

## Smoke

```bash
./perf/run_benchmarks.sh --pattern PAIR --msg-sizes 64 --duration 0.2
./perf/run_benchmarks_multi.sh --pattern PUBSUB --msg-sizes 64 --clients 2 --duration 0.2
```
