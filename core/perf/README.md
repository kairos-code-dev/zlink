# Core PERF Benchmark Scripts

`core/perf` is an execution index for the benchmark entrypoints and the
policy documents that define their behavior.

- `run_benchmarks.sh` for the single suite
- `run_benchmarks_multi.sh` for the multi suite

Source of truth:

- [PERF_POLICY.md](../../doc/perf/PERF_POLICY.md)
- [PERF_SINGLE_TEST_POLICY.md](../../doc/perf/PERF_SINGLE_TEST_POLICY.md)
- [PERF_MULTI_TEST_POLICY.md](../../doc/perf/PERF_MULTI_TEST_POLICY.md)

Key behavior:

- `run_benchmarks.sh` owns the single suite and dispatches the single comparison runner.
- `run_benchmarks_multi.sh` owns the multi suite, normalizes multi defaults, and invokes the shared comparison runner directly.
- `core/build/` is the only supported build directory.
- The default perf surface is the Tier 1 set: `throughput`, `bandwidth`,
  `latency`, `latency_p95`, `latency_p99`.

Official reports are written under `core/perf/results/.../report/`.

Examples:

```bash
./core/perf/run_benchmarks.sh --build-dir /home/hep7/project/kairos/zlink/core/build
./core/perf/run_benchmarks_multi.sh --build-dir /home/hep7/project/kairos/zlink/core/build
```

```bash
./core/perf/run_benchmarks.sh \
  --pattern PAIR \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64,1024 \
  --runs 3 \
  --duration 5
```

```bash
./core/perf/run_benchmarks_multi.sh \
  --pattern STREAM \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --clients 5000 \
  --duration 10 \
  --transports tcp
```

Refer to the policy documents above for phase rules, supported pattern
matrices, and result semantics.
