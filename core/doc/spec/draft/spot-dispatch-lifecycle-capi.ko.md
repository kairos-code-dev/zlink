# SPOT Dispatch Lifecycle C API 초안

> 이 문서는 구현 전 초안이며 현재 공개 계약이 아니다.
> 현재 공개 계약은 `core/include/zlink.h`와 정식 spec 문서가 기준이다.

## 배경

SPOT은 하나의 실행 context에서 routed receive, subscription receive, Actor join
request, Actor message, timer event를 순서 있게 처리할 수 있어야 한다. 이 모델은
사용자가 core 내부 thread에서 바로 실행되는 callback에 의존하지 않고, dispatch
event를 받은 뒤 같은 context에서 필요한 receive 함수를 호출하도록 만든다.

현재 C API에는 이 모델과 충돌하거나 중복되는 direct callback API가 남아 있다.

- `zlink_spot_handler(...)`
- `zlink_spot_actor_lifecycle_handler(...)`

`zlink_spot_handler(...)`는 routed message를 직접 callback으로 전달한다.
하지만 같은 기능은 이미 `ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE`과
`zlink_spot_recv_part(...)` 계열 drain으로 처리할 수 있다. 두 API는 동시에 등록할 수
없고, 같은 기능을 서로 다른 실행 모델로 제공하므로 사용자가 어느 모델을 선택해야
하는지 헷갈린다.

`zlink_spot_actor_lifecycle_handler(...)`는 Actor join/leave 상태 변화를 직접
callback으로 전달한다. 이 기능은 현재 dispatch event 모델에 대체 API가 없기 때문에
단순 제거만 하면 모든 바인딩이 lifecycle 의미를 각자 재구성해야 한다. 모든 바인딩의
의미를 맞추려면 Actor lifecycle도 dispatch event와 명시적 drain API로 제공하는 편이
낫다.

## 목표

1. SPOT data-plane receive와 Actor lifecycle 알림을 dispatch event 모델로 통일한다.
2. direct callback으로 routed message나 lifecycle event payload를 전달하지 않는다.
3. Actor lifecycle 상태 전이는 core가 단일 기준으로 정의하고, 모든 바인딩은 같은
   event와 drain API를 노출한다.
4. `zlink_send_ready_handler(...)`는 이번 변경 대상에서 제외한다. send-ready는 receive
   drain 모델이 아니라 pending send retry를 깨우는 readiness 알림이다.

## 비목표

- `zlink_send_ready_handler(...)` 제거
- Actor join admission 요청 API 제거
- Actor message receive API 제거
- framework가 제공하는 application lifecycle helper의 모양 확정
- 기존 poller API 재설계

## 변경 대상 요약

| 구분 | 현재 API | 변경 |
|------|----------|------|
| routed receive direct callback | `zlink_spot_handler(...)` | 제거 |
| routed receive dispatch | `ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE` | 유지 |
| routed receive drain | `zlink_spot_recv_part(...)` | C 저수준 helper로 유지 |
| Actor lifecycle direct callback | `zlink_spot_actor_lifecycle_handler(...)` | 제거 |
| Actor lifecycle dispatch | 없음 | `ACTOR_LIFECYCLE_READABLE` 추가 |
| Actor lifecycle drain | 없음 | `zlink_spot_recv_actor_lifecycle(...)` 추가 |
| send-ready callback | `zlink_send_ready_handler(...)` | 유지 |
| routed consume-forward helper | `zlink_spot_forward_routed(...)` | 제거 |
| DEALER request sequence helper | `zlink_dealer_request_frame_part(...)` | 제거 |
| DEALER reply helper | `zlink_dealer_reply_part(...)` | 제거 |
| SPOT subscription event receive | `zlink_spot_subscription_event_recv(...)` | `zlink_spot_recv_subscription_event(...)`로 이름 변경 |
| C API 조회 이름 | `*_snapshot`, `*_query` 혼재 | resource 중심 조회 이름으로 정리 |

## 새 C API 초안

### Actor lifecycle event kind

Actor lifecycle event는 join과 leave를 같은 receive API로 drain한다. dispatch event는
"읽을 수 있음"만 알려주고, 실제 event kind는 drain 결과에 들어간다.

기존 `zlink_spot_actor_lifecycle_info_t`는 상태 전이 정보를 이미 담고 있다. 새 event
구조체는 event kind와 기존 info를 함께 묶는다. event kind와 payload 타입 이름은
분리한다.

```c
typedef enum zlink_spot_actor_lifecycle_event_kind_t
{
    ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED = 1,
    ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT = 2
} zlink_spot_actor_lifecycle_event_kind_t;

typedef struct zlink_spot_actor_lifecycle_event_t
{
    zlink_spot_actor_lifecycle_event_kind_t kind;
    zlink_spot_actor_lifecycle_info_t info;
} zlink_spot_actor_lifecycle_event_t;
```

`kind`와 event payload 타입 이름이 분리되어 C API 사용자가 타입을 읽기 쉽다.
`kind`는 `info`에 들어 있는 상태 전이를 명시적으로 분류한 편의 필드이며, 항상
`info`와 같은 전이를 가리켜야 한다. 구현에서 둘이 모순되는 값을 만들면 `kind`가
우선하는 것이 아니라 lifecycle event 생성 로직의 버그로 본다.

### Dispatch event 추가

```c
typedef enum zlink_spot_dispatch_event_t
{
    ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE = 1,
    ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE = 2,
    ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE = 3,
    ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE = 4,
    ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE = 5,
    ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE = 6,
    ZLINK_SPOT_DISPATCH_EVENT_ACTOR_LIFECYCLE_READABLE = 7
} zlink_spot_dispatch_event_t;
```

위 예시는 현재 enum에 값 7 하나를 추가하는 변경을 보여준다.
`ACTOR_LIFECYCLE_READABLE`의 `subject_kind`는 `ZLINK_SPOT_DISPATCH_SUBJECT_SPOT`을
사용한다. lifecycle event는 특정 Actor queue를 drain하는 것이 아니라 해당 Spot의
lifecycle event queue를 drain하는 동작이기 때문이다.
dispatch event의 `subject` 값은 lifecycle event에서 의미가 없다. core는 `subject`를
`NULL`로 설정하고, 바인딩은 이 event에서 `subject`를 actor handle로 해석하면 안 된다.

### Actor lifecycle receive

```c
ZLINK_EXPORT zlink_recv_result_t zlink_spot_recv_actor_lifecycle (
  void *spot_,
  zlink_spot_actor_lifecycle_event_t *event_out_,
  zlink_recv_flags_t flags_);
```

계약은 다음과 같다.

- `spot_ == NULL` 또는 `event_out_ == NULL`이면 `ZLINK_RECV_INVALID_HANDLE`을
  반환한다. 현재 `zlink_recv_result_t`에는 잘못된 인자를 따로 표현하는 result bucket이
  없으므로, NULL output buffer도 recv 계약에서는 invalid handle 계열로 처리한다.
- 성공하면 `event_out_`에 lifecycle event 한 개를 복사한다.
- `ZLINK_DONTWAIT`이고 event가 없으면 `ZLINK_RECV_NO_DATA`와 `EAGAIN`을 반환한다.
- dispatch handler 안에서는 `ZLINK_DONTWAIT`으로 반복 drain하는 사용을 권장한다.
- 반환된 구조체는 payload message 소유권을 갖지 않는다. 별도 close 함수가 필요 없다.

## 제거할 C API

### `zlink_spot_handler(...)`

아래 API는 제거 대상이다.

```c
typedef void (*zlink_spot_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  const zlink_routing_id_t *spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);

ZLINK_EXPORT zlink_handler_result_t zlink_spot_handler (
  void *spot_, zlink_spot_handler_fn handler_, void *userdata_);
```

대체 경로는 다음과 같다.

1. `zlink_spot_dispatch_event_handler(...)`를 등록한다.
2. `ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE`을 받는다.
3. C API 사용자는 `zlink_spot_recv_part(...)`로 drain한다.
4. 바인딩 public API는 part helper를 직접 노출하지 않고 `Received` 또는 언어별 동등한
   aggregate routed receive 결과를 제공해야 한다. 이 aggregate receive 표면이 없는
   바인딩은 direct callback 제거 전에 먼저 해당 표면을 추가한다.
5. `request_seq_ != 0`이면 SPOT reply 경로로 응답한다. source가 SPOT이면
   `zlink_spot_reply_spot_part(...)`, source가 ROUTER이면
   `zlink_spot_reply_router_part(...)`를 사용한다. 바인딩은 이를
   `received.reply()` 같은 request context 기반 API로 감싼다.

### `zlink_spot_actor_lifecycle_handler(...)`

아래 API는 제거 대상이다.

```c
typedef void (*zlink_spot_actor_lifecycle_handler_fn) (
  void *spot_,
  const zlink_spot_actor_lifecycle_info_t *info_,
  void *userdata_);

ZLINK_EXPORT zlink_handler_result_t zlink_spot_actor_lifecycle_handler (
  void *spot_,
  zlink_spot_actor_lifecycle_handler_fn on_join_,
  zlink_spot_actor_lifecycle_handler_fn on_leave_,
  void *userdata_);
```

대체 경로는 다음과 같다.

1. `zlink_spot_dispatch_event_handler(...)`를 등록한다.
2. `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_LIFECYCLE_READABLE`을 받는다.
3. `zlink_spot_recv_actor_lifecycle(..., ZLINK_DONTWAIT)`로 lifecycle event를 drain한다.
4. `event.kind`가 `JOINED`인지 `LEFT`인지 보고 처리한다.

### `zlink_spot_forward_routed(...)`

아래 API는 제거 대상이다.

```c
typedef struct zlink_spot_forward_result_t
{
    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
    uint64_t request_seq;
    uint32_t has_request_seq;
    size_t part_count;
    size_t payload_bytes;
} zlink_spot_forward_result_t;

ZLINK_EXPORT zlink_submit_result_t zlink_spot_forward_routed (
  void *spot_,
  zlink_recv_flags_t recv_flags_,
  zlink_send_flags_t send_flags_,
  zlink_spot_forward_result_t *result_out_);
```

이 함수는 SPOT routed message를 하나 소비한 뒤, payload를 caller에게 노출하지 않고
원래 source Spot으로 그대로 다시 전송한다. request sequence가 있는 routed request는
forward하지 못하고, payload 검사나 수정도 할 수 없다. 일반 application API라기보다
특정 바인딩 perf echo 경로를 빠르게 만들기 위한 consume-forward fast path에 가깝다.

public C API에서 이 helper를 유지하면 바인딩 표준 public surface에 좁은 성능 전용 기능이
섞인다. 성능 측정은 public API의 실제 비용을 드러내야 하므로, 이 helper로 바인딩 overhead를
숨기지 않는다.

대체 경로는 다음과 같다.

1. C API에서는 dispatch event 뒤 `zlink_spot_recv_part(...)`로 drain하고, 바인딩에서는
   언어별 aggregate receive API로 받는다.
2. payload를 그대로 되돌려야 하는 sample/perf도 public send API로 다시 보낸다.
3. payload를 검사하거나 수정해야 하는 caller는 기존 receive 결과와 send builder를 조합한다.
4. perf runner는 이 helper 없이 측정하고, 수치 하락이 있으면 public API overhead로 기록한다.

### `zlink_dealer_request_frame_part(...)`

아래 API는 제거 대상이다.

```c
ZLINK_EXPORT zlink_submit_result_t zlink_dealer_request_frame_part (
  void *dealer_,
  uint64_t request_seq_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);
```

이 함수는 caller가 DEALER request sequence를 직접 주입해서 request frame을 전송하게 한다.
하지만 DEALER request sequence는 core가 생성하고 추적해야 하는 내부 request/reply state다.
public C API가 `request_seq_` 주입 경로를 열어 두면 바인딩과 framework가 core 내부
sequencing 규칙을 알아야 한다.

대체 경로는 `zlink_dealer_request_part(...)`다. 바인딩은 request builder 또는 언어별
request operation에서 payload와 timeout, reply callback만 전달하고, request sequence 생성과
frame encoding은 core에 맡긴다.

.NET binding이나 framework 내부에 남아 있는 `RequestFrame(...)` 같은 직접 sequence 주입
wrapper도 제거한다. 비동기 completion이 필요하면 `zlink_dealer_request_part(...)`의
reply callback을 언어별 task, future, promise로 연결한다.

### `zlink_dealer_reply_part(...)`

아래 API는 제거 대상이다.

```c
ZLINK_EXPORT zlink_submit_result_t zlink_dealer_reply_part (
  void *dealer_,
  uint64_t request_token_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_);
```

DEALER는 ROUTER처럼 특정 peer routing id를 선택해 응답을 보낼 수 없다. 따라서 DEALER가
`request_token_`만으로 reply를 보낸다는 public 의미는 잘못된 모델을 만든다. DEALER는
request를 시작하고 reply를 받을 수 있지만, ROUTER나 SPOT routed request처럼 특정 source에
reply하는 주체가 아니다.

대체 경로는 없다. DEALER에서 받은 reply는 caller가 소비하고, 새로운 요청이 필요하면
`zlink_dealer_request_part(...)`로 별도 request를 시작한다.
구현 전에는 sample, perf, framework, 각 바인딩 runtime에서
`zlink_dealer_reply_part(...)` 직접 사용처가 없는지 확인한다. 사용처가 있다면
DEALER가 reply 주체인 모델로 남기지 않고 ROUTER/SPOT reply context 또는 별도 DEALER
request로 고친다.

## SPOT subscription event receive 이름 정리

`zlink_spot_subscribe_part(...)`와 `zlink_spot_subscription_event_recv(...)`는 역할이
다르다. 전자는 publish된 topic payload를 part 단위로 받는 data-plane receive이고, 후자는
topic subscribe/unsubscribe 상태 변화를 받는 control-plane event receive다.

현재 이름은 역할이 겹치지는 않지만, `subscription_event_recv`처럼 동사가 뒤에 있어 다른
receive API와 축이 맞지 않는다. 이 초안은 receive 동사를 앞에 둔다.

| 현재 API | 변경 뒤 API | 변경 방식 |
|----------|-------------|-----------|
| `zlink_spot_subscription_event_recv(spot, source_rid_out, subscribed_out, topic_id_buf, topic_id_capacity, topic_id_len_out, flags)` | `zlink_spot_recv_subscription_event(spot, source_rid_out, subscribed_out, topic_id_buf, topic_id_capacity, topic_id_len_out, flags)` | 이름 변경 |

`zlink_spot_subscribe_part(...)`는 payload part receive 함수이므로 이번 이름 변경 대상이 아니다.
바인딩 public API는 part 단위 함수를 직접 노출하지 않고, 언어별 aggregate receive 결과로
감싼다.

## C API 조회 이름 정리

SPOT node, Registry, Monitor 조회 API는 현재 `snapshot` 접미어와 `query` 접미어가
섞여 있다. 대부분의 함수는 호출 시점의 값을 caller buffer에 복사한다는 같은 계약을
가진다. 이 의미를 모든 함수명에 `_snapshot`으로 반복하면 이름이 길어지고, `query`와도
축이 섞인다.

이 초안은 함수명에서는 resource 이름을 우선하고, "호출 시점 복사본" 의미는 함수 설명과
`entries/count` 계약으로 명시한다. filter가 필요한 목록 조회는 별도 `*_query` 함수로
분리하지 않고, 같은 resource 함수에 `filter == NULL`이면 전체 조회라는 규칙을 둔다.

### SPOT 함수 이름 변경 매핑

아래 표는 현재 공개 이름과 변경 뒤 이름을 1:1로 보여준다. `peers`만 현재
전체 조회와 조건 조회가 두 함수로 갈라져 있으므로, 새 API에서는 하나의 함수로 합친다.

| 현재 API | 변경 뒤 API | 변경 방식 |
|----------|-------------|-----------|
| `zlink_spot_node_status_snapshot(node, out)` | `zlink_spot_node_status(node, out)` | 이름 변경 |
| `zlink_spot_node_peers_snapshot(node, entries, count)` | `zlink_spot_node_peers(node, NULL, entries, count)` | `peers`로 통합 |
| `zlink_spot_node_peers_query(node, filter, entries, count)` | `zlink_spot_node_peers(node, filter, entries, count)` | `peers`로 통합 |
| `zlink_spot_node_subjects_snapshot(node, filter, entries, count)` | `zlink_spot_node_subjects(node, filter, entries, count)` | 이름 변경 |
| `zlink_spot_node_internal_sockets_snapshot(node, filter, entries, count)` | `zlink_spot_node_internal_sockets(node, filter, entries, count)` | 이름 변경 |
| `zlink_spot_node_spots_snapshot(node, entries, count)` | `zlink_spot_node_spots(node, entries, count)` | 이름 변경 |
| `zlink_spot_node_actors_snapshot(node, entries, count)` | `zlink_spot_node_actors(node, entries, count)` | 이름 변경 |
| `zlink_spot_actors_snapshot(spot, entries, count)` | `zlink_spot_actors(spot, entries, count)` | 이름 변경 |

### Registry 함수 이름 변경 매핑

Registry도 SPOT node와 같은 조회 계약을 가진다. 상태 조회는 resource 이름만 남기고,
목록 조회는 filter 인자를 같은 함수에 둔다.

| 현재 API | 변경 뒤 API | 변경 방식 |
|----------|-------------|-----------|
| `zlink_registry_status_snapshot(registry, out)` | `zlink_registry_status(registry, out)` | 이름 변경 |
| `zlink_registry_service_summary_snapshot(registry, filter, entries, count)` | `zlink_registry_service_summary(registry, filter, entries, count)` | 이름 변경 |
| `zlink_registry_topology_snapshot(registry, entries, count)` | `zlink_registry_topology(registry, NULL, entries, count)` | `topology`로 통합 |
| `zlink_registry_topology_query(registry, filter, entries, count)` | `zlink_registry_topology(registry, filter, entries, count)` | `topology`로 통합 |
| `zlink_registry_query_snapshot(client, filter, entries, count)` | `zlink_registry_query_client_topology(client, filter, entries, count)` | query client resource를 이름에 명시 |
| `zlink_registry_query_destroy(client_p)` | `zlink_registry_query_client_destroy(client_p)` | query client handle 이름과 맞춤 |

`zlink_registry_query_client_new(...)`와 `zlink_registry_query_client_connect(...)`는 이미
query client handle 이름을 담고 있으므로 유지한다.

### Monitor 함수와 타입 이름 변경 매핑

Monitor는 단일 현재 상태를 읽는 API다. 함수명과 타입명은 `snapshot`보다 `status`가
사용자에게 더 직접적이다.

| 현재 이름 | 변경 뒤 이름 | 변경 방식 |
|-----------|--------------|-----------|
| `zlink_monitor_snapshot(monitor, out)` | `zlink_monitor_status(monitor, out)` | 이름 변경 |
| `zlink_monitor_snapshot_t` | `zlink_monitor_status_t` | 타입 이름 변경 |
| `zlink_monitor_snapshot_detail_mask_t` | `zlink_monitor_status_detail_mask_t` | 타입 이름 변경 |
| `zlink_monitor_snapshot_detail_flag_e` | `zlink_monitor_status_detail_flag_e` | enum 타입 이름 변경 |
| `ZLINK_MONITOR_STATUS_DETAIL_SND_PENDING_MSGS` | `ZLINK_MONITOR_STATUS_DETAIL_SND_PENDING_MSGS` | enum 값 이름 변경 |
| `ZLINK_MONITOR_STATUS_DETAIL_RCV_PENDING_MSGS` | `ZLINK_MONITOR_STATUS_DETAIL_RCV_PENDING_MSGS` | enum 값 이름 변경 |
| `ZLINK_MONITOR_STATUS_DETAIL_AUTO_HWM_BUDGET` | `ZLINK_MONITOR_STATUS_DETAIL_AUTO_HWM_BUDGET` | enum 값 이름 변경 |
| `ZLINK_MONITOR_STATUS_DETAIL_AUTO_HWM_BUFFERS` | `ZLINK_MONITOR_STATUS_DETAIL_AUTO_HWM_BUFFERS` | enum 값 이름 변경 |

### SPOT 타입 이름 변경 매핑

함수 이름에서 `snapshot`을 제거하면 함수 전용 filter/entry 타입에도 같은 기준을 적용한다.
호출 시점 복사본이라는 의미는 타입 이름이 아니라 조회 계약에 둔다.

| 현재 타입 | 변경 뒤 타입 | 비고 |
|-----------|--------------|------|
| `zlink_spot_node_socket_snapshot_filter_t` | `zlink_spot_node_socket_filter_t` | internal socket 목록 조회 filter |
| `zlink_spot_node_socket_snapshot_entry_t` | `zlink_spot_node_socket_entry_t` | internal socket 목록 조회 entry |
| `zlink_spot_node_socket_snapshot_entry_t.snapshot` | `zlink_spot_node_socket_entry_t.monitor_status` | internal socket monitor 상태 필드 |

### 새 SPOT C API 초안

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_status (
  void *node_,
  zlink_spot_node_status_t *out_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_peers (
  void *node_,
  const zlink_spot_node_peer_filter_t *filter_,
  zlink_spot_node_peer_entry_t *entries_,
  size_t *count_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_subjects (
  void *node_,
  const zlink_spot_node_subject_filter_t *filter_,
  zlink_spot_node_subject_entry_t *entries_,
  size_t *count_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_internal_sockets (
  void *node_,
  const zlink_spot_node_socket_filter_t *filter_,
  zlink_spot_node_socket_entry_t *entries_,
  size_t *count_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spots (
  void *node_,
  zlink_spot_node_spot_entry_t *entries_,
  size_t *count_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_actors (
  void *node_,
  zlink_spot_node_actor_entry_t *entries_,
  size_t *count_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_actors (
  void *spot_,
  zlink_actor_ref_t *entries_,
  size_t *count_);
```

### 조회 계약

- `entries_ == NULL`이고 `count_ != NULL`이면 필요한 entry 개수를 `*count_`에 쓴다.
- `entries_ != NULL`이면 `*count_`는 caller가 제공한 entry capacity다.
- 성공하면 `*count_`는 실제로 쓴 entry 수 또는 필요한 entry 수를 나타낸다. 정확한
  over-capacity 의미는 기존 조회 API와 동일하게 유지한다.
- filter 인자가 있는 함수에서 `filter_ == NULL`이면 전체 조회다.
- 반환된 entry는 호출 시점의 복사본이다. caller가 별도 close를 호출하지 않는다.
- 함수 이름에 `snapshot`이 없더라도 live view나 borrowed pointer를 뜻하지 않는다.

### 새 Registry C API 초안

```c
ZLINK_EXPORT zlink_config_result_t zlink_registry_status (
  void *registry_,
  zlink_registry_status_t *out_);

ZLINK_EXPORT zlink_config_result_t zlink_registry_service_summary (
  void *registry_,
  const zlink_registry_service_summary_filter_t *filter_,
  zlink_registry_service_summary_entry_t *entries_,
  size_t *count_);

ZLINK_EXPORT zlink_config_result_t zlink_registry_topology (
  void *registry_,
  const zlink_registry_topology_filter_t *filter_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_);

ZLINK_EXPORT zlink_config_result_t zlink_registry_query_client_topology (
  void *client_,
  const zlink_registry_topology_filter_t *filter_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_);

ZLINK_EXPORT zlink_close_result_t zlink_registry_query_client_destroy (
  void **client_p_);
```

### 새 Monitor C API 초안

```c
typedef struct zlink_monitor_status_t
{
  /* zlink_monitor_status_t의 기존 필드를 그대로 옮기고,
     detail_flags 타입은 zlink_monitor_status_detail_mask_t로 바꾼다. */
} zlink_monitor_status_t;

ZLINK_EXPORT zlink_config_result_t zlink_monitor_status (
  void *monitor_,
  zlink_monitor_status_t *out_);
```

## 실행 모델

SPOT dispatch handler는 readiness 알림만 전달한다. 실제 data나 lifecycle payload는
명시적 receive 함수로 꺼낸다.

```text
+-----------------------------+
| Core state transition       |
+-----------------------------+
              |
              v
+-----------------------------+
| Event queue / ready signal  |
+-----------------------------+
              |
              v
+-----------------------------+
| Dispatch event callback     |
+-----------------------------+
              |
              v
+-----------------------------+
| Explicit nonblocking drain  |
+-----------------------------+
```

이 구조에서 callback은 application handler를 직접 실행하는 수단이 아니라, 어떤 queue를
drain해야 하는지 알려주는 신호다. 바인딩은 이 신호를 언어별 실행 context나 serial queue로
넘긴 뒤 drain할 수 있다.

## Actor lifecycle queue 정책

Actor lifecycle queue는 Spot별 queue다. queue 항목은 `JOINED` 또는 `LEFT` event와
`zlink_spot_actor_lifecycle_info_t` 값을 담는다.

정책 후보는 두 가지다.

| 대안 | 설명 | 장점 | 단점 |
|------|------|------|------|
| A. core가 lifecycle event를 항상 queue에 넣음 | dispatch handler 등록 여부와 무관하게 queue를 유지 | event 손실이 적음 | handler가 없는 Spot에서 queue가 계속 쌓일 수 있음 |
| B. dispatch handler가 있는 Spot에만 queue | lifecycle event를 받을 실행 context가 있을 때만 저장 | 메모리 정책이 단순함 | 나중에 handler를 붙여도 과거 event는 받을 수 없음 |

이 초안은 **B안**을 선택한다. SPOT dispatch event는 readiness 모델이고, dispatch handler가
없는 Spot은 lifecycle event를 drain할 주체가 없다. 과거 lifecycle history가 필요하면
상태 또는 목록 조회 API를 사용한다.

따라서 lifecycle event를 받으려면 첫 Actor join/leave가 발생하기 전에
`zlink_spot_dispatch_event_handler(...)`가 등록되어 있어야 한다. 이 API는 event log가
아니므로 handler 등록 전의 join/leave event를 나중에 재생하지 않는다. 현재 상태 확인은
조회 API로 가능하지만, "언제 join되었는지" 같은 일회성 event history는 복원하지 않는다.
framework가 raw lifecycle event를 application lifecycle source로 삼는 경우에는 Spot
초기화 단계에서 dispatch handler 등록을 Actor join 시작보다 먼저 끝내야 한다.

queue overflow 정책은 기존 Actor queue나 request queue의 정책과 맞춰야 한다. 첫 구현은
bounded queue option을 추가하지 않고, 기존 runtime queue 정책을 재사용한다. overflow가
발생할 수 있는 구조라면 core는 lifecycle event를 조용히 버리지 말고 진단 가능한 result나
runtime event를 남겨야 한다.

## Dispatch 우선순위

기존 dispatch event 처리 순서는 구현 상세에 가깝지만, starvation을 피하려면 lifecycle
event도 기존 readable event와 함께 공정하게 dispatch되어야 한다.

권장 우선순위는 다음과 같다.

1. `SUBSCRIBE_READABLE`
2. `ROUTED_READABLE`
3. `ACTOR_JOIN_READABLE`
4. `ACTOR_LIFECYCLE_READABLE`
5. `ACTOR_READABLE`
6. `CHANNEL_REPLY_READABLE`
7. `TIMER_READABLE`

이 순서는 공개 의미가 아니라 첫 구현 기준이다. 정식 spec에 반영할 때는 순서를 보장할지
또는 보장하지 않을지 별도로 결정한다.

## 바인딩 영향

모든 바인딩은 아래 표면으로 의미를 맞춘다.

- routed receive direct callback을 public API에서 제거한다.
- Actor lifecycle direct callback을 public API에서 제거한다.
- `DispatchEvent.ActorLifecycleReadable` 또는 언어 관례에 맞는 값을 추가한다.
- `RecvActorLifecycle(...)` 또는 언어 관례에 맞는 drain API를 추가한다.
- `SendReady` callback은 유지한다. 이 callback은 dispatch receive 모델과 다르며, pending
  send retry를 깨우는 readiness 신호다.

.NET 기준 예시는 다음과 같다.

```csharp
spot.SetDispatchHandler(info =>
{
    if (info.Event == SpotDispatchEvent.ActorLifecycleReadable)
    {
        while (spot.RecvActorLifecycle(out var ev, RecvFlags.DontWait))
        {
            if (ev.Kind == ActorLifecycleEventKind.Joined)
            {
                // joined
            }
            else if (ev.Kind == ActorLifecycleEventKind.Left)
            {
                // left
            }
        }
    }
});
```

## 기존 framework와의 관계

.NET framework는 이미 native routed direct callback에 의존하지 않고 dispatch event를
받은 뒤 serial execution queue로 넘긴다. Actor lifecycle도 native lifecycle callback을
핵심 경로로 쓰지 않고 framework join/leave 흐름에서 application lifecycle handler를
호출한다.

core가 lifecycle readable event를 제공하면 framework는 두 선택지를 가진다.

1. 기존 framework lifecycle coordinator를 유지하고, raw binding 표면만 새 event를 노출한다.
2. framework lifecycle source를 core lifecycle event로 바꾼다.

첫 구현에서는 1번이 안전하다. framework 동작을 바꾸지 않고도 모든 raw binding의 C API
기준 의미를 맞출 수 있다. 이후 framework와 raw binding lifecycle 의미를 완전히 하나로
맞출 필요가 있으면 2번을 별도로 검토한다.

2번을 선택하는 후속 작업에서는 framework 초기화 순서가 dispatch handler 등록을 먼저
완료한 뒤 Actor join을 시작한다는 점을 테스트로 고정해야 한다. 이 순서를 보장하지 못하면
core가 B안 queue 정책을 따를 때 join/leave event를 놓칠 수 있다.

## 마이그레이션

이번 정리는 호환성 없는 변경으로 진행한다. 제거 대상 API는 deprecated 경로를 두지 않고
core 공개 헤더와 export에서 바로 제거한다. 바인딩은 호환 wrapper를 추가하지 않고 새
표면으로 맞춘다.

구현 순서는 다음과 같다.

1. 새 dispatch event와 lifecycle recv API를 추가한다.
2. 제거 대상 direct callback, consume-forward, DEALER helper API를 public surface에서
   제거한다.
3. 조회 API와 subscription event receive 이름을 새 이름으로 바꾼다.
4. 모든 bindings 문서와 public API를 새 core 계약에 맞춘다.

## 테스트 요구사항

| ID | 검증 |
|----|------|
| SDL-01 | `ROUTED_READABLE` dispatch 뒤 routed message를 drain할 수 있다 |
| SDL-02 | `zlink_spot_handler(...)` 없이 routed request reply가 동작한다 |
| SDL-03 | Actor join 성공 뒤 `ACTOR_LIFECYCLE_READABLE`이 발생한다 |
| SDL-04 | Actor leave 성공 뒤 `ACTOR_LIFECYCLE_READABLE`이 발생한다 |
| SDL-05 | lifecycle event drain 결과가 joined/left kind와 기존 lifecycle info를 보존한다 |
| SDL-06 | `ZLINK_DONTWAIT` drain에서 event가 없으면 `ZLINK_RECV_NO_DATA`가 반환된다 |
| SDL-07 | dispatch handler가 없는 Spot은 lifecycle event를 누적하지 않는다 |
| SDL-08 | `zlink_send_ready_handler(...)` 동작은 변경되지 않는다 |
| SDL-09 | direct callback 제거 뒤 모든 바인딩 public surface가 dispatch event 모델로 맞춰진다 |
| SDL-10 | routed consume-forward helper 없이 public receive/send API로 relay 동작을 구성할 수 있다 |
| SDL-11 | lifecycle event를 소비하는 binding/framework는 dispatch handler 등록을 Actor join보다 먼저 끝낸다 |
| SDL-12 | SPOT routed request reply는 DEALER reply가 아니라 SPOT reply context로만 제공된다 |

## 회귀 테스트 계획

구현은 먼저 core 회귀 테스트를 추가한 뒤 C API를 고친다. 테스트가 새 계약을 명확히
고정해야 바인딩 작업에서 언어별 해석이 갈라지지 않는다.

### core C/C++ 테스트

아래 테스트는 `core/tests`의 기존 SPOT service mode, dispatch event, Actor 테스트에
나누어 추가한다.

| ID | 테스트 |
|----|--------|
| CORE-SDL-01 | `zlink_spot_handler(...)`와 `zlink_spot_handler_fn` public declaration/export 제거 확인 |
| CORE-SDL-02 | `zlink_spot_actor_lifecycle_handler(...)`와 `zlink_spot_actor_lifecycle_handler_fn` public declaration/export 제거 확인 |
| CORE-SDL-03 | `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_LIFECYCLE_READABLE` enum 값 확인 |
| CORE-SDL-04 | Actor join 성공 뒤 lifecycle readable event가 발생하고 `zlink_spot_recv_actor_lifecycle(..., ZLINK_DONTWAIT)`로 `JOINED` event를 받는다 |
| CORE-SDL-05 | Actor leave 성공 뒤 lifecycle readable event가 발생하고 `LEFT` event를 받는다 |
| CORE-SDL-06 | lifecycle event payload가 기존 `zlink_spot_actor_lifecycle_info_t` 필드를 손실 없이 보존한다 |
| CORE-SDL-07 | lifecycle queue가 비었을 때 nonblocking receive는 `ZLINK_RECV_NO_DATA`와 `EAGAIN`을 반환한다 |
| CORE-SDL-08 | dispatch handler가 없는 Spot은 lifecycle event를 누적하지 않고, 나중에 handler를 붙여도 과거 event를 drain하지 않는다 |
| CORE-SDL-09 | routed receive는 `zlink_spot_handler(...)` 없이 dispatch event + `zlink_spot_recv_part(...)` drain으로 동작한다 |
| CORE-SDL-10 | `zlink_send_ready_handler(...)` 기존 테스트가 그대로 통과한다 |
| CORE-SDL-11 | `*_snapshot`/`*_query` SPOT 조회 함수 public declaration과 export가 제거된다 |
| CORE-SDL-12 | 새 SPOT 조회 함수가 기존 조회 값과 같은 값을 반환하고, `entries == NULL` count 조회가 동작한다 |
| CORE-SDL-13 | `zlink_spot_node_peers(node, NULL, entries, count)`가 기존 전체 peer 조회와 같은 결과를 반환한다 |
| CORE-SDL-14 | `zlink_spot_node_peers(node, filter, entries, count)`가 기존 조건 peer 조회와 같은 결과를 반환한다 |
| CORE-SDL-15 | Registry `*_snapshot`/`*_query` 조회 함수 public declaration과 export가 제거된다 |
| CORE-SDL-16 | `zlink_registry_topology(registry, NULL, entries, count)`가 기존 전체 topology 조회와 같은 결과를 반환한다 |
| CORE-SDL-17 | `zlink_registry_topology(registry, filter, entries, count)`가 기존 조건 topology 조회와 같은 결과를 반환한다 |
| CORE-SDL-18 | `zlink_registry_query_client_topology(client, filter, entries, count)`가 기존 query client 조회와 같은 결과를 반환한다 |
| CORE-SDL-19 | `zlink_monitor_status(monitor, out)`가 기존 monitor 현재 상태 조회와 같은 값을 반환하고, status detail 타입과 enum 값 이름이 새 이름으로 노출된다 |
| CORE-SDL-20 | `zlink_spot_forward_routed(...)`와 `zlink_spot_forward_result_t` public declaration과 export가 제거된다 |
| CORE-SDL-21 | routed send echo/relay 회귀 테스트는 public receive/send API 조합으로 통과한다 |
| CORE-SDL-22 | `zlink_dealer_request_frame_part(...)` public declaration과 export가 제거된다 |
| CORE-SDL-23 | `zlink_dealer_reply_part(...)` public declaration과 export가 제거된다 |
| CORE-SDL-24 | DEALER request/reply 회귀 테스트는 `zlink_dealer_request_part(...)`와 core-managed request sequence로 통과한다 |
| CORE-SDL-25 | `zlink_spot_subscription_event_recv(...)` declaration과 export가 `zlink_spot_recv_subscription_event(...)`로 변경된다 |
| CORE-SDL-26 | 새 subscription event receive API가 topic subscribe/unsubscribe event를 기존과 같은 값으로 반환한다 |
| CORE-SDL-27 | lifecycle dispatch event의 `subject_kind`는 `SPOT`, `subject`는 `NULL`로 전달된다 |
| CORE-SDL-28 | dispatch handler 등록 전에 발생한 lifecycle event는 나중에 drain되지 않는다 |
| CORE-SDL-29 | `zlink_spot_recv_actor_lifecycle(...)`의 `kind`와 `info`가 같은 상태 전이를 가리킨다 |

### 바인딩 회귀 테스트

모든 바인딩은 같은 의미를 검증한다. 언어별 테스트 이름은 다를 수 있지만 검증 항목은
같아야 한다.

| ID | 테스트 |
|----|--------|
| BIND-SDL-01 | public API에서 routed direct callback 등록 함수가 사라졌다 |
| BIND-SDL-02 | public API에서 actor lifecycle direct callback 등록 함수가 사라졌다 |
| BIND-SDL-03 | dispatch event enum에 actor lifecycle readable 값이 있다 |
| BIND-SDL-04 | actor join 뒤 dispatch handler에서 lifecycle event를 drain할 수 있다 |
| BIND-SDL-05 | actor leave 뒤 dispatch handler에서 lifecycle event를 drain할 수 있다 |
| BIND-SDL-06 | routed receive는 dispatch event + aggregate receive API로 처리된다 |
| BIND-SDL-07 | send-ready handler 또는 equivalent readiness API는 유지된다 |
| BIND-SDL-08 | SPOT node/Spot 조회 public API에서 `Snapshot`/`Query` 접미어가 사라지고 resource 이름으로 노출된다 |
| BIND-SDL-09 | Registry 조회 public API에서 `Snapshot`/`Query` 접미어가 사라지고 resource 이름으로 노출된다 |
| BIND-SDL-10 | Monitor 현재 상태 조회 public API는 `Snapshot` 대신 `Status` 계열 이름으로 노출된다 |
| BIND-SDL-11 | Go `Spot.ForwardRouted(...)` 같은 consume-forward public API가 제거된다 |
| BIND-SDL-12 | sample/perf는 consume-forward helper 없이 public receive/send API로 동작한다 |
| BIND-SDL-13 | DEALER public API에 request frame sequence 주입이나 reply API가 노출되지 않는다 |
| BIND-SDL-14 | SPOT subscription event receive API는 `RecvSubscriptionEvent` 또는 언어 관례에 맞는 receive-first 이름으로 노출된다 |
| BIND-SDL-15 | SPOT lifecycle drain API는 receive-first C API와 대응되는 이름으로 노출된다 |
| BIND-SDL-16 | direct callback 제거 전 각 바인딩에 aggregate routed receive API가 존재한다 |
| BIND-SDL-17 | binding native declaration과 required-symbol 목록에 제거된 C symbol이 남지 않는다 |

## 문서 수정 계획

구현 전에는 이 draft만 수정한다. 구현이 끝난 뒤에는 아래 순서로 정식 문서를 갱신한다.

1. `core/include/zlink_enum.h`, `core/include/zlink/socket.h`,
   `core/include/zlink/spot.h`, `core/include/zlink/registry.h`,
   `core/include/zlink/monitoring.h` 변경을 기준으로 C API 계약을 확정한다.
2. `doc/spec/`의 core C API 문서에 새 dispatch event, lifecycle receive, 제거된 direct
   callback API를 반영한다.
3. errno/result 문서에 `zlink_spot_recv_actor_lifecycle(...)`의 no-data와
   invalid-handle 결과를 추가한다.
4. core C API 문서의 SPOT node/Spot, Registry, Monitor 조회 함수 이름을 새 resource
   중심 이름으로 정리한다.
5. SPOT node/Spot과 Registry 목록 조회 함수 설명에는 호출 시점 복사본, caller buffer,
   two-pass count 조회, `filter == NULL` 전체 조회 규칙을 명시한다.
6. Monitor 현재 상태 조회 함수 설명에는 호출 시점 복사본이라는 의미를 계약에 명시하고,
   함수와 타입 이름에서는 `Status`를 사용한다.
7. `doc/spec/draft/binding-consume-forward-path.ko.md`는 정식 spec으로 승격하지 않고,
   perf 전용 fast path 후보가 기각되었음을 기록한다.
8. `doc/spec/bindings/README.md`에 SPOT receive 표준을
   dispatch event + explicit drain으로 정리한다.
9. 각 언어별 `doc/spec/bindings/*/README.md`에서 제거 함수와 새 lifecycle drain API를
   언어 관례에 맞게 반영한다.
10. 각 언어별 바인딩 문서에서 SPOT node/Spot, Registry, Monitor 조회 API 이름은
   `Snapshot`/`Query` 접미어 없이 resource 이름 또는 status 이름으로 설명한다.
11. 각 언어별 바인딩 문서에서 consume-forward helper가 public API가 아님을 반영하고,
   relay는 receive/send 조합으로 설명한다.
12. 각 언어별 바인딩 문서에서 DEALER는 request 시작과 reply receive만 제공하고,
   request sequence 직접 주입이나 reply API를 제공하지 않는다고 설명한다.
13. SPOT subscription event receive 문서는 payload receive인 subscribe와 구독 상태 event
   receive를 구분하고, receive-first 이름을 사용한다.
14. 각 언어별 binding native declaration, required-symbol 목록, generated wrapper 문서에
   제거된 C symbol과 예전 이름이 남지 않게 맞춘다.
15. guide 문서에는 내부 queue나 handler 제거 배경을 길게 넣지 않는다. 사용자가 알아야 하는
   사용 패턴만 예제로 설명한다.
16. internals 문서에는 lifecycle queue, dispatch event 발생 시점, dispatch handler 없는
   Spot의 event 보존 정책을 다이어그램과 함께 정리한다.

## 모든 바인딩 적용 계획

core C API 구현과 테스트가 끝난 뒤 바인딩을 순차 적용한다. 모든 바인딩은 같은 공개 의미를
가져야 하지만 언어별 이름과 비동기 표현은 관례를 따른다.

### 공통 적용 규칙

1. routed direct callback 등록 API를 제거한다.
2. actor lifecycle direct callback 등록 API를 제거한다.
3. dispatch event enum 또는 상수에 actor lifecycle readable을 추가한다.
4. actor lifecycle event 값 타입을 추가한다.
5. `RecvActorLifecycle(...)` 또는 언어 관례에 맞는 drain API를 추가한다.
6. routed receive는 public aggregate receive 결과로 노출한다. aggregate receive 표면이
   없는 바인딩은 direct callback 제거보다 먼저 추가한다.
7. examples, samples, perf, framework wrapper가 제거 API를 호출하지 않게 바꾼다.
8. send-ready readiness API는 제거하지 않는다.
9. SPOT node/Spot 조회 API는 언어 관례에 맞춘 resource 이름으로 노출하고,
   `Snapshot`/`Query` 접미어는 public API 이름에서 제거한다.
10. Registry 조회 API도 같은 규칙으로 노출한다.
11. Monitor 현재 상태 조회 API는 `Snapshot` 대신 `Status` 계열 이름으로 노출한다.
12. consume-forward helper는 public API에서 제거한다. Go `Spot.ForwardRouted(...)`도
   제거하고 perf는 public receive/send API 조합으로 다시 맞춘다.
13. DEALER request frame sequence 주입 API와 DEALER reply API는 public API에서 제거한다.
14. SPOT subscription event receive API 이름을 receive-first 형태로 맞춘다.
15. public surface test를 추가해 제거 API가 다시 노출되지 않게 한다.

### 적용 순서

1. C binding: core 공개 헤더와 가장 가까운 기준으로 먼저 맞춘다.
2. .NET binding: 현재 논의의 기준 구현으로 삼고 framework wrapper까지 같이 맞춘다.
3. Java binding: dispatch event와 lifecycle drain API를 public contract에 반영하고,
   sample/perf에서 direct callback을 제거한다.
4. C++ binding: RAII ownership과 enum naming을 맞춘다.
5. Node, Python, Go, Rust bindings: 각 언어의 async/event-loop 관례에 맞춰 drain API를
   추가하되 semantics는 동일하게 유지한다.
6. Go perf의 routed echo/relay 경로에서 `Spot.ForwardRouted(...)` 사용을 제거하고,
   public receive/send API 기준으로 측정한다.
7. DEALER request/reply sample과 perf가 request sequence 직접 주입 API 없이 동작하는지
   확인한다.
8. SPOT subscription event receive sample과 테스트를 새 이름으로 갱신한다.
9. 모든 bindings의 sample과 perf를 다시 빌드하거나 smoke 실행한다.

### core library 로컬 배포

core 구현이 끝나고 `core/build` 기준 runtime을 다시 만든 뒤, 바인딩 검증 전에 아래
스크립트로 로컬 core library를 각 바인딩 개발 경로에 배포한다.

```bash
cmake --build core/build
bindings/dev_sync_local_core_libs.sh
```

이 순서를 지키지 않으면 바인딩 테스트가 오래된 `libzlink`를 로드해서 새 symbol이나 enum
동작을 찾지 못할 수 있다. 바인딩 테스트 실패를 해석하기 전에 실제 로드된 runtime 경로와
빌드 시각을 확인한다.

## 구현 단계 체크리스트

1. core regression test를 먼저 추가한다.
2. C API enum, struct, function declaration을 추가한다.
3. direct callback API declaration과 구현을 제거한다.
4. Actor lifecycle queue와 dispatch event 발생 경로를 구현한다.
5. `zlink_spot_recv_actor_lifecycle(...)`를 구현한다.
6. SPOT node/Spot, Registry, Monitor 조회 API 이름을 변경한다.
7. `zlink_spot_forward_routed(...)`와 관련 result 타입을 public C API에서 제거한다.
8. Go `Spot.ForwardRouted(...)`와 perf 사용처를 제거한다.
9. `zlink_dealer_request_frame_part(...)`와 `zlink_dealer_reply_part(...)`를 public C API에서
   제거한다.
10. 바인딩과 framework에서 request sequence 직접 주입 경로를 제거하고 core-managed
   request API로 맞춘다.
11. `zlink_spot_subscription_event_recv(...)`를 `zlink_spot_recv_subscription_event(...)`로
   변경한다.
12. `cmake --build core/build`로 core runtime을 갱신한다.
13. `bindings/dev_sync_local_core_libs.sh`로 로컬 바인딩 runtime을 동기화한다.
14. `bindings/c/include`, `bindings/cpp/include`, `bindings/go/include`,
   `bindings/rust/include`에 복사된 공개 헤더가 core 헤더와 같은지 확인한다.
15. .NET, Java, Node, Python, Rust, C++, Go의 native declaration, symbol guard,
   generated wrapper 또는 FFI 목록을 새 C API 이름으로 갱신한다.
16. C binding 테스트를 실행한다.
17. 나머지 bindings와 sample/perf를 순차 적용하고 검증한다.
18. 정식 spec, bindings spec, guide, internals 문서를 구현 결과에 맞춰 갱신한다.

## 정식 반영 위치

구현이 끝난 뒤 아래 문서와 헤더를 함께 갱신한다.

- `core/include/zlink_enum.h`
- `core/include/zlink/socket.h`
- `core/include/zlink/spot.h`
- `core/include/zlink/registry.h`
- `core/include/zlink/monitoring.h`
- C API errno/result spec
- `doc/spec/bindings/README.md`
- 각 언어별 `doc/spec/bindings/*/README.md`
