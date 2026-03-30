# Java PERF

Policy-aligned perf suite for `bindings/java`.

## Entrypoints

- `perf/run_benchmarks.sh` or `perf/single/run_benchmarks.sh`
- `perf/run_benchmarks_multi.sh` or `perf/multi/run_benchmarks.sh`

Each suite builds and runs the Java binding perf entrypoints directly. There is
no shared cross-binding runner.

## Layout

- `perf/common/` shared Java perf utility
- `perf/single/Zlink.BindingBench/` single-suite sources
- `perf/multi/Zlink.BindingBench.Multi/` multi-suite sources
- `perf/results/single/{tmp,report,baseline}`
- `perf/results/multi/{tmp,report,baseline}`

## Smoke

```bash
./perf/run_benchmarks.sh --pattern PAIR --transports tcp --msg-sizes 64 --warmup 1 --duration 1
./perf/run_benchmarks_multi.sh --pattern MULTI_SPOT --recv callback --transports tcp --msg-sizes 64 --clients 4 --warmup 1 --duration 1
```
