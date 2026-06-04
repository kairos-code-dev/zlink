# Service Monitor / Readiness 구현 계획

> 상태 메모
> 이 문서는 초기 설계안이다. 본문에 남아 있는
> `zlink_service_monitor_set_handler()` 등 생성 후 handler 교체 서술은
> 현재 canonical public API가 아니다. 최신 기준은
> `direct-callback-recv-interface-review.ko.md`와 `core/include/zlink.h`를 따른다.

## 1. 목적

이 문서는 service facade 기준의 monitor/readiness surface를
callback-only 모델로 정리하는 구현 계획을 정의한다.

이 문서의 canonical 전제는
[`direct-callback-recv-rewrite-spec.ko.md`](./direct-callback-recv-rewrite-spec.ko.md)
다.

핵심 목표는 다음과 같다.

- 사용자가 raw internal socket을 직접 꺼내지 않고도
  service의 상태 전이(topology/readiness/registration)를 관찰하게 한다.
- data readiness와 state transition을 분리한다.
- monitor도 data recv와 같은 방향으로 callback-only로 정렬한다.

즉 목표 상태는 다음 한 줄이다.

```text
poller = data readiness
monitor = state transition callback
```

## 2. 범위와 비범위

### 2.1 범위

대상 public subject:

- `Discovery`
- `Gateway`
- `SpotPub`
- `SpotSub`
- `spot` facade monitor

문서/바인딩 영향 범위:

- `core/include/zlink.h`
- `core/src/api/zlink.cpp`
- `core/src/services/discovery/*`
- `core/src/services/gateway/*`
- `core/src/services/spot/*`
- bindings facade surface
- `doc/spec/core/*`
- `doc/guide/*`

### 2.2 비범위

- `receiver` public type 복원
- `SpotNode`를 public monitor target으로 승격
- `monitor_recv()` 또는 pollable monitor handle 재도입
- monitor를 `poller` 대체재로 사용
- perf 전용 wait helper를 public API로 추가

## 3. 설계 원칙

### 3.1 public subject 고정

이 문서의 public monitor subject는 다음으로 고정한다.

- `zlink_discovery_monitor_open()`
- `zlink_gateway_monitor_open()`
- `zlink_spot_monitor_open()`

두지 않는다.

- `zlink_receiver_monitor_*`
- `zlink_spot_node_monitor_*`
- `zlink_service_monitor_recv()`
- `zlink_poller_add_monitor()`

### 3.2 callback-only delivery

monitor event는 open 시점에 callback을 등록하고
이후 라이브러리가 상태 전이 시 callback을 직접 호출하는 방식으로 정렬한다.

공통 규칙:

- monitor open 시 non-`NULL` handler를 요구한다.
- callback 제거 API는 제공하지 않는다.
- 생성 후 handler 교체는 `*_monitor_set_handler()` 재호출로만 허용한다.
- event는 service instance별 발생 순서를 보존한다.
- destroy/close 이후에는 더 이상 callback이 오면 안 된다.

### 3.3 poller와 역할 분리

`poller`는 계속 data-plane readiness만 담당한다.

- `zlink_poller_add_gateway()`
- `zlink_poller_add_spot_pub()`
- `zlink_poller_add_spot_sub()`

monitor는 상태 전이만 담당한다.

- `READY`
- `LOST`
- register/unregister 결과
- peer up/down
- providers changed
- queue/backpressure 상태

즉 setup 단계에서 "ready를 기다린다"는 것은
`monitor callback`으로 하고,
runtime 단계에서 data plane I/O readiness를 본다는 것은
계속 `poller`로 한다.

## 4. 공통 API 초안

### 4.1 기본 형태

메인 스펙과 정렬된 public shape는 다음이다.

```c
typedef void (*zlink_service_monitor_handler_fn) (
    const zlink_service_event_t *event);

void *zlink_gateway_monitor_open(void *gateway,
                                 int events,
                                 zlink_service_monitor_handler_fn handler);

void *zlink_spot_monitor_open(void *spot,
                              int role,
                              int events,
                              zlink_service_monitor_handler_fn handler);

int zlink_service_monitor_set_handler(void *monitor,
                                      zlink_service_monitor_handler_fn handler);

int zlink_service_monitor_close(void **monitor_p);
```

`Discovery`도 같은 패턴으로 정렬한다.

```c
void *zlink_discovery_monitor_open(void *discovery,
                                   int events,
                                   zlink_service_monitor_handler_fn handler);
```

### 4.2 event struct

event payload는 compact struct를 유지한다.

```c
typedef struct zlink_service_event_t
{
    uint16_t service_kind;
    uint16_t event_type;
    int32_t status;
    int32_t error_code;
    uint32_t value;
    uint32_t detail_flags;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
} zlink_service_event_t;
```

설명:

- `service_kind`: discovery/gateway/spot
- `event_type`: 실제 event 식별자
- `status`: register result 같은 상태 코드
- `error_code`: 실패 시 errno 성격 코드
- `value`: connection count, provider count 같은 단일 수치
- `detail_flags`: `service_name`, `endpoint`, `routing_id` 유효 여부

## 5. 서비스별 이벤트

### 5.1 Discovery

필요 이벤트:

- `DISCOVERY_SERVICE_UP`
- `DISCOVERY_SERVICE_DOWN`
- `DISCOVERY_PROVIDERS_CHANGED`

효과:

- `service_available()` polling loop를 줄일 수 있다.
- setup 단계에서 특정 service 등장 시점을 event-driven으로 처리할 수 있다.

### 5.2 Gateway

필요 이벤트:

- `GATEWAY_READY`
- `GATEWAY_LOST`
- `GATEWAY_REGISTER_OK`
- `GATEWAY_REGISTER_FAILED`
- `GATEWAY_UNREGISTER_OK`
- `GATEWAY_UNREGISTER_FAILED`
- `GATEWAY_CONNECTION_COUNT_CHANGED`
- `GATEWAY_ROUTE_UP`
- `GATEWAY_ROUTE_DOWN`

효과:

- 과거 `receiver_register_result()` polling을 unified `gateway` monitor event로 흡수한다.
- route/connection 변화와 registration 결과를 같은 facade 기준으로 설명할 수 있다.

### 5.3 SpotSub / SpotPub / `spot` facade

`SpotNode` 자체는 monitor target이 아니다.
SPOT monitor는 facade subject에 붙는다.

필요 이벤트:

- `SPOT_SUB_PEER_UP`
- `SPOT_SUB_PEER_DOWN`
- `SPOT_SUB_FILTER_APPLIED`
- `SPOT_PUB_PEER_UP`
- `SPOT_PUB_PEER_DOWN`

선택 이벤트:

- `SPOT_SUB_SUBSCRIPTION_READY`
- `SPOT_PUB_QUEUE_FULL`
- `SPOT_PUB_QUEUE_DRAINED`

주의:

- `SUBSCRIPTION_READY`는 protocol 확장이 필요한 강한 의미 이벤트다.
- `PEER_UP`과 `FILTER_APPLIED`만으로는
  "다음 publish가 절대 드롭되지 않는다"를 보장하지 않는다.

## 6. 사용 예시

### 6.1 Discovery

```c
static void on_discovery_event(const zlink_service_event_t *ev)
{
    if (ev->event_type == ZLINK_DISCOVERY_SERVICE_UP
        && strcmp(ev->service_name, "svc-a") == 0) {
        /* setup gate open */
    }
}

void *discovery = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, registry_router);

void *mon = zlink_discovery_monitor_open(
    discovery,
    ZLINK_DISCOVERY_SERVICE_UP | ZLINK_DISCOVERY_PROVIDERS_CHANGED,
    on_discovery_event);
```

### 6.2 Gateway

```c
static void on_gateway_event(const zlink_service_event_t *ev)
{
    if (ev->event_type == ZLINK_GATEWAY_REGISTER_FAILED) {
        /* inspect ev->status / ev->error_code */
    }
}

void *gw = zlink_gateway_new(ctx, discovery, "gw-1", on_gateway_msg);
zlink_gateway_register(gw, "svc-a", advertise_ep, 1);

void *mon = zlink_gateway_monitor_open(
    gw,
    ZLINK_GATEWAY_REGISTER_OK
    | ZLINK_GATEWAY_REGISTER_FAILED
    | ZLINK_GATEWAY_ROUTE_UP,
    on_gateway_event);
```

### 6.3 Spot

```c
static void on_spot_event(const zlink_service_event_t *ev)
{
    if (ev->event_type == ZLINK_SPOT_SUB_FILTER_APPLIED) {
        /* local subscription propagated */
    }
}

void *spot = zlink_spot_new(node, ZLINK_SPOT_ROLE_SUB, on_spot_msg);
zlink_spot_subscribe(spot, "bench");

void *mon = zlink_spot_monitor_open(
    spot,
    ZLINK_SPOT_ROLE_SUB,
    ZLINK_SPOT_SUB_PEER_UP | ZLINK_SPOT_SUB_FILTER_APPLIED,
    on_spot_event);
```

## 7. 구현 우선순위

### Phase 0: 공통 primitive

작업:

- `zlink_service_event_t` 정리
- `*_monitor_open(..., handler)` 도입
- `zlink_service_monitor_set_handler()` 도입
- `zlink_service_monitor_close()` 도입

완료 기준:

- `Gateway ROUTE_UP` 1개를 end-to-end callback으로 전달 가능
- open 시 handler 등록과 생성 후 handler 교체가 모두 동작

### Phase 1: Discovery + Gateway

작업:

- discovery service up/down
- gateway ready/lost
- gateway register/unregister result
- gateway route/connection count 변화

완료 기준:

- setup code에서 discovery/gateway polling helper를 문서상 제거 가능

### Phase 2: SpotPub / SpotSub

작업:

- `PEER_UP`, `PEER_DOWN`, `FILTER_APPLIED`
- 필요 시 `QUEUE_FULL`, `QUEUE_DRAINED`

완료 기준:

- SPOT setup code에서 peer polling helper를 줄일 수 있음

### Phase 3: 강한 readiness

작업:

- `SUBSCRIPTION_READY` protocol 정의
- compatibility / test matrix 확정

완료 기준:

- setup handshake 없이도 strong subscription-ready 계약을 설명 가능

## 8. 테스트 계획

핵심 검증:

- discovery service up/down callback
- gateway register ok/failed callback
- gateway route up/down callback
- spot peer up/down callback
- spot filter applied callback
- 동일 instance 내 ordering 보장
- handler 교체 후 in-flight callback과 subsequent callback 분리 보장
- destroy/close 이후 추가 callback 부재 보장

중요한 제외:

- `monitor_recv()` drain loop 테스트
- pollable monitor handle 테스트
- `receiver` monitor 테스트

## 9. 연관 문서

- 메인 스펙:
  [`direct-callback-recv-rewrite-spec.ko.md`](./direct-callback-recv-rewrite-spec.ko.md)
- option surface:
  [`service-option-surface-plan.ko.md`](./service-option-surface-plan.ko.md)
- RID 정책:
  [`service-routing-id-policy-plan.ko.md`](./service-routing-id-policy-plan.ko.md)
- registry topology:
  [`registry-topology-introspection-plan.ko.md`](./registry-topology-introspection-plan.ko.md)

## 10. Definition of Done

- `Discovery`, `Gateway`, `spot` facade monitor API가 callback-only surface로 존재한다.
- `receiver`와 pollable monitor 전제가 문서/코드에서 제거되어 있다.
- bindings facade가 동일한 event callback model을 사용한다.
- `SpotNode`는 public monitor 대상이 아님이 문서에 명시되어 있다.
- guide sample이 raw socket이나 polling loop 없이 readiness/state를 설명할 수 있다.
