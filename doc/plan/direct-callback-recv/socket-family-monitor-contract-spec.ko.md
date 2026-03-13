# 소켓 패밀리별 Monitor Contract 및 회귀 테스트 스펙

## 1. 목적

이 문서는 raw socket monitor 이벤트와 service monitor 이벤트를
`패밀리별 제어 contract` 관점에서 다시 정리한다.

핵심 목표는 두 가지다.

1. 각 public monitor event가 어떤 상태를 보장하는지 고정한다.
2. 그 contract를 깨뜨리는 회귀를 소켓 패밀리군별 테스트로 막는다.

이 문서는 "이벤트가 발생했다"를 검증하는 문서가 아니라
"이 이벤트를 본 뒤 어떤 제어를 해도 되는가"를 고정하는 문서다.

## 2. 배경

최근 readiness 관련 버그는 공통 패턴을 가진다.

- raw transport event를 실제 first-delivery gate로 과하게 해석했다.
- service readiness event가 실제 data-plane readiness보다 앞서 발생했다.
- perf / scenario code가 sleep, retry, peer polling 없이 monitor event만으로
  시작 조건을 판정하려고 할 때 contract가 흔들렸다.

관련 리포트:

- [single PAIR: ready 이후 no delivery](../../bug/direct-callback/2026-03-13-single-pair-ready-but-no-delivery.md)
- [single SPOT delivery-ready flake](../../bug/direct-callback/2026-03-13-single-spot-delivery-ready-flake.md)
- [SPOT subscription-ready WSS delivery gap](../../bug/direct-callback/2026-03-13-spot-subscription-ready-wss-delivery-gap.md)
- [multi SPOT delivery-ready gap](../../bug/direct-callback/2026-03-13-multi-spot-delivery-ready-gap.md)

## 3. 범위

이 문서는 아래를 다룬다.

- raw socket monitor (`zlink_socket_monitor_open`, `zlink_monitor_snapshot`)
- socket family별 raw monitor contract
- service overlay (`Gateway`, `SPOT`)가 raw monitor 위에 추가로 보장해야 하는
  readiness contract
- 회귀 테스트가 어떤 축으로 짜여야 하는지

이 문서는 아래는 다루지 않는다.

- 모든 transport 조합의 exhaustive matrix
- 운영적 지표 수집 정책
- bindings별 API shape

## 4. 기본 원칙

### 4.1 모든 public event는 제어 의미가 있어야 한다

public event는 적어도 하나의 명시적인 제어에 써도 되는 의미를 가져야 한다.

다만 모든 event가 같은 제어에 쓰일 필요는 없다.

- `ACCEPTED`는 `inbound transport 생성` 제어에는 쓸 수 있다.
- `CONNECTION_READY`는 `raw session 송수신 시작` 제어에는 쓸 수 있다.
- `*_DELIVERY_READY_CHANGED`는 `first delivery 시작` 제어에는 쓸 수 있다.

반대로 어떤 event가 자기 레벨에서도 제어 의미가 없다면 public API에 둘 이유가
없다. 그런 event는 debug/observability 전용으로 내려야 한다.

### 4.2 레벨을 넘는 해석을 금지한다

다음 세 레벨을 섞지 않는다.

1. transport-progress
2. session-ready
3. delivery-ready

예:

- `ACCEPTED`를 first-delivery gate로 쓰면 안 된다.
- raw `CONNECTION_READY`를 `PUB/SUB` 첫 publish gate로 쓰면 안 된다.
- `PEER_UP`를 `SPOT` publisher first-delivery gate로 쓰면 안 된다.

### 4.3 readiness event는 monotonic한 postcondition을 가져야 한다

readiness 성격의 public event는 emit 직후 아래 성질을 만족해야 한다.

- 이벤트가 보장하는 상태가 실제로 성립한다.
- 그 상태를 바로 사용해도 추가 sleep/retry가 필요 없다.
- 같은 의미의 상태가 transient하게 거짓으로 흔들리지 않는다.

이 원칙을 만족하지 못하면 event 의미를 강화하거나, 더 강한 상위 event를
새로 정의해야 한다.

### 4.4 공통 사용 패턴은 `open -> snapshot -> incremental events`

현재 상태가 필요하면 monitor handle을 연 뒤 바로 snapshot을 읽고,
그 뒤 변화는 event로 받는다.

- raw socket monitor: `zlink_socket_monitor_open()`
- service monitor: `zlink_*_monitor_open()`
- snapshot: `zlink_monitor_snapshot()`

handler가 필요 없으면 `NULL`을 넘기지 않고 아래 symbol을 사용한다.

- `zlink_monitor_ignore_handler`
- `zlink_service_monitor_ignore_handler`

## 5. 공통 Event 분류

### 5.1 Raw Socket Event 분류

| 이벤트 | 레벨 | 보장하는 상태 | 제어용으로 써도 되는 것 | 제어용으로 쓰면 안 되는 것 |
|---|---|---|---|---|
| `LISTENING` | transport-progress | bind/listen 성공 | bind 완료 후 후속 connect 시작 | data-plane 송수신 시작 |
| `BIND_FAILED` | failure | bind 실패 | 즉시 fail-fast, fallback endpoint 선택 | 이후 계속 진행 |
| `CONNECTED` | transport-progress | outbound transport 연결 수립 | connect progress 추적, reconnect state 관리 | session-ready, first-delivery 시작 |
| `CONNECT_DELAYED` | transport-progress | sync connect 미완료, async retry 예정 | reconnect/backoff 상태 전이 | ready 판정 |
| `CONNECT_RETRIED` | transport-progress | reconnect 진행 중 | retry 관찰, health logging | ready 판정 |
| `ACCEPTED` | transport-progress | inbound transport 수락 | peer slot 생성, transport 관찰 | session-ready, first-delivery 시작 |
| `ACCEPT_FAILED` | failure | accept 실패 | fail-fast, resource/error 처리 | 계속 정상 진행 |
| `CONNECTION_READY` | session-ready | zlink session handshake 완료 | raw session 첫 송수신 시작, routing_id 사용 시작 | `PUB/SUB` 첫 publish delivery 보장 |
| `HANDSHAKE_FAILED_*` | failure | handshake 실패 | 즉시 fail-fast, 보안/프로토콜 오류 분기 | 성공 경로 계속 진행 |
| `DISCONNECTED` | terminal/failure | 기존 session 종료 | reconnect, peer teardown, state drop | readiness 유지 가정 |
| `CLOSED` | terminal | intentional local close | local shutdown bookkeeping | remote disconnect reason 해석 |
| `CLOSE_FAILED` | failure | close 실패 | 즉시 fail-fast, cleanup error surface | 정상 종료로 간주 |
| `MONITOR_STOPPED` | terminal | monitor가 더 이상 event를 내지 않음 | monitor teardown 완료 | 이후 event 기대 |

### 5.2 Service Event 분류

| 이벤트 군 | 레벨 | 보장하는 상태 | 비고 |
|---|---|---|---|
| `ZLINK_GATEWAY_SERVICE_READY/LOST` | publication-ready | 로컬 gateway service publication 상태 | send 가능 여부와 분리 |
| `ZLINK_GATEWAY_SEND_READY_CHANGED` | delivery-ready | gateway가 요청 송신 가능한 aggregate 상태 | `value`는 `0/1` |
| `ZLINK_GATEWAY_ROUTE_UP/DOWN` | session-ready | peer route 증감 | route count 변화 통지 |
| `ZLINK_SPOT_SUB_FILTER_APPLIED` | control-plane-ready | local filter 설치 완료 | remote forwarding 보장 아님 |
| `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED` | delivery-ready | subscriber가 해당 subject 첫 delivery 수신 가능 | `value`는 `0/1` |
| `ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED` | delivery-ready | remote ready-ack count 변화 관찰 | 운영/관찰용 count surface |
| `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED` | delivery-ready | publisher가 해당 subject로 첫 publish를 제어 gate로 시작 가능 | `value`는 first-delivery-safe ready subscriber 수 |
| `ZLINK_MONITOR_EVENT_READY/LOST` | generic readiness | 서비스 계열 generic alias | concrete event로 재해석 필요 |

## 6. 소켓 패밀리별 Contract

### 6.1 PAIR

#### canonical gate

- bind-side: `CONNECTION_READY`
- connect-side: `CONNECTION_READY`

`ACCEPTED`는 transport 관찰용일 뿐 canonical delivery gate가 아니다.

#### 보장해야 하는 postcondition

두 endpoint가 각자 `CONNECTION_READY`를 본 뒤에는 즉시 양방향 첫 송수신이
성공해야 한다.

- send 이후 추가 settle sleep 불필요
- send/recv timeout 내에 첫 메시지 도달
- snapshot의 `READY`와 `ready_peer_count >= 1` 성립

#### 회귀 테스트 요구사항

- bind-side `LISTENING`
- bind-side `ACCEPTED`
- 양쪽 `CONNECTION_READY`
- `CONNECTION_READY` 직후 첫 client->server 전송 성공
- 같은 시점의 첫 server->client 전송 성공
- disconnect 후 `DISCONNECTED`

### 6.2 DEALER / ROUTER

#### canonical gate

- dealer: `CONNECTION_READY`
- router: `CONNECTION_READY`

`ACCEPTED`는 router 쪽 transport 관찰용이다.
router가 실제 data plane 제어에 쓸 수 있는 기준은 `CONNECTION_READY`와 그때
채워진 `routing_id`다.

#### 보장해야 하는 postcondition

- dealer는 `CONNECTION_READY` 직후 첫 request frame을 보낼 수 있어야 한다.
- router는 `CONNECTION_READY` event의 `routing_id`를 사용해 즉시 첫 reply를
  보낼 수 있어야 한다.
- router snapshot은 `READY`, `ready_peer_count >= 1`을 보여야 한다.
- `DISCONNECTED`는 handshake 완료된 peer에 대해 동일한 `routing_id`를
  유지해야 한다.

#### 회귀 테스트 요구사항

- bind-side `LISTENING`
- bind-side `ACCEPTED`
- 양쪽 `CONNECTION_READY`
- router `CONNECTION_READY.routing_id` non-empty
- immediate dealer->router 2-frame 수신 성공
- immediate router->dealer routed reply 성공
- disconnect 시 same `routing_id` correlation 검증

### 6.3 PUB / SUB

#### canonical gate

raw socket monitor 차원에서는 `first delivery`를 보장하는 canonical gate가 없다.

- raw `CONNECTION_READY`는 transport/session readiness까지만 뜻한다.
- subscription forwarding 완료와 첫 publish delivery 가능은 포함하지 않는다.

즉 raw `PUB/SUB`는 monitor event만으로 "지금 첫 publish를 보내면 반드시
subscriber가 받는다"를 public API로 보장하지 않는다.

#### 보장해야 하는 postcondition

- bind/connect 및 handshake 상태는 raw monitor로 관찰 가능
- subscription/delivery readiness는 raw monitor contract에 포함되지 않음
- first-delivery 제어가 필요하면 application-level barrier 또는 상위 service
  contract를 사용해야 함

#### 회귀 테스트 요구사항

- bind-side `LISTENING`
- bind-side `ACCEPTED`
- 양쪽 `CONNECTION_READY`
- snapshot `READY`, `ready_peer_count >= 1`
- raw `CONNECTION_READY`를 first publish gate로 사용하지 않는다는 negative
  contract 문서화
- service layer인 `SPOT`에서는 별도 `DELIVERY_READY_CHANGED`를 검증

### 6.4 STREAM

#### canonical gate

- server: `CONNECTION_READY`
- external client: raw socket monitor 없음. server event가 canonical anchor다.

`ACCEPTED`는 transport accept만 뜻한다.
stream routing 대상 식별과 첫 payload 송수신 가능은 `CONNECTION_READY`에서만
보장한다.

#### 보장해야 하는 postcondition

- server `CONNECTION_READY`의 `routing_id`는 4B stream peer id여야 한다.
- 그 `routing_id`로 즉시 첫 payload 송신이 가능해야 한다.
- external client가 보낸 첫 payload도 즉시 server recv path로 도달해야 한다.
- disconnect 시 같은 `routing_id`로 peer correlation이 가능해야 한다.

#### 회귀 테스트 요구사항

- bind-side `LISTENING`
- bind-side `ACCEPTED`
- server `CONNECTION_READY.routing_id.size == 4`
- ready 직후 server->client 첫 payload 성공
- ready 직후 client->server 첫 payload 성공
- `DISCONNECTED`와 same `routing_id` correlation 검증

## 7. 서비스 Overlay Contract

### 7.1 Gateway

`Gateway`는 raw ROUTER monitor 위에 한 단계 높은 sendability contract를 올린다.

- `ZLINK_GATEWAY_SERVICE_READY/LOST`
  - 로컬 service publication 상태
  - send 가능 여부 gate가 아님
- `ZLINK_GATEWAY_SEND_READY_CHANGED(value=1)`
  - 요청 송신 시작 gate
  - aggregate ready route가 `0 -> 1`로 바뀐 상태
- `ZLINK_GATEWAY_ROUTE_UP/DOWN`
  - route 증감 관찰 및 route-count 기반 control에 사용
  - first request gate 자체는 `SEND_READY_CHANGED`

즉 gateway 제어에서는 `SERVICE_READY`와 `SEND_READY_CHANGED`를 분리해서
사용해야 한다.

### 7.2 SPOT

`SPOT`은 raw `PUB/SUB` monitor의 한계를 덮는 delivery-ready contract를 제공해야
한다.

- subscriber 측
  - `FILTER_APPLIED`: 로컬 filter 설치 완료
  - `SUB_DELIVERY_READY_CHANGED(value=1)`: 해당 subject 첫 delivery 수신 가능
- publisher 측
  - `PUB_DELIVERY_READY_CHANGED(value>=1)`: 해당 subject로 실제 첫 publish 시작 가능

금지 규칙:

- raw `CONNECTION_READY`를 first publish gate로 사용하지 않음
- `PEER_UP`를 pub first-delivery gate로 사용하지 않음
- `SUBSCRIPTION_READY`가 유지되더라도 `DELIVERY_READY_CHANGED`보다 약한 의미로만
  취급

## 8. 회귀 테스트 스위트 스펙

### 8.1 기본 정책

모든 monitor contract 회귀 테스트는 아래를 따른다.

- fixed sleep 금지
- retry loop 금지
- poll-until-success 패턴 금지
- 단일 bounded wait만 허용
- gate를 통과한 뒤 즉시 첫 I/O를 실행
- timeout이면 바로 실패

즉 테스트는 contract를 검증해야지, 구현의 흔들림을 가려주면 안 된다.

### 8.2 스위트 구성 원칙

이벤트 상수 전체를 커버하되, 모든 상수를 모든 소켓 타입에 중복해서 검증하지는
않는다. 대신 `패밀리군별 canonical scenario`를 정한다.

| 축 | canonical scenario |
|---|---|
| bind success/failure | tcp bind-capable family 1개 이상 |
| connect progress | tcp connect-capable family 1개 이상 |
| inbound accept | bind-capable family 1개 이상 |
| raw first-I/O gate | `PAIR`, `DEALER/ROUTER`, `STREAM` |
| raw non-gate contract | `PUB/SUB` |
| disconnect reason | `PAIR` 또는 `DEALER/ROUTER` |
| protocol handshake failure | incompatible socket pairing 1개 이상 |
| auth failure | `STREAM + tls/wss` 대표 시나리오 1개 이상 |
| monitor terminal lifecycle | raw/socket monitor open-close canonical test |
| service delivery-ready | `Gateway`, `SPOT` |

### 8.3 패밀리별 필수 테스트

#### PAIR suite

- `LISTENING -> ACCEPTED -> CONNECTION_READY` ordering
- ready 이후 first bidirectional delivery
- disconnect reason propagation
- late-open monitor + snapshot 복구

#### DEALER/ROUTER suite

- `LISTENING -> ACCEPTED -> CONNECTION_READY` ordering
- router `routing_id` availability
- ready 이후 first request/reply delivery
- disconnect on same `routing_id`

#### PUB/SUB suite

- `LISTENING -> ACCEPTED -> CONNECTION_READY` ordering
- snapshot ready state
- raw event만으로 first-delivery gate를 제공하지 않는다는 negative assertion
- SPOT overlay와 결합될 때는 service delivery-ready suite에서 검증

#### STREAM suite

- `LISTENING -> ACCEPTED -> CONNECTION_READY` ordering
- stream peer `routing_id` size/identity contract
- ready 이후 첫 payload 양방향 성공
- disconnect correlation

#### Gateway suite

- `SERVICE_READY/LOST`는 publication 상태만 뜻함
- `SEND_READY_CHANGED(1)` 이후 첫 request 가능
- `ROUTE_UP/DOWN.value`는 post-transition ready count
- snapshot `SEND_READY`, `BOUND_READY`, `ready_peer_count`

#### SPOT suite

- `FILTER_APPLIED`는 local control-plane only
- `SUB_DELIVERY_READY_CHANGED(1)` 이후 첫 subscribe delivery 보장
- `PUB_DELIVERY_READY_CHANGED(value>=1)` 이후 첫 publish delivery 보장
- raw `CONNECTION_READY` 또는 `PEER_UP`만으로 phase 시작 금지

### 8.4 transport 우선순위

기본 regression transport는 `tcp`다.

추가 transport는 contract가 갈리는 곳만 별도 검증한다.

- `wss/tls`: auth / handshake / ordering 이슈
- `ipc`: transport-specific event parity가 필요할 때만
- `inproc`: raw monitor contract 대표 transport로 사용하지 않음

## 9. 구현 반영 방향

테스트는 아래 축으로 배치하는 것이 맞다.

- raw family contract:
  - `core/tests/integration/monitoring/test_monitor_enhanced.cpp`
  - `core/tests/integration/monitoring/test_monitor_service_contract.cpp`
  - `core/tests/integration/test_stream_socket.cpp`
- service overlay contract:
  - `core/tests/integration/monitoring/test_monitor_service_contract.cpp`
  - `core/tests/integration/test_monitor_with_handler.cpp`
  - `core/tests/e2e/discovery/test_service_introspection.cpp`
  - `core/tests/e2e/spot/test_spot_service_introspection.cpp`

중요한 점은 테스트 이름보다 contract다.
새 suite를 쪼개든 기존 suite를 확장하든, 위 8장의 contract를 그대로 assertion으로
옮겨야 한다.

## 10. 결정

이 문서 기준으로 앞으로 monitor event는 다음 규칙을 따른다.

1. public event는 최소 하나의 명시적 제어 의미를 가진다.
2. 그 의미는 event 이름과 같은 레벨이어야 한다.
3. readiness 성격의 event는 emit 직후 추가 sleep/retry 없이 사용할 수 있어야 한다.
4. raw monitor로 표현할 수 없는 first-delivery readiness는 service overlay event로
   올린다.
5. 회귀 테스트는 "event 존재"가 아니라 "gate 이후 즉시 제어 가능"을 검증한다.
