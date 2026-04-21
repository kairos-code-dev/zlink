[English](spot.md) | [한국어](spot.ko.md)

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [서비스 공통](README.ko.md)

# SPOT

이 문서는 현재 공개 헤더 `core/include/zlink.h`에 들어 있는 SPOT 계약만 정리한다.
구현 전 설계나 개편 방향은 `doc/spec/draft/` 아래 초안 문서를 본다.

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
void *zlink_spot_node_new(void *ctx);
zlink_close_result_t zlink_spot_node_destroy(void **node_p);

void *zlink_spot_new(void *node);
zlink_close_result_t zlink_spot_destroy(void **spot_p);
```

- `zlink_spot_node_new()`는 새 SPOT node runtime을 만든다.
- `zlink_spot_new()`는 기존 `SpotNode`를 빌려 unified `Spot` facade를 만든다.
- `zlink_spot_destroy()`는 facade만 닫는다.
- `zlink_spot_node_destroy()`는 node와 내부 runtime 자원을 정리한다.
- discovery에 attach된 node는 보통 `zlink_discovery_destroy()` 흐름에서 함께 정리된다.

## SpotNode 계약

### 토폴로지와 discovery

```c
zlink_bind_result_t zlink_spot_node_bind(void *node, const char *endpoint);
zlink_connect_result_t zlink_spot_node_connect_peer(void *node,
                                                    const char *peer_endpoint);
zlink_connect_result_t zlink_spot_node_disconnect_peer(void *node,
                                                       const char *peer_endpoint);
zlink_config_result_t zlink_spot_node_attach_discovery(void *node,
                                                       void *discovery);
```

- `zlink_spot_node_bind()`는 node endpoint를 bind한다.
- `zlink_spot_node_connect_peer()`와 `zlink_spot_node_disconnect_peer()`는
  수동 mesh 연결에만 쓴다.
- discovery가 이미 attach된 node에서 `connect_peer()` 또는
  `disconnect_peer()`를 호출하면 `EBUSY`로 실패한다.
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
- channel request의 reply 귀속은 요청을 보낸 `DEALER`에 고정된다.
- `Spot`에서는 direct `rid`로 `ROUTER`를 지정해 ordinary one-way send를 하지 않는다.
  direct routed request 시작은 아래 별도 섹션을 참조한다.

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
- 수신한 요청이 SPOT origin이면 `zlink_spot_reply_spot()`으로 reply한다.
- 수신한 요청이 ROUTER origin이면 `zlink_spot_reply_router()`로 reply한다.
- reply 경로는 수신 이벤트가 알려준 concrete source address를 그대로 사용해야 한다.

### Handler

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

## Spot routed request 시작

`Spot`은 routed request를 직접 시작할 수 있다. one-way direct send는 공개
표면에 없지만, request/reply 짝을 맞추기 위해 아래 두 경로를 공개한다.

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
```

- `zlink_spot_request_spot()`은 `zlink_spot_reply_spot(_part)`와 reply 짝을 이룬다.
- `zlink_spot_request_router()`는 `zlink_router_reply_spot(_part)`와 reply 짝을 이룬다.
- submit이 `ZLINK_SUBMIT_OK`이면 `handler_`가 정확히 한 번 호출된다.
- 그 외 반환값이면 handler는 등록되지 않는다.
- 반환 코드 의미는 `doc/draft/spot-routed-request-api.ko.md` 8절을 참조한다.

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
void *zlink_service_monitor_open(
  void *target,
  const zlink_service_monitor_open_options_t *options);

zlink_handler_result_t zlink_service_monitor_handler(
  void *monitor,
  zlink_service_monitor_handler_fn handler,
  void *userdata);

zlink_recv_result_t zlink_service_monitor_recv(
  void *monitor,
  zlink_service_monitor_event_t *out,
  zlink_recv_flags_t flags);

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
- service monitor와 snapshot/query API를 사용해 상태를 관찰한다.
- `zlink_spot_node_status_t`와 peer/subject entry 구조체의 `service_name`
  필드는 현재 공개 이름을 유지한다.

## 제약 요약

- `SpotNode` mesh peer 자동 연결 대상은 SPOT discovery peer뿐이다.
- 일반 socket service provider는 `SpotNode` mesh peer로 섞이지 않는다.
- channel 호출은 attach된 `DEALER`로만 처리한다.
- `SpotNode.router`를 channel 호출 경로로 우회해서 쓰지 않는다.
- discovery attach와 수동 peer connect를 같은 peer 관계에 동시에 섞지 않는다.
- attach 함수는 socket 생성과 connect를 대신하지 않는다.
