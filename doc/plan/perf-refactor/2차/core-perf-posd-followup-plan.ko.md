# `core/perf` POSD 후속 리팩토링 계획

> 후속 검토 결론: 현재 `core/perf`는 완료 상태이지만 `multi/common` 쪽에 아직 큰 공통 헤더가 남아 있어 유지보수 관점의 추가 분리가 가능하다.
> 전제: perf 목적 범위 내, PERF 정책 준수, `core/perf`와 동일한 측정 의미 유지.
> 대상: `core/perf/` (single, multi, common, runner scripts)

## TODO

- [ ] `perf_common.hpp` 책임 목록 재분류
- [ ] `perf_multi_client_helpers.hpp` 분해 범위 확정
- [ ] `perf_multi_spot_control.hpp` 분해 범위 확정
- [ ] runner/orchestration 규칙 집중 방식 확정
- [ ] 구조 분해 후 정책 회귀 점검
- [ ] single/multi smoke 검증 완료
- [ ] 완료 정의 충족 여부 최종 리뷰

## 1. 목표

- `core/perf`의 측정 의미는 유지하면서, 남아 있는 대형 공통 헤더의 변경 증폭을 줄인다.
- hot path는 계속 각 패턴 파일과 runner에서 직접 보이도록 유지한다.
- ready gate, client handshake, echo policy, metric/report helper를 더 명확한 소유권으로 분리한다.

## 2. 현재 구조 요약

- single 측은 `bench_common.hpp`와 `bench_common_runtime.hpp`로 큰 분리가 이미 끝나 있다.
- multi 측은 `perf_common.hpp`, `perf_common_multi.hpp`, `perf_multi_client_helpers.hpp`, `perf_multi_spot_control.hpp`가 아직 비교적 큰 편이다.
- `perf_multi_stream_session.hpp`, `perf_multi_relay_server.hpp`, `perf_multi_handshake.hpp`는 역할이 분리되어 있으나 공통 제어 흐름은 아직 넓은 헤더에 걸쳐 있다.

## 3. 남은 POSD 문제

- `core/perf/multi/common/perf_common.hpp`에는 여전히 transport/metric/control 성격의 공용 코드가 함께 모여 있다.
- `core/perf/multi/common/perf_multi_client_helpers.hpp`는 client fan-out, ready gate, send loop 보조를 함께 다룬다.
- `core/perf/multi/common/perf_multi_spot_control.hpp`는 SPOT barrier, discovery, ready state, stop path를 넓게 감싸고 있어 변경 증폭이 크다.
- 현재 구조가 정책상 hot path를 숨기지는 않지만 작은 정책 변경에도 넓은 헤더를 함께 손대야 하는 temporal coupling이 남아 있다.

## 4. 우선순위

- P1: `perf_common.hpp`를 generic metric/report helper와 transport/control helper로 더 좁게 나눈다.
- P2: `perf_multi_client_helpers.hpp`에서 client fan-out과 stop coordination을 별도 helper로 분리한다.
- P3: `perf_multi_spot_control.hpp`에서 SPOT 전용 barrier 프로토콜과 공통 ready-gate 보조를 분리한다.

## 5. 단계별 작업

1. 현황 동결
- `perf_common.hpp`와 `perf_multi_client_helpers.hpp`의 책임 목록을 다시 나눈다.
- SPOT/barrier 관련 코드와 일반 multi 공통 코드의 경계를 표로 정리한다.

2. 공통 제어 책임 분리
- generic metric/report helper만 남기는 방향으로 `perf_common.hpp`의 폭을 줄인다.
- client fan-out용 helper와 server ready helper를 분리한다.

3. SPOT 전용 제어 정리
- `perf_multi_spot_control.hpp`의 barrier/discovery/stop 책임을 더 작은 모듈로 나눈다.
- measurement 의미나 RESULT 포맷은 건드리지 않는다.

## 6. 검증 방법

- `./core/build.sh` 또는 `./core/builds/linux/build.sh x64 ON`
- `./core/tests/run_test_lanes.sh --include-e2e --include-regression`
- `./core/perf/run_benchmarks.sh`
- `./core/perf/run_benchmarks_multi.sh`
- `rg -n "wait_for_spot_ready_settle|snapshot polling|internal env" core/perf`

## 7. 완료 정의

- `multi/common`에서 control 책임이 더 좁은 모듈로 분리된다.
- hot path는 각 패턴 파일과 runner에 그대로 보인다.
- `core/perf`의 throughput/bandwidth/latency 의미와 RESULT 포맷은 기존과 동일하다.
- PERF 정책을 위반하는 새 추상화가 없다.

## 8. 비범위

- measurement formula 변경
- RESULT line 포맷 변경
- retry/sleep 기반 우회 도입
- hot path를 숨기는 통합 runner 생성
