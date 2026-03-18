# single `GATEWAY --recv recv` start-race / preflight delivery gap

## Summary

single perf `GATEWAY`의 `recv` 모드는 공개 C API 레벨에서는 가능한 경로처럼
보이지만, 현재 `core/perf/run_benchmarks.sh --pattern GATEWAY --recv recv`
공식 실행 경로에서는 안정적으로 동작하지 않는다.

현재 증상은 preflight/active 초기에 수신 경로가 메시지를 놓치면서 binary가
`exit 1`로 종료되고, runner는 `non_zero_exit_1`로 수집한다.

이 상태는 perf wrapper 문제가 아니라 `core`의 `Gateway recv model` 안정화
문제로 취급해야 한다. 문서/runner에서 지원으로 올리기 전에 core 레벨 수정이
필요하다.

## Reproduction

공식 wrapper 재현:

```bash
./core/perf/run_benchmarks.sh \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --pattern GATEWAY \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --duration 1 \
  --warmup 1 \
  --recv recv
```

관찰 결과:

- runner status: `partial`
- failure label: `GATEWAY current tcp 64B: non_zero_exit_1`
- RESULT line 없음

직접 binary 재현:

```bash
PERF_RECV_MODE=recv \
PERF_SINGLE_WARMUP_SECONDS=1 \
PERF_SINGLE_DURATION_SECONDS=1 \
PERF_SINGLE_SNDTIMEO_MS=200 \
PERF_SINGLE_RCVTIMEO_MS=200 \
PERF_IO_THREADS=2 \
core/build/bin/perf_gateway current tcp 64
```

관찰 결과:

- `PERF_DEBUG` 비활성 시 `exit 1`로 0-result 종료 가능
- `PERF_DEBUG=1` 활성 시 성공하는 경우가 있어, 타이밍 의존 start-race 가능성이 높다

## Observed facts

- single `SPOT --recv recv`는 동일 runner surface에서 complete까지 나온다.
- `Gateway`는 현재 callback 경로에서는 동작한다.
- `Gateway recv`용 direct recv loop와 poller gate를 넣어도 wrapper 경로에서
  여전히 flake/실패가 재현된다.
- `perf_gateway.cpp` 내부 debug 로그 활성화 시 성공 확률이 높아져, 단순
  policy/runner 문제보다 core timing bug 가능성이 크다.

## Current hypothesis

가능성은 두 갈래다.

1. `Gateway recv model`의 readiness/start gate가 monitor-ready와 실제 data-plane
   receive 가능 시점을 동일하게 보장하지 못한다.
2. direct recv + service route priming 사이에 control-plane/data-plane handoff gap이
   있어, preflight 시점 메시지가 안정적으로 관찰되지 않는다.

## Non-goal

아래는 이번 이슈의 해결로 간주하지 않는다.

- perf wrapper에서 `GATEWAY --recv recv`를 묵시적으로 `callback`으로 fallback
- sleep 증가나 runner-side retry로 증상 은폐
- debug/slow path에서만 우연히 통과하는 상태를 지원으로 승격

## Required fix direction

- `Gateway recv model`에서 official start gate를 명시적으로 정의해야 한다.
- monitor-ready와 direct recv 가능 시점 사이 gap이 있다면 core에서 닫아야 한다.
- fix 후에는 아래가 공식 검증 기준이다.

```bash
./core/perf/run_benchmarks.sh \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --pattern GATEWAY \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --duration 1 \
  --warmup 1 \
  --recv recv
```

기대 결과:

- `status: complete`
- `RESULT,current,GATEWAY,tcp,64,...` 5개 이상 출력
- `non_zero_exit_1` 없음

## Current repo decision

- 공식 single support matrix는 아직 `GATEWAY recv`를 지원으로 올리지 않는다.
- 계획 문서에는 미구현 항목으로 유지한다.
- perf 쪽 임시 우회는 추가하지 않는다.
