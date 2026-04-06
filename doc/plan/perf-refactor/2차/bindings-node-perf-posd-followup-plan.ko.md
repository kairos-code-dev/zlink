# `bindings/node/perf` POSD 후속 리팩토링 계획

> 후속 검토 결론: follow-up 리팩토링 가치가 있다. `perf_metrics.ts` 가 args/payload/report/collector 를 광범위하게 품고 있고, `perf_multi_orchestrator.ts` 와 `run_benchmarks.ts` 의 orchestration/dispatch 분리 여지도 분명하다.
> 전제: perf 목적 범위 내, PERF 정책 준수, `core/perf`와 동일한 측정 의미 유지.
> 대상: `bindings/node/perf/`

## TODO

- [ ] `perf_metrics.ts` 책임 분류와 분리 대상 확정
- [ ] args/payload/report/collector 파일 경계 설계
- [ ] `perf_multi_orchestrator.ts` lifecycle/scenario 분리안 정리
- [ ] `run_benchmarks.ts` dispatch table 구조 설계
- [ ] callback tuning 위치 확정
- [ ] single/multi smoke 검증 완료
- [ ] grep 검증 완료
- [ ] 완료 정의 충족 여부 최종 리뷰

## 1. 목표

- `perf_metrics.ts` 의 args/payload/report/collector 책임을 더 깊은 모듈로 분리한다.
- `perf_multi_orchestrator.ts` 의 프로세스 lifecycle 과 `run_benchmarks.ts` 의 패턴 dispatch 를 분리해 orchestration 경계를 더 선명하게 만든다.
- callback tuning 은 측정 공통부가 아니라 runner policy 로 두되, `core/perf` 와 같은 측정 의미는 유지한다.

## 2. 현재 구조 요약

- 공통 모듈 [perf_metrics.ts](/home/hep7/project/kairos/zlink/bindings/node/perf/common/perf_metrics.ts) 가 CLI parsing, payload header encode/decode, result formatting, callback heuristics, file/report helpers, collector helper 를 함께 관리한다.
- [perf_multi_orchestrator.ts](/home/hep7/project/kairos/zlink/bindings/node/perf/multi/perf_multi_orchestrator.ts) 는 server/client spawn, readiness wait, termination, capture 를 모두 담당한다.
- [run_benchmarks.ts](/home/hep7/project/kairos/zlink/bindings/node/perf/multi/run_benchmarks.ts) 는 pattern matrix, result accumulation, report emission 을 한 번에 처리한다.
- [single/run_benchmarks.ts](/home/hep7/project/kairos/zlink/bindings/node/perf/single/run_benchmarks.ts) 는 상대적으로 얇지만, pattern loop 와 result/report emission 을 함께 다루므로 shared runner 규칙을 볼 때 참고 대상이다.
- [perf_metrics.ts](/home/hep7/project/kairos/zlink/bindings/node/perf/common/perf_metrics.ts) 는 477라인, [perf_multi_orchestrator.ts](/home/hep7/project/kairos/zlink/bindings/node/perf/multi/perf_multi_orchestrator.ts) 는 283라인, [run_benchmarks.ts](/home/hep7/project/kairos/zlink/bindings/node/perf/multi/run_benchmarks.ts) 는 325라인이다.
- single/multi pattern 파일은 이미 send/recv 루프를 직접 보여주고 있어 hot path 가시성은 유지된다.

## 3. 남은 POSD 문제

- `perf_metrics.ts` 에 `parseCommonArgs`, `resolveSinglePatternNames`, `resolveMultiPatternNames`, `createPayload`, `stampPayload`, `callbackSendBurstLimit`, `callbackDrainTicks`, `computeMetrics`, `writeReport` 계열이 공존해 있다.
- `perf_multi_orchestrator.ts` 는 process spawn, stdout/stderr capture, server stop, stream client 특례를 한 파일에 함께 품고 있다.
- `run_benchmarks.ts` 는 orchestration, pattern dispatch, report write, stream client execution policy 를 모두 조율한다.
- 그래서 args/payload/report/collector 책임과 orchestration/dispatch 책임을 분리하면 change amplification 을 줄일 수 있다.

## 4. 우선순위

- P1: `perf_metrics.ts` 에서 args/payload/report/collector 를 역할별 파일로 나눈다.
- P2: `perf_multi_orchestrator.ts` 와 `run_benchmarks.ts` 의 orchestration/dispatch 경계를 분리한다.
- P3: callback tuning heuristic 은 runner policy 로 유지하되, generic metrics 와 분리한다.

## 5. 단계별 작업

1. metrics/report 책임 분리
- CLI parsing, payload header, metric calculation, report writer 를 분리한다.

2. orchestration/dispatch 분리
- `perf_multi_orchestrator.ts` 의 process lifecycle 과 `run_benchmarks.ts` 의 matrix dispatch 를 서로 다른 책임으로 나눈다.

3. tuning policy 위치 정리
- `callbackSendBurstLimit`, `callbackDrainTicks` 를 generic metrics 에 둘지 runner policy 에 둘지 확정한다.

## 6. 검증 방법

- `cd bindings/node/perf && ./run_benchmarks.sh --pattern PAIR --transports inproc --msg-sizes 64 --warmup 1 --duration 1`
- `cd bindings/node/perf && ./run_benchmarks_multi.sh --pattern MULTI_SPOT --recv callback --transports tcp --msg-sizes 64 --clients 4 --warmup 1 --duration 1`
- `rg -n "callbackSendBurstLimit|callbackDrainTicks|parseCommonArgs|spawnMultiPair|collectLines|waitForLine" bindings/node/perf`

## 7. 완료 정의

- argument/payload/report 책임이 더 좁은 파일로 분리된다.
- orchestration/dispatch 경계가 더 명확해진다.
- callback burst heuristic 의 위치가 명확해진다.
- `core/perf` 와 동일한 측정 의미는 그대로 유지된다.
- follow-up 대상이 runner-layer 책임 분리로 명확히 고정된다.

## 8. 비범위

- JS/TS 공장식 재생성
- 결과 포맷 변경
- 측정 의미 변경
- perf 목적 밖의 런타임 정리
