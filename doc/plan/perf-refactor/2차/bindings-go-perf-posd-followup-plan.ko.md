# `bindings/go/perf` POSD 후속 리팩토링 계획

> 후속 검토 결론: follow-up 리팩토링 가치가 있다. `ready.go` 의 ready-wait 구조와 `multi/spot.go` 의 ready/barrier/drain 흐름은 정책 범위 안에서 더 잘게 나눌 수 있다.
> 전제: perf 목적 범위 내, PERF 정책 준수, `core/perf`와 동일한 측정 의미 유지.
> 대상: `bindings/go/perf/`

## TODO

- [ ] `ready.go` monitor/probe ready-wait 책임 분리 설계
- [ ] `waitMonitorReady` 경로의 timeout/ready 의미 문서화
- [ ] `multi/spot.go`의 ready/barrier/drain 흐름 분리안 정리
- [ ] SPOT control과 measurement 경계 재검토
- [ ] `go build ./...` 검증 완료
- [ ] single/multi smoke 검증 완료
- [ ] grep 검증 완료
- [ ] 완료 정의 충족 여부 최종 리뷰

## 1. 목표

- `internal/perfcommon/ready.go` 의 ready-wait 정책을 더 명확한 helper 로 나눈다.
- `multi/spot.go` 의 ready/barrier/drain 흐름을 나눠 SPOT 제어와 측정의 경계를 분명히 한다.
- measurement 의미와 RESULT 포맷은 `core/perf` 와 똑같이 유지한다.

## 2. 현재 구조 요약

- 공통 책임은 [common.go](/home/hep7/project/kairos/zlink/bindings/go/perf/internal/perfcommon/common.go), [measurement.go](/home/hep7/project/kairos/zlink/bindings/go/perf/internal/perfcommon/measurement.go), [ready.go](/home/hep7/project/kairos/zlink/bindings/go/perf/internal/perfcommon/ready.go), [runtime.go](/home/hep7/project/kairos/zlink/bindings/go/perf/internal/perfcommon/runtime.go) 등으로 나뉘어 있다.
- 다만 [ready.go](/home/hep7/project/kairos/zlink/bindings/go/perf/internal/perfcommon/ready.go) 는 monitor/probe ready wait 를 하나의 helper 로 묶고 있고, [multi/spot.go](/home/hep7/project/kairos/zlink/bindings/go/perf/multi/spot.go) 는 ready tracking, active publish, recv drain 을 같은 흐름에서 처리한다.
- [ready.go](/home/hep7/project/kairos/zlink/bindings/go/perf/internal/perfcommon/ready.go) 는 99라인, [multi/spot.go](/home/hep7/project/kairos/zlink/bindings/go/perf/multi/spot.go) 는 164라인이다.
- single/multi 패턴은 각 파일에서 시나리오를 직접 보여주므로 hot path 가시성은 이미 확보돼 있다.

## 3. 남은 POSD 문제

- `ready.go` 는 `WaitReady` 에서 monitor 기반 wait 와 probe 기반 wait 를 함께 다루고, probe 경로는 사실상 busy wait 에 가깝다.
- `waitMonitorReady` 는 goroutine 과 `time.After` 반복을 쓰고 있어 ready gate 정책이 한눈에 드러나지 않는다.
- `multi/spot.go` 의 `waitForMultiSpotReady` 와 `drainMultiSpotOnce` 는 SPOT barrier, callback ready tracking, active publish, recv drain 을 한 파일의 한 흐름에 섞어 둔다.
- 그래서 ready-wait 와 SPOT barrier 는 지금 상태에서 follow-up 가치가 분명하다.

## 4. 우선순위

- P1: `ready.go` 의 monitor/probe ready wait 를 정책별 helper 로 나눈다.
- P2: `multi/spot.go` 의 barrier/discovery/ready tracking/drain 흐름을 나눈다.
- P3: 측정 의미, RESULT 포맷, policy 제약은 그대로 두고, 변경 증폭이 큰 ready path 부터 먼저 줄인다.

## 5. 단계별 작업

1. ready-wait 책임 정리
- `WaitReady` 의 monitor/probe 분기와 timeout 정책을 따로 설명할 수 있을 만큼 나눈다.

2. SPOT barrier 흐름 분리
- `waitForMultiSpotReady` 와 `drainMultiSpotOnce` 의 역할을 나누고, ready tracking 과 active drain 도 따로 떼어 낸다.

3. 계약 유지
- `core/perf` 와 동일한 측정 의미를 지키고, perf 정책 밖의 새 경로는 더하지 않는다.

## 6. 검증 방법

- `cd bindings/go/perf && go build ./...`
- `cd bindings/go/perf && ./run_benchmarks.sh`
- `cd bindings/go/perf && ./run_benchmarks_multi.sh`
- `rg -n "WaitReady|waitMonitorReady|waitForMultiSpotReady|drainMultiSpotOnce" bindings/go/perf/internal/perfcommon bindings/go/perf/multi`

## 7. 완료 정의

- ready-wait helper 가 monitor/probe 정책별로 더 명확해진다.
- SPOT barrier/readiness/drain 흐름이 더 좁은 책임 단위로 나뉜다.
- `core/perf` 와 동일한 측정 의미가 유지된다.
- follow-up 범위가 ready gate 와 SPOT control/drain 경계 정리로 분명히 고정된다.

## 8. 비범위

- 측정 formula 변경
- RESULT 포맷 변경
- hot path를 감추는 공통 러너 추가
