## CppServer Snapshot (Vendored)

This directory contains a vendored snapshot of the upstream CppServer project
for apples-to-apples benchmarking against `asio` and `zlink` stream scenarios.

- Upstream: https://github.com/chronoxor/CppServer
- Snapshot source: local clone from `/tmp/CppServer_bench`
- Copied path: `core/tests/scenario/stream/cppserver/upstream`

### Included

- Almost full upstream source tree (without `.git` and build artifacts)
- `include/`, `source/`, `modules/`, `performance/`, etc.

### Excluded from snapshot

- `.git`
- `build/`
- `build-*`

### Build and run

Use the helper script:

```bash
core/tests/scenario/stream/cppserver/run_cppserver_bench.sh
```

5-stack 동일 시나리오 러너:

```bash
core/tests/scenario/stream/cppserver/run_stream_scenarios.sh
```

이 스크립트는 `s0/s1/s2`를 공통 규약(`RESULT/METRIC/metrics.csv`)으로 실행한다.

Common options:

- `MODE=echo` or `MODE=multicast` (default: `echo`)
- `THREADS=32`
- `CLIENTS=10000`
- `MESSAGES=30` (echo: per-client in-flight count)
- `SIZE=1024`
- `DURATION=10`
- `PORT=27410`

Example:

```bash
MODE=echo THREADS=32 CLIENTS=10000 MESSAGES=30 SIZE=1024 DURATION=10 \
  core/tests/scenario/stream/cppserver/run_cppserver_bench.sh
```

참고:

- `upstream/`이 없으면 스크립트가 자동으로 CppServer를 클론한다.
- 필요 시 특정 ref 고정: `CPPSERVER_UPSTREAM_REF=<tag-or-sha>`
