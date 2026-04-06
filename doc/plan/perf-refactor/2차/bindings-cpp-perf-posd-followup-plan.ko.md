# `bindings/cpp/perf` POSD 후속 리팩토링 계획

> 작성 기준: 2026-04-06
> 판단: **follow-up 리팩토링 가치 있음, 단 범위는 유지보수성 정리 수준으로 축소됨**
>
> 전제:
> - 이미 완료된 settle/env 정리 과제는 재제안하지 않는다.
> - 현재 `bindings/cpp/perf` 기준으로 `rg -n "wait_for_spot_ready_settle|PERF_MULTI_SPOT_READY_SETTLE_MS" /home/hep7/project/kairos/zlink/bindings/cpp/perf` 는 0건이다.
> - 제안은 perf 목적 범위 안에 있어야 하며, PERF 정책과 `core/perf`와 동일한 측정 의미를 유지해야 한다.
> - 코드 수정은 하지 않는다. 이 문서는 후속 계획만 정리한다.

## TODO

- [ ] 단계 0 계약 재확인
- [ ] `perf_spot_client.cpp` 책임 분리 설계
- [ ] callback/recv metrics helper 공통 골격 정리
- [ ] `perf_single_common.hpp` 추가 분리 가치 재판정
- [ ] single smoke 검증 완료
- [ ] multi callback/recv smoke 검증 완료
- [ ] 결과 파일 및 RESULT 포맷 유지 확인
- [ ] 완료 정의 충족 여부 최종 리뷰

## 1. 목표

`bindings/cpp/perf`를 다음 성질에 더 가깝게 정리한다.

- SPOT 경로의 책임을 `perf_spot_client.cpp` 한 파일에 계속 누적하지 않는다.
- callback/recv 모드가 같은 metrics 수집 골격을 각자 복제하지 않도록 한다.
- `single/common/perf_single_common.hpp`의 넓은 공용 facade를 더 좁은 책임 단위로 분해할 수 있는지 판단한다.
- 측정 의미, 결과 포맷, smoke 검증 흐름은 `core/perf`와 동일하게 유지한다.

## 2. 현재 구조 요약

- `bindings/cpp/perf/multi/src/perf_spot_client.cpp`는 1007라인이고, control-plane 준비, callback/recv active 실행, drain 처리, 결과 출력, resource probe, worker stop까지 함께 가진다.
- 같은 파일 안에서 `run_callback_active()`와 `run_recv_active()`가 서로 다른 동기화 모델을 가지고 있고, 종료/수집 경로도 별도이다.
- `callback_client_state_t`는 phase wait, thread metrics registry, active start/fatal 상태를 함께 들고 있다.
- `perf_spot_client_callback.hpp`와 `perf_spot_client_recv.hpp`는 둘 다 thread-local metrics 등록, mutex 보호 registry, epoch reset, snapshot aggregation 패턴을 거의 같은 형태로 구현한다.
- `single/common/perf_single_common.hpp`는 382라인이고, queue probe, callback receiver, subscribe callback receiver, phase runner 선언을 한 헤더에 함께 둔다.
- `multi/common/perf_common.hpp`는 664라인으로 result 출력과 resource probe helper를 같이 담고 있지만, 이쪽은 이미 공용 helper 성격이 강해서 이번 후속의 1차 타깃은 아니다.
- runner orchestration은 `run_binding_single.sh`(808라인), `run_binding_multi.sh`(1012라인) 쪽에 따로 존재한다. 이건 크기는 크지만 측정 의미 자체를 바꾸는 구조 문제로 보기는 어렵다.

근거:

- `bindings/cpp/perf/multi/src/perf_spot_client.cpp:214-223`
- `bindings/cpp/perf/multi/src/perf_spot_client.cpp:225-235`
- `bindings/cpp/perf/multi/src/perf_spot_client.cpp:614-679`
- `bindings/cpp/perf/multi/src/perf_spot_client.cpp:682-733`
- `bindings/cpp/perf/multi/src/perf_spot_client.cpp:735-760`
- `bindings/cpp/perf/multi/src/perf_spot_client.cpp:905-953`
- `bindings/cpp/perf/multi/common/perf_spot_client_callback.hpp:36-102`
- `bindings/cpp/perf/multi/common/perf_spot_client_recv.hpp:57-147`
- `bindings/cpp/perf/single/common/perf_single_common.hpp:118-192`
- `bindings/cpp/perf/single/common/perf_single_common.hpp:198-348`
- `bindings/cpp/perf/multi/common/perf_common.hpp:515-638`

## 3. 남은 POSD 문제

### 3.1 `perf_spot_client.cpp`가 너무 많은 책임을 가진다

이 파일은 SPOT control-plane, callback path, recv path, metrics aggregation, recv worker 수명, result emission을 동시에 다룬다.
특히 `run_callback_active()`와 `run_recv_active()`는 서로 다른 phase 대기 모델을 쓰고, `drain_recv()`는 데이터-plane 처리까지 직접 수행한다.
이 구조는 과거에 지적된 정책 이슈는 이미 정리됐지만, 여전히 deep module 관점에서는 책임 경계가 넓다.

근거:

- `bindings/cpp/perf/multi/src/perf_spot_client.cpp:614-679`
- `bindings/cpp/perf/multi/src/perf_spot_client.cpp:682-733`
- `bindings/cpp/perf/multi/src/perf_spot_client.cpp:735-760`
- `bindings/cpp/perf/multi/src/perf_spot_client.cpp:905-953`

### 3.2 callback/recv metrics helper family가 같은 골격을 두 번 가진다

`perf_spot_client_callback.hpp`와 `perf_spot_client_recv.hpp`는 둘 다 같은 흐름을 반복한다.

- thread-local metrics 객체를 둔다.
- owner 포인터로 등록 상태를 판단한다.
- mutex 아래에서 registry vector에 넣는다.
- epoch가 바뀌면 count와 sampler를 리셋한다.
- 마지막에 snapshot을 합산한다.

모드별 차이는 실제 수집 소스와 출력 시점만 다르고, registry/collection skeleton은 거의 동일하다. 이건 현재도 grep 구조상 확인 가능하다.

근거:

- `bindings/cpp/perf/multi/common/perf_spot_client_callback.hpp:36-102`
- `bindings/cpp/perf/multi/common/perf_spot_client_recv.hpp:57-118`

### 3.3 `single/common/perf_single_common.hpp`는 아직 broad facade 성격이 강하다

이 헤더는 queue probe, callback receiver, subscribe callback receiver, phase runner 선언을 한 곳에 묶는다.
`callback_receiver_t`와 `subscribe_callback_receiver_t`는 서로 비슷한 event queue / worker / result wait 구조를 공유하면서도 별도 타입으로 유지된다.
즉, single perf 공용부는 이미 많이 정리됐지만, 여전히 "공용 도구"와 "모드별 receiver state machine"이 같은 파일에 공존한다.

근거:

- `bindings/cpp/perf/single/common/perf_single_common.hpp:148-192`
- `bindings/cpp/perf/single/common/perf_single_common.hpp:198-348`

## 4. 우선순위

### P0. `perf_spot_client.cpp` 책임 경계 축소

가장 먼저 해야 할 일이다. SPOT client가 control-plane, recv worker lifecycle, result emission을 동시에 가지는 구조를 줄여야 한다.

### P1. callback/recv metrics helper 골격 통합

중복은 작아 보여도 패턴이 동일하므로, registry/epoch/snapshot 흐름을 한 번만 정의하는 쪽이 더 깊은 모듈이 된다.

### P2. `single/common/perf_single_common.hpp`의 공용 facade 축소 여부 결정

이 파일은 이미 maintenance-heavy 해졌다. 완전한 재설계가 아니라도, receiver state machine과 queue/report 도구를 분리할 경계가 명확한지 먼저 판정해야 한다.

## 5. 단계별 작업

### 단계 0. 계약 재확인

할 일:

- 현재 코드 기준으로 settle/env 이름이 0건인지 다시 확인한다.
- `perf_spot_client.cpp`가 어디까지 책임지는지 함수/필드 단위로 다시 분해한다.
- `single/common/perf_single_common.hpp`가 실제로 어떤 책임들을 묶고 있는지 재분류한다.

완료 기준:

- 이미 끝난 settle/env 정리는 계획에서 완전히 제외된다.
- 새 follow-up 범위가 `perf_spot_client.cpp`, helper headers, single common header로만 좁혀진다.

### 단계 1. `perf_spot_client.cpp` 책임 분리

할 일:

- callback-active 전용 상태와 recv-active 전용 상태의 소유 경계를 다시 나눈다.
- `phase_mutex` / `_phase_mutex` / `_recv_metrics_mutex` / `_recv_stop` / `_recv_fatal`의 소유 이유를 한 번에 설명 가능한 수준으로 정리한다.
- `run_callback_active()`와 `run_recv_active()`가 하나의 클래스 안에서 서로 다른 state machine을 공유하는 부분을 줄인다.

완료 기준:

- control-plane wait와 metrics collection의 경계가 코드로 드러난다.
- hot path 의미는 유지되고, 측정 결과와 phase contract는 바뀌지 않는다.

### 단계 2. callback/recv metrics helper 통합

할 일:

- `bind_spot_callback_thread_metrics()`와 `bind_spot_recv_thread_metrics()`의 공통 등록 패턴을 추출한다.
- `collect_spot_callback_thread_metrics()`와 `collect_spot_recv_thread_metrics()`의 공통 합산 패턴을 추출한다.
- mode-specific 차이는 owner 타입, worker 타입, 결과 출력 포맷처럼 꼭 필요한 부분만 남긴다.

완료 기준:

- registry/epoch/snapshot 흐름이 한 번만 정의된다.
- RESULT 포맷과 latency 의미는 그대로 유지된다.

### 단계 3. `single/common/perf_single_common.hpp` 경계 정리

할 일:

- queue probe / result formatting / receiver state machine이 같은 헤더에 있을 이유를 다시 평가한다.
- `callback_receiver_t`와 `subscribe_callback_receiver_t`의 공통 구조를 분리할 수 있는지 판단한다.
- split 가치가 낮다면 문서상으로 maintenance-only로 확정한다.

완료 기준:

- 공용 도구와 모드별 receiver state machine의 경계가 명시된다.
- 분리하지 않기로 한 부분은 왜 유지보수 영역인지 설명 가능해야 한다.

### 단계 4. 검증

할 일:

- single smoke를 실행한다.
- multi callback smoke를 실행한다.
- multi recv smoke를 실행한다.
- 결과 파일과 RESULT 라인이 정상인지 확인한다.

완료 기준:

- smoke perf가 정상 종료한다.
- 결과 파일이 생성된다.
- 측정 의미, 출력 포맷, phase contract가 유지된다.

## 6. 검증 방법

- single smoke:
  - `bindings/cpp/perf/run_benchmarks.sh --pattern PAIR --transports tcp --msg-sizes 64 --runs 1`
- multi callback smoke:
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern MULTI_SPOT --recv callback --transports tcp --msg-sizes 64 --runs 1 --clients 10 --warmup 1 --duration 1`
- multi recv smoke:
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern MULTI_SPOT --recv recv --transports tcp --msg-sizes 64 --runs 1 --clients 10 --warmup 1 --duration 1`
- 구조 확인:
  - `rg -n "wait_for_spot_ready_settle|PERF_MULTI_SPOT_READY_SETTLE_MS" /home/hep7/project/kairos/zlink/bindings/cpp/perf`
  - `rg -n "bind_spot_callback_thread_metrics|collect_spot_callback_thread_metrics|bind_spot_recv_thread_metrics|collect_spot_recv_thread_metrics" /home/hep7/project/kairos/zlink/bindings/cpp/perf`
- 결과 확인:
  - `bindings/cpp/perf/results/single/report/`
  - `bindings/cpp/perf/results/multi/report/`

## 7. 완료 정의

- settle/env 관련 항목은 follow-up 대상이 아니다.
- `perf_spot_client.cpp`의 책임 경계가 더 좁아진다.
- callback/recv helper의 중복 골격이 줄어든다.
- `single/common/perf_single_common.hpp`가 유지보수용 broad facade인지, 더 쪼갤 수 있는지 결론이 난다.
- smoke 결과가 정상이고 RESULT 포맷이 유지된다.
- `core/perf`와 동일한 측정 의미가 유지된다.

## 8. 비범위

- 이미 완료된 settle/env 정리는 다시 제안하지 않는다.
- `core/perf`나 다른 language binding은 건드리지 않는다.
- 측정 의미, phase contract, RESULT 포맷을 바꾸지 않는다.
- runner 정책이나 build policy를 새로 설계하지 않는다.
- perf 목적과 무관한 일반 기능 추가는 하지 않는다.

## 9. 결론

현재 기준으로 `bindings/cpp/perf`는 더 이상 settle/env 류의 policy 위반 후속 과제를 갖고 있지 않다.
남아 있는 가치는 `perf_spot_client.cpp`의 책임 분해, callback/recv helper 중복 축소, `single/common/perf_single_common.hpp`의 경계 정리처럼 유지보수성 중심의 구조 개선에 한정된다.
즉, 이 디렉토리는 완전한 maintenance-only는 아니지만, follow-up 리팩토링의 범위는 이미 매우 좁아졌다.
