# Single SPOT full-sequence abort in full single run

## Summary

single perf smoke와 isolated `SPOT tcp`는 모두 통과한다. 하지만
`run_benchmarks.sh --pattern ALL`로 full single을 돌리면 마지막 `SPOT tcp`
구간에서 특정 size가 `SIGABRT`로 죽는다.

- full single `ALL`: `SPOT current tcp 1024B: non_zero_exit_-6`
- full single `ALL`: `SPOT current tcp 131072B: non_zero_exit_-6`
- 같은 binary를 isolated로 다시 돌리면 `1024B`, `131072B`, `tcp` 전체 size 묶음이
  모두 정상 통과한다.

즉 현재 문제는 `SPOT tcp` 단독 기능 불량보다는, full single sequence 전체를 거친
뒤에만 드러나는 누적 상태/자원/ordering 상호작용 bug로 보는 편이 맞다.

## Reproduction

### Failing full single

```bash
./core/perf/run_benchmarks.sh \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build \
  --pattern ALL
```

Observed:

- result file:
  `core/perf/results/single/report/perf_linux_20260313_230949.txt`
- final status: `partial`
- failure lines:
  - `SPOT current tcp 1024B: non_zero_exit_-6`
  - `SPOT current tcp 131072B: non_zero_exit_-6`

### Passing isolated reproductions

1. `SPOT tcp 1024`

```bash
./core/perf/run_benchmarks.sh \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build \
  --pattern SPOT \
  --transports tcp \
  --msg-sizes 1024 \
  --runs 1 \
  --duration 5
```

Observed:

- result file:
  `core/perf/results/single/report/perf_linux_20260313_233436.txt`
- status: `complete`

2. `SPOT tcp 131072`

```bash
./core/perf/run_benchmarks.sh \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build \
  --pattern SPOT \
  --transports tcp \
  --msg-sizes 131072 \
  --runs 1 \
  --duration 5
```

Observed:

- same result file series as above
- status: `complete`

3. `SPOT tcp` full size set only

```bash
./core/perf/run_benchmarks.sh \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build \
  --pattern SPOT \
  --transports tcp \
  --runs 1 \
  --duration 5
```

Observed:

- result file:
  `core/perf/results/single/report/perf_linux_20260313_233508.txt`
- status: `complete`
- `64,256,1024,65536,131072,262144` 전부 성공

## What this means

현재 증상은 아래 둘을 시사한다.

1. `SPOT tcp` 자체의 steady-state data path는 살아 있다.
2. full single sequence 전체를 수행한 뒤에만 `SPOT tcp` invocation 일부가
   `SIGABRT`로 죽는다.

즉 transport readiness event 부족 문제라기보다, 장시간 full run에서 축적되는
상태가 `perf_spot` 또는 core 내부 assertion을 깨뜨리는 가능성이 높다.

## Scope observed in this run

full single `ALL` 중 아래는 정상 통과했다.

- raw socket patterns: `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`,
  `ROUTER_ROUTER`
- service pattern: `GATEWAY`
- isolated `SPOT tcp`
- isolated `SPOT tcp 1024`
- isolated `SPOT tcp 131072`

이번 run에서 abort가 관측된 것은 full single `ALL` 안의 `SPOT tcp` 두 size뿐이다.

## Suspect areas

- `core/perf/single/src/perf_spot.cpp`
- `core/perf/single/common/bench_common.hpp`
- `core/src/services/spot/spot_*`
- long full-run sequence 뒤 resource cleanup / monitor teardown / fixed-port reuse

특히 아래 축을 우선 보는 게 맞다.

- earlier patterns가 남긴 monitor/service state가 `perf_spot` 시작 시점에
  간섭하는지
- `perf_spot`의 bind/connect/monitor open/close 순서가 특정 pid/port 조합에서
  assert를 유발하는지
- close/teardown 이후 lingering resource가 다음 pattern에 영향을 주는지

## Requested action

이건 현재 정책상 perf workaround로 덮지 않는다.

- full run 재시도 loop 추가 안 함
- settle sleep 추가 안 함
- transport/size 임의 제외 안 함

필요한 건 core/perf integration 관점의 원인 파악이다.

- full single `ALL` sequence에서 `SPOT tcp 1024` 또는 `131072` abort를 다시
  붙잡아 backtrace 확보
- abort 시점의 assert/errno/source file 확인
- 필요하면 `perf_spot` startup/teardown 회귀 테스트 추가

현재 이 issue가 닫히기 전까지는 full single gate를 완료로 판정하지 않는다.
