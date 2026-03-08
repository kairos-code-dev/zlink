# Service Monitor / Readiness 구현 계획

## 1. 목적

이 문서는 service facade 기준의 `event/monitor` API를 도입하기 위한
구현 계획을 정의한다.

핵심 목표는 다음 두 가지다.

- 사용자가 raw internal socket을 직접 꺼내지 않고도
  service의 상태 전이(topology/readiness/registration)를 관찰할 수 있게 한다.
- `poller`와 `monitor`의 역할을 분리해 API 의미를 단순하게 유지한다.

이 계획의 전제는 다음과 같다.

- `SpotNode`는 public monitor 대상이 아니다.
- `SpotNode`는 연결, registry/discovery, TLS 같은 공통 wiring owner로만 남는다.
- public monitor 대상은 facade/service handle이다.
- `recv/send ready`는 기존 `poller`가 담당한다.
- `state/topology/readiness`는 새 `monitor`가 담당한다.
- service identity는 `service-routing-id-policy-plan.ko.md`의
  representative RID 정책을 따른다.
- registry topology에서도 같은 public subject를 사용한다.
  즉 `SpotPub`와 `SpotSub`는 monitor와 registry 양쪽에서 별도 subject다.

즉, 목표 상태는 다음 한 줄로 요약된다.

`poller = data readiness`, `monitor = state transition`

### 1.1 사용 목적

이 기능은 단순히 “event API를 하나 더 만든다”가 목적이 아니다.
실제 사용 목적은 다음과 같다.

- service 준비 완료를 polling loop 없이 기다리기
- topology 변화(peer up/down, service up/down)를 이벤트로 받기
- register/unregister 결과를 polling 없이 받기
- data plane은 그대로 `poller`로 처리하고,
  control/state plane만 `monitor`로 분리하기
- raw internal socket 노출 없이도 setup/readiness를 설명할 수 있게 하기

### 1.2 대표 사용자 시나리오

대표 시나리오는 다음과 같다.

- Discovery를 붙여 놓고,
  특정 service가 실제로 올라오는 시점만 기다리고 싶다.
- Gateway를 만든 뒤
  어떤 service가 ready인지 event-driven으로 알고 싶다.
- Receiver가 register를 요청한 뒤
  성공/실패를 callback polling이 아니라 event로 받고 싶다.
- SpotSub가 peer를 발견했고 local filter를 적용했는지 알고 싶다.
- poller loop는 그대로 유지하면서,
  readiness/topology만 별도 제어 흐름으로 다루고 싶다.

## 2. 왜 필요한가

현재 public API에는 일부 상태 조회 API는 있지만,
대부분 polling 기반 조회에 머물러 있다.

예:

- Discovery:
  `zlink_discovery_receiver_count`,
  `zlink_discovery_service_available`
- Gateway:
  `zlink_gateway_connection_count`
- Receiver:
  `zlink_receiver_register_result`
- SPOT:
  `zlink_spot_node_pub_peers`,
  `zlink_spot_node_sub_peers`

이 구조의 문제는 다음과 같다.

- 사용자가 setup/readiness를 위해 반복 polling 루프를 작성해야 한다.
- 어떤 상태를 “준비 완료”로 봐야 하는지 service마다 해석이 다르다.
- facade를 쓰는 사용자가 internal owner 구조를 알게 된다.
- data plane readiness와 topology/state readiness가 뒤섞인다.

### 2.1 기대 효과

사용자 관점 기대 효과:

- setup 코드가 단순해진다.
- `while (count == 0)` 같은 polling loop가 줄어든다.
- raw socket이나 owner 객체를 직접 이해할 필요가 줄어든다.
- 같은 패턴을 Discovery/Gateway/Receiver/SPOT에 일관되게 적용할 수 있다.

바인딩 관점 기대 효과:

- Java/.NET/Node/Python에서 readiness helper를 각자 따로 만들 필요가 줄어든다.
- bindings별 polling helper 구현이 줄고 공통 계약으로 수렴할 수 있다.
- 문서/예제가 언어별로 덜 갈라진다.

문서 관점 기대 효과:

- `poller`와 `monitor`를 역할로 분리해 설명할 수 있다.
- “왜 setup handshake가 필요한가”를 service별 event 의미로 설명할 수 있다.
- raw internal socket 노출을 readiness 해결책으로 안내하지 않아도 된다.

perf/test 관점 기대 효과:

- benchmark와 test setup에서 반복 polling helper를 줄일 수 있다.
- readiness를 event로 검증할 수 있어 회귀 테스트가 더 명확해진다.
- registry summary가 있더라도, perf의 final strict start gate는 local monitor가 맡는다는
  역할 분리를 명확히 할 수 있다.

## 3. 범위와 비범위

### 3.1 범위

대상 service:

- `Discovery`
- `Gateway`
- `Receiver`
- `SpotSub`
- `SpotPub`

문서/바인딩 영향 범위:

- `core/include/zlink.h`
- `core/src/api/zlink.cpp`
- `core/src/services/discovery/*`
- `core/src/services/gateway/*`
- `core/src/services/spot/*`
- bindings(Java/.NET/Node/Python/C++) facade surface
- `doc/api/*`
- `doc/guide/*`

### 3.2 비범위

- raw socket monitor를 public facade 대체재로 만드는 것
- `SpotNode`를 public monitor target으로 승격하는 것
- `recv ready`, `send ready`를 monitor로 다시 노출하는 것
- benchmark/perf만을 위한 ad-hoc readiness helper 추가

## 4. 설계 결정

### 4.1 SpotNode는 제외

`SpotNode`는 lifecycle/config/runtime owner이지,
사용자가 직접 send/recv 의미를 소비하는 facade가 아니다.

따라서 public monitor는 다음처럼 둔다.

- `zlink_discovery_*_monitor_*`
- `zlink_gateway_monitor_*`
- `zlink_receiver_monitor_*`
- `zlink_spot_sub_monitor_*`
- `zlink_spot_pub_monitor_*`

두지 않는다:

- `zlink_spot_node_monitor_*`

### 4.2 poller와 monitor 역할 분리

유지:

- `zlink_poller_add_spot_sub`
- `zlink_poller_add_spot_pub`
- `zlink_poller_add_gateway`
- `zlink_poller_add_receiver`

추가:

- service별 `monitor_open`
- service별 `monitor_recv`
- `zlink_service_monitor_close`
- `zlink_poller_add_monitor`

원칙:

- poller는 지금처럼 `POLLIN/POLLOUT`만 다룬다.
- monitor는 state event만 다룬다.
- 같은 service에서 둘을 함께 써도 의미가 충돌하면 안 된다.
- monitor handle은 pollable해야 한다.
- setup 단계에서는 `monitor_recv(..., 0)` blocking wait를 쓸 수 있고,
  runtime 단계에서는 같은 poller loop 안에 monitor를 함께 등록할 수 있어야 한다.
- registry summary가 있어도, strict readiness가 필요한 setup/perf에서는
  monitor가 최종 gate다.

### 4.3 facade 대상만 제공

사용자는 다음처럼 이해하면 된다.

- 메시지를 읽거나 쓰고 싶다: facade + poller
- 서비스가 준비됐는지, peer가 붙었는지, 등록이 끝났는지 알고 싶다:
  facade + monitor

internal owner/socket 개념은 public 문서에서 숨긴다.
service identity는 representative RID로 설명한다.
registry topology도 같은 service subject 기준으로 연결된다.

## 5. 공통 API 제안

### 5.1 기본 형태

공통 형태는 다음으로 통일한다.

```c
void *zlink_gateway_monitor_open(void *gateway, int events);
void *zlink_receiver_monitor_open(void *receiver, int events);
void *zlink_discovery_monitor_open(void *discovery, int events);
void *zlink_spot_sub_monitor_open(void *sub, int events);
void *zlink_spot_pub_monitor_open(void *pub, int events);

int zlink_service_monitor_recv(void *monitor,
                               zlink_service_event_t *event,
                               int flags);

int zlink_service_monitor_close(void **monitor_p);

int zlink_poller_add_monitor(void *poller,
                             void *monitor,
                             void *user_data,
                             short events);
```

공통 recv 함수를 둘지, service별 recv를 둘지는 구현 단계에서 선택한다.
권장안은 공통 recv 하나다.

이유:

- bindings 구현이 단순해진다.
- event struct와 event mask를 공통화하기 쉽다.
- 서비스별로 open만 다르고 recv path는 동일하게 유지할 수 있다.
- setup용 blocking wait와 runtime용 poller 통합을 같은 handle로 처리할 수 있다.

### 5.2 event struct

초안:

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

- `service_kind`: discovery/gateway/receiver/spot_sub/spot_pub
- `event_type`: 실제 event 식별자
- `status`: register result 같은 상태 코드
- `error_code`: 실패 시 errno 성격 코드
- `value`: connection count, provider count, queue depth 같은 단일 수치
- `detail_flags`: `service_name`, `endpoint`, `routing_id` 유효 여부
- `service_name`: service-scoped event용
- `endpoint`: 변경된 peer/provider endpoint
- `routing_id`: event subject의 representative RID 또는 peer RID

권장 `detail_flags` 예:

- `ZLINK_EVENT_DETAIL_SERVICE_NAME`
- `ZLINK_EVENT_DETAIL_ENDPOINT`
- `ZLINK_EVENT_DETAIL_SUBJECT_RID`
- `ZLINK_EVENT_DETAIL_PEER_RID`

RID 정책:

- service-facing identity는 `service-routing-id-policy-plan.ko.md`를 따른다.
- service-level event에서는 representative RID를 우선 사용한다.
- peer-specific event에서는 peer RID가 payload에 들어갈 수 있다.
- `routing_id` 필드는 `SUBJECT_RID` 또는 `PEER_RID` flag와 함께 해석한다.
- 두 flag가 모두 없으면 `routing_id`는 유효하지 않은 것으로 본다.

추가 메모:

- `count_before/count_after` 둘 다 두기보다
  대부분의 소비자가 실제로 쓰는 `현재 값` 하나만 두는 쪽이 낫다.
- event struct 크기는 실제 구현 spike에서 다시 확인한다.
- topology event 빈도가 낮아 inline 문자열이 허용 가능하면 flat struct를 유지하고,
  복사 비용이 문제면 detail accessor 분리를 후속 검토한다.

### 5.3 event mask

공통 mask와 service별 확장을 분리한다.

공통:

- `ZLINK_MONITOR_EVENT_READY`
- `ZLINK_MONITOR_EVENT_LOST`
- `ZLINK_MONITOR_EVENT_PEER_UP`
- `ZLINK_MONITOR_EVENT_PEER_DOWN`
- `ZLINK_MONITOR_EVENT_ERROR`

service별 확장:

- discovery:
  `SERVICE_UP`, `SERVICE_DOWN`, `PROVIDERS_CHANGED`
- gateway:
  `SERVICE_READY`, `SERVICE_LOST`, `CONNECTION_COUNT_CHANGED`,
  `ROUTE_UP`, `ROUTE_DOWN`
- receiver:
  `REGISTER_OK`, `REGISTER_FAILED`, `UNREGISTER_OK`, `UNREGISTER_FAILED`
- spot_sub:
  `FILTER_APPLIED`, `SUBSCRIPTION_READY`
- spot_pub:
  `QUEUE_FULL`, `QUEUE_DRAINED`

### 5.4 사용 예시

이 API는 `poller`를 대체하지 않는다.
전형적인 사용 흐름은 다음과 같다.

- monitor로 service state를 본다.
- poller로 data readiness를 본다.
- 실제 send/recv는 facade API로 한다.

#### 예시 1: Discovery에서 service up 대기

```c
void *discovery = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, registry_pub);
zlink_discovery_subscribe(discovery, "svc-a");

void *mon = zlink_discovery_monitor_open(
    discovery,
    ZLINK_MONITOR_EVENT_READY | ZLINK_DISCOVERY_PROVIDERS_CHANGED);

for (;;) {
    zlink_service_event_t ev;
    if (zlink_service_monitor_recv(mon, &ev, 0) != 0)
        break;

    if (ev.event_type == ZLINK_DISCOVERY_SERVICE_UP
        && strcmp(ev.service_name, "svc-a") == 0) {
        break;
    }
}
```

핵심:

- service 등장 여부는 monitor로 기다린다.
- `receiver_count()` polling loop를 직접 쓰지 않는다.

#### 예시 2: Gateway runtime에서 monitor와 poller를 같은 loop에 통합

```c
void *gw = zlink_gateway_new(ctx, discovery, "gw-1");
void *mon = zlink_gateway_monitor_open(
    gw,
    ZLINK_MONITOR_EVENT_READY
    | ZLINK_MONITOR_EVENT_LOST
    | ZLINK_GATEWAY_CONNECTION_COUNT_CHANGED);
void *poller = zlink_poller_new();

zlink_poller_add_gateway(poller, gw, NULL, ZLINK_POLLIN);
zlink_poller_add_monitor(poller, mon, (void *) "gateway-monitor", ZLINK_POLLIN);

for (;;) {
    zlink_poller_event_t pe;
    if (zlink_poller_wait(poller, &pe, -1) <= 0)
        continue;

    if (pe.user_data == (void *) "gateway-monitor") {
        zlink_service_event_t ev;
        if (zlink_service_monitor_recv(mon, &ev, ZLINK_DONTWAIT) == 0) {
            if (ev.event_type == ZLINK_GATEWAY_SERVICE_READY) {
                /* update local service state */
            } else if (ev.event_type == ZLINK_GATEWAY_CONNECTION_COUNT_CHANGED) {
                /* ev.value = current connection count */
            }
        }
        continue;
    }

    zlink_msg_t *parts = NULL;
    size_t count = 0;
    char service_name[256] = "";
    if (zlink_gateway_recv(gw, &parts, &count, ZLINK_DONTWAIT, service_name) == 0) {
        /* handle request/reply */
        zlink_multipart_close(parts, count);
    }
}
```

핵심:

- monitor와 data socket을 같은 event loop에서 처리할 수 있어야 한다.
- `SERVICE_READY`와 `CONNECTION_COUNT_CHANGED`는 monitor가 담당한다.
- `POLLIN`은 poller가 담당한다.
- data receive는 계속 `zlink_gateway_recv()`를 사용

#### 예시 3: Receiver register 결과를 event로 소비

```c
void *rx = zlink_receiver_new(ctx, "rx-1");
zlink_receiver_connect_registry(rx, registry_router);
zlink_receiver_register(rx, "svc-a", advertise_ep, 1);

void *mon = zlink_receiver_monitor_open(
    rx,
    ZLINK_MONITOR_EVENT_READY | ZLINK_MONITOR_EVENT_ERROR);

for (;;) {
    zlink_service_event_t ev;
    if (zlink_service_monitor_recv(mon, &ev, 0) != 0)
        break;

    if (ev.event_type == ZLINK_RECEIVER_REGISTER_OK
        && strcmp(ev.service_name, "svc-a") == 0) {
        break;
    }
    if (ev.event_type == ZLINK_RECEIVER_REGISTER_FAILED) {
        /* inspect ev.status / ev.error_code */
        break;
    }
}
```

핵심:

- `register_result()` polling을 외부에서 직접 반복하지 않아도 된다.

#### 예시 4: SpotSub의 현실적인 사용 방식

Phase 1에서는 strong ready를 약속하지 않는다.
따라서 사용 예도 두 단계로 본다.

```c
void *sub = zlink_spot_sub_new(node);
zlink_spot_sub_subscribe(sub, "bench");

void *mon = zlink_spot_sub_monitor_open(
    sub,
    ZLINK_MONITOR_EVENT_PEER_UP | ZLINK_SPOT_SUB_FILTER_APPLIED);

bool peer_up = false;
bool filter_applied = false;

while (!peer_up || !filter_applied) {
    zlink_service_event_t ev;
    if (zlink_service_monitor_recv(mon, &ev, 0) != 0)
        break;

    if (ev.event_type == ZLINK_SPOT_SUB_PEER_UP)
        peer_up = true;
    if (ev.event_type == ZLINK_SPOT_SUB_FILTER_APPLIED)
        filter_applied = true;
}
```

해석:

- Phase 1 의미:
  “peer가 있고, local subscribe가 적용됐다”
- Phase 4 의미:
  여기에 protocol-level `SUBSCRIPTION_READY`까지 추가해
  “다음 publish가 드롭되지 않는다”로 강화

즉 SpotSub는 처음부터 강한 ready를 약속하지 않고,
문서와 API 의미를 단계적으로 강화해야 한다.

### 5.5 사용성 검토 결과와 현재 결정

monitor 자체는 필요하지만, 현재 단계에서 convenience API를 같이 많이 넣는 것은
오히려 public surface를 과도하게 키울 가능성이 크다.

현재 결정은 다음과 같다.

- 1차 core C API에는 low-level monitor primitive를 넣는다.
- 단, `monitor_close`와 `poller_add_monitor`는 primitive의 일부로 본다.
- `wait_*`, `snapshot_*`, `event_name`, default mask helper 같은 convenience API는
  1차 범위에서 제외한다.
- 일반 사용성 보강은 먼저 문서/바인딩 레벨 wrapper로 검증한다.
- 반복 패턴이 실제로 충분히 확인되면 그때 core API 추가를 재검토한다.

즉, 지금 단계의 기준은 다음이다.

```text
core C API:
  monitor_open + monitor_recv + monitor_close
  + poller_add_monitor + event struct + event mask

후속 후보:
  wait_* / snapshot_* / helper 계층
```

이 결정을 택하는 이유는 다음과 같다.

- capability를 여는 핵심 API와 편의성 API를 분리할 수 있다.
- 처음부터 helper를 많이 넣으면 API 표면적이 빠르게 커진다.
- helper는 bindings나 userland wrapper로도 충분히 실험 가능하다.
- monitor primitive만 잘 설계되면 대부분의 helper는 나중에도 위에 얹을 수 있다.

### 5.6 현재 범위에서 제외하는 후보 API

다음 API들은 “나쁘다”가 아니라,
지금 당장 core C API에 넣지는 않는다는 의미다.

후속 후보:

- `zlink_service_event_name()`
- `zlink_service_kind_name()`
- service별 default monitor mask 상수
- `zlink_discovery_wait_service_up()`
- `zlink_gateway_wait_service_ready()`
- `zlink_gateway_wait_service_lost()`
- `zlink_receiver_wait_register()`
- `zlink_spot_sub_wait_peer()`
- `zlink_spot_sub_wait_filter_applied()`
- `zlink_spot_pub_wait_peer()`
- `zlink_spot_sub_wait_subscription_ready()`
- `*_state_snapshot()`

### 5.7 사용성 관점 최종 권장 surface

현재 문서 기준의 최종 권장 surface는 다음이다.

```text
setup 단계 상태 대기:
  monitor_open() + monitor_recv()

runtime 단계 상태 처리:
  poller_add_monitor() + monitor_recv(DONTWAIT)

데이터 수신/송신:
  poller + facade recv/send
```

즉, 1차 core API는 작게 유지한다.

문서/예제/바인딩에서는 필요하면 다음 형태의 wrapper를 둘 수 있다.

- bindings helper
- guide sample helper
- perf/test 내부 helper

하지만 그 helper를 바로 core C API로 올리지는 않는다.

### 5.8 서비스별 옵션 surface 정리

option surface 정리는 monitor/readiness 설계와 직접 연결되지만,
검토 범위가 너무 넓어지지 않도록 별도 문서로 분리한다.

별도 문서:

- `doc/plan/service-option-surface-plan.ko.md`

이 문서에서는 다음 원칙만 유지한다.

- monitor/readiness public surface는 facade 기준으로 설계한다.
- option surface 역시 internal socket role이 아니라 facade/service 기준으로 정리한다.
- 전역 aggregate 조회는 `registry-topology-introspection-plan.ko.md`에서 별도로 다룬다.
- representative RID 정책은 `service-routing-id-policy-plan.ko.md`에서 별도로 다룬다.

## 6. 서비스별 필요 이벤트 목록

### 6.1 Discovery

현재 상태:

- public 조회는 `get_receivers`, `receiver_count`, `service_available` 중심이다.

필요 이벤트:

- `DISCOVERY_SERVICE_UP`
- `DISCOVERY_SERVICE_DOWN`
- `DISCOVERY_PROVIDERS_CHANGED`

선택 이벤트:

- `DISCOVERY_PROVIDER_ADDED`
- `DISCOVERY_PROVIDER_REMOVED`

효과:

- 사용자는 receiver count polling 루프를 작성하지 않아도 된다.
- Gateway/Spot setup을 bindings에서 더 자연스럽게 감쌀 수 있다.

구현 포인트:

- `discovery_t::notify_observers()` 직전/직후 diff를 계산해 event를 생성한다.
- 내부 `_service_seq`와 provider snapshot을 이용하면 된다.

### 6.2 Gateway

현재 상태:

- public 조회는 `connection_count`, `router_peers` 정도다.
- 내부적으로는 이미 ROUTER socket monitor를 사용해 ready/down을 추적한다.

필요 이벤트:

- `GATEWAY_SERVICE_READY`
- `GATEWAY_SERVICE_LOST`
- `GATEWAY_CONNECTION_COUNT_CHANGED`
- `GATEWAY_ROUTE_UP`
- `GATEWAY_ROUTE_DOWN`

선택 이벤트:

- `GATEWAY_LB_POOL_CHANGED`
- `GATEWAY_ROUTE_ERROR`

효과:

- setup에서 `connection_count()` polling을 없앨 수 있다.
- 사용자 코드가 service availability 변화를 event-driven으로 처리할 수 있다.

구현 포인트:

- `gateway_t::process_monitor_events()`에서 route ready/down 전이를 event로 승격
- `refresh_pool()` 또는 `connection_count()` 경로에서 pool count 변화 event 생성
- event 폭주를 막기 위해 count-changed는 coalescing 허용

### 6.3 Receiver

현재 상태:

- 등록 완료는 `zlink_receiver_register_result()` polling으로 확인한다.
- public으로는 peer topology event가 없다.

필요 이벤트:

- `RECEIVER_REGISTER_OK`
- `RECEIVER_REGISTER_FAILED`
- `RECEIVER_UNREGISTER_OK`
- `RECEIVER_UNREGISTER_FAILED`
- `RECEIVER_ROUTER_PEER_UP`
- `RECEIVER_ROUTER_PEER_DOWN`

효과:

- service register confirmation polling을 없앨 수 있다.
- gateway/client가 실제로 붙는 시점을 더 분명하게 관찰할 수 있다.

구현 포인트:

- registry 응답 처리 지점에서 register/unregister result event 생성
- ROUTER peer 변화는 socket peer snapshot diff 또는 socket monitor 재사용 검토

주의:

- `request ready`는 monitor 대상이 아니다.
- 이건 기존 poller가 이미 담당한다.

### 6.4 SpotSub

현재 상태:

- public facade에는 `subscribe`, `unsubscribe`, `recv`, `set_handler`만 있다.
- 외부에서 “지금 바로 publish하면 첫 메시지가 안 버려지는가”를 확정할 API가 없다.

필요 이벤트:

- `SPOT_SUB_PEER_UP`
- `SPOT_SUB_PEER_DOWN`
- `SPOT_SUB_FILTER_APPLIED`

강한 의미의 선택 이벤트:

- `SPOT_SUB_SUBSCRIPTION_READY`

중요한 결정:

- `PEER_UP`과 `FILTER_APPLIED`만으로는
  “다음 publish가 절대 드롭되지 않는다”를 보장할 수 없다.
- 이 강한 의미의 `SUBSCRIPTION_READY`는 protocol 확장 없이는 완전하게 만들 수 없다.

즉, SpotSub는 2단계로 나눈다.

- Phase 1:
  `PEER_UP`, `PEER_DOWN`, `FILTER_APPLIED`
- Phase 2:
  protocol-level ack 기반 `SUBSCRIPTION_READY`

Phase 2 구현 후보:

- dealer control plane을 이용한 subscription generation sync
- local subscribe 적용 후 peer ack를 수집하는 방식

이 단계가 있어야 setup handshake를 완전히 제거할 수 있다.

### 6.5 SpotPub

현재 상태:

- publish는 가능하지만 peer/topology/backpressure 상태를 facade에서 직접 받지 못한다.

필요 이벤트:

- `SPOT_PUB_PEER_UP`
- `SPOT_PUB_PEER_DOWN`

선택 이벤트:

- `SPOT_PUB_QUEUE_FULL`
- `SPOT_PUB_QUEUE_DRAINED`
- `SPOT_PUB_DROPPING`

효과:

- async pub mode에서 queue/backpressure를 event-driven으로 다룰 수 있다.
- perf나 application이 queue 상태를 stats polling 없이 볼 수 있다.

주의:

- `publish ready` 자체는 poller 대상이 아니고,
  facade publish는 monitor보다 return code가 우선이다.

## 7. 구현 우선순위

### Phase 0: 공통 기반

작업:

- `zlink_service_event_t`와 공통 event recv path 도입
- service monitor handle lifecycle 정의
- pollable monitor handle 구현 매체 결정
- `zlink_service_monitor_close`
- `zlink_poller_add_monitor`

권장 구현:

- poller와 자연스럽게 통합될 수 있는 pollable handle
- 1차 우선안은 inproc PAIR 또는 이에 준하는 pollable transport
- event 생성은 service control thread 또는 state transition 지점에서만 수행

완료 기준:

- `Gateway ROUTE_UP` 1개를 end-to-end로 recv 가능
- 같은 monitor handle을 blocking recv와 poller 등록 양쪽으로 검증 가능
- representative RID getter public API가 후속 phase에 있더라도,
  Phase 0 event payload에는 내부적으로 생성된 representative RID를 넣을 수 있어야 한다.

### Phase 1: Discovery + Gateway

이유:

- topology/state readiness 효과가 가장 크다.
- 현재 내부 정보가 이미 있어 구현 리스크가 낮다.

작업:

- `discovery_monitor_open`
- `gateway_monitor_open`
- `zlink_service_monitor_close`
- `zlink_poller_add_monitor`
- service up/down, providers changed, route up/down, connection count changed 구현
- bindings facade에 read-only event API 추가

완료 기준:

- setup code에서 discovery/gateway polling helper를 문서상 제거 가능

### Phase 2: Receiver

작업:

- register/unregister result를 event로 노출
- router peer up/down event 추가

완료 기준:

- `receiver_register_result` polling 없이 등록 완료 흐름 구성 가능

### Phase 3: SpotPub / SpotSub 기본 이벤트

작업:

- SpotSub:
  `PEER_UP`, `PEER_DOWN`, `FILTER_APPLIED`
- SpotPub:
  `PEER_UP`, `PEER_DOWN`
- async pub mode 사용 시 queue full/drained event 추가

완료 기준:

- SPOT setup code에서 peer polling 일부 제거 가능

### Phase 4: SpotSub 강한 readiness

작업:

- subscription generation / ack protocol 설계
- `SUBSCRIPTION_READY`의 정확한 의미 정의
- protocol 호환성 검토
- test matrix 추가

완료 기준:

- “첫 publish 드롭 없음”을 setup handshake 없이 보장 가능

### 7.5 문서 간 Phase 의존관계

이 문서는 다른 plan 문서와 다음 순서로 맞물린다.

- RID Phase 0 -> Monitor Phase 0
  monitor event struct의 `routing_id` 의미와 자동 생성 계약은
  RID 공통 기반이 먼저 고정되어야 한다.
- Monitor Phase 0/1 -> Registry Phase 1/2
  registry summary report가 어떤 상태 전이를 의미하는지는
  monitor event semantics가 먼저 고정되어야 해석이 흔들리지 않는다.
- RID Phase 1 -> Registry Phase 2
  gateway summary report는 representative `router rid`를 써야 하므로
  Gateway RID contract가 먼저 고정되어야 한다.
- Option surface plan -> RID/monitor subject model 이후
  option 정리는 transport mechanics보다 public subject naming에 더 크게 의존한다.
  따라서 `Gateway/Receiver/SpotPub/SpotSub` subject model이 먼저 고정되어야 한다.

즉 구현 순서의 큰 틀은 다음처럼 보는 것이 안전하다.

```text
RID 공통 기반
-> monitor 공통 primitive
-> Gateway/Receiver monitor + RID 확정
-> registry summary/reporting
-> SpotPub/SpotSub 확장
-> option surface 정리
```

## 8. 내부 구현 방식

### 8.1 event source

service별 event source는 다음을 기본으로 한다.

- Discovery:
  provider snapshot diff
- Gateway:
  internal socket monitor + service pool diff
- Receiver:
  registry result 처리 + router peer diff
- SpotSub/SpotPub:
  peer snapshot diff + control path state transition

### 8.2 event delivery

delivery 기본 원칙:

- event는 best-effort가 아니라 lossless를 기본으로 한다.
- 다만 `COUNT_CHANGED`류는 coalescing 가능하다.
- `READY/LOST`, `REGISTER_OK/FAILED` 같은 경계 이벤트는 coalescing 금지

handle 형태:

- monitor handle은 pollable해야 한다.
- setup 단계에서는 `monitor_recv(..., 0)` blocking wait가 가능해야 한다.
- runtime 단계에서는 `zlink_poller_add_monitor()`로 같은 event loop에 붙일 수 있어야 한다.
- 1차 구현은 poller 통합이 쉬운 pollable transport를 우선 검토한다.

queue backpressure 정책:

- 기본: unbounded 금지
- monitor queue HWM 도입
- HWM 초과 시 기본은 `EAGAIN` 또는 oldest-drop 중 하나를 명시

권장 기본:

- control/lifecycle event는 drop 금지
- count-changed event만 coalescing
- coalescing은 time-based보다는 drain-cycle 또는 batch 단위가 낫다

lifecycle 종료 의미:

- service destroy 시 monitor에는 terminal event 또는 EOF가 전달되어야 한다.
- 이후 `monitor_recv()`는 `-1`과 terminal errno를 반환해야 한다.
- bindings는 이를 dispose/close 완료 신호로 해석할 수 있어야 한다.

### 8.3 ordering

ordering은 service별로 다음을 보장한다.

- 같은 service instance 내 event는 발생 순서대로 recv 가능
- 서로 다른 service instance 간 전역 ordering은 보장하지 않음

예:

- `REGISTER_OK` 뒤에 `PEER_UP`이 오는 식의 local ordering은 보장
- 다른 receiver와 gateway 사이의 cross-object strict ordering은 비보장

## 9. 바인딩 설계

언어별 bindings는 모두 같은 surface를 갖는 쪽이 좋다.

예:

- Java:
  `ServiceMonitor monitor = gateway.openMonitor(mask);`
- .NET:
  `using var monitor = gateway.OpenMonitor(mask);`
- Python:
  `monitor = gateway.open_monitor(mask)`
- Node:
  `const monitor = gateway.openMonitor(mask)`

반환형은 언어별로 달라도 의미는 같아야 한다.

- `recv()` / `recvEvent()` / iterator style 중 하나
- event type enum과 payload field는 공통 유지

허용 가능한 언어별 관용:

- Java:
  iterator, listener, `CompletableFuture` 기반 wrapper 허용
- .NET:
  `IAsyncEnumerable` 또는 event stream wrapper 허용
- Python:
  iterator 또는 async generator wrapper 허용
- Node:
  async iterator 또는 event emitter wrapper 허용

단, core contract는 동일해야 한다.

- open
- recv
- close
- poller 통합 가능

## 10. 문서 계획

추가/수정 대상:

- `doc/api/*.md`
- `doc/guide/*.md`
- service poller 문서
- bindings usage guide

문서 원칙:

- monitor는 readiness/state용이라고 명시
- poller는 data readiness용이라고 명시
- raw socket 접근을 readiness 해결책으로 안내하지 않음

## 11. 테스트 계획

### 11.1 core 테스트

- Discovery
  - service up/down
  - provider add/remove
- Gateway
  - route up/down
  - connection count changed
  - service lost
- Receiver
  - register ok/failed
  - unregister ok/failed
  - router peer up/down
- SpotSub
  - peer up/down
  - filter applied
- SpotPub
  - peer up/down
  - queue full/drained
- poller 통합
  - monitor handle을 poller에 등록 가능
  - data socket과 monitor를 같은 loop에서 함께 수신 가능
  - `Gateway`를 기준으로 `ROUTE_UP` event와 data readiness를 같은 thread에서 처리 가능

### 11.2 순서 테스트

- 동일 instance에서 event ordering 보장 검증
- coalescing event와 non-coalescing event 혼합 검증
- monitor event와 data readiness를 섞어도 단일 loop contract가 깨지지 않는지 검증

### 11.3 bindings 테스트

- 각 언어에서 open/recv/close smoke
- service lifecycle과 함께 event ordering 검증

### 11.4 perf/guide 검증

- bench/setup code가 count polling 대신 monitor를 쓸 수 있는지 검토
- guide sample이 raw socket 없이 readiness를 설명할 수 있는지 검토

## 12. 리스크와 완화

| 리스크 | 설명 | 완화 |
|---|---|---|
| event 폭주 | topology churn 시 event가 과도하게 많아짐 | count-changed를 drain-cycle 단위로 coalescing |
| event payload 크기 | inline detail 필드가 커지면 queue 복사 비용이 커질 수 있음 | `value` 중심 compact struct를 우선하고, 필요 시 detail accessor를 후속 검토 |
| 의미 모호성 | `READY`가 service별로 다르게 해석될 수 있음 | service별 의미를 문서에 고정 |
| SPOT 강한 readiness 부재 | peer up만으로 subscription delivery 보장 불가 | Phase 4 protocol extension 분리 |
| bindings 불일치 | 언어마다 monitor surface가 달라짐 | 공통 event model 강제 |
| lifetime 문제 | service destroy 중 monitor handle 사용 | `monitor_close`, destroy ordering, EOF/terminal event 정의 |
| poller 통합 실패 | monitor가 pollable하지 않으면 별도 제어 loop가 필요 | 1차 구현부터 `zlink_poller_add_monitor` 포함 |

## 13. 명시적 비권장 항목

다음은 구현하지 않는 쪽을 권장한다.

- `SpotNode` public monitor
- service monitor를 raw socket monitor thin wrapper로만 제공
- `recv ready` / `send ready`를 monitor로 중복 제공
- perf 전용 readiness helper를 public API로 추가

## 14. Definition of Done

- `Discovery`, `Gateway`, `Receiver`, `SpotSub`, `SpotPub` monitor API가 core에 존재
- poller와 monitor 역할이 문서/코드에서 충돌하지 않음
- bindings facade에서 동일 개념으로 사용 가능
- `SpotNode`는 public monitor 대상이 아님이 문서에 명시됨
- SPOT strong readiness는 Phase 4 전까지 제한적 의미임이 문서에 명시됨
- guide sample이 raw socket 노출 없이 readiness/state를 설명 가능

## 15. 자기 리뷰 및 수정 포인트

초안 리뷰 결과 반영 사항:

- 사용 목적과 대표 사용자 시나리오를 문서 앞부분에 추가했다.
- 사용자/바인딩/문서/perf 관점 기대 효과를 분리해 명시했다.
- Discovery, Gateway, Receiver, SpotSub 기준의 C API 사용 예시를 추가했다.
- convenience API를 한 번에 많이 넣지 않고,
  1차 core C API는 monitor primitive 중심으로 유지하는 방향으로 수정했다.
- `monitor_close`는 후속 후보가 아니라 1차 primitive에 포함시키도록 수정했다.
- `wait_*`, `snapshot_*`는 현재 범위에서 제외하도록 정리했다.
- `SpotNode`를 monitor 대상에서 명시적으로 제외했다.
- `poller`와 `monitor`의 역할을 명확히 분리했다.
- monitor handle은 pollable해야 하며,
  runtime 단계에서는 poller와 같은 loop에 통합할 수 있어야 한다는 점을 추가했다.
- `Gateway ROUTE_UP` 실제 e2e를 Phase 0 최소 완료 기준으로 구체화했다.
- `count_before/count_after` 대신 단일 `value` 중심으로 event struct를 단순화했다.
- `SpotSub SUBSCRIPTION_READY`는 즉시 제공 가능한 이벤트와
  protocol 확장이 필요한 강한 의미 이벤트를 분리했다.
- `Receiver request ready`처럼 기존 poller로 충분한 영역은
  monitor 대상에서 제외했다.
- `Gateway`는 이미 내부 socket monitor가 있으므로,
  새 public monitor는 이를 facade 의미로 승격하는 방향으로 정리했다.
- service option surface 정리는 별도 문서로 분리했다.
