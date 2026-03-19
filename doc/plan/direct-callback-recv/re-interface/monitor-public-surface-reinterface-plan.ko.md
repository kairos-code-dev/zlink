# Monitor Public Surface Re-Interface Plan

> 상태: 제안.
> 이 문서는 monitor public API를 사용성 중심으로 다시 설계하기 위한
> source of truth다.
> 본 문서는 호환성 유지 의무를 두지 않는다.

## 1. 목적

현재 monitor surface는 내부 구현 관점에서는 상당 부분 공통화되어 있지만,
public API 관점에서는 다음 문제가 남아 있다.

- monitor를 여는 함수 이름이 대상마다 제각각이다.
- event payload는 service 계열에서 이미 상당히 통일돼 있는데,
  호출자가 그 사실을 API shape만 보고 바로 이해하기 어렵다.
- `spot` / `spot_node` monitor는 `role` 인자를 통해 내부 구조를 알아야만
  사용할 수 있다.
- `zlink_service_monitor_handler_fn`과 `zlink_service_event_t`는
  실제 적용 범위에 비해 naming이 좁고, socket monitor와의 관계도 흐리다.

즉 현재 표면은 "무슨 대상인지"보다 "어떤 서브시스템 내부 구조를 아는지"를
사용자에게 더 많이 요구한다.

이 문서의 목표는 monitor API를 다음 한 줄로 설명 가능한 형태로 재구성하는
것이다.

```text
모든 monitor는 같은 방식으로 연다.
이벤트 해석은 socket/service class 하나만 알면 된다.
```

핵심 목표:

- monitor open/snapshot/close 규칙을 하나의 public 패턴으로 통일한다.
- socket-level event와 service-level event를
  명시적으로 두 계층으로 분리한다.
- monitor delivery policy를 기존 recv/callback 전환 규칙과 동일하게 맞춘다.
- `spot`의 내부 pub/sub 구조 지식을 public open contract에서 제거한다.
- naming을 역할에 맞게 다시 정리한다.
- direct callback recv 재정렬 방향과 충돌하지 않게 한다.

추가 제약:

- 새 API는 현재보다 호출자가 외워야 할 규칙 수를 줄여야 한다.
- 하나의 entrypoint를 만든다는 이유로 `void *handler` 같은 약한 타입 계약을
  public에 도입하지 않는다.
- target handle에서 충분히 결정 가능한 정보를 중복 인자로 다시 받지 않는다.


## 2. 설계 판단

### 2.1 무엇을 통합하고 무엇을 분리할 것인가

monitor는 모두 같은 것이 아니다.
하지만 현재 차이는 "완전히 별개 API"일 만큼 크지도 않다.

정리 원칙은 다음과 같다.

- open/snapshot/close lifecycle은 통합한다.
- event delivery 방식은 통합한다.
- event payload class는 두 개만 유지한다.
- socket event와 service event는 분리한다.

즉 사용자가 외워야 할 것은 세 가지뿐이다.

1. socket monitor
2. service monitor
3. recv model에서 시작해서 callback 부착 시 callback-only로 전환된다

이 둘까지 하나의 event struct로 완전히 합치면,
public struct에 socket-specific field와 service-specific field가 같이 섞여
오히려 사용자가 유효 필드 규칙을 더 많이 외워야 한다.
그것은 POSD 기준에서도 깊은 모듈이 아니라 넓은 공용 쓰레기통에 가깝다.


## 3. 현재 surface의 사용성 문제

### 3.1 open 함수가 대상별로 흩어져 있다

현재는 대략 다음 계열이 존재한다.

- `zlink_socket_monitor_open()`
- `zlink_discovery_monitor_open()`
- `zlink_gateway_monitor_open()`
- `zlink_spot_node_monitor_open()`
- `zlink_spot_monitor_open()`

문제는 "monitor를 여는 규칙"이 아니라
"대상별 예외 이름"을 기억하게 만든다는 점이다.

### 3.2 `spot` monitor가 internal topology를 노출한다

`zlink_spot_monitor_open()`과 `zlink_spot_node_monitor_open()`은 `role`을 받는다.
이것은 사용자가 `spot`을 하나의 facade로 보는 대신,
내부에 pub/sub가 따로 있다는 사실을 알아야 함을 뜻한다.

이 방식은 다음 비용을 만든다.

- monitor 대상 선정 전에 내부 구성을 먼저 학습해야 한다.
- 문서가 "무엇을 관찰하고 싶은가"가 아니라
  "어느 내부 측면에 붙을 것인가"를 설명하게 된다.
- facade를 제공하는 이유가 약해진다.

### 3.3 naming이 service 범위를 정확히 표현하지 못한다

`zlink_service_monitor_handler_fn`은
discovery/gateway/spot/spot_node 계열 전체에 쓰인다.
이 문서의 최종 방향은 `service` 분류를 유지하는 것이다.
다만 현재 public surface에서는 그 범위가 충분히 선명하게 드러나지 않는다.

- socket monitor와 병치되는 public 분류라는 점이 약하다.
- discovery/gateway/spot/spot_node facet을 함께 포괄한다는 점이 덜 보인다.
- topology/query/readiness와의 관계를 설명하는 문맥이 더 필요하다.

### 3.4 event mask 타입이 불필요하게 분절돼 있다

현재는 discovery/gateway/spot별 마스크 타입이 따로 있다.
`spot_node`는 별도 open 함수로 드러나지만 event namespace는 사실상
같은 service monitor 문맥에 있다.
이 자체가 잘못은 아니지만,
사용자 입장에서는 "어차피 bitmask인데 왜 타입만 나뉘는가"라는 의문을 만든다.

실제 중요한 것은 타입 구분보다
어떤 class의 event를 받을 수 있는지다.


## 4. 목표 public model

### 4.1 monitor class를 두 개로 고정한다

public monitor class는 다음 둘만 둔다.

- socket monitor
- service monitor

정의:

- socket monitor
  - raw socket family를 대상으로 한다.
  - connection/bind/accept/disconnect/handshake 등 socket-level event를 다룬다.
- service monitor
  - discovery/gateway/spot/spot_node service family를 대상으로 한다.
  - readiness, peer topology, route, provider, subscription state 같은
    service-level event를 다룬다.

이 구분은 사용자가 이해하기 쉽다.

- 소켓 상태를 보고 싶다: socket monitor
- 서비스 동작 상태를 보고 싶다: service monitor

추가 원칙:

- handshake 진행/성공/실패 관찰은 반드시 socket monitor 책임으로 둔다.
- handshake 정보는 service monitor나 snapshot으로 승격해서 대체하지 않는다.
- TLS/ZMTP handshake 진단은 service-level 의미 상태가 아니라
  socket-level 사실로 본다.


## 5. Canonical Public API

### 5.1 공통 lifecycle

모든 monitor는 다음 흐름으로 사용한다.

1. open
2. recv 또는 callback 부착
3. snapshot
4. close

초안 1에서는 `monitor_class + target_kind + void *handler` 조합을 검토했지만,
사용성 관점에서 그 설계는 폐기한다.

이유:

- `target_kind`만 보면 충분한데 `monitor_class`를 다시 적어야 한다.
- handler를 `void *`로 받으면 C API 타입 계약이 약해진다.
- "하나의 open 함수"를 얻는 대신
  호출자가 cast 규칙과 조합 제약을 추가로 외워야 한다.

따라서 canonical public surface는
"공통 진입점 하나"보다 "공통 lifecycle + 얕지 않은 typed entrypoint 두 개"를
우선한다.

새 public canonical 함수는 다음이다.

```c
typedef struct zlink_socket_monitor_open_options_t
{
    zlink_socket_monitor_event_mask_t events;
} zlink_socket_monitor_open_options_t;

typedef struct zlink_service_monitor_open_options_t
{
    zlink_service_monitor_event_mask_t events;
} zlink_service_monitor_open_options_t;

ZLINK_EXPORT void *zlink_socket_monitor_open (
  void *socket_,
  const zlink_socket_monitor_open_options_t *options_);

ZLINK_EXPORT void *zlink_service_monitor_open (
  void *target_,
  const zlink_service_monitor_open_options_t *options_);

ZLINK_EXPORT int zlink_socket_monitor_handler (
  void *monitor_,
  zlink_socket_monitor_handler_fn handler_,
  void *userdata_);

ZLINK_EXPORT int zlink_service_monitor_handler (
  void *monitor_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_);

ZLINK_EXPORT int zlink_socket_monitor_recv (
  void *monitor_,
  zlink_socket_monitor_event_t *out_);

ZLINK_EXPORT int zlink_service_monitor_recv (
  void *monitor_,
  zlink_service_monitor_event_t *out_);

ZLINK_EXPORT int zlink_monitor_snapshot (
  void *monitor_,
  zlink_monitor_snapshot_t *out_);

ZLINK_EXPORT int zlink_monitor_close (void **monitor_p_);
```

핵심은 사용자가 함수 이름 다섯 개를 외우는 대신,
open은 두 개만 기억하면 된다는 점이다.

- raw socket이면 `zlink_socket_monitor_open()`
- discovery/gateway/spot/spot_node면 `zlink_service_monitor_open()`
- open 직후 monitor는 기본적으로 recv model이다
- callback을 붙이면 callback-only로 단방향 전환된다

### 5.2 open 규칙

`zlink_socket_monitor_open()` contract:

- 대상은 raw socket family다.
- 기본 delivery model은 recv model이다.
- open 직후 `zlink_socket_monitor_recv()`가 가능하다.
- open 직후 callback은 설치되지 않는다.

`zlink_service_monitor_open()` contract:

- `target_`는 monitor를 걸 대상 instance다.
- 대상 kind는 `target_`의 runtime tag에서 결정한다.
- 허용 대상은 `DISCOVERY`, `GATEWAY`, `SPOT`, `SPOT_NODE`다.
- 기본 delivery model은 recv model이다.
- open 직후 `zlink_service_monitor_recv()`가 가능하다.
- open 직후 callback은 설치되지 않는다.

공통 전환 규칙:

- `zlink_socket_monitor_handler()` 또는 `zlink_service_monitor_handler()`를
  호출하면 monitor는 callback-only model로 단방향 전환된다.
- callback 부착 이후 direct `recv`는 `EBUSY`다.
- callback 제거로 recv model로 되돌아가는 것은 지원하지 않는다.
- `snapshot`은 recv model / callback-only model 모두에서 허용된다.

### 5.3 구현 가능성 기준 보정

구현 단순성을 위해 다음 규칙을 명시한다.

- `service monitor open`은 `target_kind`를 인자로 다시 받지 않는다.
  discovery/gateway/spot/spot_node 구분은 이미 handle tag에서
  결정 가능하기 때문이다.
- monitor handle은 별도 opaque object를 새로 만드는 대신,
  monitor event stream을 읽는 socket-backed handle로 유지한다.
- typed recv API는 thin wrapper여야 한다.
  즉 `zlink_socket_monitor_recv()`와 `zlink_service_monitor_recv()`는
  event decoder만 다르고 handle lifecycle은 공유한다.
- `snapshot`은 monitor handle 공통 API 하나로 유지한다.
  class 차이는 반환 payload 내부의 `kind`와 nested struct에서만 드러낸다.


## 6. Handler / Event Naming

### 6.1 공통 naming 원칙

현재 `service`라는 이름 자체가 문제라기보다,
그 분류를 public surface에서 더 명확히 드러내야 한다.

canonical naming:

- `zlink_service_monitor_event_t`
- `zlink_service_monitor_handler_fn`
- `zlink_socket_monitor_event_t`
- `zlink_socket_monitor_handler_fn`

socket 쪽은 기존 `zlink_monitor_handler_fn`이 있다면
새 이름으로 치환한다.
service 쪽은 기존 `service` 분류를 유지하되 public naming을 더 정돈한다.

### 6.2 handler 서명

```c
typedef void (*zlink_socket_monitor_handler_fn) (
  const zlink_socket_monitor_event_t *event_,
  void *userdata_);

typedef void (*zlink_service_monitor_handler_fn) (
  const zlink_service_monitor_event_t *event_,
  void *userdata_);
```

typed open과 typed recv를 분리했기 때문에
문서와 바인딩은 callback cast 규칙을 추가로 설명할 필요가 없다.
이것이 사용성 측면에서 더 단순하다.

### 6.3 delivery model

monitor delivery는 기존 recv/callback 전환 정책을 따른다.

- open 직후 기본은 recv model
- `*_monitor_handler()` 호출 시 callback-only로 단방향 전환
- 전환 후 direct `recv`는 `EBUSY`
- `snapshot`은 양쪽 model에서 모두 허용

recv path도 open과 같은 축으로 typed API를 사용한다.

- socket monitor는 `zlink_socket_monitor_recv()`
- service monitor는 `zlink_service_monitor_recv()`


## 7. Event Surface

### 7.0 event curation 원칙

monitor re-interface에서는 "현재 존재하는 이벤트를 그대로 재포장"하지 않는다.
event는 다음 기준으로 다시 걸러야 한다.

- 테스트/perf가 실제 readiness gate 또는 ordering contract로 사용한다.
- snapshot으로 같은 정보를 더 단순하게 표현할 수 없다.
- event 이름만 봐도 사용자가 다음 행동을 결정할 수 있다.

반대로 다음 성격의 이벤트는 canonical set에서 우선 제외한다.

- snapshot으로 충분히 대체되는 aggregate hint
- 실제 테스트/perf가 기다리지 않는 low-signal transition
- generic alias 때문에 의미가 흐려지는 이벤트

현재 코드베이스 사용처 기준 권장 분류:

- socket core
  - `CONNECTED`
  - `ACCEPTED`
  - `CONNECTION_READY`
  - `DISCONNECTED`
  - failure 계열: `BIND_FAILED`, `ACCEPT_FAILED`, `CLOSE_FAILED`,
    `HANDSHAKE_FAILED_*`
- socket optional
  - `LISTENING`
  - `CLOSED`
  - `MONITOR_STOPPED`
- service core
  - discovery: `SERVICE_UP`, `SERVICE_DOWN`
  - gateway: `SERVICE_READY`, `SEND_READY_CHANGED`, `ROUTE_UP`, `ROUTE_DOWN`
  - spot-sub: `SUB_FILTER_APPLIED`, `SUBSCRIPTION_READY`,
    `SUB_DELIVERY_READY_CHANGED`
  - spot-pub: `PUB_FIRST_DELIVERY_READY_CHANGED`
  - common lifecycle: `ERROR`, `CLOSED`
- service optional
  - discovery: `PROVIDERS_CHANGED`
  - gateway: `SERVICE_LOST`
  - spot-pub: `PUB_DELIVERY_READY_CHANGED`
- service remove 후보
  - generic alias `READY`, `LOST`, `PEER_UP`, `PEER_DOWN`
  - queue state `PUB_QUEUE_FULL`, `PUB_QUEUE_DRAINED`

### 7.1 socket event

socket event는 기존 socket monitor event를 계승한다.
이 계층은 raw socket 수준 사실만 전달한다.

특히 handshake 계열은 반드시 이 계층에 남긴다.
TLS/ZMTP handshake 성공/실패와 원인 코드는 service event로 끌어올리지 않는다.

예:

- listening
- accepted
- connected
- connect delayed
- connect retried
- disconnected
- handshake succeeded
- handshake failed
- closed
- error

public bitmask 이름은 다음처럼 단일 축으로 정리한다.

```c
typedef uint32_t zlink_socket_monitor_event_mask_t;

#define ZLINK_SOCKET_MONITOR_EVENT_CONNECTED ...
#define ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED ...
#define ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED ...
#define ZLINK_SOCKET_MONITOR_EVENT_CLOSED ...
#define ZLINK_SOCKET_MONITOR_EVENT_ALL ...
```

이 계층은 raw socket에서만 사용한다.

### 7.2 service event

service event는
discovery/gateway/spot/spot_node service family의 service-level state를
전달한다.

예:

- error
- closed
- discovery service up
- discovery service down
- gateway service ready
- gateway send ready changed
- gateway route up
- gateway route down
- spot sub filter applied
- spot subscription ready
- spot sub delivery ready changed
- spot pub first delivery ready changed

public bitmask는 subject-specific typedef를 없애고
하나의 service event mask로 통합한다.

```c
typedef uint32_t zlink_service_monitor_event_mask_t;

#define ZLINK_SERVICE_MONITOR_EVENT_ERROR ...
#define ZLINK_SERVICE_MONITOR_EVENT_CLOSED ...

#define ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP ...
#define ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN ...

#define ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_SERVICE_READY ...
#define ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_SEND_READY_CHANGED ...
#define ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_ROUTE_UP ...
#define ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_ROUTE_DOWN ...

#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED ...
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY ...
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUB_DELIVERY_READY_CHANGED ...
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_FIRST_DELIVERY_READY_CHANGED ...

#define ZLINK_SERVICE_MONITOR_EVENT_ALL ...
```

중요한 점은,
모든 service target이 모든 event를 내보내는 것은 아니라는 점이다.
그러나 event namespace는 하나로 유지한다.
유효성은 `event->target_kind`와 `event->event_type` 조합으로 해석한다.

canonical service namespace에서는 generic alias `READY`, `LOST`,
`PEER_UP`, `PEER_DOWN`를 두지 않는다.
이 alias들은 현재 테스트에서 편의상 자주 쓰이지만,
re-interface 목표인 "이벤트 이름만 보고 의미가 바로 드러나는 surface"와는
충돌한다.

`PROVIDERS_CHANGED`, `SERVICE_LOST`, `SPOT_PUB_DELIVERY_READY_CHANGED`는
canonical minimal set에는 넣지 않는다.
필요하다면 optional extension event로 별도 문서화한다.

optional extension 예:

```c
#define ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED ...
#define ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_SERVICE_LOST ...
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_PUB_DELIVERY_READY_CHANGED ...
```


## 8. Service Event Struct

### 8.1 목표

service event struct는 다음 요구를 동시에 만족해야 한다.

- discovery/gateway/spot/spot_node를 하나의 handler 타입으로 처리할 수
  있어야 한다.
- 그러나 `spot pub/sub` 같은 내부 구성 지식은 노출하지 않아야 한다.
- event 유효 필드 규칙이 설명 가능해야 한다.

먼저 service 계열 공통 target kind는 다음처럼 둔다.

```c
typedef enum zlink_monitor_target_kind_t
{
    ZLINK_MONITOR_TARGET_SOCKET = 1,
    ZLINK_MONITOR_TARGET_DISCOVERY = 2,
    ZLINK_MONITOR_TARGET_GATEWAY = 3,
    ZLINK_MONITOR_TARGET_SPOT = 4,
    ZLINK_MONITOR_TARGET_SPOT_NODE = 5
} zlink_monitor_target_kind_t;
```

### 8.2 canonical struct

```c
typedef uint32_t zlink_service_monitor_event_detail_mask_t;

#define ZLINK_SERVICE_MONITOR_DETAIL_SERVICE_NAME ...
#define ZLINK_SERVICE_MONITOR_DETAIL_ENDPOINT ...
#define ZLINK_SERVICE_MONITOR_DETAIL_SUBJECT_RID ...
#define ZLINK_SERVICE_MONITOR_DETAIL_PEER_RID ...
#define ZLINK_SERVICE_MONITOR_DETAIL_TOPIC ...
#define ZLINK_SERVICE_MONITOR_DETAIL_PATTERN ...

typedef struct zlink_service_monitor_event_t
{
    zlink_monitor_target_kind_t target_kind;
    uint32_t event_type;
    int32_t status;
    int32_t error_code;
    uint32_t value;
    zlink_service_monitor_event_detail_mask_t detail_flags;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    char subject[256];
} zlink_service_monitor_event_t;
```

### 8.3 field 해석 원칙

- `target_kind`
  - discovery/gateway/spot/spot_node 중 어느 service target인지 나타낸다.
- `event_type`
  - service event bit와 같은 namespace를 공유한다.
- `status`
  - operation result, readiness state, transition outcome를 표현한다.
- `error_code`
  - 실패 원인의 `errno` 계열 코드다.
- `value`
  - provider count, peer count, queue depth 성격의 단일 수치를 담는다.
- `detail_flags`
  - 어떤 추가 필드가 유효한지 명시한다.

`subject_kind`를 별도 enum으로 두는 대신,
`subject` 문자열과 detail flag만으로 충분한 범위만 남긴다.
현재 public API에서 topic/pattern 구분은 이미 문자열 규칙으로 충분히 설명되며,
service event가 그 이상 복잡한 subject taxonomy를 드러낼 필요는 없다.


## 9. `spot` / `spot_node` 재설계 원칙

### 9.1 `role` 제거

새 public surface에서는 다음을 제거한다.

- `zlink_spot_monitor_open(... role ...)`
- `zlink_spot_node_monitor_open(... role ...)`

canonical service target은 `spot` 하나다.
`spot_node`도 별도 service target으로 포함한다.

```c
zlink_service_monitor_open_options_t opts;
memset(&opts, 0, sizeof(opts));
opts.events = ...;

monitor = zlink_service_monitor_open(spot, &opts);
monitor = zlink_service_monitor_open(node, &opts);
```

### 9.2 내부 facet 분리는 event로만 드러낸다

사용자가 정말 알고 싶은 것은
"pub facet에 delivery pressure가 생겼는가"
"sub facet에 subscription readiness가 떴는가"이지,
open 시점에 pub/sub 중 어느 내부 객체에 붙어야 하는가가 아니다.

따라서 내부 facet 차이는 다음으로만 드러낸다.

- event type
- detail field
- snapshot state

open contract는 facade/service 수준으로 고정한다.

### 9.3 필요하면 snapshot에서 facet별 상태를 확장한다

`spot` 또는 `spot_node`가 pub/sub 양쪽 상태를 모두 가질 필요가 있다면,
그 복잡성은 open 인자 대신 snapshot struct에서 받아야 한다.

예:

- pub delivery ready
- sub delivery ready
- ready peer count
- pending send/recv

이 접근은 사용자가 lifecycle을 단순하게 유지한 채
더 많은 상태를 필요할 때만 읽게 만든다.


## 10. Snapshot Model

### 10.1 현재 방향 유지

`zlink_monitor_snapshot()`은 유지한다.
monitor handle에서 현재 상태를 읽는 API라는 개념 자체는 일관되고 유용하다.

다만 snapshot은 event struct와 같은 실수를 반복하면 안 된다.
초안 1의 "모든 숫자를 한 struct에 optional field로 다 넣는 방식"은
여기서 폐기한다.

이유:

- socket snapshot과 service snapshot의 관심사가 다르다.
- 공통 필드보다 optional 필드가 많아지면 다시 넓은 표면이 된다.
- event struct를 둘로 나눈 설계 판단과도 맞지 않는다.

### 10.2 canonical snapshot

```c
typedef enum zlink_monitor_snapshot_kind_t
{
    ZLINK_MONITOR_SNAPSHOT_SOCKET = 1,
    ZLINK_MONITOR_SNAPSHOT_SERVICE = 2
} zlink_monitor_snapshot_kind_t;

typedef struct zlink_socket_monitor_snapshot_t
{
    uint32_t state_flags;
    uint32_t detail_flags;
    uint32_t ready_peer_count;
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
} zlink_socket_monitor_snapshot_t;

typedef struct zlink_service_monitor_snapshot_t
{
    uint32_t state_flags;
    uint32_t detail_flags;
    uint32_t ready_peer_count;
    uint32_t provider_count;
    uint32_t route_count;
    uint32_t service_flags;
} zlink_service_monitor_snapshot_t;

typedef struct zlink_monitor_snapshot_t
{
    zlink_monitor_snapshot_kind_t kind;
    zlink_monitor_target_kind_t target_kind;
    union {
        zlink_socket_monitor_snapshot_t socket;
        zlink_service_monitor_snapshot_t service;
    } data;
} zlink_monitor_snapshot_t;
```

원칙:

- snapshot은 모든 subject에 대해 하나의 함수로 읽는다.
- top-level에서는 `kind`와 `target_kind`만 공통으로 둔다.
- 실제 수치 필드는 class별 nested snapshot에 둔다.
- nested snapshot 내부에서도 `detail_flags`로 유효 필드를 판별한다.

이 방식은 `zlink_monitor_snapshot()`이라는 하나의 mental model은 유지하면서도
payload shape를 class별로 분리해 설명할 수 있다.


## 11. Close Contract

close도 socket/service 공통 함수로 통일한다.

```c
ZLINK_EXPORT int zlink_monitor_close (void **monitor_p_);
```

규칙:

- close는 socket/service monitor 공통 lifecycle 종료 API다.
- callback dispatch 중 동시 close는 구현 단순성을 위해 우선 `EBUSY`로 정의한다.
- 첫 구현에서는 callback 내부 self-close를 지원 대상으로 잡지 않는다.
- close 성공 후 snapshot/read/callback은 모두 금지된다.

기존 `zlink_service_monitor_close()`처럼 특정 계층 이름을 넣지 않는다.
사용자 입장에서는 monitor handle이면 모두 같은 규칙을 가져야 한다.


## 12. Public Examples

### 12.1 socket monitor recv model

```c
zlink_socket_monitor_open_options_t opts;
memset(&opts, 0, sizeof(opts));
opts.events = ZLINK_SOCKET_MONITOR_EVENT_CONNECTED
              | ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED;

void *mon = zlink_socket_monitor_open(sock, &opts);

zlink_socket_monitor_event_t ev;
memset(&ev, 0, sizeof(ev));

if (zlink_socket_monitor_recv(mon, &ev) == 0
    && ev.event_type == ZLINK_SOCKET_MONITOR_EVENT_CONNECTED) {
    /* socket connected */
}
```

### 12.2 gateway service callback model

```c
static void on_gateway (
  const zlink_service_monitor_event_t *event_,
  void *userdata_)
{
    (void) userdata_;
    if (event_->event_type == ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_ROUTE_UP) {
        /* route became available */
    }
}

zlink_service_monitor_open_options_t opts;
memset(&opts, 0, sizeof(opts));
opts.events = ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_SERVICE_READY
              | ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_ROUTE_UP
              | ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_ROUTE_DOWN;

void *mon = zlink_service_monitor_open(gateway, &opts);
zlink_service_monitor_handler(mon, &on_gateway, NULL);
```

### 12.3 spot service callback model

```c
static void on_spot (
  const zlink_service_monitor_event_t *event_,
  void *userdata_)
{
    (void) userdata_;
    switch (event_->event_type) {
    case ZLINK_SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED:
        break;
    case ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY:
        break;
    case ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUB_DELIVERY_READY_CHANGED:
        break;
    case ZLINK_SERVICE_MONITOR_EVENT_SPOT_FIRST_DELIVERY_READY_CHANGED:
        break;
    }
}

zlink_service_monitor_open_options_t opts;
memset(&opts, 0, sizeof(opts));
opts.events = ZLINK_SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED
              | ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY
              | ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUB_DELIVERY_READY_CHANGED
              | ZLINK_SERVICE_MONITOR_EVENT_SPOT_FIRST_DELIVERY_READY_CHANGED;

void *mon = zlink_service_monitor_open(spot, &opts);
zlink_service_monitor_handler(mon, &on_spot, NULL);
```

### 12.4 spot_node service callback model

```c
static void on_spot_node (
  const zlink_service_monitor_event_t *event_,
  void *userdata_)
{
    (void) userdata_;
    if (event_->target_kind == ZLINK_MONITOR_TARGET_SPOT_NODE
        && event_->event_type
             == ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY) {
        /* node-owned sub became ready */
    }
}

zlink_service_monitor_open_options_t opts;
memset(&opts, 0, sizeof(opts));
opts.events = ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY
              | ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUB_DELIVERY_READY_CHANGED;

void *mon = zlink_service_monitor_open(node, &opts);
zlink_service_monitor_handler(mon, &on_spot_node, NULL);
```


## 13. 제거할 public surface

호환성 제약이 없으므로 다음 legacy surface는 제거 대상이다.

- `zlink_service_monitor_close()`
- `zlink_service_monitor_ignore_handler()`
- subject-specific open 함수군
- callback을 open 인자에서 받는 surface
- discovery/gateway/spot별 monitor event mask typedef

즉 monitor 관련 public naming은 새 canonical family로 일괄 치환한다.
compat wrapper는 두지 않는다.


## 14. 구현 매핑 원칙

내부 구현은 이미 다음 공통 축을 상당 부분 가지고 있다.

- monitor handle registry
- callback dispatch worker
- snapshot provider
- socket/service monitor 구분

따라서 재설계의 핵심은
"새 public contract로 입구를 단순화하고,
 내부에서 target kind별 provider/decoder를 고르는 것"이다.

권장 내부 분해:

- `monitor_open()` 공통 진입점
- target validation layer
- socket target adapter
- service target adapter
- snapshot provider registry
- common close path

실구현으로 내릴 때의 매핑 원칙:

- `socket monitor open`은 raw socket handle 검증 후 monitor stream을 연다.
- `service monitor open`은 discovery/gateway/spot/spot_node handle 검증 후
  해당 service adapter에 연결된 monitor stream을 연다.
- handshake 관련 contract와 failure surface는 socket monitor 쪽에만 둔다.
- `service target kind`는 open 옵션이 아니라 target handle tag에서 유도한다.
- `*_monitor_handler()`는 기존 monitor state registry에 dispatch worker를 붙이는
  전환 API로 구현한다.
- `*_monitor_recv()`는 monitor stream decode wrapper로 구현한다.
- `zlink_monitor_snapshot()`은 현재처럼 monitor handle 기반 provider lookup을 유지한다.

`spot`과 `spot_node`는 service family adapter로 흡수하고,
pub/sub/internal receiver 선택은 adapter 내부 정책으로 숨긴다.


## 15. 문서화 원칙

새 guide는 monitor를 대상별 함수 목록으로 설명하지 않는다.

문서 구조는 다음 순서를 기본으로 한다.

1. socket monitor와 service monitor의 차이
2. `zlink_socket_monitor_open()` /
   `zlink_service_monitor_open()` 공통 규칙
3. target kind별 사용 예
4. event namespace reference
5. snapshot 해석법
6. close/lifecycle 제약

이 순서는 사용자가 먼저 mental model을 얻고,
그 뒤에 event catalog를 보게 만든다.


## 16. 바인딩 원칙

언어 바인딩도 동일한 public model을 반영해야 한다.

- C++
  - `socket_monitor_open()` / `service_monitor_open()` 또는
    class-safe wrapper 제공
- Java/C#
  - typed options object와 typed open 메서드 제공
- Python/Node
  - socket/service helper를 분리하되
    공통 close/snapshot 규칙은 유지

중요한 점은 바인딩마다 다시 subject-specific open 함수를 복제하지 않는 것이다.
그렇게 하면 C API에서 정리한 사용성 이점을 다시 잃는다.


## 17. 단계별 구현 순서

### Phase 1. 타입/이름 정리

- 새 enum/struct/function 이름 도입
- service/socket event namespace 분리
- old service naming 제거

### Phase 2. recv-first public monitor surface 정리

- `zlink_socket_monitor_open()`
- `zlink_service_monitor_open()`
- `zlink_socket_monitor_recv()`
- `zlink_service_monitor_recv()`
- `zlink_socket_monitor_handler()`
- `zlink_service_monitor_handler()`
- `zlink_monitor_close()`
- recv model -> callback-only 전환 경로 정리

### Phase 3. `spot` / `spot_node` service surface 재구성

- `role` 제거
- service-family adapter 도입
- snapshot/event에서 facet 차이만 노출

### Phase 4. 바인딩/문서/테스트 동기화

- guide/example 전면 교체
- binding public API 재정렬
- unit/integration/e2e 테스트의 open path 교체

### Phase 2.5 구현 난이도 절감 규칙

- `service_monitor_open(options.target_kind)` 같은 중복 인자는 도입하지 않는다.
- generic `zlink_monitor_recv()` envelope는 도입하지 않는다.
- optional event는 1차 구현 범위에서 제외할 수 있다.
- callback 내부 self-close/deferred close는 1차 구현 범위에서 제외한다.
- 먼저 thin wrapper와 state registry 정리로 구현 가능한 최소 surface를 만든다.


## 18. 기대 효과

이 재설계가 주는 가장 큰 이점은
monitor 사용법이 설명 한 문단으로 끝난다는 점이다.

사용자는 더 이상 다음을 고민하지 않는다.

- discovery/gateway/spot/spot_node마다 여는 함수가 왜 다른가
- `service monitor`와 `socket monitor`가 어떤 관계인가
- `spot`에서 왜 `role`을 넣어야 하는가
- callback 없는 monitor handle을 왜 따로 열어야 하는가

대신 다음만 기억하면 된다.

- socket을 보고 싶으면 socket monitor
- service 의미 상태를 보고 싶으면 service monitor
- open은 `zlink_socket_monitor_open()` 또는
  `zlink_service_monitor_open()`으로 하고 기본은 recv model이다
- callback이 필요하면 `zlink_socket_monitor_handler()` 또는
  `zlink_service_monitor_handler()`를 붙인다
- callback을 붙인 뒤에는 direct `recv`로 되돌아가지 않는다
- 상태 조회와 종료는 항상
  `zlink_monitor_snapshot()` / `zlink_monitor_close()`를 쓴다

이것이 사용성 측면에서 가장 단순하고,
POSD 기준으로도 내부 복잡성을 public surface 밖으로 밀어내는 방향이다.


## 18.5 1차 구현 범위

문서를 바로 구현 작업으로 내리기 위해,
1차 구현 범위는 다음으로 고정한다.

- public open은 `zlink_socket_monitor_open()` /
  `zlink_service_monitor_open()` 두 개만 둔다.
- open-time handler 인자는 제거하고 기본 recv model로 시작한다.
- callback 전환 API는 `zlink_socket_monitor_handler()` /
  `zlink_service_monitor_handler()` 두 개만 둔다.
- recv API는 generic envelope 없이
  `zlink_socket_monitor_recv()` /
  `zlink_service_monitor_recv()` 두 개만 둔다.
- close는 `zlink_monitor_close()` 하나로 통일한다.
- snapshot은 `zlink_monitor_snapshot()` 하나를 유지한다.
- service open은 target handle tag로 kind를 결정한다.
- `spot role`, `ignore handler`, subject-specific open 함수군은 제거한다.
- `spot_node` monitor 기능은 제거가 아니라 `zlink_service_monitor_open(node, ...)`
  로 흡수한다.
- optional event와 callback 내부 self-close는 1차 구현 범위에서 제외한다.

즉 첫 구현의 성공 기준은
"typed open/recv/handler + generic snapshot/close"가 실제 코드로
정리되는 것이다.


## 19. Existing Usage Review

이 섹션은 "지금 정의된 monitor event를 모두 유지할 필요가 있는가"를
기존 테스트/perf 사용처 기준으로 검토한 결과다.

검토 원칙:

- 테스트나 perf가 실제 대기 조건으로 쓰는 이벤트는 우선 유지한다.
- 단순히 마스크에 포함되기만 하고 실제 contract 검증에 쓰이지 않는 이벤트는
  낮은 우선순위로 본다.
- snapshot으로 충분히 대체 가능한 이벤트는 canonical event set에서
  제외를 우선 검토한다.

### 19.0 review summary

먼저 결론만 요약하면 다음과 같다.

- canonical minimal set에는 실제 readiness gate / ordering contract로 쓰이는
  이벤트만 남긴다.
- optional extension에는 진단용 또는 일부 시나리오에서만 의미가 있는 이벤트를 둔다.
- generic alias와 low-signal queue hint는 public canonical set에서 제거한다.
- monitor delivery는 callback-only로 고정하지 않고,
  기본 recv model에서 callback 부착 시 callback-only로 전환한다.

요약 표:

| 영역 | canonical minimal set | optional extension | remove 후보 |
| --- | --- | --- | --- |
| socket | `CONNECTED`, `ACCEPTED`, `CONNECTION_READY`, `DISCONNECTED`, `BIND_FAILED`, `ACCEPT_FAILED`, `CLOSE_FAILED`, `HANDSHAKE_FAILED_*` | `LISTENING`, `CLOSED`, `MONITOR_STOPPED`, `CONNECT_DELAYED`, `CONNECT_RETRIED` | 없음 |
| discovery | `SERVICE_UP`, `SERVICE_DOWN`, `ERROR`, `CLOSED` | `PROVIDERS_CHANGED` | generic `READY`, `LOST` |
| gateway | `SERVICE_READY`, `SEND_READY_CHANGED`, `ROUTE_UP`, `ROUTE_DOWN`, `ERROR`, `CLOSED` | `SERVICE_LOST` | `CONNECTION_COUNT_CHANGED` 복원 |
| spot | `SUB_FILTER_APPLIED`, `SUBSCRIPTION_READY`, `SUB_DELIVERY_READY_CHANGED`, `PUB_FIRST_DELIVERY_READY_CHANGED`, `ERROR`, `CLOSED` | `PUB_DELIVERY_READY_CHANGED` | generic `READY`, `LOST`, `PEER_UP`, `PEER_DOWN`, `PUB_QUEUE_FULL`, `PUB_QUEUE_DRAINED` |

이 표가 re-interface의 event 축소 기준이다.
이후 public header 초안, guide, 바인딩 설계는 이 분류를 기준으로 맞춘다.

### 19.1 socket event review

socket 쪽은 현재도 실사용 근거가 충분하다.

핵심 사용처:

- `CONNECTION_READY` / `DISCONNECTED`
  - integration contract와 enhanced monitor test가 직접 기다린다.
- `ACCEPTED` / `CONNECTED`
  - perf connect monitor와 perf-like contract handler가 직접 집계한다.
- failure 계열
  - `BIND_FAILED`, `ACCEPT_FAILED`, `CLOSE_FAILED`,
    `HANDSHAKE_FAILED_*`는 perf/test monitor state에서 error source로 사용된다.

대표 사용처:

- [test_monitor_enhanced.cpp](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_enhanced.cpp#L327)
- [bench_common.hpp](/home/hep7/project/kairos/zlink/core/perf/single/common/bench_common.hpp#L453)
- [test_monitor_perf_contract.cpp](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_perf_contract.cpp#L220)

판단:

- 유지: `CONNECTED`, `ACCEPTED`, `CONNECTION_READY`, `DISCONNECTED`,
  `BIND_FAILED`, `ACCEPT_FAILED`, `CLOSE_FAILED`,
  `HANDSHAKE_FAILED_NO_DETAIL`, `HANDSHAKE_FAILED_PROTOCOL`,
  `HANDSHAKE_FAILED_AUTH`
- optional: `LISTENING`, `CLOSED`, `MONITOR_STOPPED`
- `CONNECT_DELAYED`, `CONNECT_RETRIED`는 진단용 가치가 있어 optional 유지가 가능하다.

### 19.2 discovery event review

discovery는 실제로 의미 있는 것은 대부분 `SERVICE_UP` / `SERVICE_DOWN`이다.

대표 사용처:

- [test_service_introspection.cpp](/home/hep7/project/kairos/zlink/core/tests/e2e/discovery/test_service_introspection.cpp#L588)
- [test_service_introspection.cpp](/home/hep7/project/kairos/zlink/core/tests/e2e/discovery/test_service_introspection.cpp#L988)
- [test_monitor_with_handler.cpp](/home/hep7/project/kairos/zlink/core/tests/integration/test_monitor_with_handler.cpp#L726)

관찰:

- `SERVICE_UP`은 discovery monitor의 대표 contract로 직접 검증된다.
- `SERVICE_DOWN`도 ordering contract에서 직접 검증된다.
- `CLOSED`는 lifecycle/EBUSY 관련 계약 검증에 쓰인다.
- `PROVIDERS_CHANGED`는 문서에는 있지만 현재 `core/tests`, `core/perf`에서
  직접 기다리는 contract가 보이지 않는다.
- generic `READY` / `LOST`는 discovery에서 핵심 사용자 의미가 약하다.

판단:

- 유지: `SERVICE_UP`, `SERVICE_DOWN`, `ERROR`, `CLOSED`
- optional: `PROVIDERS_CHANGED`
- remove 후보: discovery의 generic `READY`, `LOST`

### 19.3 gateway event review

gateway는 실제 사용 패턴이 비교적 선명하다.
사용자는 route 형성, send readiness, local service publication 상태를 본다.

대표 사용처:

- [test_monitor_service_contract.cpp](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_service_contract.cpp#L939)
- [test_gateway_with_handler.cpp](/home/hep7/project/kairos/zlink/core/tests/integration/discovery/test_gateway_with_handler.cpp#L88)
- [perf_gateway.cpp](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_gateway.cpp#L202)
- [perf_common.hpp](/home/hep7/project/kairos/zlink/core/perf/multi/common/perf_common.hpp#L731)

관찰:

- `SERVICE_READY`는 server-side publication readiness gate로 쓰인다.
- `SEND_READY_CHANGED`는 first send 가능 조건과 snapshot state 검증에 쓰인다.
- `ROUTE_UP` / `ROUTE_DOWN`은 peer availability 추적에 직접 쓰인다.
- `ERROR`는 공통 failure channel로 필요하다.
- `SERVICE_LOST`는 여러 monitor mask에 포함되지만,
  현재 테스트/perf에서 직접 event contract를 검증하는 사례는 찾기 어렵다.
- `CLOSED`는 lifecycle 검증에는 필요하지만 steady-state service state에는 핵심이 아니다.
- 과거 문서에 있던 `CONNECTION_COUNT_CHANGED`는 현재 헤더 public surface에 없고,
  테스트/perf 기준 핵심 contract도 아니다.

판단:

- 유지: `SERVICE_READY`, `SEND_READY_CHANGED`, `ROUTE_UP`, `ROUTE_DOWN`,
  `ERROR`, `CLOSED`
- optional: `SERVICE_LOST`
- remove 후보: 별도 `CONNECTION_COUNT_CHANGED` 복원

### 19.4 spot event review

spot은 generic topology event보다 subject-aware readiness event가 훨씬 중요하다.

대표 사용처:

- [test_spot_service_introspection.cpp](/home/hep7/project/kairos/zlink/core/tests/e2e/spot/test_spot_service_introspection.cpp#L130)
- [test_monitor_service_contract.cpp](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_service_contract.cpp#L1122)
- [test_monitor_service_contract.cpp](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_service_contract.cpp#L1239)
- [perf_spot.cpp](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_spot.cpp#L812)

관찰:

- `SUB_FILTER_APPLIED`는 local subscribe control이 반영됐는지 보는 첫 gate다.
- `SUBSCRIPTION_READY`는 legacy/compat 의미로 아직 직접 대기하는 테스트가 많다.
- `SUB_DELIVERY_READY_CHANGED`는 실제 delivery 가능 여부를 나타내는 핵심 event다.
- `PUB_FIRST_DELIVERY_READY_CHANGED`는 publisher가 안전하게 첫 publish를 시작할
  수 있는 canonical gate로 쓰인다.
- `PUB_DELIVERY_READY_CHANGED`는 존재하지만 현재 계약상 핵심 gate는
  `FIRST_DELIVERY_READY_CHANGED` 쪽이다.
- `PUB_QUEUE_FULL` / `PUB_QUEUE_DRAINED`는 public 정의는 있으나
  현재 테스트/perf의 핵심 대기 조건으로 보이지 않는다.
- generic `READY` / `LOST` / `PEER_UP` / `PEER_DOWN`는
  예전 시나리오 helper에서는 쓰이지만,
  실제 의미 계약은 subject-aware event가 대신하고 있다.
- `CLOSED`는 lifecycle/teardown 검증에 필요하다.

판단:

- 유지:
  - `SUB_FILTER_APPLIED`
  - `SUBSCRIPTION_READY`
  - `SUB_DELIVERY_READY_CHANGED`
  - `PUB_FIRST_DELIVERY_READY_CHANGED`
  - `ERROR`
  - `CLOSED`
- optional:
  - `PUB_DELIVERY_READY_CHANGED`
- remove 후보:
  - `PUB_QUEUE_FULL`
  - `PUB_QUEUE_DRAINED`
  - generic `READY`, `LOST`, `PEER_UP`, `PEER_DOWN`

### 19.5 canonical event set recommendation

re-interface 기준 권장 canonical set은 다음과 같다.

- socket core
  - `CONNECTED`
  - `ACCEPTED`
  - `CONNECTION_READY`
  - `DISCONNECTED`
  - `BIND_FAILED`
  - `ACCEPT_FAILED`
  - `CLOSE_FAILED`
  - `HANDSHAKE_FAILED_NO_DETAIL`
  - `HANDSHAKE_FAILED_PROTOCOL`
  - `HANDSHAKE_FAILED_AUTH`
- discovery core
  - `SERVICE_UP`
  - `SERVICE_DOWN`
  - `ERROR`
  - `CLOSED`
- gateway core
  - `SERVICE_READY`
  - `SEND_READY_CHANGED`
  - `ROUTE_UP`
  - `ROUTE_DOWN`
  - `ERROR`
  - `CLOSED`
- spot core
  - `SUB_FILTER_APPLIED`
  - `SUBSCRIPTION_READY`
  - `SUB_DELIVERY_READY_CHANGED`
  - `PUB_FIRST_DELIVERY_READY_CHANGED`
  - `ERROR`
  - `CLOSED`

optional set:

- socket: `LISTENING`, `CLOSED`, `MONITOR_STOPPED`,
  `CONNECT_DELAYED`, `CONNECT_RETRIED`
- discovery: `PROVIDERS_CHANGED`
- gateway: `SERVICE_LOST`
- spot: `PUB_DELIVERY_READY_CHANGED`

remove 후보:

- generic alias `READY`, `LOST`, `PEER_UP`, `PEER_DOWN`
- `PUB_QUEUE_FULL`
- `PUB_QUEUE_DRAINED`

### 19.6 design consequence

이 검토 결과는 re-interface 문서의 기본 방향과 일치한다.

- generic alias를 줄이고 concrete service event를 남긴다.
- snapshot으로 표현 가능한 aggregate 변화는 event를 최소화한다.
- spot은 queue 상태보다 delivery readiness를 canonical event로 삼는다.
- gateway는 route/send-ready 중심으로 남기고,
  service-lost는 optional로 낮춘다.

즉 re-interface는 "event를 더 예쁘게 다시 묶는 작업"이 아니라
"정말 필요한 contract event만 남기고 나머지는 snapshot이나 내부 개념으로
밀어 넣는 작업"이어야 한다.


## 20. Header Draft

이 섹션은 본 문서의 설계 결론을
`core/include/zlink.h`에 바로 반영할 수 있는 수준의 선언 초안으로 정리한 것이다.

목적:

- 구현 착수 전에 public surface를 한 번에 검토할 수 있게 한다.
- 함수/타입 naming이 문서 앞부분의 정책과 실제로 맞는지 확인한다.
- 1차 구현 범위를 벗어나는 optional surface를 선언 단계에서 미리 배제한다.

### 20.1 draft principles

이 초안은 다음 원칙을 따른다.

- open은 `socket` / `service` 두 계열만 둔다.
- open-time handler는 받지 않는다.
- 기본 model은 recv다.
- callback은 별도 `*_monitor_handler()`로 부착한다.
- recv는 generic envelope 없이 typed recv 둘로 나눈다.
- snapshot/close는 공통 함수로 둔다.
- `spot role`, `ignore handler`는 선언에서 제거한다.
- `spot_node`는 별도 open 함수가 아니라 `service monitor` 대상에 포함한다.
- optional event는 1차 헤더 초안에 넣지 않는다.

### 20.2 header draft

```c
/*  monitor target kind  */

typedef enum zlink_monitor_target_kind_t
{
    ZLINK_MONITOR_TARGET_SOCKET = 1,
    ZLINK_MONITOR_TARGET_DISCOVERY = 2,
    ZLINK_MONITOR_TARGET_GATEWAY = 3,
    ZLINK_MONITOR_TARGET_SPOT = 4,
    ZLINK_MONITOR_TARGET_SPOT_NODE = 5
} zlink_monitor_target_kind_t;


/*  socket monitor events  */

typedef uint32_t zlink_socket_monitor_event_mask_t;

#define ZLINK_SOCKET_MONITOR_EVENT_CONNECTED                   ((uint32_t) 0x00000001u)
#define ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED                    ((uint32_t) 0x00000002u)
#define ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY            ((uint32_t) 0x00000004u)
#define ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED                ((uint32_t) 0x00000008u)
#define ZLINK_SOCKET_MONITOR_EVENT_BIND_FAILED                 ((uint32_t) 0x00000010u)
#define ZLINK_SOCKET_MONITOR_EVENT_ACCEPT_FAILED               ((uint32_t) 0x00000020u)
#define ZLINK_SOCKET_MONITOR_EVENT_CLOSE_FAILED                ((uint32_t) 0x00000040u)
#define ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_NO_DETAIL  ((uint32_t) 0x00000080u)
#define ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_PROTOCOL   ((uint32_t) 0x00000100u)
#define ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_AUTH       ((uint32_t) 0x00000200u)

#define ZLINK_SOCKET_MONITOR_EVENT_ALL                         ((uint32_t) 0x000003ffu)


/*  service monitor events  */

typedef uint32_t zlink_service_monitor_event_mask_t;

#define ZLINK_SERVICE_MONITOR_EVENT_ERROR                             ((uint32_t) 0x00000001u)
#define ZLINK_SERVICE_MONITOR_EVENT_CLOSED                            ((uint32_t) 0x00000002u)

#define ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP              ((uint32_t) 0x00000004u)
#define ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN            ((uint32_t) 0x00000008u)

#define ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_SERVICE_READY             ((uint32_t) 0x00000010u)
#define ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_SEND_READY_CHANGED        ((uint32_t) 0x00000020u)
#define ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_ROUTE_UP                  ((uint32_t) 0x00000040u)
#define ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_ROUTE_DOWN                ((uint32_t) 0x00000080u)

#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED               ((uint32_t) 0x00000100u)
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY           ((uint32_t) 0x00000200u)
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUB_DELIVERY_READY_CHANGED   ((uint32_t) 0x00000400u)
#define ZLINK_SERVICE_MONITOR_EVENT_SPOT_FIRST_DELIVERY_READY_CHANGED ((uint32_t) 0x00000800u)

#define ZLINK_SERVICE_MONITOR_EVENT_ALL                               ((uint32_t) 0x00000fffu)


/*  socket monitor event payload  */

typedef struct zlink_socket_monitor_event_t
{
    uint32_t event_type;
    int32_t status;
    int32_t error_code;
    char endpoint[256];
    uint64_t value;
} zlink_socket_monitor_event_t;


/*  service monitor event payload  */

typedef uint32_t zlink_service_monitor_event_detail_mask_t;

#define ZLINK_SERVICE_MONITOR_DETAIL_SERVICE_NAME  ((uint32_t) 0x00000001u)
#define ZLINK_SERVICE_MONITOR_DETAIL_ENDPOINT      ((uint32_t) 0x00000002u)
#define ZLINK_SERVICE_MONITOR_DETAIL_SUBJECT       ((uint32_t) 0x00000004u)
#define ZLINK_SERVICE_MONITOR_DETAIL_ROUTING_ID    ((uint32_t) 0x00000008u)

typedef struct zlink_service_monitor_event_t
{
    zlink_monitor_target_kind_t target_kind;
    uint32_t event_type;
    int32_t status;
    int32_t error_code;
    uint32_t value;
    zlink_service_monitor_event_detail_mask_t detail_flags;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    char subject[256];
} zlink_service_monitor_event_t;


/*  monitor handlers  */

typedef void (*zlink_socket_monitor_handler_fn) (
  const zlink_socket_monitor_event_t *event_,
  void *userdata_);

typedef void (*zlink_service_monitor_handler_fn) (
  const zlink_service_monitor_event_t *event_,
  void *userdata_);


/*  monitor open options  */

typedef struct zlink_socket_monitor_open_options_t
{
    zlink_socket_monitor_event_mask_t events;
} zlink_socket_monitor_open_options_t;

typedef struct zlink_service_monitor_open_options_t
{
    zlink_service_monitor_event_mask_t events;
} zlink_service_monitor_open_options_t;


/*  monitor snapshot  */

typedef enum zlink_monitor_snapshot_kind_t
{
    ZLINK_MONITOR_SNAPSHOT_SOCKET = 1,
    ZLINK_MONITOR_SNAPSHOT_SERVICE = 2
} zlink_monitor_snapshot_kind_t;

typedef struct zlink_socket_monitor_snapshot_t
{
    uint32_t state_flags;
    uint32_t detail_flags;
    uint32_t ready_peer_count;
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
} zlink_socket_monitor_snapshot_t;

typedef struct zlink_service_monitor_snapshot_t
{
    uint32_t state_flags;
    uint32_t detail_flags;
    uint32_t ready_peer_count;
    uint32_t provider_count;
    uint32_t route_count;
    uint32_t service_flags;
} zlink_service_monitor_snapshot_t;

typedef struct zlink_monitor_snapshot_t
{
    zlink_monitor_snapshot_kind_t kind;
    zlink_monitor_target_kind_t target_kind;
    union {
        zlink_socket_monitor_snapshot_t socket;
        zlink_service_monitor_snapshot_t service;
    } data;
} zlink_monitor_snapshot_t;


/*  monitor lifecycle  */

ZLINK_EXPORT void *zlink_socket_monitor_open (
  void *socket_,
  const zlink_socket_monitor_open_options_t *options_);

ZLINK_EXPORT void *zlink_service_monitor_open (
  void *target_,
  const zlink_service_monitor_open_options_t *options_);

ZLINK_EXPORT int zlink_socket_monitor_handler (
  void *monitor_,
  zlink_socket_monitor_handler_fn handler_,
  void *userdata_);

ZLINK_EXPORT int zlink_service_monitor_handler (
  void *monitor_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_);

ZLINK_EXPORT int zlink_socket_monitor_recv (
  void *monitor_,
  zlink_socket_monitor_event_t *out_);

ZLINK_EXPORT int zlink_service_monitor_recv (
  void *monitor_,
  zlink_service_monitor_event_t *out_);

ZLINK_EXPORT int zlink_monitor_snapshot (
  void *monitor_,
  zlink_monitor_snapshot_t *out_);

ZLINK_EXPORT int zlink_monitor_close (void **monitor_p_);
```

### 20.3 replacement map

이 초안 기준으로 기존 public surface는 다음처럼 치환된다.

- `zlink_socket_monitor_open(socket, events, handler, userdata)`
  → `zlink_socket_monitor_open(socket, &opts)` +
  `zlink_socket_monitor_handler(monitor, handler, userdata)`
- `zlink_discovery_monitor_open(...)`
  → `zlink_service_monitor_open(discovery, &opts)`
- `zlink_gateway_monitor_open(...)`
  → `zlink_service_monitor_open(gateway, &opts)`
- `zlink_spot_monitor_open(...)`
  → `zlink_service_monitor_open(spot, &opts)`
- `zlink_spot_node_monitor_open(...)`
  → `zlink_service_monitor_open(node, &opts)`
- `zlink_service_monitor_ignore_handler(...)`
  → 제거
- `zlink_service_monitor_close(...)`
  → `zlink_monitor_close(...)`

### 20.4 implementation notes

헤더 초안 기준 구현 시 주의점은 다음이다.

- `zlink_service_monitor_open()`은 target runtime tag로
  discovery/gateway/spot/spot_node를 판별해야 한다.
- `zlink_socket_monitor_handler()` /
  `zlink_service_monitor_handler()`는 한 번만 성공해야 하며,
  성공 이후 recv는 `EBUSY`여야 한다.
- `zlink_socket_monitor_recv()` /
  `zlink_service_monitor_recv()`는 monitor stream decode thin wrapper여야 한다.
- `zlink_monitor_snapshot()`은 현재 monitor handle 기반 provider lookup 구조를
  유지하는 편이 구현 비용이 낮다.
- `ZLINK_SERVICE_MONITOR_EVENT_ALL`과
  `ZLINK_SOCKET_MONITOR_EVENT_ALL`은 1차 canonical set 기준으로만 구성한다.
  optional event를 나중에 추가하면 비트 범위도 같이 재조정해야 한다.


## 21. Implementation Context Bootstrap

이 섹션은 새 컨텍스트에서 바로 구현 작업을 이어받을 수 있도록
현재 코드 기준의 시작점을 정리한 것이다.

### 21.1 source of truth

- 설계 source of truth:
  [monitor-public-surface-reinterface-plan.ko.md](/home/hep7/project/kairos/zlink/doc/plan/direct-callback-recv/re-interface/monitor-public-surface-reinterface-plan.ko.md)
- public header target:
  [zlink.h](/home/hep7/project/kairos/zlink/core/include/zlink.h)
- 현재 API 구현 진입점:
  [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp)

### 21.2 current implementation anchors

현재 구현에서 바로 재사용하거나 교체 기준으로 삼아야 하는 anchor는 다음이다.

- socket monitor open:
  [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp#L2698)
- socket monitor raw recv decoder:
  [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp#L2755)
- service monitor raw recv decoder:
  [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp#L2872)
- current service monitor close:
  [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp#L2898)
- current generic snapshot:
  [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp#L2948)
- discovery monitor open:
  [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp#L3389)
- gateway monitor open:
  [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp#L3603)
- spot node monitor open:
  [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp#L3880)
- spot monitor open:
  [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp#L4329)
- monitor handler registry:
  [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp#L1420)

### 21.3 implementation facts confirmed from current code

이 문서가 구현 가능하다고 보는 근거는 다음 사실들이다.

- monitor handle은 이미 socket-backed handle이다.
- socket/service monitor event decode path가 내부 함수로 이미 나뉘어 있다.
- snapshot은 이미 monitor handle 공통 API 하나로 구현돼 있다.
- service monitor는 현재도 discovery/gateway/spot/spot_node별 open 이후
  공통 monitor state registry에 연결된다.
- `spot_node`는 현재 별도 open 함수로 노출되지만,
  새 surface에서는 같은 service monitor family로 흡수 가능하다.
- `ignore handler`는 callback 없는 recv/direct poll 수요를 우회하기 위한
  legacy 장치이며, 새 설계에서는 제거 대상이다.

즉 1차 구현의 본질은 새 엔진을 만드는 작업이 아니라
기존 monitor stream + registry + snapshot 구조를
새 public contract에 맞게 재배치하는 작업이다.

### 21.4 explicit non-goals

이번 re-interface 1차 구현에서 하지 않는 일:

- optional event public surface 복원
- callback 내부 self-close 지원
- generic `zlink_monitor_recv()` envelope 추가
- service handle을 `socket monitor` 대상처럼 허용하는 우회
- service monitor에 raw socket diagnostic event를 그대로 섞는 일
- handshake 상태를 service event나 snapshot으로 재표현하는 일

### 21.5 first implementation checklist

새 컨텍스트에서 바로 작업할 때의 최소 체크리스트는 다음 순서를 따른다.

1. [zlink.h](/home/hep7/project/kairos/zlink/core/include/zlink.h)에
   section 20 header draft 기준 선언을 반영한다.
2. [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp)에서
   open-time handler required 제약을 제거한다.
3. `zlink_socket_monitor_open()` /
   `zlink_service_monitor_open()`이 기본 recv model monitor를 반환하도록 정리한다.
4. `zlink_socket_monitor_handler()` /
   `zlink_service_monitor_handler()`를 추가해서 registry 기반 callback 전환을 만든다.
5. `zlink_socket_monitor_recv()` /
   `zlink_service_monitor_recv()`를 public wrapper로 노출한다.
6. `zlink_monitor_close()`를 공통 close entry로 추가하고
   기존 service monitor close 호출 지점을 치환한다.
7. `zlink_service_monitor_ignore_handler()`와
   subject-specific open 함수를 제거하거나 비공개화한다.
   `spot_node` monitor 기능은 제거하지 말고
   `zlink_service_monitor_open(node, ...)`로 흡수한다.
8. 문서에 적은 canonical event set과 실제 bit layout이 일치하는지 다시 검토한다.

### 21.5.1 test and perf authoring rule

monitor re-interface를 반영하거나 새 검증을 추가할 때는 다음 원칙을 따른다.

- `core/tests`, `core/perf`, `core/bench`는 public C API만 사용한다.
- 내부 헤더나 내부 helper를 새 검증 경로에 추가하지 않는다.
- handshake 진행/성공/실패 검증은 `socket monitor` 이벤트로만 관찰한다.
- service readiness 검증은 `service monitor`와 `snapshot`으로만 표현한다.

### 21.6 verification starting point

구현 검증은 전체 테스트를 한 번에 보기보다,
monitor contract 관련 경로부터 확인하는 편이 낫다.

우선 확인 대상:

- [test_monitor_enhanced.cpp](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_enhanced.cpp)
- [test_monitor_service_contract.cpp](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_service_contract.cpp)
- [test_monitor_with_handler.cpp](/home/hep7/project/kairos/zlink/core/tests/integration/test_monitor_with_handler.cpp)
- [test_gateway_with_handler.cpp](/home/hep7/project/kairos/zlink/core/tests/integration/discovery/test_gateway_with_handler.cpp)
- [test_service_introspection.cpp](/home/hep7/project/kairos/zlink/core/tests/e2e/discovery/test_service_introspection.cpp)
- [test_spot_service_introspection.cpp](/home/hep7/project/kairos/zlink/core/tests/e2e/spot/test_spot_service_introspection.cpp)

권장 명령:

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build
ctest --test-dir core/build --output-on-failure -L integration -j1
ctest --test-dir core/build --output-on-failure -L e2e -j1
```

### 21.7 handoff summary

새 컨텍스트에서 이 문서를 읽고 바로 이어서 작업할 때
가장 먼저 기억해야 할 결론만 다시 적으면 다음과 같다.

- monitor public model은 `socket` / `service` 두 축이다.
- open은 기본 recv model로 시작한다.
- callback은 open-time 인자가 아니라 후행 부착 API다.
- callback을 부착하면 callback-only로 단방향 전환된다.
- recv는 typed API 둘로 나누고 envelope는 두지 않는다.
- snapshot/close는 공통 API를 유지한다.
- service open은 handle tag에서 kind를 유도한다.
- `spot role`, `ignore handler`, generic alias event는 제거 방향이다.
- `spot_node`는 제거 대상이 아니라 `service monitor` 대상에 포함된다.

### 21.8 current core gap status

현재 `core`는 public header와 runtime entry 기준으로는
문서 방향을 대부분 반영했고,
monitor 호출부도 canonical naming 기준으로 정리된 상태다.

이미 반영된 항목:

- `zlink_service_monitor_open()`이 추가되어
  discovery/gateway/spot/spot_node를 `service monitor` 축으로 열 수 있다.
- `zlink_socket_monitor_handler()` /
  `zlink_service_monitor_handler()`가 추가되어
  기본 recv model 이후 callback-only 전환이 가능하다.
- `zlink_socket_monitor_recv()` /
  `zlink_service_monitor_recv()`가 추가되어 typed recv가 가능하다.
- `zlink_monitor_close()`가 추가되어 공통 close entry가 생겼다.
- handshake 관찰은 계속 `socket monitor` 경로에 남아 있다.
- public header에서는 다음 legacy service surface가 제거됐다.
  - `zlink_discovery_monitor_open()`
  - `zlink_gateway_monitor_open()`
  - `zlink_spot_monitor_open()`
  - `zlink_spot_node_monitor_open()`
  - `zlink_service_monitor_ignore_handler()`
  - `zlink_service_monitor_close()`
- `core/tests`, `core/perf`, `core/bench`의 monitor 호출은
  `zlink_service_monitor_open()` /
  `zlink_socket_monitor_open()` +
  `*_monitor_handler()` +
  `zlink_monitor_close()` 직접 호출로 치환됐다.
- `legacy_api_compat.hpp`의 monitor compat wrapper는 제거됐다.

아직 남아 있는 미구현:

- monitor public surface 자체 기준으로는
  문서에 남겨 둔 핵심 미구현이 없다.
- 다만 repo 전체를 "모든 test/perf가 C API만 사용"으로 넓히면
  monitor 범위를 넘어서는 기존 internal test utility / unittest 의존은
  별도 정리 과제로 남아 있다.

따라서 현재 상태는
"public service surface 제거, canonical runtime 반영,
monitor 호출부 direct-call 치환, monitor compat 제거까지 완료"
로 요약할 수 있다.

### 21.9 remaining implementation order

monitor re-interface 자체는 `core`에서 구현 완료 상태다.
이후 남는 확장 과제는 다음 순서가 자연스럽다.

1. repo-wide 정책으로 `tests/perf는 C API만 사용`을 강제하려면
   monitor 외 test utility / unittest internal include를 별도 정리한다.
2. monitor 장기 검증은 직접 바이너리 실행과 lane 기반 검증을 병행한다.
   일부 umbrella test는 CTest 개별 timeout 30초에 걸릴 수 있다.
3. 이후 registry / spot-node topology introspection 문서를
   현재 monitor canonical surface에 맞춰 정렬한다.
