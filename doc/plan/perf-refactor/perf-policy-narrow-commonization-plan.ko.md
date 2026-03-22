# `core/perf` 좁은 정책 공통화 리팩토링 계획

> 범위: `core/perf`에서 패턴 전체를 템플릿화하지 않고,
> 정책/phase/유틸 성격의 중복만 공통화하는 계획이다.
>
> 이 문서는
> [`perf-send-recv-template-refactor-plan.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/perf-refactor/perf-send-recv-template-refactor-plan.ko.md)
> 의 범위를 재조정한 후속 계획이다. 두 문서가 충돌하면 이 문서가 우선한다.

---

## 1. 목표

이 계획의 목표는 "파일 수를 줄이는 것"이 아니다. 아래를 만족하는
**좁고 깊은 공통화**를 만드는 것이다.

- multi 정책 변경 시 여러 패턴 파일을 동시 수정하는 지점을 줄인다.
- 패턴별 실행 모델은 파일 안에 남겨서 읽는 사람이 의미를 바로 파악할 수 있게 한다.
- `GATEWAY`, `PUBSUB`, `SPOT`, `STREAM`의 고유 lifecycle/monitor/callback 모델은 숨기지 않는다.
- 정책 공통화가 성능/의미 변경으로 번지지 않도록 범위를 제한한다.

### 1.1 기준 원칙

- **정책만 공통화**: send blocked 분류, phase 집계, 공통 옵션, READY/STOP/QUEUE surface 같은
  정책성 규칙만 공통화한다.
- **패턴 모델 유지**: socket/gateway/service 생성과 destroy, monitor 연결, callback vs recv 실행 모델,
  pending queue 구조는 각 패턴 파일에 남긴다.
- **`EAGAIN only` 고정**: multi send backpressure retry는 `EAGAIN`만 허용한다.
  다른 오류를 패턴별 예외로 되돌리지 않는다.
- **recv mode 정책 고정**: multi `recv` 모드는 `poller/poll + nonblocking recv drain`
  모델을 유지한다. 즉 `POLLIN`에서 가능한 만큼 `DONTWAIT recv`를 비우는 규칙은
  공통 정책으로 본다.
- **callback mode 정책 고정**: multi `callback` 모드는 `recv_handler +
  send_ready_handler` 모델을 유지한다. recv mode와 callback mode를 하나의 실행
  템플릿으로 섞지 않는다.
- **얕은 템플릿 금지**: `Policy::send()`, `Policy::recv()`, `Policy::is_blocked_send_errno()`를
  잔뜩 주입하는 큰 템플릿은 만들지 않는다.
- **POSD 우선**: 구현자가 알아야 할 세부사항을 줄이는 공통화만 허용하고,
  단순한 중복 제거용 wrapper는 만들지 않는다.

---

## 2. 현재 코드 기준 판단

### 2.1 공통화 가치가 높은 부분

- `DEALER_ROUTER` / `ROUTER_ROUTER` relay server 2개
  - 현재 [`core/perf/multi/src/perf_multi_dealer_router_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_dealer_router_server.cpp)
    와 [`core/perf/multi/src/perf_multi_router_router_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_router_router_server.cpp)
    는 사실상 `token`, `routing_id 설정`만 다르다.
  - 이 둘은 공통 relay server helper로 옮겨도 패턴 의미가 거의 훼손되지 않는다.

- multi echo client 3개의 phase/metric 정책
  - 대상:
    - [`core/perf/multi/src/perf_multi_dealer_router_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_dealer_router_client.cpp)
    - [`core/perf/multi/src/perf_multi_router_router_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_router_router_client.cpp)
    - [`core/perf/multi/src/perf_multi_gateway_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_gateway_client.cpp)
  - 이 3개에는 다음 중복이 있다.
    - `mark_fatal`
    - percentile 계산
    - active metric reset/집계
    - `warmup -> active` phase 전환
    - `send_enabled`, `auto_send_on_recv`, `inflight` 규칙
    - `start_phase_requests()` / `wait_phase_duration()` 구조
  - 다만 `GATEWAY`는 공통 state 템플릿에 넣지 않고, 공통 phase helper를
    얇게 호출하는 adapter 수준까지만 허용한다.

- 공용 유틸
  - `make_routing_id`
    - 현재
      [`core/perf/multi/src/perf_multi_router_router_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_router_router_client.cpp)
      와
      [`core/perf/multi/src/perf_multi_gateway_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_gateway_client.cpp)
      에 중복돼 있다.
  - stdin `QUEUE/STOP` 명령 파싱
  - 기본 option/env fallback 해석

### 2.2 공통화하면 안 되는 부분

- `GATEWAY client` 전체 lifecycle
  - `ready_monitor`, gateway 생성/파괴, 포인터 기반 slot ownership이
    socket 기반 echo client와 다르다.
  - phase loop 일부는 공통화할 수 있지만, client 전체를 같은 템플릿으로 묶으면 안 된다.

- `GATEWAY server`
  - pending reply queue, backpressure drain, stored multipart ownership이
    단순 relay server와 다르다.

- `PUBSUB` / `SPOT`
  - 둘 다 publish loop처럼 보이지만 `delivery-ready monitor`, `service start gate`,
    queue probe 의미가 다르다.
  - 공통 helper로 묶더라도 매우 얕은 wrapper가 될 가능성이 높다.

- `STREAM`
  - `recv_handler + send_ready_handler + owned message handoff` 모델이라
    poller 기반 echo/relay와 실행 모델이 다르다.
  - 따라서 send backpressure 정책은 공유할 수 있어도, recv-mode poller drain과
    callback send 재개를 같은 공통 실행 모듈로 합치면 안 된다.

- blocked errno policy를 패턴별 policy로 되돌리는 것
  - 현재 multi 정책은 `EAGAIN only`가 맞다.
  - 이를 `Policy::is_blocked_send_errno()` 같은 훅으로 다시 열어두면 안 된다.

---

## 3. 리팩토링 대상과 비대상

### 3.1 이번 계획에서 실제로 할 것

- 신규 공통 header 1: `multi/common/perf_multi_echo_policy.hpp`
  - 형태:
    - 상태를 소유하는 큰 템플릿이 아니라, phase/metric/backpressure 규칙을 모은
      **작은 함수 집합**으로 만든다.
    - 기존
      [`core/perf/multi/common/perf_multi_client_helpers.hpp`](/home/hep7/project/kairos/zlink/core/perf/multi/common/perf_multi_client_helpers.hpp)
      를 대체하지 않고, 그 위에 echo request/reply phase 정책만 추가한다.
  - 책임:
    - multi send backpressure 정책 (`EAGAIN only`)
    - recv-mode 공통 규칙의 **불변식** 문서화
      - `POLLIN` 이후 `DONTWAIT recv drain`을 유지해야 한다는 규칙
      - helper가 recv drain 호출 시점까지 직접 실행 모델로 소유하지는 않는다
    - `mark_fatal`
    - percentile 계산
    - active metric reset/helper
    - `warmup/active` phase 전환 공통 함수
    - `start_phase_requests()` / `wait_phase_duration()` 공통 루프
  - 비책임:
    - socket/gateway 생성
    - send/recv API 호출
    - monitor open/close
    - slot storage layout 결정
    - callback send 재개 (`send_ready_handler`) 실행
    - poller wait / recv drain 호출 시점 결정

- 신규 공통 header 2: `multi/common/perf_multi_relay_server.hpp`
  - 책임:
    - `DEALER_ROUTER` / `ROUTER_ROUTER` server의 공통 relay loop
    - queue probe / READY / STOP 처리
    - 공통 resource metric 출력
  - 차이점 주입:
    - pattern name
    - token
    - socket type
    - server routing id 사용 여부

- 기존 helper 확장
  - `perf_multi_client_helpers.hpp`에 남길 것
    - socket 생성/monitor 준비 계열 helper
    - one-way / round-robin 공용 루프
    - metric header decode/match 보조 함수
  - 새 `perf_multi_echo_policy.hpp`로 옮길 것
    - echo request/reply phase helper
    - echo fatal/latency/active metric helper
  - `make_routing_id`는 `perf_multi_client_helpers.hpp`로 이동
  - stdin `QUEUE` command parsing은 기존 공통 위치 유지 또는 거기로 통합

- 적용 순서 고정
  - 1단계: `DEALER_ROUTER` / `ROUTER_ROUTER` relay server 2개부터 공통화
  - 2단계: `DEALER_ROUTER` / `ROUTER_ROUTER` client에 phase helper 적용
  - 3단계: `GATEWAY client`는 별도 lifecycle을 유지한 채 같은 phase helper를
    thin adapter로만 호출
  - 4단계: `GATEWAY` 적용에서 아래 중 하나라도 만족하면,
    `GATEWAY`는 이번 공통화 범위에서 제외한다.
    - slot iteration adapter 코드가 패턴 파일 기준 10줄을 넘는 경우
    - 공통 helper 내부에 `GATEWAY` 전용 분기가 3개 이상 필요한 경우
    - `MULTI_GATEWAY tcp 64/256/1024` 성능 검증에서 별도 원인 분석이 필요한
      회귀가 생기는 경우
  - 5단계: `GATEWAY`를 제외하면, `echo_policy`는 `DEALER_ROUTER` /
    `ROUTER_ROUTER` 전용 helper로 축소할 수 있다. 이 경우 큰 generic echo
    helper를 유지하는 것보다 DR/RR 전용 config/helper로 단순화하는 안을 먼저 검토한다.

### 3.2 이번 계획에서 하지 않을 것

- `GATEWAY`, `PUBSUB`, `SPOT`, `STREAM` 전체를 하나의 send/recv template로 묶지 않음
- `Policy::recv()` / `Policy::send()` 중심의 대형 template framework를 만들지 않음
- callback/recv 모델을 하나의 공통 실행기에서 모두 숨기지 않음
- backpressure event (`POLLOUT`, `send_ready_handler`) semantics 자체를 변경하지 않음
- `QUEUE/STOP/READY` watcher를 perf 전체 공용 인프라로 확대하지 않음
  - 이번 단계에서는 relay server 2개 공통 helper 안에서만 묶는다.
- `DEALER_DEALER`는 이번 공통화 대상에 포함하지 않음
  - one-way send window 모델이라 echo request/reply phase helper와 분리한다.

---

## 4. 구현 방식

### 4.1 echo client 공통화 경계

- helper 인터페이스와 구현 위치는 아래 수준으로 고정한다.
  - `echo_mark_fatal(state, err)`
  - `echo_reset_active_metrics(state, run_id, msg_size)`
  - `echo_configure_phase_slots(slots, run_id, msg_size, phase, send_enabled)`
  - `echo_stop_phase(slots)`
  - `echo_start_phase_requests(state, timeout_ms, try_send_fn, service_fn)`
  - `echo_wait_phase_duration(state, seconds, service_fn)`
  - `echo_percentile_from_sorted(samples, q)`
  - `echo_is_blocked_send_errno(err)`는 `err == EAGAIN` 고정 helper
  - echo latency 요약/출력 helper
- 여기서 `try_send_fn`과 `service_fn`은 패턴 파일이 넘기는 callable이다.
  - `try_send_fn(slot)`은 `send_status_t`를 돌려준다.
  - `service_fn(state, timeout_ms, progressed_out)`은 기존 `service_*_slots()` 계약을 유지한다.
  - `service_fn` 내부에는 poller event 계산/수정, `POLLIN` 후 drain 반복,
    `POLLOUT` 후 pending send 재시도, pattern-specific recv API shape 처리가 그대로 남는다.
- 공통 helper는 socket/gateway 타입을 알지 못한다.
  - slot 순회와 상태 접근은 패턴 파일이 넘기는 `state`/`slots`에 대해 기존 코드와
    같은 이름의 필드 접근만 사용하는 수준으로 제한한다.
  - `GATEWAY`와 socket 계열의 차이를 숨기기 위한 큰 공통 state 템플릿은 만들지 않는다.
  - 대신 패턴 파일이 제공하는 얇은 slot iteration adapter만 허용한다.
    - 권장 형태는 `for_each_slot(fn)` 같은 패턴 파일 내부 helper다.
    - hot loop 안에서 일반 range adapter/view를 새로 조합하는 방식은 기본안으로 쓰지 않는다.

- structural contract
  - `echo_*` helper는 깊은 template 추론 대신, 아래와 같은 최소 필드 계약을 전제로 한다.
    - `state`: `fatal`, `collect_active`, `active_run_id`
    - `slot`: `send_enabled`, `inflight`, `phase`, `next_seq`
  - 실제 구현 시 이 계약은 문서뿐 아니라
    `perf_multi_echo_policy.hpp` 상단 주석에도 그대로 적는다.
  - 필드 이름을 바꾸는 리팩토링은 helper 적용 대상 패턴에서 동시에 수정해야 한다.

- 패턴 파일에 남길 것
  - `struct slot_t` 정의
  - socket/gateway 생성/cleanup
  - `send_*_request()`
  - `receive_*_reply()`
  - poller 등록/제거
  - recv drain 호출 시점 결정
  - callback mode의 `send_ready_handler` wiring
  - `GATEWAY`의 pointer-slot ownership과 monitor lifecycle

- recv drain을 공통 helper로 올리지 않는 이유
  - 공통 helper는 recv-mode 불변식만 고정하고 drain 함수 자체는 소유하지 않는다.
  - 이유는 pattern별 recv API shape, routing id 처리, gateway handle surface가
    직접 얽혀 있어 callable 주입 비용이 얻는 이점보다 크기 때문이다.

- 상태 모델
  - 공통 header가 slot/state 구체 타입을 소유하지 않는다.
  - `DEALER_ROUTER` / `ROUTER_ROUTER`는 value-slot 기반 helper 호출로 먼저 정리한다.
  - `GATEWAY`는 공통 state 템플릿에 억지로 넣지 않고,
    별도 slot/container를 유지한 채 같은 phase helper만 호출한다.
  - 즉 공통화 단위는 "state 타입"이 아니라 "phase 함수"다.
  - `GATEWAY`는 가능한 한 `for_each_slot(fn)` 같은 얇은 adapter만 제공한다.
    adapter가 커지면 `GATEWAY`는 제외하고 DR/RR 전용 helper로 축소한다.

### 4.2 relay server 공통화 경계

- relay helper 인터페이스는 설정 struct 1개로 고정한다.
  - `pattern_name`
  - `token`
  - `socket_type`
  - `has_server_routing_id`
  - `server_routing_id`
- helper는 다음만 수행한다.
  - socket 생성/기본 option/TLS setup
  - bind / READY 출력
  - stdin `QUEUE/STOP` watcher
  - `zlink_recv(..., 0)` -> 동일 peer로 즉시 echo하는 `relay_router_once()`
  - resource/queue metric 출력
- helper는 pending queue/backpressure 저장을 도입하지 않는다.
  - 현재 `DEALER_ROUTER` / `ROUTER_ROUTER` server의 blocking relay semantics를 그대로 유지한다.

- 공통 header가 소유할 것
  - `run_server_benchmark()`
  - `relay_router_once()`
  - queue probe/metrics/READY/STOP 루프

- 패턴 파일이 전달할 것
  - `k_pattern`
  - `k_token`
  - `k_server_socket_type`
  - `k_server_has_routing_id`
  - `k_server_routing_id`

- `GATEWAY server`는 이 helper를 사용하지 않는다.

---

## 5. 검증 항목

- 빌드
  - `cmake --build core/build`

- 기능/정책 검증
  - `DEALER_ROUTER`, `ROUTER_ROUTER`, `GATEWAY` recv mode smoke
  - `EAGAIN` 외 오류가 retry로 숨겨지지 않는지 확인
  - recv mode에서 `POLLIN` 후 `DONTWAIT recv drain`이 유지되는지 확인
  - `QUEUE/STOP/READY` 동작이 기존과 동일한지 확인
  - `DEALER_ROUTER` / `ROUTER_ROUTER` server는 기존과 동일하게 blocking relay 의미를 유지하는지 확인

- 성능 검증
  - 최소 비교 대상
    - `MULTI_DEALER_ROUTER tcp 64/256/1024`
    - `MULTI_ROUTER_ROUTER tcp 64/256/1024`
    - `MULTI_GATEWAY tcp 64/256/1024`
  - acceptance
    - 리팩토링 전후 throughput/latency 5% 이내를 목표로 하되,
      helper 추가가 실제로 성능 회귀를 만들면 공통화 범위를 다시 줄인다.
    - `GATEWAY` adapter는 hot loop에서 추가 indirection 비용을 만들지 않는지
      별도 확인한다.

---

## 6. 최종 판단 기준

- 공통 header를 읽지 않고도 각 패턴 파일의 "무엇이 다른지"가 즉시 보여야 한다.
- 정책 변경 시 수정 파일 수는 줄어야 하지만,
  패턴별 lifecycle 차이를 숨기는 추상화가 생기면 실패로 본다.
- `relay server 2개 + echo client phase 정책` 범위를 넘어서면
  이번 계획의 취지에서 벗어난 것으로 판단한다.
- `GATEWAY`를 억지로 유지하려고 helper가 커지면 실패로 본다.
  그 경우 `DEALER_ROUTER` / `ROUTER_ROUTER` 전용 공통화로 축소하는 편이 낫다.
