# C++ Binding Perf

C++ binding perf runner for `zlink` using only `bindings/cpp/include/zlink` API.

## Entry points

- Single: `bindings/cpp/perf/run_benchmarks.sh`
- Multi: `bindings/cpp/perf/run_benchmarks_multi.sh`

Both wrappers call `bindings/perf/run_policy_bench.py --binding cpp`.

## Quick start

```bash
# single smoke
bindings/cpp/perf/run_benchmarks.sh \
  --pattern PAIR --transports tcp --msg-sizes 64 --runs 1

# multi smoke
bindings/cpp/perf/run_benchmarks_multi.sh \
  --pattern MULTI_DEALER_DEALER --transports tcp --msg-sizes 64 \
  --runs 1 --clients 10 --warmup 1 --duration 1
```

## Result paths

- `bindings/cpp/perf/results/single/tmp/`
- `bindings/cpp/perf/results/single/report/`
- `bindings/cpp/perf/results/multi/tmp/`
- `bindings/cpp/perf/results/multi/report/`

`--result` 옵션을 주면 `status=complete`일 때 report 파일이 생성됩니다.

## TLS certs

TLS transport uses:

- `bindings/cpp/tests/certs/gen/server.crt`
- `bindings/cpp/tests/certs/gen/server.key`
- `bindings/cpp/tests/certs/gen/ca.crt`
