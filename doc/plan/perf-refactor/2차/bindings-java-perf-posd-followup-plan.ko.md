# `bindings/java/perf` POSD 후속 리팩토링 계획

> 후속 검토 결론: Java perf는 현재 정책 정렬은 끝났지만, `PerfUtil` 이 여전히 config, result, transport, monitor, ready signal, payload, file naming 을 한데 묶고 있어 가장 명확한 follow-up 대상이다.
> 전제: perf 목적 범위 내, PERF 정책 준수, `core/perf`와 동일한 측정 의미 유지.
> 대상: `bindings/java/perf/`

## TODO

- [ ] `PerfUtil` 책임 목록 재분류
- [ ] policy/config 분리안 정리
- [ ] measurement/report 분리안 정리
- [ ] transport/control helper 분리안 정리
- [ ] facade 유지 방식 확정
- [ ] compile 검증 완료
- [ ] single/multi smoke 검증 완료
- [ ] 완료 정의 충족 여부 최종 리뷰

## 1. 목표

- `PerfUtil` 을 더 깊은 모듈로 나눠 변경 증폭을 줄인다.
- hot path는 그대로 패턴 파일에 남기고, 정책/보고/제어만 더 얇게 공통화한다.
- single/multi 공통 의미는 유지하되 모듈별 책임을 명확히 한다.

## 2. 현재 구조 요약

- 공통 모듈 [PerfUtil.java](/home/hep7/project/kairos/zlink/bindings/java/perf/common/src/main/java/dev/kairoscode/zlink/perf/PerfUtil.java) 가 config parsing, payload/header, latency 계산, endpoint 생성, TLS, ready signal, monitor wait, result format, result file name 을 모두 떠안고 있다.
- single/multi 패턴 파일은 hot path와 send/recv 시나리오를 직접 드러내고 있어 policy상 올바르다.
- [SingleSendLoops.java](/home/hep7/project/kairos/zlink/bindings/java/perf/single/Zlink.BindingBench/src/main/java/dev/kairoscode/zlink/perf/single/SingleSendLoops.java) 와 [MultiSendLoops.java](/home/hep7/project/kairos/zlink/bindings/java/perf/multi/Zlink.BindingBench.Multi/src/main/java/dev/kairoscode/zlink/perf/multi/MultiSendLoops.java) 는 이미 측정 루프 가시성을 해치지 않는 수준의 보조 책임만 맡는다.

## 3. 남은 POSD 문제

- `PerfUtil` 이 너무 많은 cross-cutting concern 을 품고 있어 transport 또는 reporting 정책 한 군데가 바뀌면 여러 함수가 함께 움직인다.
- `parseSingleArgs`, `parseMultiArgs`, `validateMultiRecvMode` 같은 정책 로직과 `Result.toLine`, `ensureResultsDir`, `resultFileName` 같은 보고 로직이 같은 파일에 있다.
- `waitForMonitorEvent`, `waitForReadySignal`, `sendReadySignal` 같은 control path 와 `configure*Tls` 가 같은 helper에 있어 책임 경계가 아직 넓다.

## 4. 우선순위

- P1: `PerfUtil` 을 policy/config, measurement/report, transport/control 로 나눈다.
- P2: 공통 facade 는 유지하되 실제 구현은 작은 파일로 옮긴다.
- P3: pattern 파일의 hot path inline 원칙은 유지한다.

## 5. 단계별 작업

1. 책임 분해
- `PerfUtil` 에서 config parsing 과 execution helper 를 분리한다.
- `Result`/report formatting 과 endpoint/TLS/control helper 를 나눈다.

2. facade 유지
- 외부 호출부는 가능하면 `PerfUtil` façade를 계속 쓰되 내부 구현만 얇게 만든다.

3. policy 재검증
- single 은 callback-only, multi 는 정책상 허용된 recv 조합만 유지한다.

## 6. 검증 방법

- `gradle :perf-single:compileJava :perf-multi:compileJava`
- `cd bindings/java/perf && ./run_benchmarks.sh --pattern PAIR --transports tcp --msg-sizes 64 --warmup 1 --duration 1`
- `cd bindings/java/perf && ./run_benchmarks_multi.sh --pattern MULTI_SPOT --recv callback --transports tcp --msg-sizes 64 --clients 4 --warmup 1 --duration 1`
- `rg -n "PHASE_DRAIN|settle|recv mode|validateMultiRecvMode|Result.toLine" bindings/java/perf`

## 7. 완료 정의

- `PerfUtil` 의 책임 경계가 더 얇아지고 새 정책 변경 시 수정 범위가 줄어든다.
- hot path와 pattern 파일 가시성은 유지된다.
- `core/perf` 와 동일한 측정 의미가 변하지 않는다.

## 8. 비범위

- send/recv loop를 공통 runner 뒤로 숨기기
- 정책 밖 recv mode 추가
- RESULT semantics 변경
