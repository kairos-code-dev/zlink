# `[05]` `core` 시스템 리팩토링 Phase 3 Engine / Transport / Service 재구성 계획

> 상태: draft
> 목적: engine pipeline, transport adapter, service facade 경계 재정의
> 주의: 상위 계획의 Phase 3(engine) + Phase 4(transport) + Phase 5(service)를
> 구현 관점에서 하나로 묶은 문서

| nav | link |
| --- | --- |
| 목록 | [README](README.ko.md) |
| 이전 | [04 Phase 2 Socket Runtime Split](04-core-system-phase2-socket-runtime-split.ko.md) |
| 다음 | [06 Review Log](06-core-system-review-log.ko.md) |
| 관련 | [00 상위 계획](00-core-system-posd-refactor-plan.ko.md), [01 Baseline](01-core-system-phase0-baseline.ko.md) |
| thread-safe 규약 | [thread-safe-socket-plan](../plan/thread-safe/thread-safe-socket-plan.ko.md) — 3계층 전체 (API 처리 시 hot path/control path/lifecycle 모두 확인). 현재 구현 수준을 유지한다. |

## 1. 목적

Phase 3의 목적은 다음 세 경계를 동시에 정리하는 것이다.

- `asio_engine_t` 중심 engine pipeline
- transport connect/listen/open 계층
- service facade와 internal topology 계층

이 단계는 `core` 전체 리팩토링의 구조 중심부다.

세 영역을 하나의 구현 문서로 묶은 이유:

- engine pipeline의 상위 계약은 transport adapter가 제공하는 channel 추상화에 의존한다.
  engine을 먼저 분리하면 transport 경계가 불안정한 상태에서 engine 인터페이스를 확정해야 한다.
- service facade가 내부 socket 조합 구조를 숨기려면 socket runtime과 engine pipeline 경계가
  정리된 뒤에야 실현 가능하다.
  service만 먼저 바꾸면 아직 정리되지 않은 engine/transport 세부를 다시 노출하게 된다.
- 따라서 세 영역의 경계를 동시에 확인하면서 정리하는 것이
  각각 독립적으로 바꾸는 것보다 change amplification
  (한 기능 변경이 여러 파일을 건드리는 현상)이 적다.

문서 번호 해석:

- 상위 계획 기준: `Phase 3 + Phase 4 + Phase 5`
- 구현 문서 기준: 하나의 통합 설계 문서

## 2. 현재 문제

### 2.1 engine

`asio_engine_t`는 현재 아래를 함께 가진다.

- handshake
- timer
- read/write buffer
- speculative I/O
- gather write
- transport completion 처리

즉 facade, pipeline, perf policy가 한 타입에 공존한다.

### 2.2 transport

transport 구현은 현재 scheme별 파일 분리는 되어 있지만,
상위 관점에서는 다음 작업이 한곳에 모여 있지 않다.

- URI parse
- address normalize
- connect/listen object 선택
- transport open
- TLS/WS/WSS handshake layering

### 2.3 service

service는 public 의미 API를 제공하지만,
일부 경로는 여전히 internal socket topology와 lifecycle details를 암시한다.

## 3. 목표 구조

```text
service facade
        |
        v
service runtime
        |
        +--> socket runtime
        +--> monitor/runtime bridge
        |
        v
engine facade
        |
        v
engine pipeline
        |
        v
transport adapter
        |
        v
protocol codec
```

핵심은 상위가 아래 두 가지를 몰라도 되게 만드는 것이다.

- transport stack 구체 layering
- engine hot path 최적화 세부

### 3.1 각 계층의 추상화 차이

POSD의 "different layer, different abstraction"
(각 계층은 서로 다른 추상화를 제공해야 한다) 원칙상
각 계층이 pass-through(받은 호출을 그대로 아래에 전달하기만 하는 것)가
아니라 다른 추상화를 제공해야 한다.
아래 표는 각 계층이 상위에 제공하는 추상화와
내부로 숨기는 복잡성이 무엇인지 정리한 것이다.

```text
계층                    상위에 제공하는 추상화            내부로 숨기는 것
─────────────────────  ─────────────────────────────  ─────────────────────────────
service facade         service 의미 API                internal socket topology,
                       (create/attach/destroy)         control socket 조합,
                                                       monitor bridge wiring

service runtime        lifecycle state machine,        socket open/close 순서,
                       readiness contract              drain mechanics,
                                                       internal topology snapshot

engine facade          connection start/stop,          handshake state machine,
                       frame ingress/egress            timer/heartbeat,
                                                       buffer strategy

engine pipeline        async read/write                speculative I/O policy,
                       completion contract              gather write,
                                                       buffer growth

transport adapter      client/server endpoint open,    URI parse, address normalize,
                       async channel                   TLS/WS/WSS handshake layering,
                                                       scheme-specific async primitive

protocol codec         frame boundary                  raw/zmp wire encoding,
                                                       version negotiation
```

facade가 pass-through에 그치면 안 되는 이유:

- service facade는 "내부 socket 조합 구조"라는 복잡성을 숨긴다.
  service runtime은 lifecycle state 관리라는 별도 복잡성을 숨긴다.
  둘의 추상화 대상이 다르므로 별개 계층이다.
- engine facade는 "connection이라는 논리 단위"를 상위에 제공하고,
  engine pipeline은 "async I/O 최적화"라는 별도 복잡성을 숨긴다.
  facade가 pipeline 메서드를 단순 위임만 한다면 facade를 제거하고
  pipeline을 직접 노출하는 것이 맞다.
  facade가 존재하는 이유는 connection lifecycle(start/stop/state)을
  pipeline detail(buffer/timer/speculative)과 분리하기 위해서다.

## 4. engine 재구성 원칙

요약 결정:

- `asio_engine_t`는 facade다.
- pipeline은 hot path policy owner다.
- transport adapter는 scheme-specific mechanism owner다.
- service facade는 public 의미 owner다.

### 4.1 `asio_engine_t`는 facade 처럼 읽혀야 한다

상위에 보여야 할 것은 다음 정도다.

- connection start
- frame ingress
- frame egress
- connection state notify

### 4.2 hot path 정책은 pipeline 안으로 내린다

아래는 pipeline 내부 정책으로 정리한다.

- speculative read/write
- gather write
- buffer growth
- heartbeat timer 연동

### 4.3 raw / zmp 차이는 codec 경계에서 정규화한다

engine 상위가 protocol branch를 많이 알면 안 된다.

## 5. transport 재구성 원칙

### 5.1 상위는 scheme보다 capability 를 본다

상위 계약은 다음처럼 읽히는 것이 맞다.

- client endpoint open
- server endpoint open
- framed / raw capability
- secure / non-secure capability

### 5.2 TLS/WS/WSS layering 을 adapter 내부로 내린다

현재처럼 상위에서 handshake나 address detail을 더 알수록
change amplification이 커진다.

### 5.3 address / connecter / listener / transport 책임을 한 경계 아래 묶는다

파일 수를 줄이는 것이 목적은 아니지만,
상위가 transport 조립 디테일을 덜 보게 만드는 것이 목적이다.

## 6. service 재구성 원칙

### 6.1 public API 는 service 의미 중심으로 재설계한다

이번 리팩토링은 **기존 API 호환성을 유지하지 않는다.**

- C API, C++ facade 모두 리팩토링 대상이다.
- 호환성보다 **semantic purity**(service 의미 중심의 깨끗한 인터페이스)를 우선한다.
- 기존 API가 internal concept을 노출하는 경우 호환성 없이 제거하거나 재설계한다.

service는 아래 의미로 설명 가능해야 한다.

- discovery service
- gateway service
- spot service

반대로 아래는 public 설명에서 사라져야 한다.

- internal socket role
- internal topology wiring
- control socket 조합 detail

### 6.2 현재 surface inventory

리팩토링 후 내부 socket 조합 구조 없이 설명되어야 할 현재 surface를 정리한다.
각 행은 현재 호출 가능한 API(callable surface)를 적고,
그 API가 간접적으로 드러내는 내부 구현 개념(internal concept)을 별도 구분한다.

아래 표는 C API와 C++ facade 모두를 리팩토링 대상으로 분석한 inventory다.

**gateway — callable surface**

```text
범주                    C++ facade (gateway_t)                     C API (zlink_gateway_*)
──────────────────────  ─────────────────────────────────────────  ─────────────────────────────────
lifecycle               destroy()                                  zlink_gateway_new, _destroy
message                 send(), send_rid()                         zlink_gateway_send, _send_rid
connection              bind(), connect(), disconnect()            zlink_gateway_bind, _connect, _disconnect
discovery               attach_discovery()                         zlink_gateway_attach_discovery
routing / LB            set_lb_strategy(), set_routing_id(),       zlink_gateway_set_lb_strategy,
                        routing_id(), update_peer_weight(),        _set_routing_id, _routing_id,
                        lock_routing_id()                          _update_peer_weight
option                  set_option(), set_socket_option()          zlink_gateway_set_option
TLS                     set_tls_client(), set_tls_server()         zlink_gateway_set_tls_client, _set_tls_server
handler                 set_handler(), set_send_ready_handler(),   zlink_gateway_set_send_ready_handler
                        dispatch_message(), dispatch_send_ready()
monitor                 monitor_open(), fill_monitor_snapshot()    zlink_gateway_monitor_open
endpoint                last_endpoint()                            zlink_gateway_last_endpoint
internal accessor       router()                                   —
mode                    enter_pollable_mode(), ensure_facade_mode() —
```

> 근거: `core/src/services/gateway/gateway.hpp` public section

**gateway — current surface가 암시하는 internal concept**

- `router()` → `router_socket` 포인터 직접 반환 — 상위가 socket topology를 안다
- `dispatch_message()` / `dispatch_send_ready()` → callback wiring이 facade 밖에 노출
- `enter_pollable_mode()` → polling/callback 이중 경로가 facade에서 분기
- `gateway_runtime_t` 전체가 public struct — pools, manual_routes, ready/inflight/down endpoint map 등 내부 상태가 그대로 노출

**spot — callable surface**

```text
범주                    C++ facade                                  C API (zlink_spot_*)
──────────────────────  ──────────────────────────────────────────  ─────────────────────────────────
node lifecycle          spot_node_t(), destroy()                    zlink_spot_node_new, _destroy
child creation          create_spot_pub(), create_spot_sub()        zlink_spot_new (pub+sub)
default helpers         ensure_default_pub(), ensure_default_sub(), —
                        default_pub(), default_sub()
internal receiver       ensure_internal_receiver(),                 —
                        internal_receiver()
connection              bind(), connect_peer_pub(),                 zlink_spot_node_bind,
                        disconnect_peer_pub()                       _connect_peer_pub, _disconnect_peer_pub
discovery               attach_discovery()                          zlink_spot_node_attach_discovery
option                  set_pub_option(), set_sub_option()          zlink_spot_node_set_pub_option, _set_sub_option
TLS                     set_tls_client(), set_tls_server()          zlink_spot_node_set_tls_server, _set_tls_client
handler                 set_send_ready_handler()                    zlink_spot_node_set_send_ready_handler
pub API                 spot_pub_t::publish(), set_option(),        zlink_spot_publish, _set_pub_option,
                        set_send_ready_handler(), routing_id(),     _set_send_ready_handler
                        monitor_open(), destroy()
sub API                 spot_sub_t::subscribe(), subscribe_pattern(), zlink_spot_subscribe, _subscribe_pattern,
                        unsubscribe(), recv(), set_option(),          _unsubscribe, _sub_recv, _set_sub_option,
                        set_direct_handler(), routing_id(),           _set_send_ready_handler
                        monitor_open(), destroy()
internal accessor       ctx(), runtime(),                            —
                        pub_ingress_endpoint(), sub_fanout_endpoint(),
                        public_endpoint(), has_active_peers()
state management        wake_control_task(),                         —
                        replay_subscriptions_if_active_peers(),
                        schedule_subscription_replay(),
                        note_local_sub_filters_changed()
```

> 근거: `core/src/services/spot/spot_node.hpp`, `spot_pub.hpp`, `spot_sub.hpp`, `spot_internal_receiver.hpp`

**spot — current surface가 암시하는 internal concept**

- `runtime()` → `spot_runtime_t` 포인터 직접 반환 — data_ctrl, mesh_pub/xsub 등 internal socket topology 노출
- `ensure_internal_receiver()` → internal receiver가 public child와 같은 레벨로 노출
- `wake_control_task()`, `schedule_subscription_replay()` → control plane 세부가 facade에서 호출 가능
- `spot_runtime_t` 전체가 public struct — attachment map, connected_peer_endpoints, data plane thread 등 노출

**discovery — callable surface**

```text
범주                    C++ facade (discovery_t)                    C API (zlink_discovery_*)
──────────────────────  ─────────────────────────────────────────  ─────────────────────────────────
lifecycle               discovery_t(), destroy()                   zlink_discovery_new, _destroy
registry connect        connect_registry()                         zlink_discovery_connect_registry
routing                 set_routing_id(), routing_id()             zlink_discovery_set_routing_id, _routing_id
socket option           set_socket_option()                        zlink_discovery_setsockopt
monitor                 monitor_open()                             zlink_discovery_monitor_open
service registration    register_service(), update_service_weight(), —
                        unregister_service()
query                   snapshot_providers(), latest_registry_uplink(), —
                        update_seq(), service_update_seq()
observer                add_observer(), remove_observer()          —
summary                 set_discovery_summary_enabled(),            —
                        upsert_service_summary(),
                        upsert_gateway_peer_summary(),
                        erase_service_summary()
```

> 근거: `core/src/services/discovery/discovery.hpp`

**discovery — current surface가 암시하는 internal concept**

- `set_socket_option(socket_role, ...)` → internal socket role 열거를 상위가 알아야 함
- `upsert_service_summary()` / `erase_service_summary()` → registry protocol 세부가 facade에 노출
- `add_observer()` → observer pattern이 facade contract — 내부 notification wiring이 상위에 유출

**목표**:

- API는 service 의미 중심으로 재설계한다.
- internal concept을 노출하는 API는 호환성 없이 제거하거나 재설계한다.
- 각 service의 "internal concept" 항목은 C API와 C++ facade 모두에서
  제거되거나 facade 내부로 숨겨져야 한다.

### 6.3 service API 처리 매트릭스

각 현재 API를 어떻게 처리할지, 그 근거는 무엇인지를 분류한다.
호환성 제약이 없으므로 **가장 단순한 public surface**를 기준으로 판정한다.

처리 분류:

- **keep** — 의미 기반 API로 유효, 유지
- **replace** — 새 의미 기반 API로 교체
- **remove** — internal concept 노출이거나 불필요, 제거
- **internalize** — facade 내부로 이동 (internal header 또는 private)

```text
service      API / concept                         처리           근거
───────────  ─────────────────────────────────────  ─────────────  ─────────────────────────────
gateway      send(), send_rid()                    keep           핵심 data path
gateway      bind(), connect(), disconnect()       keep           connection management
gateway      attach_discovery()                    keep           service 의미 API
gateway      set_lb_strategy(), routing_id() 등    keep           routing 의미 API
gateway      set_option()                          keep           option API
gateway      set_tls_client(), set_tls_server()    keep           TLS 설정
gateway      monitor_open()                        keep           observability
gateway      set_handler(), set_send_ready_handler() keep         callback 설정
gateway      router()                              internalize    socket topology 노출 제거
gateway      dispatch_message(), dispatch_send_ready() internalize callback wiring 내부화
gateway      enter_pollable_mode()                 internalize    polling/callback 분기 내부화
gateway      ensure_facade_mode()                  internalize    내부 검증용
gateway      gateway_runtime_t public struct       internalize    내부 상태

spot         create_spot_pub(), create_spot_sub()  keep           child creation
spot         bind(), connect_peer_pub() 등         keep           connection management
spot         attach_discovery()                    keep           service 의미 API
spot         set_pub_option(), set_sub_option()    keep           option 설정
spot         set_tls_client(), set_tls_server()    keep           TLS 설정
spot         ensure_default_pub(), ensure_default_sub() keep      convenience helper
spot         pub/sub: publish, subscribe, recv 등  keep           핵심 data path
spot         ensure_internal_receiver()            internalize    internal receiver는 child와 분리
spot         internal_receiver()                   internalize    internal concept
spot         runtime()                             internalize    socket topology 노출 제거
spot         ctx()                                 internalize    내부 접근자
spot         pub/sub_ingress/fanout_endpoint()     internalize    internal topology
spot         wake_control_task()                   internalize    control plane 세부
spot         schedule_subscription_replay()        internalize    control plane 세부
spot         note_local_sub_filters_changed()      internalize    control plane 세부
spot         has_active_peers()                    internalize    내부 상태 query
spot         spot_runtime_t public struct          internalize    내부 상태

discovery    connect_registry()                    keep           핵심 설정
discovery    set_routing_id(), routing_id()        keep           routing 의미 API
discovery    monitor_open()                        keep           observability
discovery    register_service() 등                 keep           service registration
discovery    snapshot_providers() 등               keep           query API
discovery    set_socket_option(socket_role, ...)   remove         아래 방침 참조
discovery    add_observer(), remove_observer()     internalize    내부 notification wiring
discovery    upsert_service_summary()              internalize    registry protocol 세부
discovery    upsert_gateway_peer_summary()         internalize    registry protocol 세부
discovery    erase_service_summary()               internalize    registry protocol 세부
discovery    set_discovery_summary_enabled()        internalize    내부 설정
```

internalize 처리된 항목(`router()`, `runtime()`, `gateway_runtime_t` public struct 등)은
facade 내부 또는 internal header로 이동한다.

- `gateway_runtime_t`, `spot_runtime_t` public struct → internal header로 이동
- `router()`, `runtime()`, `ctx()` 등 internal accessor → facade/header에서 제거
- control plane 세부 (`wake_control_task()`, `schedule_subscription_replay()` 등) → private

**discovery `set_socket_option(socket_role, ...)` remove 방침:**

현재 `zlink_discovery_setsockopt(discovery, socket_role, opt, val, len)` C API는
호출자가 internal socket role 열거 (sub socket, dealer 등)를 알아야 한다.
이것은 information hiding 위반이다.

처리 방향:

- **기존 `zlink_discovery_setsockopt` C API를 제거**한다.
- socket role 기반 옵션 API를 **service semantic 기반 API로 재설계**한다.
  예: `set_heartbeat_interval()`, `set_registry_timeout()` 등
  호출자가 "이 discovery에 SUB socket이 있다"를 아예 모르게 만드는 것이 목표다.
- semantic option API는 최소한 아래 범주로 나눈다.
  - **registry link options** — registry uplink/connect/bootstrap 성격의 옵션
  - **heartbeat/report options** — heartbeat 주기, report interval, topology report 관련 옵션
  - **routing/identity options** — discovery routing id, identity 성격의 옵션
  - **monitor/observability options** — monitor/snapshot/summary 출력 정책
- 새 API는 "어느 내부 socket에 적용되는가"가 아니라
  "discovery service의 어떤 의미 동작을 조정하는가"로 이름을 정한다.
- 기존 role enum 값에 대한 하위 호환은 유지하지 않는다.
- 호환성 제약이 없으므로 가장 깨끗한 semantic API를 설계한다.

### 6.4 공통 lifecycle / monitor / readiness 경계를 맞춘다

`gateway`, `spot`, `discovery`는 같은 문장 구조로 설명 가능해야 한다.

예:

- service runtime starts
- internal sockets open
- control task starts
- monitor bridge attaches
- service becomes running

## 7. contract freeze(인터페이스 확정) 순서

세 영역은 상호 의존하므로 구현은 함께 발전시키되(co-evolve),
**contract(인터페이스 시그니처) 확정 순서**는 아래를 따른다.
contract freeze란 해당 계층의 공개 인터페이스를 고정하여
이후 시그니처 변경을 금지하는 것을 뜻한다.

```text
step 1: engine facade contract freeze
           ↓
step 2: transport adapter contract freeze
           ↓
step 3: service surface redesign
```

각 step에서 contract가 확정되면 **해당 인터페이스 시그니처 변경은 금지**된다.
다음 step은 이전 step의 contract가 안정된 상태에서 진행한다.

### 7.1 step 1. engine facade contract freeze

선행 조건:

- Phase 2 socket runtime 분리 완료

확정 대상:

- engine facade가 상위에 제공하는 인터페이스 (connection start/stop, frame ingress/egress)

변경 가능 영역:

- engine pipeline 내부 (speculative I/O, gather write, buffer growth)

금지 영역:

- engine facade 시그니처 변경

완료 조건:

- `asio_engine_t` public method 수 ≤ 현재의 50%
- engine pipeline 내부 변경이 facade 시그니처를 건드리지 않음

### 7.2 step 2. transport adapter contract freeze

선행 조건:

- step 1 engine facade contract 확정

확정 대상:

- transport adapter가 engine에 제공하는 channel 추상화

변경 가능 영역:

- scheme별 adapter 내부 (TLS/WS/WSS handshake layering)

금지 영역:

- transport adapter 인터페이스 시그니처 변경

완료 조건:

- 새 transport scheme 추가 시 수정 파일 수 ≤ 3
- 상위(engine)가 scheme별 handshake detail을 모름

### 7.3 step 3. service surface redesign

선행 조건:

- step 1, step 2 contract 확정

확정 대상:

- service가 사용자에게 제공하는 전체 API (C API + C++ facade)

변경 가능 영역:

- service 내부 topology, runtime 구조
- C API 시그니처 (호환성 제약 없음)

완료 조건:

- 6.3 처리 매트릭스에서 internalize로 분류된 항목이 facade/header에서 제거되거나 internal header로 이동됨
- remove로 분류된 API가 제거됨
- replace로 분류된 API가 새 의미 기반 API로 교체됨
- service API가 internal role 노출 없이 설명 가능

### 7.4 Package H. monitor / readiness 공통 경계 정리

완료 조건:

- service별 monitor/readiness 설명이 공통 구조 언어로 이어짐

### 7.5 죽은 코드 및 불필요한 파일 정리

Phase 3 리팩토링 과정에서 발견되는 아래 항목을 같이 제거한다.

- `asio_engine_t`에서 pipeline으로 이동한 뒤 원본에 남은 중복 코드
- transport scheme 통합 후 사용되지 않는 개별 scheme 헬퍼
- service facade 정리 후 호출처 없는 internal accessor (router(), runtime(), ctx() 등)
- `gateway_runtime_t`, `spot_runtime_t`가 public struct에서 private으로 전환된 뒤
  외부 직접 접근 코드
- 레거시 호환 shim, 주석 처리된 구현, `#if 0` 블록

제거 기준: 호출처 없음이 확인된 것만 제거하고, 기능 게이트를 통과해야 한다.

## 8. 성능 게이트

우선 감시 대상:

- `SPOT`
- `GATEWAY`
- `STREAM_CALLBACK`
- `PUBSUB`
- `DEALER_ROUTER`

핵심 확인:

- handshake 이후 steady-state branch 증가 여부
- transport stack layering으로 인한 alloc/copy 증가 여부
- callback path 깊이 증가 여부

## 9. 문서화 산출물

Phase 3 구현이 시작되면 아래 문서도 같이 갱신한다.

- `doc/internals/architecture.ko.md`
- `doc/guide/10-performance.ko.md`
- service 관련 internals / API 문서

## 10. 완료 조건

정성 기준:

- engine facade, transport adapter, service facade 경계가 분명하다.
- 상위가 handshake / transport layering 세부를 덜 안다.
- service 문서가 internal topology 노출 없이 읽힌다.
- perf baseline 대비 비퇴행 또는 개선이다.

정량 기준 (Phase 3 시작 시 baseline 측정, 완료 시 비교):

- `asio_engine_t` public method 수 감소 (목표: ≤ 현재의 50%)
- 새 transport scheme 추가 시 수정 파일 수 (목표: ≤ 3)
- 6.3 처리 매트릭스의 internalize 항목이 public header에서 제거됨
- remove 항목이 제거되고, replace 항목이 새 의미 기반 API로 교체됨
- destroy path 변경 시 영향 모듈 수 (Phase 시작 시 baseline과 비교)
- 동일 기능 추가 시 cross-layer touch file 수 (Phase 시작 시 baseline과 비교)

정량 기준값은 Phase 3 구현 시작 시 baseline에서 채우고,
00-core-system-posd-refactor-plan.ko.md 인터페이스 축소 표와 직접 연결한다.

## 11. 이후 문서 갱신 체크리스트

- internals architecture 문서 갱신
- service API/internals 문서 갱신
- performance guide의 hot path 설명 갱신
- review log에 구조 결정 반영
