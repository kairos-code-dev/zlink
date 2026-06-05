[English](./spot.md) | [한국어](./spot.ko.md)

[스펙 목차](https://github.com/kairos-code-dev/zlink/blob/main/README.ko.md) · [코어 목차](https://github.com/kairos-code-dev/zlink/blob/main/doc/README.ko.md) · [서비스 공통](./README.ko.md)

# SPOT

이 문서는 현재 공개 헤더 `core/include/zlink.h`에 들어 있는 SPOT 계약만 정리한다.
구현 전 설계나 개편 방향은 별도 draft 문서를 본다.

## 개요

SPOT 공개 표면은 두 핸들로 나뉜다.

- `SpotNode`
  SPOT 토폴로지, discovery 기반 peer 연결, 수동 peer 연결, channel 호출용
  `DEALER` 등록, 외부 publish ingress 등록을 관리한다.
- `Spot`
  `SpotNode` 위에 올라가는 데이터 평면 facade이다. 토픽 publish/subscribe,
  routed recv/reply, channel send/request를 제공한다.

`Spot`은 `SpotNode`를 빌려서 만든다. `Spot`을 destroy해도 backing `SpotNode`는
자동으로 destroy되지 않는다.

## 생성과 종료

```c
typedef enum zlink_spot_node_mode_t {
  ZLINK_SPOT_NODE_MODE_PUBSUB = 1,
  ZLINK_SPOT_NODE_MODE_ROUTED = 2,
  ZLINK_SPOT_NODE_MODE_ALL = 3
} zlink_spot_node_mode_t;

typedef struct zlink_spot_node_options_t {
  zlink_spot_node_mode_t mode;
} zlink_spot_node_options_t;

void *zlink_spot_node_new(
  void *ctx,
  const zlink_spot_node_options_t *options);
zlink_close_result_t zlink_spot_node_destroy(void **node_p);

void *zlink_spot_new(void *node);
zlink_close_result_t zlink_spot_destroy(void **spot_p);
```

- `zlink_spot_node_new()`는 새 SPOT node runtime을 만든다.
- `options == NULL`이면 `ZLINK_SPOT_NODE_MODE_ALL`로 동작한다.
- `options->mode == 0`도 `ZLINK_SPOT_NODE_MODE_ALL`로 동작한다.
- 잘못된 mode 값은 `NULL`과 `errno == EINVAL`로 실패한다.
- `PUBSUB` mode는 topic publish/subscribe만 켠다. routed API는
  `ENOTSUP`으로 실패한다.
- `ROUTED` mode는 routed request/reply와 direct routed send만 켠다.
  topic publish/subscribe API는 `ENOTSUP`으로 실패한다.
- `ALL` mode는 두 기능을 모두 켠다.
- `zlink_spot_new()`는 기존 `SpotNode`를 빌려 unified `Spot` facade를 만든다.
- `Spot`은 backing `SpotNode`의 mode를 그대로 물려받는다. 생성 뒤에는 mode를
  바꿀 수 없다.
- 꺼진 기능을 호출하면 내부 socket을 새로 만들지 않고 실패한다.
- `zlink_spot_destroy()`는 facade만 닫는다.
- `zlink_spot_destroy()`는 routed target lookup을 먼저 제거한 뒤 owned subject를
  닫는다. destroy 시점에 남아 있던 unread routed 메시지는 close 과정에서 버려질
  수 있으며, 호출자는 destroy 전에 unread를 끝까지 drain(큐에 쌓인 메시지를 꺼내 소비하는 행위)해야 할 의무를 지지
  않는다.
- `zlink_spot_node_destroy()`는 node와 내부 runtime 자원을 정리한다.
- discovery에 attach된 node는 보통 `zlink_discovery_destroy()` 흐름에서 함께 정리된다.

## Entry Spot

`SpotNode`가 생성되면 내부적으로 `Entry Spot`(진입 수신점, 새 Actor가 처음 배정되는 논리적 수신 지점) logical state도 함께 생성된다.
`Entry Spot`은 `SpotNode`가 소유하며, application이 제거할 수 없다. 새로
만들어진 모든 Actor는 생성 직후 반드시 `Entry Spot`에 속한다.

`Entry Spot`은 별도의 `Spot` dispatch context를 가진다. application은 이 context에
dispatch handler(메시지 수신 시 호출되는 콜백)를 등록해 새 Actor의 초기 메시지 처리, 인증, 대상 Spot 선택 같은
작업을 수행한다.

### Entry Spot handle

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_entry_spot(
  void *node_,
  void **spot_out_);
```

- `node_ == NULL`이면 `ZLINK_CONFIG_INVALID_HANDLE`로 실패하고 `errno`는 `EFAULT`다.
- `spot_out_ == NULL`이면 `ZLINK_CONFIG_INVALID_ARGUMENT`로 실패하고 `errno`는 `EINVAL`이다.
- 실패하면 `*spot_out_ = NULL`이다.
- 성공하면 `*spot_out_`에 새 Entry Spot facade handle을 반환한다.
- 반환된 facade는 `zlink_spot_dispatch_event_handler()` 등 기존 `Spot` API를 지원한다.
- `Entry Spot` logical state는 `SpotNode`가 소유한다. 반환된 facade handle은 application이
  소유하며 `zlink_spot_destroy()`로 닫아야 한다.
- `zlink_spot_destroy()`는 Entry Spot facade만 닫고 logical Entry Spot은 제거하지 않는다.
- 같은 node에서 이 API를 여러 번 호출하면 서로 다른 facade handle이 같은 logical
  Entry Spot을 가리킨다.

### Entry Spot routing id

`Entry Spot`은 일반 `Spot`처럼 routing id를 가진다.
기본값은 `SpotNode` 생성 시 자동 생성되는 random routing id다.
application이 고정 rid를 원하면 `zlink_spot_node_entry_spot()`으로 facade를 얻은 뒤
`zlink_set_routing_id(entry_spot, data, size)`를 호출한다.

Entry Spot rid 설정은 **configuration phase**에서만 허용한다.
configuration phase는 첫 Actor 생성, Discovery attach, SpotNode bind/connect,
Spot owner route publish, Actor active route publish 중 하나가 발생하기 전 단계다.

- Actor가 하나라도 생성된 뒤 Entry Spot rid를 바꾸려고 하면
  `ZLINK_CONFIG_INVALID_STATE`로 실패하고 `errno`는 `EBUSY`다.
- Entry Spot rid가 Actor active route나 Spot owner route로 publish된 뒤에는 바꿀 수 없다.
- Entry Spot rid는 같은 `SpotNode` 안 다른 user Spot rid와 중복될 수 없다.

### Spot 조회

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spot_lookup(
  void *node_,
  const zlink_routing_id_t *spot_rid_,
  void **spot_out_);
```

- `node_ == NULL`이면 `ZLINK_CONFIG_INVALID_HANDLE`로 실패하고 `errno`는 `EFAULT`다.
- `spot_rid_ == NULL` 또는 `spot_out_ == NULL`이면 `ZLINK_CONFIG_INVALID_ARGUMENT`로 실패하고
  `errno`는 `EINVAL`이다.
- 같은 rid의 live local Spot이 없으면 `ZLINK_CONFIG_NOT_FOUND`로 실패하고 `errno`는 `ENOENT`이며
  `*spot_out_`은 변경하지 않는다.
- 성공하면 `*spot_out_`에 새 owned Spot facade handle을 반환한다. application이
  `zlink_spot_destroy()`로 닫아야 한다.
- Entry Spot rid로 조회하면 Entry Spot facade를 반환한다. Entry Spot logical state는
  `SpotNode`가 소유하므로 마지막 facade가 닫혀도 제거되지 않는다.
- remote Spot 조회는 Discovery Spot owner resolve가 담당한다. 이 함수는 local `SpotNode`
  안의 Spot만 조회한다.

## SpotNode 계약

SpotNode는 HWM을 `Spot`에서 `SpotNode`로 들어오는 admission control(수신 허가 제어, 새 메시지·연결의 수락 여부를 결정하는 관문)로만 공개한다.
공개 옵션은 `ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE`,
`ZLINK_SPOT_NODE_OPT_ROUTER_HWM`,
`ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE`,
`ZLINK_SPOT_NODE_OPT_PUBSUB_HWM` 네 가지다. 두 admission 채널의 기본 profile은
balanced이며 HWM `16`으로 해석된다. compact는 `4`, low latency는 `8`,
throughput은 `32`다.
양수 HWM을 직접 설정하면 해당 채널 profile 값보다 우선한다. 숫자 HWM에 `0`을
설정하면 override를 지우고 profile 값으로 돌아간다. 음수와 알 수 없는 profile은
`EINVAL`로 실패한다.

`Spot` handle은 common `ZLINK_OPT_SNDHWM` 또는 `ZLINK_OPT_RCVHWM` 설정을 받지
않는다. `Spot`은 생성 시점의 SpotNode admission HWM을 캡처하며, 이후 SpotNode HWM
변경은 나중에 생성되는 `Spot`에만 적용된다. SpotNode 내부 relay와 delivery socket은
HWM `0`을 사용한다. 제거된 방향별 SpotNode HWM option과 queue hard limit option은
공개 계약에 없으며, 기존 enum 숫자는 예약 상태다.

SpotNode와 Spot에는 public weight 설정 옵션이 없습니다. peer weight는 raw
ROUTER와 DEALER 소켓에서만 설정합니다. Spot peer snapshot에 남아 있는
`weight` 필드는 discovery나 peer 신호에서 배운 remote peer 상태이며,
Spot/SpotNode의 로컬 옵션이 아닙니다.

SpotNode는 topology와 설정 handle이며 topic publisher가 아니다. SpotNode에
`zlink_publish*()`를 호출하거나 send-ready handler를 설치하면 `ENOTSUP`으로
실패한다. topic publish는 `Spot` facade를 만들어 그 handle에서 수행한다.

### 내부 socket snapshot

```c
typedef struct zlink_spot_node_socket_snapshot_filter_t {
  zlink_spot_node_socket_owner_t owner;
  zlink_socket_type_t socket_type;
  char socket_name[64];
} zlink_spot_node_socket_snapshot_filter_t;

typedef struct zlink_spot_node_socket_snapshot_entry_t {
  zlink_spot_node_socket_owner_t owner;
  uint64_t owner_id;
  char owner_name[64];
  char socket_name[64];
  zlink_socket_type_t socket_type;
  uint32_t auto_hwm_visible;
  zlink_monitor_snapshot_t snapshot;
} zlink_spot_node_socket_snapshot_entry_t;

zlink_config_result_t zlink_spot_node_internal_sockets_snapshot(
  void *node,
  const zlink_spot_node_socket_snapshot_filter_t *filter,
  zlink_spot_node_socket_snapshot_entry_t *entries,
  size_t *count);
```

- snapshot은 이미 존재하는 내부 socket만 반환한다.
- snapshot 호출은 꺼진 기능이나 lazy socket을 새로 만들지 않는다.
- `entries == NULL`이면 성공하며 필요한 row 수를 `*count`에 쓴다.
- `*count`가 부족하면 `ENOBUFS`로 실패하고 필요한 row 수를 쓴다.
- `owner`는 `ANY`, `NODE`, `SPOT` 중 하나다.
- `socket_type`은 `ZLINK_SOCKET_ANY` 또는 공개 `ZLINK_SOCKET_*` 값만 받는다.
- `socket_name`이 비어 있지 않으면 정확히 같은 내부 socket 이름만 반환한다.
- `auto_hwm_visible == 1`인 row는 기본 Auto-HWM perf 출력 대상이다.
- `snapshot.auto_hwm_profile`, `snapshot.auto_hwm_policy_class`,
  `snapshot.auto_hwm_unit_budget_bytes`, `snapshot.auto_hwm_size_cap`,
  `snapshot.auto_hwm_socket_message_slots`는 진단용 자동 HWM planner 결과다.
- 현재 SPOT topology의 주요 node socket 이름은 `ingress-sub`, `local-pub`,
  `mesh-pub`, `mesh-xsub`, `internal-router`, `external-router`다.
- `PUBSUB` mode에서는 routed socket이 생성되지 않고, `ROUTED` mode에서는 topic
  socket이 생성되지 않는다. snapshot 호출은 꺼진 plane을 활성화하지 않는다.

### 토폴로지와 discovery

```c
zlink_config_result_t zlink_spot_node_set_pub_bind(void *node, const char *endpoint);
zlink_connect_result_t zlink_spot_node_connect_peer(void *node,
                                                    const char *peer_endpoint);
zlink_connect_result_t zlink_spot_node_disconnect_peer(void *node,
                                                       const char *peer_endpoint);
zlink_connect_result_t zlink_spot_node_disconnect_peer_rid(
  void *node,
  const zlink_routing_id_t *target_node_rid);
zlink_config_result_t zlink_spot_node_attach_discovery(void *node,
                                                       void *discovery);
```

- `zlink_spot_node_set_pub_bind()`는 node endpoint를 bind한다.
- `zlink_spot_node_connect_peer()`와 `zlink_spot_node_disconnect_peer()`는
  endpoint를 알고 있는 수동 mesh 연결에 쓴다.
- `zlink_spot_node_disconnect_peer_rid()`는 target node routing id를 기준으로
  peer node 연결을 종료한다. target node 아래의 개별 spot routing id는 이
  API의 대상이 아니다.
- discovery가 이미 attach된 node에서 `connect_peer()` 또는
  `disconnect_peer()`를 호출하면 `EBUSY`로 실패한다.
- `Spot` facade에는 별도 peer rid disconnect 함수가 없다. peer 연결은
  `SpotNode` runtime이 소유하기 때문이다.
- `zlink_spot_node_attach_discovery()`는 SPOT channel view를 제공하는
  discovery만 받는다.
- node에는 한 번에 하나의 active SPOT discovery view만 둘 수 있다.

### Channel 호출용 socket 등록

```c
zlink_config_result_t zlink_spot_node_attach_channel_dealer(
  void *node,
  void *discovery,
  void *dealer);

zlink_config_result_t zlink_spot_node_attach_channel_dealer_manual(
  void *node,
  const char *channel_name,
  void *dealer);

zlink_config_result_t zlink_spot_node_attach_pub_ingress(
  void *node,
  void *pub);
```

- `attach_channel_dealer()`는 discovery 기반 channel 대상 집합에 붙어 있는
  `DEALER`를 node에 등록한다.
- `attach_channel_dealer_manual()`은 호출자가 직접 connect를 끝낸 `DEALER`를
  지정한 `channel_name` 아래에 등록한다.
- 같은 channel 이름에는 자동 attach와 수동 attach를 합쳐서 `DEALER` 하나만
  등록할 수 있다. 중복 등록은 `EBUSY`다.
- attach 함수는 socket 생성이나 connect를 대신하지 않는다.
- attach된 `DEALER`는 `SpotNode` 전용 자원이다. 소유권은 호출자가 유지하지만,
  attach 뒤에는 다른 owner가 같은 socket을 일반 용도로 함께 써서는 안 된다.
- `zlink_spot_node_attach_pub_ingress()`는 외부 일반 `PUB`를 `Spot` 입력 경로에
  연결한다.
- ingress `PUB`는 node당 하나만 등록할 수 있다. 두 번째 등록은 `EBUSY`다.
- ingress `PUB`도 `SpotNode` 전용 자원으로 취급한다.

## Socket Channel Name Metadata

attach된 `DEALER`에 논리 channel name을 미리 기록해 두는 편의 API다.

```c
ZLINK_EXPORT zlink_config_result_t zlink_socket_set_channel_name (
  void *socket_,
  const char *channel_name_);

ZLINK_EXPORT zlink_config_result_t zlink_socket_get_channel_name (
  void *socket_,
  char *channel_name_buf_,
  size_t channel_name_capacity_,
  size_t *channel_name_len_out_);
```

- setter는 socket에 fixed logical channel name metadata를 기록한다.
- 이 metadata는 transport connect, bind, routing, discovery를 자동으로 바꾸지 않는다.
- getter는 현재 기록된 channel name을 돌려준다.
- channel name이 설정되지 않은 socket이면 `ENOENT`로 실패한다.
- 비어 있거나 잘못된 `channel_name`은 `EINVAL`이다.
- attach나 discovery가 귀속을 확정한 뒤에는 setter 변경을 `EBUSY` 또는 `EINVAL`로
  거부한다.

attach와의 관계는 아래처럼 고정한다.

- discovery attach 시 socket metadata가 비어 있으면 discovery channel 이름을 채운다.
- discovery attach 시 기록된 값과 discovery channel이 같으면 허용한다.
- discovery attach 시 기록된 값과 discovery channel이 다르면 `EINVAL`이다.
- manual attach 시 socket metadata가 비어 있으면 attach 인자의 `channel_name`을 채운다.
- manual attach 시 기록된 값과 attach 인자가 같으면 허용한다.
- manual attach 시 기록된 값과 attach 인자가 다르면 `EINVAL`이다.

`CHANNEL_REPLY_READABLE` callback에서 `subject_kind == CHANNEL_DEALER`일 때
`zlink_socket_get_channel_name(subject, ...)`를 호출해 해당 dealer가 어느 channel에
속하는지 읽을 수 있다.

## Spot 데이터 평면 계약

### Channel send/request

```c
zlink_submit_result_t zlink_spot_send_channel(
  void *spot,
  const char *channel_name,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_submit_result_t zlink_spot_request_channel(
  void *spot,
  const char *channel_name,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

- channel 호출은 attach된 `DEALER` 경로로만 나간다.
- lookup 키는 `channel_name`이다.
- 같은 `channel_name`에 attach된 `DEALER`가 없으면 send/request는 실패한다.
- `Spot`에서는 direct `rid`로 `ROUTER`를 지정해 ordinary one-way send를 하지 않는다.
  direct routed request 시작은 아래 별도 섹션을 참조한다.

channel request의 transport owner와 delivery owner는 다르다.

- **transport owner**: 실제 request를 내보내고 network reply를 받는 attached `DEALER`
- **delivery owner**: 최종 user callback을 실행하는 `Spot` dispatch stream

즉 network reply는 선택된 `DEALER` 경로로 돌아오지만, 최종 callback 실행은 request를
시작한 `Spot`의 dispatch stream이 맡는다. reply는 `CHANNEL_REPLY_READABLE` dispatch
event로 올라오며, `zlink_spot_channel_reply_progress_from()` 호출 안에서 callback이
실행된다.

### Topic publish/subscribe

```c
zlink_submit_result_t zlink_spot_publish(
  void *spot,
  const char *service_name,
  const char *topic_id,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_recv_result_t zlink_spot_subscribe(
  void *spot,
  zlink_routing_id_t *source_rid_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  char *service_name_out,
  size_t *service_name_len_out,
  char *topic_id_out,
  size_t *topic_id_len_out,
  zlink_recv_flags_t flags);

zlink_recv_result_t zlink_spot_subscription_event(
  void *spot,
  zlink_routing_id_t *source_rid_out,
  int *subscribed_out,
  char *service_name_out,
  size_t *service_name_len_out,
  char *topic_id_out,
  size_t *topic_id_len_out,
  zlink_recv_flags_t flags);
```

- 토픽 평면의 공개 인자 이름은 아직 `service_name`을 쓴다.
- 이 이름은 topic plane namespace를 구분하는 현재 계약 이름이다.
- `zlink_spot_node_attach_pub_ingress()`로 연결한 외부 `PUB`는 이 토픽 입력
  경로로 합류한다.

### Routed recv/reply

```c
zlink_recv_result_t zlink_spot_recv(
  void *spot,
  const zlink_routing_id_t **source_rid_out,
  const zlink_routing_id_t **spot_rid_out,
  uint64_t *request_seq_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  zlink_recv_flags_t flags);

zlink_submit_result_t zlink_spot_reply_spot(
  void *spot,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  uint64_t request_seq,
  zlink_msg_t *parts,
  size_t part_count);

zlink_submit_result_t zlink_spot_reply_router(
  void *spot,
  const zlink_routing_id_t *peer_rid,
  uint64_t request_seq,
  zlink_msg_t *parts,
  size_t part_count);
```

- `zlink_spot_recv()`는 routed receive plane을 읽는다.
- `zlink_spot_recv()`는 현재 `Spot`에 귀속된 routed ingress만 읽는다.
- 첫 `zlink_spot_recv()`가 routed target registration이나 hidden queue open을
  수행하지 않는다.
- `ZLINK_DONTWAIT`에서 `EAGAIN`이 나오면, 그 시점에는 해당 `Spot`의 routed ingress에
  읽을 데이터가 없다는 뜻이다.
- 수신한 요청이 SPOT origin이면 `zlink_spot_reply_spot()`으로 reply한다.
- 수신한 요청이 ROUTER origin이면 `zlink_spot_reply_router()`로 reply한다.
- reply 경로는 수신 이벤트가 알려준 concrete source address를 그대로 사용해야 한다.
- routed recv metadata인 `source_rid`, `spot_rid`, `request_seq`는 local forward가
  끼어도 application recv 표면까지 그대로 유지되어야 한다.

### Handler

#### Dispatch event 타입

```c
typedef enum zlink_spot_dispatch_event_t {
  ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE    = 1,
  ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE       = 2,
  ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE        = 3,
  ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE = 4,
  ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE        = 5,
  ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE   = 6
} zlink_spot_dispatch_event_t;

typedef enum zlink_spot_dispatch_subject_kind_t {
  ZLINK_SPOT_DISPATCH_SUBJECT_SPOT           = 1,
  ZLINK_SPOT_DISPATCH_SUBJECT_TIMER          = 2,
  ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER = 3,
  ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR          = 4
} zlink_spot_dispatch_subject_kind_t;

typedef struct zlink_spot_dispatch_info_t {
  zlink_spot_dispatch_event_t        event;
  zlink_spot_dispatch_subject_kind_t subject_kind;
  void                              *subject;
} zlink_spot_dispatch_info_t;

typedef void (*zlink_spot_dispatch_event_handler_fn)(
  void *spot_,
  const zlink_spot_dispatch_info_t *info_,
  void *userdata_);
```

- `event`는 어떤 종류의 work가 준비됐는지 나타낸다.
- `subject_kind`는 `subject` 포인터를 어떤 타입으로 해석해야 하는지를 나타낸다.
- `subject`는 실제 drain 대상 인스턴스다.

각 조합의 의미는 아래와 같다.

| event | subject_kind | subject | drain 방법 |
|-------|-------------|---------|------------|
| `SUBSCRIBE_READABLE` | `SPOT` | `spot_` (또는 NULL) | `zlink_spot_subscribe()` |
| `ROUTED_READABLE` | `SPOT` | `spot_` (또는 NULL) | `zlink_spot_recv()` |
| `TIMER_READABLE` | `TIMER` | timer handle | `zlink_timer_recv()` |
| `CHANNEL_REPLY_READABLE` | `CHANNEL_DEALER` | attached dealer handle | `zlink_spot_channel_reply_progress_from()` |
| `ACTOR_READABLE` | `ACTOR` | callback lifetime의 `const zlink_actor_ref_t *` | `zlink_spot_node_actor_recv_part()` |
| `ACTOR_JOIN_READABLE` | `SPOT` | `spot_` | `zlink_spot_actor_join_recv()` |

dispatch 우선순위는 아래 순서로 고정한다.

1. `SUBSCRIBE_READABLE`
2. `ROUTED_READABLE`
3. `CHANNEL_REPLY_READABLE`
4. `TIMER_READABLE`
5. `ACTOR_JOIN_READABLE`
6. `ACTOR_READABLE`

`CHANNEL_REPLY_READABLE` 이벤트가 뜻하는 것은 "해당 attached dealer를 통해 시작한
channel request 중 user callback을 실행할 준비가 끝난 completion이 하나 이상 있다"는
것이다. raw dealer frame 수신 여부가 아니라, request completion 준비 상태를 알린다.

`SUBSCRIBE_READABLE`과 `ROUTED_READABLE`은 메시지 개수 이벤트가 아니라
readiness 이벤트다.

- callback 1회가 메시지 1개를 뜻하지 않는다.
- 이미 readable인 동안 같은 plane으로 메시지가 더 들어오더라도, dispatch callback
  개수와 메시지 개수는 1:1로 대응하지 않을 수 있다.
- 호출자는 해당 plane에서 `EAGAIN`이 나올 때까지 모두 읽어 내는 방식으로 처리해야 한다.
- `SUBSCRIBE_READABLE`은 node-wide broad fan-out이 아니라, 해당 `Spot`이 실제로
  subscribe recv를 할 수 있을 때만 올라와야 한다.
- `ACTOR_READABLE`은 특정 Actor의 unread part가 준비됐다는 뜻이다. callback의
  `subject`는 callback lifetime 동안만 유효한 `const zlink_actor_ref_t *`이며,
  호출자는 값을 복사한 뒤 `zlink_spot_node_actor_recv_part()`에 넘겨 drain한다.
- `ACTOR_JOIN_READABLE`은 Spot에 처리할 Actor join request가 있다는 뜻이다.
  `zlink_spot_actor_join_recv()`가 `ZLINK_RECV_NO_DATA`를 반환할 때까지 drain한다.

#### Channel reply progress

```c
ZLINK_EXPORT int zlink_spot_channel_reply_progress_from (
  void *spot_,
  void *dealer_);
```

- `CHANNEL_REPLY_READABLE` callback이 전달한 `subject`(dealer handle)에 대해
  `spot_`에 귀속된 channel reply completion을 drain한다.
- drain 중 해당 dealer source queue에 적재된 request completion callback을 실행한다.
- `dealer_`가 해당 `Spot`에 attach된 channel dealer가 아니면 `EINVAL` 또는 `ENOENT`로
  실패한다.
- `subject`는 raw socket처럼 직접 recv하라는 뜻이 아니라, 이 함수를 호출하라는 신호다.

#### Handler 등록

```c
zlink_handler_result_t zlink_spot_handler(
  void *spot,
  zlink_spot_handler_fn handler,
  void *userdata);

zlink_handler_result_t zlink_spot_dispatch_event_handler(
  void *spot,
  zlink_spot_dispatch_event_handler_fn handler,
  void *userdata);
```

`zlink_spot_handler()`는 **routed 전용 직접 callback**이다. callback 안에서 routed
message payload를 직접 받는다. subscribe, channel reply, timer, Actor 이벤트는
이 handler에 전달되지 않는다. routed 이외 이벤트가 필요하면 이 handler를 쓸 수 없다.

`zlink_spot_dispatch_event_handler()`는 **통합 readiness notification**이다. 모든
이벤트 종류(subscribe, routed, channel reply, timer, Actor join, Actor readable)를
readiness 형태로 알린다. callback은 "읽을 것이 있다"는 신호이며, 실제 데이터는
각 plane의 drain API(`zlink_spot_recv()`, `zlink_spot_subscribe()` 등)로 읽는다.

subscribe, channel reply, timer, Actor 이벤트는 **직접 callback 모드가 없다**.
이 이벤트들은 `zlink_spot_dispatch_event_handler()` readiness 모드로만 소비할 수 있다.

두 handler는 상호 배타적이다. 하나가 등록된 상태에서 다른 쪽을 등록하면
`ZLINK_HANDLER_BUSY`로 실패한다. `zlink_spot_handler()`를 선택하면 해당 Spot에서
subscribe, channel reply, timer, Actor 이벤트를 받을 수 없다.

### Poller와의 관계

현재 public poller 계약은 바뀌지 않았다.

- `zlink_poller_event_t`는 owner spot / event kind / subject를 함께 표현하지 않는다.
- 따라서 현재 공개 계약에서 `Spot` 직접 등록으로 dispatch와 같은 의미를 받는
  poller 표면은 아직 없다.
- `Spot`의 subscribe/routed/timer/channel-reply readiness를 한 callback으로 다루려면
  `zlink_spot_dispatch_event_handler()`를 사용해야 한다.

## Actor 계약

Actor는 `SpotNode`가 소유하는 routing target이다. Actor는 public `void *` handle이
아니라 `zlink_actor_ref_t`로 식별한다. Actor는 socket, inproc endpoint, transport
endpoint를 소유하지 않는다. Actor별 HWM 설정은 공개 계약에 없다.

Actor는 항상 정확히 하나의 `Spot`에 속한다. 생성 직후에는 Entry Spot에 속하고,
`join`으로 user Spot으로 이동하며, `leave`로 Entry Spot으로 돌아온다. Actor가
dispatch context 없이 남는 구간은 없다. Actor로 들어오는 session relay message는
항상 current Spot dispatch event에서 `zlink_spot_node_actor_recv_part()`로 읽는다.

Actor 전용 dispatch context나 Actor 전용 callback은 제공하지 않는다. 이렇게 해야
join 전후 처리의 동기화 모델이 같은 Spot dispatch context 안에서 유지된다.

### Actor ref 타입

```c
#define ZLINK_ACTOR_ID_MAX 256

typedef struct zlink_actor_ref_t {
  zlink_routing_id_t node_rid;
  char actor_id[ZLINK_ACTOR_ID_MAX];
  uint64_t generation;
} zlink_actor_ref_t;

typedef struct zlink_actor_recv_info_t {
  zlink_actor_ref_t actor;
  zlink_routing_id_t source_node_rid;
  zlink_routing_id_t source_session_rid;
  uint32_t flags;
} zlink_actor_recv_info_t;

typedef struct zlink_actor_join_info_t {
  zlink_actor_ref_t source_actor;
  zlink_actor_ref_t target_actor;
  zlink_routing_id_t source_node_rid;
  zlink_routing_id_t source_spot_rid;
  zlink_routing_id_t target_node_rid;
  zlink_routing_id_t target_spot_rid;
  uint64_t join_epoch;
  void *request;
  uint32_t flags;
} zlink_actor_join_info_t;

typedef enum zlink_actor_create_status_t {
  ZLINK_ACTOR_CREATE_CREATED = 1,
  ZLINK_ACTOR_CREATE_EXISTING = 2
} zlink_actor_create_status_t;

typedef struct zlink_actor_create_result_t {
  zlink_actor_create_status_t status;
  zlink_actor_ref_t actor;
} zlink_actor_create_result_t;

typedef enum zlink_actor_admission_result_t {
  ZLINK_ACTOR_ADMISSION_ACCEPT = 1,
  ZLINK_ACTOR_ADMISSION_REJECT = 2
} zlink_actor_admission_result_t;

typedef zlink_actor_admission_result_t (*zlink_actor_admission_handler_fn)(
  void *node,
  const char *actor_id,
  const zlink_msg_t *message,
  void *userdata);
```

Actor id는 NUL 종료 UTF-8 바이트열이며, 유효 최대 길이는 `ZLINK_ACTOR_ID_MAX - 1`
(255바이트)다. 빈 id, NULL id, 255바이트를 넘는 id는 `EINVAL` 계열 실패다.
같은 `SpotNode` 안에서 live Actor id는 유일하다. 서로 다른 `SpotNode`에는 같은
Actor id가 동시에 존재할 수 있다.

`zlink_actor_ref_t.generation == 0`은 unchecked ref다. unchecked ref는 잘못된 값이
아니며, target node의 현재 같은 Actor id를 대상으로 해석한다. `generation != 0`인
checked ref는 target Actor의 현재 generation과 일치해야 한다. 일치하지 않으면
stale 또는 conflict 계열 실패로 끝난다.

### 생성과 조회

```c
zlink_config_result_t zlink_spot_node_actor_new(
  void *node,
  const char *actor_id,
  zlink_actor_ref_t *actor_out);

zlink_config_result_t zlink_spot_node_actor_lookup(
  void *node,
  const char *actor_id,
  zlink_actor_ref_t *out);

zlink_config_result_t zlink_remote_actor_get_ref(
  const zlink_routing_id_t *target_node_rid,
  const char *actor_id,
  zlink_actor_ref_t *out);
```

- `zlink_spot_node_actor_new()`는 local Actor를 Entry Spot에 만들고 checked ref를
  `actor_out`에 반환한다. Actor 생성은 Entry Spot dispatch handler나 join request
  handler를 거치지 않는다.
- 같은 node에 같은 live Actor id가 이미 있으면 생성은 `EBUSY` 계열로 실패한다.
- `node_ == NULL`이면 `ZLINK_CONFIG_INVALID_HANDLE`로 실패하고 `errno`는 `EFAULT`다.
- `actor_id_ == NULL` 또는 `actor_out_ == NULL`이면 `ZLINK_CONFIG_INVALID_ARGUMENT`로
  실패하고 `errno`는 `EINVAL`이다.
- `zlink_spot_node_actor_lookup()`은 caller node가 소유한 live local Actor를 조회하고
  checked ref를 반환한다. 같은 Actor id의 live Actor가 없으면 not-found 계열 실패다.
- `zlink_remote_actor_get_ref()`는 target node rid와 Actor id만으로 unchecked ref
  (generation 0)를 만든다. peer 연결, handshake, target Actor 존재 여부는 확인하지
  않는다. 조회 결과는 ref 기반 API의 입력으로 사용한다.

### Remote create-or-get

```c
zlink_request_result_t zlink_spot_node_create_remote_actor(
  void *node,
  const zlink_routing_id_t *target_node_rid,
  const char *actor_id,
  zlink_msg_t *message,
  zlink_actor_create_result_t *out,
  uint32_t timeout_ms);

zlink_handler_result_t zlink_spot_node_actor_admission_handler(
  void *node,
  zlink_actor_admission_handler_fn handler,
  void *userdata);
```

- target node에 해당 Actor id가 없을 때만 admission handler를 호출한다.
- target Actor가 이미 있으면 handler 호출 없이 `ZLINK_ACTOR_CREATE_EXISTING`을
  반환한다. 이미 있는 Actor의 current Spot은 바꾸지 않는다.
- handler가 승인하면 Actor를 target node의 Entry Spot에 만들고
  `ZLINK_ACTOR_CREATE_CREATED`를 반환한다.
- handler가 거부하면 `ZLINK_REQUEST_REJECTED`로 끝난다.
- remote create-or-get은 target Spot join handler를 거치지 않는다. target Spot 입장
  승인은 create-or-get 호출이 아니라 이후 `join` 요청이 결정한다.
- `timeout_ms == 0`은 nonblocking request다. 즉시 완료할 수 없으면 timeout 또는
  busy 계열 결과로 실패한다.
- submit 성공 시 `message` 소유권은 라이브러리로 이전된다. validation 실패나
  submit 전 실패에서는 호출자에게 남는다.
- remote Actor destroy도 `zlink_spot_node_actor_destroy()`로 ref 기반 요청을 보낸다.
  target node에 도달할 수 없거나 checked ref generation이 맞지 않으면 Actor slot을
  제거하지 않는 실패다.

### Spot join

```c
zlink_submit_result_t zlink_spot_node_actor_join_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *message,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

zlink_recv_result_t zlink_spot_actor_join_recv(
  void *spot,
  zlink_actor_join_info_t *info_out,
  zlink_msg_t *message_out,
  zlink_recv_flags_t flags);

zlink_submit_result_t zlink_spot_actor_join_reply(
  void *spot,
  const zlink_actor_join_info_t *info,
  uint32_t accepted,
  zlink_msg_t *message);
```

`join`은 Actor를 현재 Spot에서 target Spot으로 이동하는 요청이다. `join`이 성공하면
Actor의 current Spot이 target으로 바뀐다.

`zlink_spot_node_actor_join_spot()` 계약:

- `node`는 join request를 제출하는 request owner `SpotNode`다. session owner, backend
  service node, source Actor owner node 모두 request owner가 될 수 있다.
- `dest_node_rid`가 Actor owner node와 같으면 local join으로, 다르면 remote join
  handoff로 처리한다.
- Entry Spot이 아닌 user Spot으로 join하려면 source Actor에 bound STREAM session이
  있어야 한다. bound session이 없는 Actor의 user Spot join은 invalid-state 계열 실패다.
- 이미 대상 Spot에 있으면 join request handler를 거치지 않고 비동기 idempotent success
  completion으로 완료한다.
- join request가 pending 중인 Actor에 새 join, leave, destroy를 요청하면 busy 또는
  invalid-state 계열 실패다.
- return이 `ZLINK_SUBMIT_OK`이면 join operation이 접수된 것이지 accept가 된 것은 아니다.
  accept/reject 결과는 `zlink_reply_handler_fn` completion으로 전달한다.
- `timeout_ms`는 submit 성공 뒤 join reply와 remote handoff가 완료되기까지의 operation
  timeout이다. `timeout_ms == 0`이면 operation timeout을 설치하지 않는다. 이는 submit
  nonblocking 지시가 아니다. submit 단계의 즉시 실패 여부는 `flags`의 `ZLINK_DONTWAIT`가
  결정한다.
- submit 성공 시 `message` 소유권은 라이브러리로 이전된다. local validation 또는
  submit 전 실패가 발생하면 소유권은 caller에게 남는다.
- join request는 단일 `zlink_msg_t` payload를 싣는다. target Spot은 이 payload를 읽고
  accept 또는 reject를 결정한다.

`zlink_spot_actor_join_recv()` 계약:

- `ACTOR_JOIN_READABLE` dispatch event가 뜨면 해당 Spot에서 이 API로 drain한다.
- 성공 시 join message 소유권은 호출자에게 이전된다.
- `zlink_actor_join_info_t.flags & ZLINK_ACTOR_JOIN_INFO_REMOTE`가 0이 아니면
  remote handoff join이다. payload에서 Actor state를 복원할 수 없으면 reject해야 한다.
- remote join prepare는 `zlink_spot_node_actor_admission_handler()`를 호출하지 않는다.
  target Spot join handler가 Actor 생성과 입장 승인을 함께 결정한다.
- `zlink_actor_join_info_t.request`는 opaque one-shot handle이다. application은 이
  값을 역참조하거나 직접 저장하지 않는다. reply 호출에서 `info` 구조체를 그대로 넘긴다.

`zlink_spot_actor_join_reply()` 계약:

- `accepted`는 반드시 `0`(reject) 또는 `1`(accept)이어야 한다. 다른 값은
  `ZLINK_SUBMIT_INVALID_ARGUMENT`로 실패하고 `errno`는 `EINVAL`이다.
- `info == NULL`이면 `ZLINK_SUBMIT_INVALID_ARGUMENT`로 실패하고 `errno`는 `EINVAL`이다.
- `message == NULL`이면 payload 없는 completion이다.
- submit 성공 시 reply message 소유권은 라이브러리로 이전된다. validation 실패 또는
  duplicate reply 실패 시 소유권은 caller에게 남는다.
- 같은 `info.request`에 두 번 응답하면 두 번째는 `ZLINK_SUBMIT_INVALID_STATE`로 실패하고
  `errno`는 `EALREADY` 또는 `EINVAL`이다.
- join timeout, target Spot destroy, SpotNode shutdown 뒤 늦게 도착한 reply는
  `ZLINK_SUBMIT_INVALID_STATE`로 실패한다.
- `info.request`의 lifetime은 join request가 reply, timeout, reject cleanup, Spot
  destroy, node shutdown으로 끝날 때까지다.

join 원자성:

- accept 전까지 source Spot이 current Spot이다.
- target Spot이 거부하거나 timeout되면 Actor는 source Spot에 그대로 남는다.
- remote join에서 source Actor는 session Actor list compare-and-swap이 성공하고
  target Actor activate와 active route 갱신이 끝난 뒤 source Spot에서 제거된다.
- join 전후에 도착한 Actor queue message의 순서는 Actor queue 도착 순서로 보존한다.
- Spot destroy는 joined Actor나 pending join request가 남아 있으면 busy로 실패한다.

### Spot leave

```c
zlink_request_result_t zlink_spot_node_actor_leave_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *current_spot_rid,
  uint32_t timeout_ms);
```

`leave`는 Actor를 현재 Spot에서 Entry Spot으로 돌려보내는 요청이다.

- Actor가 이미 Entry Spot에 있으면 idempotent success다.
- `current_spot_rid`는 Actor의 현재 Spot이어야 한다. caller가 본 current Spot과
  실제 current Spot이 다르면 stale leave를 막기 위해 invalid-state 계열로 실패한다.
- Actor에 join request가 pending이면 `ZLINK_REQUEST_BUSY`로 실패하고 `errno`는
  `EBUSY`다. leave는 pending join을 취소하지 않는다.
- leave는 Entry Spot dispatch handler나 join request handler를 거치지 않는다.
- leave 성공 뒤 Actor message는 Entry Spot dispatch event로 올라간다.
- leave는 Actor queue를 비우지 않는다. leave 전후 message 순서는 보존한다.
- `node_ == NULL`이면 `ZLINK_REQUEST_INVALID_ARGUMENT`로 실패하고 `errno`는 `EFAULT`다.
- `actor_ == NULL` 또는 `current_spot_rid_ == NULL`이면
  `ZLINK_REQUEST_INVALID_ARGUMENT`로 실패하고 `errno`는 `EINVAL`이다.
- `timeout_ms == 0`은 nonblocking request다. 즉시 완료할 수 없으면 timeout 또는
  busy 계열 결과로 실패한다.

### 종료

```c
zlink_request_result_t zlink_spot_node_actor_destroy(
  void *node,
  const zlink_actor_ref_t *actor,
  uint32_t timeout_ms);
```

- Actor destroy는 Actor가 Entry Spot에 있을 때만 허용한다.
- Actor가 user Spot에 있으면 `ZLINK_REQUEST_INVALID_STATE`로 실패하고 `errno`는
  `EBUSY`다. application은 먼저 `leave`로 Entry Spot에 돌려보낸 뒤 destroy한다.
- join request가 pending이면 `ZLINK_REQUEST_BUSY` 또는 `ZLINK_REQUEST_INVALID_STATE`로
  실패하고 `errno`는 `EBUSY`다. destroy는 pending join을 취소하지 않는다.
- bound STREAM session이 있으면 destroy는 먼저 session Actor list 항목과 Actor의
  bound session ref를 제거한다. 이 cleanup은 client STREAM connection 자체를 닫지
  않는다.
- bound session cleanup을 `timeout_ms` 안에 확인할 수 없으면 timeout 계열로 실패하고
  Actor slot과 Entry Spot membership은 유지한다. client connection까지 닫아야 하면
  destroy 전에 `zlink_spot_node_actor_close_bound_session()`을 호출한다.
- destroy 성공 뒤 해당 Actor ref는 stale이 된다.
- `node_ == NULL`이면 `ZLINK_REQUEST_INVALID_ARGUMENT`로 실패하고 `errno`는 `EFAULT`다.
- `actor_ == NULL`이면 `ZLINK_REQUEST_INVALID_ARGUMENT`로 실패하고 `errno`는 `EINVAL`이다.
- `timeout_ms == 0`은 nonblocking request다.

### Message recv

```c
zlink_recv_result_t zlink_spot_node_actor_recv_part(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_actor_recv_info_t *info_out,
  zlink_msg_t *part_out,
  zlink_part_flag_t *has_more_out,
  zlink_recv_flags_t flags);
```

- current Spot dispatch context에서 `ACTOR_READABLE` event를 받은 뒤 호출한다.
- `node`는 Actor owner `SpotNode`여야 한다. remote route는 수행하지 않는다.
- `node == NULL`이거나 `node`가 Actor owner가 아니면 `ZLINK_RECV_INVALID_HANDLE`로
  실패하고 `errno`는 `EFAULT`다.
- `actor == NULL`, `info_out == NULL`, `part_out == NULL`, `has_more_out == NULL`이면
  `ZLINK_RECV_INVALID_HANDLE`로 실패하고 `errno`는 `EFAULT`다.
  (`zlink_recv_result_t`에는 invalid-argument bucket이 없으므로 NULL output pointer도
  recv 계열 invalid-handle failure로 통일한다.)
- 읽을 part가 없고 `ZLINK_DONTWAIT`이면 `ZLINK_RECV_NO_DATA`다.
- 성공 시 `part_out` 소유권은 호출자에게 이전된다.
- `has_more_out`은 `ZLINK_PART_MORE` 또는 `ZLINK_PART_FINAL`을 반환한다.
- `info_out->actor`는 drain 대상 Actor ref이고, `source_node_rid`와
  `source_session_rid`는 message를 보낸 STREAM session을 식별한다.
- `info_out->flags`는 현재 `0`이다. 알 수 없는 bit는 invalid protocol로 처리한다.

### STREAM session 연결

```c
zlink_submit_result_t zlink_spot_node_actor_send_bound_session_msg(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_msg_t *message,
  zlink_send_flags_t flags);

zlink_request_result_t zlink_spot_node_actor_close_bound_session(
  void *node,
  const zlink_actor_ref_t *actor,
  uint32_t timeout_ms);
```

session owner node와 Actor owner node는 같을 수도 다를 수도 있다.

- **local Actor**: session owner node와 Actor owner node가 같다. bind, relay,
  Actor-to-session send가 같은 node 안에서 끝난다.
- **remote Actor**: session owner node와 Actor owner node가 다르다. bind control
  request, session-to-Actor relay frame, Actor-to-session frame이 node 사이를 지난다.
- session owner는 Actor의 joined Spot 상태를 저장하지 않는다.
- Actor owner는 STREAM session의 application state를 저장하지 않는다.
- Actor active route는 Actor 생성 시점이 아니라 STREAM session bind 성공 시점에
  Actor owner node의 Discovery가 publish한다.

`zlink_spot_node_actor_send_bound_session_msg()` 계약:

- Actor의 bound STREAM session으로 `message`를 보내는 **fire-and-forget** submit이다.
  completion handler가 없으며 return 값은 command 접수 여부만 나타낸다.
- `node`가 Actor owner와 같고 submit 전에 Actor가 live 상태가 아님, stale ref, bound
  session 없음을 확인할 수 있으면 `ZLINK_SUBMIT_NOT_FOUND` 또는
  `ZLINK_SUBMIT_INVALID_STATE` 계열로 즉시 실패한다.
- `node`가 Actor owner와 다르면 stale ref, missing Actor, missing bound session은
  submit 시점에 동기적으로 보장하지 않는다. Actor owner가 command를 처리할 때 이
  조건을 발견하면 message를 닫고 protocol drop counter를 증가시킨다.
- submit 성공 시 `message` 소유권은 라이브러리로 이전된다. submit 전 validation 실패
  시 소유권은 caller에게 남는다.

`zlink_spot_node_actor_close_bound_session()` 계약:

- Actor의 bound STREAM session을 닫고 session Actor list 항목과 Actor의 bound session
  ref를 제거한다.
- bound STREAM session이 없으면 `ZLINK_REQUEST_NOT_FOUND` 계열로 실패한다.
- close 성공 뒤 Actor는 Entry Spot으로 이동한다. 이미 Entry Spot에 있으면 current Spot은
  그대로 Entry Spot이다.
- close 성공 뒤 Actor queue에 unread message가 남아 있으면 Entry Spot dispatch handler에
  `ACTOR_READABLE` event를 올린다.
- `timeout_ms == 0`은 nonblocking request다.

## Spot routed request 시작

`Spot`은 routed request와 one-way direct send를 직접 시작할 수 있다.
아래 경로들을 공개한다.

### core helper substrate

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_router_part (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);
```

### C API wrapper

```c
ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_spot (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_router (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_send_spot (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);
```

- `zlink_spot_request_spot()`은 `zlink_spot_reply_spot(_part)`와 reply 짝을 이룬다.
- `zlink_spot_request_router()`는 `zlink_router_reply_spot(_part)`와 reply 짝을 이룬다.
- submit이 `ZLINK_SUBMIT_OK`이면 `handler_`가 정확히 한 번 호출된다.
- 그 외 반환값이면 handler는 등록되지 않는다.
- 반환 코드 의미는 [errno-map.ko.md](errno-map.ko.md)의
  `zlink_submit_result_t`와 `zlink_request_result_t` 절을 참조한다.
- `zlink_spot_send_spot()`은 대상 `Spot`으로 one-way routed send를 수행한다. reply를 기다리지 않으며 handler가 없다.
- `zlink_spot_send_spot()`은 `ZLINK_SUBMIT_OK`이면 메시지가 전송 경로에 올라간 것이다.

## Router와 SPOT 직접 주소 지정

ROUTER는 concrete destination을 지정해 SPOT으로 one-way send 및 request를 보낼 수 있다.

```c
zlink_submit_result_t zlink_router_send_spot(
  void *router,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_submit_result_t zlink_router_request_spot(
  void *router,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_router_reply_spot(
  void *router,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  uint64_t request_seq,
  zlink_msg_t *parts,
  size_t part_count);
```

이 경로는 라우터 쪽 저수준 direct addressing 계약이다.

## 관찰과 스냅샷

```c
zlink_config_result_t zlink_spot_node_status_snapshot(
  void *node,
  zlink_spot_node_status_t *out);

zlink_config_result_t zlink_spot_node_peers_snapshot(
  void *node,
  zlink_spot_node_peer_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_peers_query(
  void *node,
  const zlink_spot_node_peer_filter_t *filter,
  zlink_spot_node_peer_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_subjects_snapshot(
  void *node,
  const zlink_spot_node_subject_filter_t *filter,
  zlink_spot_node_subject_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_spots_snapshot(
  void *node,
  zlink_spot_node_spot_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_actors_snapshot(
  void *node,
  zlink_spot_node_actor_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_actors_snapshot(
  void *spot,
  zlink_actor_ref_t *entries,
  size_t *count);
```

- SPOT node monitor 전용 별도 recv API는 현재 공개 계약에 없다.
- snapshot/query API를 사용해 상태를 관찰한다.
- `entries == NULL`이면 snapshot 함수는 필요한 row 수를 `*count`에 쓴다.
- `*count`가 부족하면 `ENOBUFS`로 실패하고 필요한 row 수를 쓴다.
- `zlink_spot_node_status_t`와 peer/subject entry 구조체의 `service_name`
  필드는 현재 공개 이름을 유지한다.
- `zlink_spot_node_status_t.disconnected_sub_target_count`와
  `zlink_spot_node_status_t.disconnected_routed_target_count`는 ABI 호환을 위해 남아
  있다. 현재 HWM 정책은 delivery queue가 깊어졌다는 이유로 local subscribe 또는
  routed target을 끊지 않는다.
- `zlink_spot_node_spots_snapshot()`은 local Spot 목록을 반환한다. Entry Spot도 이 목록에 포함된다.
  `joined_actor_count`, `pending_actor_join_count`, `route_synced`,
  `last_changed_ms`는 진단용 값이다.
- `zlink_spot_node_actors_snapshot()`은 live local Actor row를 반환한다. 각 row에는
  Actor ref, joined 여부, joined Spot rid, route sync 여부, unread message count,
  `last_changed_ms`가 들어간다.
- `zlink_spot_actors_snapshot()`은 특정 Spot에 join된 Actor ref 목록을 반환한다.
- snapshot 값은 flow control 계약으로 쓰지 않는다.

## 제약 요약

- `SpotNode` mesh peer 자동 연결 대상은 SPOT discovery peer뿐이다.
- 일반 socket service provider는 `SpotNode` mesh peer로 섞이지 않는다.
- channel 호출은 attach된 `DEALER`로만 처리한다.
- `SpotNode.router`를 channel 호출 경로로 우회해서 쓰지 않는다.
- discovery attach와 수동 peer connect를 같은 peer 관계에 동시에 섞지 않는다.
- attach 함수는 socket 생성과 connect를 대신하지 않는다.
- attach된 `DEALER`를 app이 raw `zlink_recv()`로 직접 읽거나 별도 poller에
  등록하면 `Spot` progress와 경합할 수 있다. attach 이후 그 socket은 SpotNode
  runtime 전용으로 취급한다.
- channel request의 transport owner는 attach된 `DEALER`지만, callback delivery
  owner는 request를 시작한 `Spot`의 dispatch stream이다.
- `CHANNEL_REPLY_READABLE` callback에서 `subject`를 일반 dealer처럼 직접 수신하지
  않는다. `zlink_spot_channel_reply_progress_from()`를 통해서만 읽어낸다.
