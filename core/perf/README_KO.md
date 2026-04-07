# Core PERF 벤치마크 스크립트

`core/perf` 실행 진입점은 두 개이며 하나의 공통 comparison runner를 공유한다.

- `run_benchmarks.sh`: single suite
- `run_benchmarks_multi.sh`: multi suite

정책 source of truth:

- [PERF_POLICY.md](../../doc/perf/PERF_POLICY.md)
- [PERF_SINGLE_TEST_POLICY.md](../../doc/perf/PERF_SINGLE_TEST_POLICY.md)
- [PERF_MULTI_TEST_POLICY.md](../../doc/perf/PERF_MULTI_TEST_POLICY.md)

공식 결과는 `core/perf/results/.../report/` 아래에 저장한다.

핵심 규칙:

- `run_benchmarks.sh`는 single suite를 소유하고 single comparison runner로 넘긴다.
- `run_benchmarks_multi.sh`는 multi suite를 소유하고 multi 기본값을 정규화한 뒤 shared comparison runner를 직접 호출한다.
- 공식 빌드 디렉터리는 `core/build/` 하나만 사용한다.
- 기본 perf surface는 `throughput`, `bandwidth`, `latency`,
  `latency_p95`, `latency_p99` 다섯 개다.

빠른 예시:

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

세부 phase 규칙, 지원 패턴 매트릭스, 결과 의미는 위 정책 문서를 따른다.
