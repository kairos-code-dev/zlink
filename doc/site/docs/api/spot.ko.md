[English](spot.md) | [한국어](spot.ko.md)

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [서비스 공통](README.ko.md)

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
  수 있으며, 호출자는 destroy 전에 unread를 끝까지 drain해야 할 의무를 지지
  않는다.
- `zlink_spot_node_destroy()`는 node와 내부 runtime 자원을 정리한다.
- discovery에 attach된 node는 보통 `zlink_discovery_destroy()` 흐름에서 함께 정리된다.

## SpotNode 계약

SpotNode와 Spot의 내부 raw 소켓 기본 HWM은 고정 숫자가 아니라 context
auto HWM 정책에서 계산된다. 사용자가
`ZLINK_SPOT_NODE_OPT_PUB_HWM`,
`ZLINK_SPOT_NODE_OPT_SUB_HWM`,
`ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM`,
`ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM`를 직접 설정하면 그 값이 자동값보다
우선한다. 기본 context 설정에서는 publish fanout 경로가 `fanout` floor `16`,
subscribe ingress와 routed send/recv 경로가 `8`에서 시작한다.

`ZLINK_SPOT_NODE_OPT_SUB_QUEUE_HARD_LIMIT`와
`ZLINK_SPOT_NODE_OPT_ROUTED_QUEUE_HARD_LIMIT`는 내부 delivery queue가 허용하는
메시지 개수 상한을 설정한다. 기본값은 각각
`ZLINK_SPOT_NODE_SUB_QUEUE_HARD_LIMIT_DFLT`,
`ZLINK_SPOT_NODE_ROUTED_QUEUE_HARD_LIMIT_DFLT`이며, 기본값은 각각 `100`과
`500`이다. 상한을 넘은
대상은 해당 sub 또는 routed delivery target만 끊기며, node 전체나 peer 전체를
닫지 않는다.

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
- 현재 SPOT topology의 주요 node socket 이름은 `ingress-sub`, `local-pub`,
  `mesh-pub`, `mesh-xsub`, `internal-router`, `external-router`다.
- `PUBSUB` mode에서는 routed socket이 생성되지 않고, `ROUTED` mode에서는 topic
  socket이 생성되지 않는다. snapshot 호출은 꺼진 plane을 활성화하지 않는다.

### 토폴로지와 discovery

```c
zlink_bind_result_t zlink_spot_node_bind(void *node, const char *endpoint);
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

- `zlink_spot_node_bind()`는 node endpoint를 bind한다.
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
  ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE = 4
} zlink_spot_dispatch_event_t;

typedef enum zlink_spot_dispatch_subject_kind_t {
  ZLINK_SPOT_DISPATCH_SUBJECT_SPOT           = 1,
  ZLINK_SPOT_DISPATCH_SUBJECT_TIMER          = 2,
  ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER = 3
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

dispatch 우선순위는 아래 순서로 고정한다.

1. `SUBSCRIBE_READABLE`
2. `ROUTED_READABLE`
3. `CHANNEL_REPLY_READABLE`
4. `TIMER_READABLE`

`CHANNEL_REPLY_READABLE` 이벤트가 뜻하는 것은 "해당 attached dealer를 통해 시작한
channel request 중 user callback을 실행할 준비가 끝난 completion이 하나 이상 있다"는
것이다. raw dealer frame 수신 여부가 아니라, request completion 준비 상태를 알린다.

`SUBSCRIBE_READABLE`과 `ROUTED_READABLE`은 메시지 개수 이벤트가 아니라
readiness 이벤트다.

- callback 1회가 메시지 1개를 뜻하지 않는다.
- 이미 readable인 동안 같은 plane으로 메시지가 더 들어오더라도, dispatch callback
  개수와 메시지 개수는 1:1로 대응하지 않을 수 있다.
- 호출자는 해당 plane에서 `EAGAIN`이 나올 때까지 drain하는 방식으로 처리해야 한다.
- `SUBSCRIBE_READABLE`은 node-wide broad fan-out이 아니라, 해당 `Spot`이 실제로
  subscribe recv를 할 수 있을 때만 올라와야 한다.

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

- `zlink_spot_handler()`는 routed callback surface다.
- `zlink_spot_dispatch_event_handler()`는 readable/send-ready plane을 알리는
  dispatch callback이다.
- 두 handler는 상호 배타적이다. 하나가 등록된 상태에서 다른 쪽을 등록하면
  `ZLINK_HANDLER_BUSY`로 실패한다.

### Poller와의 관계

현재 public poller 계약은 바뀌지 않았다.

- `zlink_poller_event_t`는 owner spot / event kind / subject를 함께 표현하지 않는다.
- 따라서 현재 공개 계약에서 `Spot` 직접 등록으로 dispatch와 같은 의미를 받는
  poller 표면은 아직 없다.
- `Spot`의 subscribe/routed/timer/channel-reply readiness를 한 callback으로 다루려면
  `zlink_spot_dispatch_event_handler()`를 사용해야 한다.

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
- 반환 코드 의미는 `doc/draft/spot-routed-request-api.ko.md` 8절을 참조한다.
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
```

- SPOT node monitor 전용 별도 recv API는 현재 공개 계약에 없다.
- snapshot/query API를 사용해 상태를 관찰한다.
- `zlink_spot_node_status_t`와 peer/subject entry 구조체의 `service_name`
  필드는 현재 공개 이름을 유지한다.
- `zlink_spot_node_status_t.disconnected_sub_target_count`는 queue hard limit 등으로
  끊긴 local subscribe delivery target 수를 나타낸다.
- `zlink_spot_node_status_t.disconnected_routed_target_count`는 queue hard limit 등으로
  끊긴 routed delivery target 수를 나타낸다.

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
- `CHANNEL_REPLY_READABLE` callback에서 `subject`를 일반 dealer처럼 raw recv하지
  않는다. `zlink_spot_channel_reply_progress_from()`를 통해서만 drain한다.
