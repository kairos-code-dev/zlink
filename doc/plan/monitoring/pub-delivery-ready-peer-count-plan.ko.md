# Monitoring Readiness Contract 전면 개편 계획

> 범위: `core/` monitoring public surface를 호환성 제약 없이 한 번에
> 정리한다.
>
> 목표: public readiness event를 모두 `*_READY_CHANGED` 형식으로 통일하고,
> `event.value`와 snapshot count가 항상 같은 의미를 갖게 만든다.

## 1. 결론

### 1.1 이번 변경의 핵심 기준

이번 변경의 기준은 아래 하나다.

> readiness public event는 모두 `*_READY_CHANGED` 형식으로 정의하고,
> `event.value`는 현재 ready count의 absolute value를 반환한다.

즉:

- event는 ready 집합이 변할 때만 발생한다.
- `event.value`는 delta가 아니라 현재 ready count다.
- boolean readiness는 `event.value > 0`에서 파생한다.
- stricter gate가 필요하면 `event.value == expected_count`를 비교한다.
- snapshot도 같은 current ready count를 반환한다.
- consumer는 이벤트 횟수를 세지 않고 `event.value`만 읽는다.

### 1.2 이번 변경으로 고정하는 결정

이번 문서는 아래를 확정한다.

1. `READY` / `LOST` 이름의 public readiness event는 제거한다.
2. readiness edge와 topology edge는 readiness count와 분리한다.
3. `CONNECTION_READY`는 제거하고 `CONNECTION_READY_CHANGED`로 교체한다.
4. service-level local readiness도 `0/1` count surface로 통일한다.
5. snapshot 필드는 `ready_peer_count`가 아니라 `ready_count`로 바꾼다.
6. generic alias인 `ZLINK_MONITOR_EVENT_READY` /
   `ZLINK_MONITOR_EVENT_LOST`는 제거한다.

### 1.3 왜 이렇게 자르는가

지금 구조는 아래가 섞여 있다.

- edge event
- boolean event
- count event
- generic alias

이 구조에서는 caller가 event별 예외 규칙을 알아야 한다.
POSD 관점에서 이런 API는 얕다.

좋은 public contract는 아래처럼 설명되어야 한다.

- 이름만 보면 형식을 알 수 있다.
- `value` 의미가 event family 전체에서 같다.
- snapshot과 event가 같은 source-of-truth를 본다.
- consumer가 producer 내부 phase를 유추하지 않아도 된다.

---

## 2. 공통 계약

## 2.1 readiness event 규칙

모든 readiness public event는 아래 규칙을 따른다.

1. 이름은 `*_READY_CHANGED`
2. 발생 조건은 ready 집합 변화
3. `event.value = current_ready_count`
4. `event.value == 0`
   - 해당 scope에서 현재 ready 대상이 없음
5. `event.value > 0`
   - 해당 scope에서 하나 이상 ready
6. snapshot의 `ready_count`는 동일한 current_ready_count

### 2.2 edge event 규칙

아래 event는 readiness가 아니라 edge detail이다.

- `CONNECTED`
- `ACCEPTED`
- `DISCONNECTED`
- `SERVICE_UP`
- `SERVICE_DOWN`
- `ROUTE_UP`
- `ROUTE_DOWN`
- `PEER_UP`
- `PEER_DOWN`
- `SUB_FILTER_APPLIED`
- `QUEUE_FULL`
- `QUEUE_DRAINED`
- `CLOSED`
- `ERROR`

이 event들은 topology나 lifecycle detail을 설명한다.
readiness count를 대신하지 않는다.

### 2.3 count scope 규칙

모든 readiness event는 count 형식은 같지만 scope는 다를 수 있다.

예:

- raw socket connection count
- raw `PUB` delivery-ready peer count
- gateway send-ready route/provider count
- service local readiness count
- spot subject별 subscriber readiness count

중요한 것은 count 대상이 문서에 명시되어 있고,
event와 snapshot이 같은 대상을 세야 한다는 점이다.

---

## 3. 새 public surface

## 3.1 raw socket

### 유지하는 edge event

- `ZLINK_EVENT_CONNECTED`
- `ZLINK_EVENT_ACCEPTED`
- `ZLINK_EVENT_DISCONNECTED`

### 제거

- `ZLINK_EVENT_CONNECTION_READY`

### 신설

- `ZLINK_EVENT_CONNECTION_READY_CHANGED`

### 의미

- scope: handshake complete 후 실제 send/recv 가능한 connection 집합
- `event.value = current_ready_connection_count`

예:

- 첫 연결 ready: `1`
- 두 번째 연결 ready: `2`
- 하나 끊김: `1`
- 모두 끊김: `0`

`DISCONNECTED`는 계속 disconnect reason 전용이다.
ready count 감소를 `DISCONNECTED.value`에 싣지 않는다.

## 3.2 raw pub/sub

### 유지하지만 의미 재정의

- `ZLINK_EVENT_PUB_DELIVERY_READY_CHANGED`
- `ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED`

### 의미

- `PUB_DELIVERY_READY_CHANGED`
  - scope: publish delivery 가능한 peer 집합
  - `event.value = current_delivery_ready_peer_count`
- `SUB_DELIVERY_READY_CHANGED`
  - scope: 해당 SUB socket의 delivery readiness
  - 현재는 사실상 `0/1` count

즉 `PUB`는 `1, 2, ..., N`까지 올라갈 수 있어야 하고,
`SUB`는 scope상 `0/1` count일 수 있어도 형식은 동일하다.

## 3.3 discovery service

### 제거

- `ZLINK_DISCOVERY_MONITOR_EVENT_READY`
- `ZLINK_DISCOVERY_MONITOR_EVENT_LOST`

### 신설

- `ZLINK_DISCOVERY_MONITOR_EVENT_READY_CHANGED`

### 유지하는 edge event

- `ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP`
- `ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN`
- `ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED`

### 의미

- scope: discovery service 인스턴스의 local operational readiness
- `event.value`는 현재는 `0/1` count

즉 discovery service 자체가 요청을 처리할 준비가 되었으면 `1`,
아니면 `0`이다.

## 3.4 gateway service

### 제거

- `ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_READY`
- `ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_LOST`

### 신설

- `ZLINK_GATEWAY_MONITOR_EVENT_READY_CHANGED`

### 유지하지만 의미 재정의

- `ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED`

### 유지하는 edge event

- `ZLINK_GATEWAY_MONITOR_EVENT_ROUTE_UP`
- `ZLINK_GATEWAY_MONITOR_EVENT_ROUTE_DOWN`

### 의미

- `GATEWAY_MONITOR_EVENT_READY_CHANGED`
  - scope: gateway service 인스턴스의 local operational readiness
  - `event.value`는 현재 `0/1` count
- `GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED`
  - scope: send 가능한 route/provider 집합
  - `event.value = current_send_ready_count`

즉 service local ready와 route-level send ready를 분리한다.

## 3.5 spot service

### 제거

- `ZLINK_SPOT_MONITOR_EVENT_READY`
- `ZLINK_SPOT_MONITOR_EVENT_LOST`
- `ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY`

### 신설

- `ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED`
- `ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED`

### 유지하지만 형식 명시

- `ZLINK_SPOT_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED`
- `ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED`
- `ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED`

### 유지하는 edge event

- `ZLINK_SPOT_MONITOR_EVENT_PEER_UP`
- `ZLINK_SPOT_MONITOR_EVENT_PEER_DOWN`
- `ZLINK_SPOT_MONITOR_EVENT_SUB_FILTER_APPLIED`
- `ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_FULL`
- `ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_DRAINED`

### 의미

- `SPOT_MONITOR_EVENT_READY_CHANGED`
  - scope: spot service 인스턴스의 local operational readiness
  - `event.value`는 현재 `0/1` count
- `SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED`
  - scope: 특정 subject subscription readiness
  - `event.value`는 현재 `0/1` count
- `SPOT_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED`
  - scope: 특정 subject의 ready subscriber 집합
  - `event.value = current_ready_subscriber_count`
- `SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED`
  - scope: 특정 subject에서 해당 SUB 측 delivery readiness
  - 현재는 사실상 `0/1` count
- `SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED`
  - scope: 특정 subject에서 "첫 delivery-ready subscriber 존재" 상태
  - 현재는 사실상 `0/1` count

`PUB_FIRST_DELIVERY_READY_CHANGED`는 이름상 aggregate count처럼 보이지 않지만,
의미를 따지면 "첫 ready subscriber가 있는지"라는 별도 contract다.
이번 변경에서는 public format만 `0/1` count로 맞추고 이름은 유지한다.

---

## 4. 제거 및 교체 매트릭스

## 4.1 canonical symbol 교체표

| 기존 public symbol | 조치 | 새 public symbol | 새 `value` 의미 |
|---|---|---|---|
| `ZLINK_EVENT_CONNECTION_READY` | 제거 | `ZLINK_EVENT_CONNECTION_READY_CHANGED` | current ready connection count |
| `ZLINK_EVENT_PUB_DELIVERY_READY_CHANGED` | 유지/재정의 | 동일 | current delivery-ready peer count |
| `ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED` | 유지/재정의 | 동일 | current ready count (`0/1`) |
| `ZLINK_DISCOVERY_MONITOR_EVENT_READY` | 제거 | `ZLINK_DISCOVERY_MONITOR_EVENT_READY_CHANGED` | current local ready count (`0/1`) |
| `ZLINK_DISCOVERY_MONITOR_EVENT_LOST` | 제거 | `ZLINK_DISCOVERY_MONITOR_EVENT_READY_CHANGED` | same event, `0` |
| `ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_READY` | 제거 | `ZLINK_GATEWAY_MONITOR_EVENT_READY_CHANGED` | current local ready count (`0/1`) |
| `ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_LOST` | 제거 | `ZLINK_GATEWAY_MONITOR_EVENT_READY_CHANGED` | same event, `0` |
| `ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED` | 유지/재정의 | 동일 | current send-ready route/provider count |
| `ZLINK_SPOT_MONITOR_EVENT_READY` | 제거 | `ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED` | current local ready count (`0/1`) |
| `ZLINK_SPOT_MONITOR_EVENT_LOST` | 제거 | `ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED` | same event, `0` |
| `ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY` | 제거 | `ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED` | current subscription ready count (`0/1`) |
| `ZLINK_SPOT_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED` | 유지/명시 | 동일 | current ready subscriber count |
| `ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED` | 유지/명시 | 동일 | current ready count (`0/1`) |
| `ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED` | 유지/명시 | 동일 | current first-ready count (`0/1`) |

## 4.2 alias 정리

아래 alias는 전부 제거한다.

- `ZLINK_MONITOR_EVENT_READY`
- `ZLINK_MONITOR_EVENT_LOST`
- `ZLINK_GATEWAY_SERVICE_READY`
- `ZLINK_GATEWAY_SERVICE_LOST`
- `ZLINK_GATEWAY_SEND_READY_CHANGED`
  - canonical symbol과 동일 이름 alias 구조가 있으면 alias 제거 후 canonical만 유지
- `ZLINK_SPOT_SUB_SUBSCRIPTION_READY`
- `ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED`
  - canonical symbol과 별칭 이중 노출 구조가 있으면 alias 제거 후 canonical만 유지
- `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED`
- `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED`

원칙은 단순하다.

- public contract는 canonical symbol 하나만 남긴다.
- generic alias나 service shorthand alias는 남기지 않는다.

### canonical symbol 기준

이번 변경에서 public 문서와 테스트가 기준으로 삼아야 하는 symbol family는 아래다.

- raw socket: `ZLINK_EVENT_*`
- discovery: `ZLINK_DISCOVERY_MONITOR_EVENT_*`
- gateway: `ZLINK_GATEWAY_MONITOR_EVENT_*`
- spot: `ZLINK_SPOT_MONITOR_EVENT_*`

즉 `ZLINK_SOCKET_MONITOR_EVENT_*` 같은 내부 레이어 매크로,
`ZLINK_GATEWAY_SERVICE_READY` 같은 shorthand alias,
`ZLINK_MONITOR_EVENT_READY` 같은 generic alias는 public canonical surface로 취급하지 않는다.

## 4.3 service monitor delivery mask 정리

현재 `service_monitor.cpp`는 concrete event를 generic
`ZLINK_MONITOR_EVENT_READY` / `LOST`로 다시 합성한다.

이번 변경 후에는 이 합성 규칙을 제거한다.

즉:

- service monitor watcher는 concrete event만 구독한다.
- "ready 계열 전체"를 보고 싶으면 caller가 명시적으로 OR mask를 만든다.
- hub 내부에서 의미가 다른 event를 generic alias로 다시 묶지 않는다.

---

## 5. snapshot 계약

## 5.1 필드명 변경

`zlink_monitor_snapshot_t.ready_peer_count`는 이름을 바꾼다.

- 기존: `ready_peer_count`
- 변경: `ready_count`

detail flag도 같이 바꾼다.

- 기존: `ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_PEER_COUNT`
- 변경: `ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_COUNT`

이유:

- connection count
- route/provider count
- service local ready count
- subscriber count

를 모두 담을 필드에 `peer`는 너무 좁다.

이번 변경은 호환성을 보지 않으므로 애매한 이름을 유지하지 않는다.

단, `zlink_spot_node_subject_entry_t.ready_peer_count` 같은
introspection/status row field는 이번 monitoring contract 변경의 직접 범위가 아니다.
그 필드들은 별도 문서에서 필요 시 rename 여부를 판단한다.

## 5.2 snapshot 규칙

`snapshot.ready_count`는 producer가 보고 있는 현재 ready count다.

중요한 결정은 아래다.

- `zlink_monitor_snapshot()`은 discovery monitor에는 적용하지 않는다.
- `snapshot.ready_count`는 monitor handle의 `source_kind`마다 정확히 하나의 scope만 가진다.
- local service readiness와 aggregate send readiness가 둘 다 필요한 경우,
  snapshot은 "실제 data plane readiness" 축을 선택한다.

source kind별 의미:

- `ZLINK_MONITOR_SOURCE_SOCKET`
  - raw connection 계열: current ready connection count
  - raw `PUB`: current delivery-ready peer count
  - raw `SUB`: current ready count (`0/1`)
- `ZLINK_MONITOR_SOURCE_GATEWAY`
  - current send-ready route/provider count
  - local gateway readiness는 snapshot에 싣지 않고
    `ZLINK_GATEWAY_MONITOR_EVENT_READY_CHANGED(0/1)`로만 노출
- `ZLINK_MONITOR_SOURCE_SPOT_PUB`
  - current ready subscriber count
- `ZLINK_MONITOR_SOURCE_SPOT_SUB`
  - current ready upstream/source count
  - local spot-sub readiness는 snapshot에 싣지 않고
    `ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED(0/1)`로만 노출

이렇게 정하는 이유는 snapshot의 주된 용도가
"지금 실제 데이터 흐름이 가능한 대상 수"를 즉시 읽는 것이기 때문이다.

local lifecycle readiness는 event로 구독하고,
aggregate data-plane readiness는 snapshot과 `*_READY_CHANGED`에서 읽는다.

### socket source 추가 규칙

`ZLINK_MONITOR_SOURCE_SOCKET`은 socket family를 따로 싣지 않으므로,
`snapshot.ready_count`의 정확한 의미는 monitor를 연 socket type의 contract를 따른다.

즉:

- `ROUTER/DEALER/CLIENT/SERVER/STREAM` 등 connection 계열
  - ready connection count
- `PUB/XPUB`
  - delivery-ready peer count
- `SUB/XSUB`
  - local delivery-ready count (`0/1`)

consumer는 `source_kind == SOCKET`만 보고 공통 해석하지 않고,
자신이 연 socket type과 함께 해석해야 한다.

## 5.3 snapshot state flag 규칙

snapshot의 state flag도 `ready_count`와 같은 축을 따라야 한다.

- `ZLINK_MONITOR_STATE_READY`
  - `ready_count > 0`이면 set
- `ZLINK_MONITOR_STATE_SEND_READY`
  - source가 send-capable이고 `ready_count > 0`이면 set
- `ZLINK_MONITOR_STATE_BOUND_READY`
  - bind/listen 등 local bind readiness 전용
- `ZLINK_MONITOR_STATE_CLOSED`
  - closed terminal state 전용

즉 state flag는 별도 숨은 의미를 만들지 않고,
`ready_count`에서 파생되는 boolean surface여야 한다.

## 5.4 source-of-truth 규칙

반드시 아래를 만족해야 한다.

1. event emit과 snapshot read는 같은 counter를 쓴다.
2. `latest_event.value == snapshot.ready_count`가 성립해야 한다.
3. event coalescing이 있어도 snapshot은 항상 현재값을 반환해야 한다.
4. count는 실제 public send/recv readiness를 결정하는 내부 상태와
   가까운 지점에서 갱신해야 한다.

---

## 6. 구현 방향

## 6.1 raw socket

필요한 구현 작업:

1. `zlink.h`
   - `ZLINK_EVENT_CONNECTION_READY` 제거
   - `ZLINK_EVENT_CONNECTION_READY_CHANGED` 추가
   - `zlink_monitor_snapshot_t.ready_peer_count`를 `ready_count`로 변경
   - `ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_PEER_COUNT`를
     `ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_COUNT`로 변경
2. `socket_base_t`
   - producer별 `current_ready_count()` query hook 추가
   - generic snapshot은 `_pipes.size()` 대신 hook 값을 사용
   - state flag는 `ready_count` 기반으로 계산
3. connection lifecycle producer
   - handshake complete set을 별도 관리
   - 증가/감소 시 `CONNECTION_READY_CHANGED` emit

## 6.2 raw pub/xpub

필요한 구현 작업:

1. `xpub_t`
   - boolean `_delivery_ready_state` 제거
   - `delivery_ready_peer_count` 유지
2. ready peer set 변화 시
   - `PUB_DELIVERY_READY_CHANGED(value=current_count)` emit
3. snapshot hook
   - 동일 count 반환

## 6.3 discovery service

필요한 구현 작업:

1. `READY/LOST` emit 지점 제거
2. local operational readiness state를 `0/1` count로 유지
3. 변화 시 `DISCOVERY_MONITOR_EVENT_READY_CHANGED` emit
4. `SERVICE_UP/DOWN`, `PROVIDERS_CHANGED`는 기존 topology/detail 의미 유지
5. discovery monitor는 `zlink_monitor_snapshot()` 대상에 넣지 않음

## 6.4 gateway service

필요한 구현 작업:

1. `SERVICE_READY/SERVICE_LOST` emit 지점 제거
2. local readiness state를 `READY_CHANGED(0/1)`로 emit
3. send-ready route/provider set을 별도 관리
4. `SEND_READY_CHANGED(value=current_send_ready_count)` emit
5. route topology는 계속 `ROUTE_UP/DOWN`으로 emit
6. gateway snapshot `ready_count`는 local ready가 아니라
   send-ready route/provider count로 고정

## 6.5 spot service

필요한 구현 작업:

1. `READY/LOST` emit 지점 제거
2. local readiness state를 `READY_CHANGED(0/1)`로 emit
3. `SUBSCRIPTION_READY`를 `SUBSCRIPTION_READY_CHANGED(0/1)`로 교체
4. subject별 delivery-ready subscriber set은 current count 유지
5. `PUB_FIRST_DELIVERY_READY_CHANGED`는 이름 유지, payload는 `0/1` count
6. spot pub snapshot `ready_count`는 ready subscriber count로 고정
7. spot sub snapshot `ready_count`는 ready upstream/source count로 고정

## 6.6 service monitor hub

필요한 구현 작업:

1. generic `READY/LOST` alias delivery 제거
2. `event_delivery_mask()`에서 alias 합성 삭제
3. watcher 구독 surface를 concrete event 기준으로 단순화

---

## 7. 문서 동기화 범위

반드시 같이 바꿔야 하는 문서:

- `core/include/zlink.h`
- `doc/api/monitoring.md`
- service별 monitoring 설명 문서
- perf/plan 문서에서 `ready_peer_count` 또는 `READY` alias를 전제로 쓴 부분

문서 규칙은 아래로 통일한다.

1. event 이름
2. ready 대상(scope)
3. `value` 의미
4. `0`의 의미
5. `>0`의 의미
6. 대표 gate 예시

---

## 8. 회귀 및 검증 계획

## 8.1 raw socket monitor 회귀

대상:

- `core/tests/integration/monitoring/test_monitor_socket_contract.cpp`
- `core/tests/integration/monitoring/test_monitor_perf_contract.cpp`
- `core/tests/integration/test_stream_socket.cpp`

검증:

1. connection 2개 형성
2. `CONNECTION_READY_CHANGED`
   - `1`, `2`, `1`, `0`
3. snapshot `ready_count == latest_event.value`
4. `DISCONNECTED.value`는 reason이고 ready count가 아님

## 8.2 raw pub/sub 회귀

대상:

- `core/tests/integration/monitoring/test_monitor_socket_contract.cpp`
- `core/tests/integration/monitoring/test_monitor_perf_contract.cpp`

검증:

1. `PUB` + `SUB` 2개 연결
2. `PUB_DELIVERY_READY_CHANGED`
   - `1`, `2`, `1`, `0`
3. snapshot `ready_count == latest_event.value`
4. `SUB_DELIVERY_READY_CHANGED`
   - `1`, `0`

## 8.3 discovery/gateway 회귀

대상:

- `core/tests/integration/test_monitor_with_handler.cpp`
- `core/tests/integration/discovery/test_gateway_with_handler.cpp`
- `core/tests/e2e/discovery/test_gateway.cpp`
- `core/tests/e2e/discovery/test_service_introspection.cpp`

검증:

1. discovery local ready는 `READY_CHANGED(1/0)`으로만 보인다
2. gateway local ready는 `READY_CHANGED(1/0)`으로만 보인다
3. gateway send-ready는 route/provider 수에 따라 `1..N..0`으로 변한다
4. `SERVICE_UP/DOWN`, `ROUTE_UP/DOWN`은 별도 edge event로 유지된다

## 8.4 spot 회귀

대상:

- `core/tests/e2e/spot/test_spot_service_introspection.cpp`
- `core/tests/e2e/spot/spot_pubsub_scenario_shared.cpp`
- `core/tests/integration/test_multi_socket_contract_regressions.cpp`

검증:

1. spot local ready는 `READY_CHANGED(1/0)`으로만 보인다
2. subscription ready는 `SUBSCRIPTION_READY_CHANGED(1/0)`으로만 보인다
3. pub delivery ready는 subscriber 수에 따라 `1..N..0`으로 변한다
4. first delivery ready는 `1/0` count surface로 동작한다

---

## 9. 구현 순서

권장 순서는 아래다.

1. `zlink.h`
   - public symbol rename/remove/add
   - snapshot field rename
2. producer 구현
   - raw socket
   - xpub
   - discovery
   - gateway
   - spot
3. service monitor hub
   - generic alias 제거
4. tests
   - symbol rename 반영
   - count semantics 회귀 추가
5. `doc/api/monitoring.md`
   - 최종 contract 문서화

이 순서가 좋은 이유는 public naming을 먼저 고정해야
하위 구현과 테스트가 흔들리지 않기 때문이다.

---

## 10. 최종 판단

이번 개편의 핵심은 "ready처럼 보이는 것을 비슷하게 두지 말고,
정말 같은 계약으로 만들자"이다.

최종 public contract는 아래로 요약된다.

- readiness public event는 모두 `*_READY_CHANGED`
- `event.value`는 항상 `current_ready_count`
- snapshot 필드는 `ready_count`
- edge/topology event는 readiness와 분리
- generic `READY` / `LOST` alias는 제거

즉 caller는 더 이상 event별 예외 규칙을 외울 필요가 없다.
`*_READY_CHANGED`를 보면 항상 같은 형식으로 해석할 수 있어야 한다.
