[English](spot.md) | [한국어](spot.ko.md)

[스펙 목차](../README.ko.md) · [코어 목차](README.ko.md)

# SPOT

### 용어

| 용어 | 설명 |
|------|------|
| SpotNode | SPOT mesh 토폴로지와 lifecycle을 관리하는 소유자 핸들 |
| Spot (facade) | SpotNode 위에 올라가는 publish/subscribe 통합 인터페이스 |
| mesh | SpotNode 간 자동 구성되는 PUB/SUB 네트워크 |
| fanout | 수신한 메시지를 로컬 subscriber에게 분배하는 내부 경로 |
| HWM (High Water Mark) | 송신/수신 큐 최대 용량. 초과 시 backpressure 발생 |
| peer batching | 같은 topic의 작은 메시지를 모아 하나의 batch frame으로 peer에 전송하는 내부 최적화 |
| routing_id | 피어를 식별하는 고유 바이트 열 (최대 255바이트) |
| inproc | 동일 프로세스 내 스레드 간 통신 transport |

SPOT public API는 두 계층으로 정리됩니다.

- `SpotNode`: bind/connect/discovery/TLS wiring owner
- `Spot`: `SpotNode`에 attach되는 unified pub/sub facade

현재 public surface에는 standalone `zlink_spot_pub_*` / `zlink_spot_sub_*`
생성자와 destroy, option, monitor API가 없습니다.

## I/O 모델

`SpotNode`와 unified `Spot` 모두 **recv 모드**로 시작하고,
`zlink_subscribe_handler()`로 receive surface를 callback 모드로 **일방 전환**
합니다. send-ready는 별도 축입니다.

| 동작 | Recv 모드 (기본) | Callback 모드 |
|------|-----------------|--------------|
| **SpotNode 수신** | *(미노출 — unified Spot 사용)* | *(미노출 — unified Spot 사용)* |
| **Spot 수신** | `zlink_subscribe()` | `subscribe_handler()` 콜백 |
| **읽기 poller** | `ZLINK_POLLIN` | `EBUSY` |
| **Send-ready** | poller 또는 `send_ready_handler()` | poller 또는 `send_ready_handler()` |

- `zlink_send_ready_handler()`는 receive callback 선행 조건이 없습니다.
- send-ready attach 이후 data-plane `ZLINK_POLLOUT` poller는 `EBUSY`로 실패합니다.
- receive callback attach 이후 `zlink_subscribe()`와 data-plane `ZLINK_POLLIN`은 `EBUSY`로 실패합니다.
- `publish()`는 두 모드 모두에서 동작합니다.

## 현재 public surface

### SpotNode

```c
void *zlink_spot_node_new(void *ctx);
int zlink_spot_node_destroy(void **node_p);

int zlink_spot_node_bind(void *node, const char *endpoint);
int zlink_spot_node_connect_peer(void *node, const char *endpoint);
int zlink_spot_node_disconnect_peer(void *node,
                                        const char *endpoint);
int zlink_spot_node_attach_discovery(void *node, void *discovery);

int zlink_set_tls_server(void *node,
                         const char *cert,
                         const char *key,
                         int require_client_cert);
int zlink_set_tls_client(void *node,
                         const char *ca_cert,
                         const char *hostname,
                         int trust_system);

int zlink_set_option(void *node,
                     zlink_option_t option,
                     const void *optval,
                     size_t optvallen);
int zlink_get_option(void *node,
                     zlink_option_t option,
                     void *optval,
                     size_t *optvallen);

int zlink_set_routing_id(void *node,
                         const void *data,
                         size_t size);
int zlink_get_routing_id(void *node,
                         zlink_routing_id_t *out);
```

`SpotNode`는 topology 및 lifecycle owner입니다. `service_name`은 연결된
Discovery 인스턴스에서 결정됩니다. SpotNode는 generic data-plane facade를
직접 노출하지 않습니다. publish/subscribe/recv callback API는
`zlink_spot_new(node)`로 unified `Spot` facade를 만들어 사용하세요.
TLS/WSS 설정도 `SpotNode`의 책임이며, `zlink_set_tls_server()` /
`zlink_set_tls_client()`는 bind/connect 전에 node handle에 적용해야 합니다.

### Unified Spot

```c
void *zlink_spot_new(void *node);
int zlink_spot_destroy(void **spot_p);

zlink_submit_result_t zlink_publish(void *spot,
                  const char *topic_id,
                  zlink_msg_t *parts,
                  size_t part_count,
                  zlink_send_flags_t flags);
int zlink_subscribe(void *subject_,
                    zlink_routing_id_t *source_rid_out_,
                    zlink_msg_t **parts_out_,
                    size_t *part_count_out_,
                    char *topic_id_out_,
                    size_t *topic_id_len_out_,
                    zlink_send_flags_t flags_);
int zlink_set_subscription (void *spot, const char *filter);
int zlink_unset_subscription (void *spot, const char *filter);
int zlink_subscription_at(void *spot, size_t index,
                          char *buf, size_t *len,
                          int *is_pattern);

int zlink_send_ready_handler(
  void *spot,
  zlink_send_ready_handler_fn handler,
  void *userdata);

int zlink_set_pub_option(void *spot,
                         zlink_pub_option_t option,
                         const void *optval,
                         size_t optvallen);
int zlink_get_pub_option(void *spot,
                         zlink_pub_option_t option,
                         void *optval,
                         size_t *optvallen);
int zlink_set_sub_option(void *spot,
                         zlink_sub_option_t option,
                         const void *optval,
                         size_t optvallen);
int zlink_get_sub_option(void *spot,
                         zlink_sub_option_t option,
                         void *optval,
                         size_t *optvallen);

int zlink_set_option(void *spot,
                     zlink_option_t option,
                     const void *optval,
                     size_t optvallen);
int zlink_get_option(void *spot,
                     zlink_option_t option,
                     void *optval,
                     size_t *optvallen);

int zlink_set_routing_id(void *spot,
                         const void *data,
                         size_t size);
int zlink_get_routing_id(void *spot,
                         zlink_routing_id_t *out);
```

`zlink_spot_new(node)`는 기존 spot node를 빌리는 unified facade를
생성합니다. publish와 subscribe 동작을 모두 제공합니다. publish-only 혹은
subscribe-only public child handle은 더 이상 제공하지 않습니다.

unified `Spot`은 transport security 설정 surface가 아닙니다. unified `Spot`
handle에 `zlink_set_tls_server()` 또는 `zlink_set_tls_client()`를 호출하면
`ENOTSUP`로 실패합니다. TLS/WSS는 backing `SpotNode`에 먼저 설정해야 합니다.

`zlink_subscribe()`는 recv 모드에서 동기식 pull 방식의 수신을 제공합니다.
다음 메시지와 source routing ID, topic을 반환합니다. 성공 시 `source_rid_out_`,
`parts_out_`, `topic_id_out_`이 채워집니다. non-blocking 동작은 `flags_`에
`ZLINK_DONTWAIT`를 전달합니다. callback 모드에서는 `EBUSY`로 실패합니다.
다만 같은 `spot_`의 활성 `zlink_spot_dispatch_event_handler()` callback 안에서
readable subscribe plane을 비우는 경우에는 호출할 수 있습니다.

관찰/운영 상태 확인은 `zlink_spot_node_status_snapshot()`,
`zlink_spot_node_peers_snapshot()`, `zlink_spot_node_subjects_snapshot()`을
사용합니다.

## SPOT routed request-reply

SPOT request-reply 는 publish/subscribe 경로와 별도입니다. 이 표면은 topic 을
통하지 않고, ZMP control part 로 목적지와 request-reply 문맥을 함께 실어
보냅니다.

핵심 규칙:

- ordinary `zlink_publish()` / `zlink_subscribe()` 와 섞이지 않습니다.
- wire 순서는 `SPOT routed envelope -> request-reply envelope -> payload` 입니다.
- reply 는 request handler 가 알려준 주소와 `request_seq` 를 그대로 사용합니다.
- `timeout_ms = 0` 이면 구현 기본값 `5000ms` 를 사용합니다.
- high-level completion 은 첫 reply 1건으로 끝납니다.

### 콜백 타입

```c
typedef void (*zlink_spot_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  const zlink_routing_id_t *spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);
```

`zlink_spot_handler_fn` 은 ordinary routed message 와 request-reply message 를
같이 받습니다. `request_seq = 0` 이면 ordinary message 이고,
`request_seq != 0` 이면 request-reply message 입니다.

### Spot 에서 시작하는 request

```c
zlink_submit_result_t zlink_spot_request_spot (void *spot_,
                             const zlink_routing_id_t *dest_node_rid_,
                             const zlink_routing_id_t *dest_spot_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             zlink_reply_handler_fn handler_,
                             void *userdata_,
                             zlink_send_flags_t flags_,
                             uint32_t timeout_ms_);

zlink_submit_result_t zlink_spot_request_router (void *spot_,
                               const zlink_routing_id_t *peer_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               zlink_reply_handler_fn handler_,
                               void *userdata_,
                               zlink_send_flags_t flags_,
                               uint32_t timeout_ms_);
```

- `zlink_spot_request_spot()` 은 destination node rid 와 destination spot rid
  를 모두 요구합니다.
- `zlink_spot_request_router()` 는 일반 `ROUTER` peer 로 보냅니다.
- 두 함수 모두 reply 는 `zlink_reply_handler_fn` 으로 비동기 완료됩니다.
- 두 함수 모두 request submit이 수락되면 `ZLINK_SUBMIT_OK`를 반환합니다.
  실패 시에는 `zlink_submit_result_t` 값을 반환합니다.

### Spot 에서 보내는 reply

```c
zlink_submit_result_t zlink_spot_reply_spot (void *spot_,
                           const zlink_routing_id_t *dest_node_rid_,
                           const zlink_routing_id_t *dest_spot_rid_,
                           uint64_t request_seq_,
                           zlink_msg_t *parts_,
                           size_t part_count_);

zlink_submit_result_t zlink_spot_reply_router (void *spot_,
                             const zlink_routing_id_t *peer_rid_,
                             uint64_t request_seq_,
                             zlink_msg_t *parts_,
                             size_t part_count_);
```

두 reply submit 함수는 성공 시 `ZLINK_SUBMIT_OK`를 반환합니다. 실패 시에는
`zlink_submit_result_t` 값을 반환하고, 상세 내부 errno는 진단을 위해
`zlink_errno()`로 유지됩니다.

### Spot typed receive callback

```c
int zlink_spot_handler (
  void *spot_, zlink_spot_handler_fn handler_, void *userdata_);
```

한 `Spot` 에 한 개의 typed receive callback 만 설치할 수 있습니다. ordinary
routed message 와 request-reply message 를 같은 callback 에서 받고,
`request_seq` 값으로 구분합니다.

### Spot dispatch event callback

```c
typedef enum zlink_spot_dispatch_event_t
{
    ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE = 1,
    ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE    = 2,
    ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE     = 3
} zlink_spot_dispatch_event_t;

typedef void (*zlink_spot_dispatch_event_handler_fn) (
  void *spot_,
  zlink_spot_dispatch_event_t event_,
  void *userdata_);

zlink_handler_result_t zlink_spot_dispatch_event_handler (
  void *spot_,
  zlink_spot_dispatch_event_handler_fn handler_,
  void *userdata_);
```

`zlink_spot_dispatch_event_handler_fn` 은 Spot dispatch callback 입니다.
같은 `spot_` 에 대해서는 callback delivery 가 순차적이어야 합니다. 구현은
같은 `spot_` 에 대해 이 callback 을 동시에 호출하거나, callback 실행 중에
다시 재진입 호출해서는 안 됩니다. 이전 callback 이 반환된 뒤에만 다음
dispatch callback 을 같은 `spot_` 에 대해 호출할 수 있습니다.

이 계약은 public API contract 입니다. 토픽 수신, routed 수신, timer fire 가
내부적으로 서로 다른 실행 경로에서 발생하더라도 callback delivery 는 `spot_`
단위로 직렬화되어야 합니다. 호출자는 dispatch callback 안에서 Spot 관련
메시징을 순차적으로 처리할 수 있어야 합니다.

같은 `spot_`의 이 dispatch callback 이 실행 중일 때 호출자는 그 `spot_`에
대해 event 로 알려진 readable plane을 동기식 recv surface로 비울 수 있습니다.
- `SUBSCRIBE_READABLE` -> `zlink_subscribe()`
- `ROUTED_READABLE` -> `zlink_spot_recv()`
- `TIMER_READABLE` -> `zlink_timer_recv()`

이 예외는 같은 `spot_`의 활성 dispatch callback 문맥에만 적용됩니다.
그 밖의 문맥에서는 recv 와 callback 의 배타 규칙이 그대로 적용됩니다.

이 직렬화 범위는 `spot_` 단위입니다. 서로 다른 `spot_` 사이에는 전역 직렬화를
요구하지 않습니다. 구현은 서로 다른 Spot 의 dispatch callback 을 병렬로
실행할 수 있어야 하며, 이 병렬성 때문에 같은 `spot_` 의 순차 처리 계약이
깨지면 안 됩니다.

구현은 고성능 데이터 경로를 유지할 수 있어야 합니다. 이를 위해 내부 topic,
routed, timer producer 경로와 user callback 실행 경로를 분리할 수 있습니다.
예를 들어 `spot_` 단위 queue, mailbox, scheduler 를 사용해 callback delivery 를
직렬화하는 것은 허용됩니다. 다만 어떤 내부 방식을 쓰더라도 public contract 는
같습니다. 호출자는 같은 `spot_` 에 대해 메시징을 순차적으로 처리할 수 있어야
하고, 서로 다른 `spot_` 은 병렬 처리될 수 있어야 합니다.

dispatch event 는 readability notification 이며, callback 1회가 논리 메시지
1개 또는 timer fire 1개를 보장하지는 않습니다. 구현은 여러 readiness cause 를
한 번의 callback 으로 합칠 수 있습니다. 호출자는 callback 안에서 해당 plane 을
더 이상 읽을 것이 없을 때까지 drain 하는 것을 기대할 수 있어야 합니다.

### Router 와 SPOT 사이 request-reply

```c
zlink_submit_result_t zlink_router_request_spot (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

zlink_submit_result_t zlink_router_reply_spot (void *router_,
                             const zlink_routing_id_t *dest_node_rid_,
                             const zlink_routing_id_t *dest_spot_rid_,
                             uint64_t request_seq_,
                             zlink_msg_t *parts_,
                             size_t part_count_);

zlink_handler_result_t zlink_router_handler (
  void *router_,
  zlink_router_handler_fn handler_,
  void *userdata_);

zlink_recv_result_t zlink_router_recv (
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);
```

이 표면으로 `router -> spot`, `spot -> router` 조합을 같은 계약으로 맞춥니다.
`ROUTER` 쪽 reply 주소는 transport `peer_rid` 가 아니라
`dest_node_rid + dest_spot_rid + request_seq` 조합입니다.

`ROUTER` 수신 표면은 하나만 둡니다.

- ordinary `ROUTER` 메시지는 `source_node_rid_` 에 peer routing id 를 담고,
  `source_spot_rid_` 는 빈 routing id 로 돌려줍니다.
- SPOT에서 온 메시지는 `source_node_rid_` 와 `source_spot_rid_` 를 함께
  reply 주소로 사용합니다.
- `request_seq_ == 0` 이면 ordinary direct send 입니다.
- `request_seq_ != 0` 이면 request-reply 트래픽입니다.

별도의 `zlink_router_spot_handler()` / `zlink_router_spot_recv()` 계약은 두지
않습니다.

`zlink_router_request_spot()`은 request submit이 수락되면
`ZLINK_SUBMIT_OK`를 반환합니다. 실패 시에는 `zlink_submit_result_t` 값을
반환합니다. reply completion은 별도로 `zlink_reply_handler_fn`을 통해
전달됩니다. `zlink_router_reply_spot()`은 reply submit 결과를 같은 enum으로
반환합니다.

## SpotNode Internal Data-Plane HWM

SpotNode는 다음 내부 data-plane을 가집니다.

- `ingress` receive queue
- `fanout` local publish queue
- `mesh_pub` peer publish queue
- `mesh_xsub` peer receive queue

SpotNode 내부 HWM 설정은 일반 `zlink_set_option(..., SNDHWM/RCVHWM)` 이 아니라
`zlink_set_spot_node_option()` 으로 다룹니다. 이유는 SpotNode 안에 topic
publish/subscribe 경로와 routed 경로가 함께 있고, 두 경로가 같은 큐 한도를
공유하면 한쪽 폭주가 다른 쪽 지연을 망칠 수 있기 때문입니다.

현재 공개 설정 축은 다음 네 가지입니다.

- `TOPIC_SEND_HWM`
- `TOPIC_RECV_HWM`
- `ROUTED_SEND_HWM`
- `ROUTED_RECV_HWM`

이 값은 내부 topic/routed 소켓군에 각각 매핑됩니다. public API는 내부 소켓
이름을 직접 노출하지 않고, SpotNode 수준에서 topic 과 routed 두 방향만
구분합니다.

## callback 계약

```c
typedef void (*zlink_subscribe_handler_fn)(const zlink_routing_id_t *source_rid,
                                           const char *topic,
                                           size_t topic_len,
                                           zlink_msg_t *parts,
                                           size_t part_count,
                                           void *userdata);
```

- callback은 `zlink_subscribe_handler(node_or_spot, handler, userdata)`로
  설치합니다.
- handle은 recv 모드로 시작하고 callback 모드로 한 번만 전환됩니다.
- callback 모드 전환 후 `zlink_subscribe()`는
  `EBUSY`로 실패합니다.
- callback은 전달받은 `parts`의 ownership을 소비해야 합니다.

## 옵션 요약

| 대상 | 설정/조회 API | 옵션 |
|------|-------------|------|
| unified `spot` publish 쪽 | `set_pub_option` / `get_pub_option` | `ZLINK_PUB_OPT_*` |
| unified `spot` subscribe 쪽 | `set_sub_option` / `get_sub_option` | `ZLINK_SUB_OPT_*` |
| common (pub 쪽) | `set_option` / `get_option` | `SNDHWM`, `SNDTIMEO`, `LINGER`, `SNDBUF`, `RCVBUF` |
| common (sub 쪽) | `set_option` / `get_option` | `RCVHWM`, `RCVTIMEO`, `LINGER`, `SNDBUF`, `RCVBUF` |
| routing_id (pub 쪽) | `set_routing_id` / `get_routing_id` | -- |
| subscription 관리 | `set_subscription` / `unset_subscription` / `subscription_at` | -- |

## 모니터링

SPOT은 더 이상 public service-monitor surface를 노출하지 않습니다.
`zlink_service_monitor_open()` 대신 SpotNode status/query API를 사용합니다.

## 스냅샷 / 인트로스펙션

SpotNode는 운영 건강 모니터링과 진단을 위한 lock-free point-in-time 스냅샷
API를 제공합니다. 이벤트 기반 모니터를 보완하는 pull 방식의 조회입니다.

### SpotNode Status Snapshot

```c
int zlink_spot_node_status_snapshot(void *node,
                                    zlink_spot_node_status_t *out);
```

SpotNode의 단일 행 운영 건강 요약을 반환합니다.

#### zlink_spot_node_status_t

```c
typedef struct zlink_spot_node_status_t
{
    char service_name[256];
    char local_endpoint[256];
    zlink_routing_id_t node_routing_id;
    zlink_spot_node_state_t state;
    uint32_t configured_peer_count;
    uint32_t active_peer_count;
    uint32_t connected_peer_count;
    uint32_t subject_count;
    uint32_t ready_subject_count;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_spot_node_status_t;
```

| 필드 | 설명 |
|------|------|
| `service_name` | 연결된 Discovery의 null 종료 서비스 이름. |
| `local_endpoint` | null 종료 로컬 바인드 엔드포인트. |
| `node_routing_id` | 이 SpotNode의 라우팅 아이덴티티. |
| `state` | `IDLE`, `CONNECTING`, `PARTIAL_READY`, `READY`, 또는 `ERROR`. |
| `configured_peer_count` | 구성된 피어 수 (수동 + discovery). |
| `active_peer_count` | 연결 중이거나 연결된 피어 수. |
| `connected_peer_count` | 현재 연결된 피어 수. |
| `subject_count` | 총 구독 subject 수. |
| `ready_subject_count` | ready 피어가 하나 이상 있는 subject 수. |
| `last_error` | 마지막 기록된 에러 코드, 또는 0. |
| `last_changed_ms` | 마지막 상태 변경 시점 (에포크 ms). |

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

---

### SpotNode Peers Snapshot / Query

```c
int zlink_spot_node_peers_snapshot(void *node,
                                   zlink_spot_node_peer_entry_t *entries,
                                   size_t *count);

int zlink_spot_node_peers_query(void *node,
                                const zlink_spot_node_peer_filter_t *filter,
                                zlink_spot_node_peer_entry_t *entries,
                                size_t *count);
```

`peers_snapshot`은 모든 피어를 반환합니다. `peers_query`는 endpoint, source,
state로 필터링을 지원합니다.

**버퍼 규약:** `entries = NULL`을 전달하면 필요한 개수만 반환합니다. 다음
호출에서 호출자가 할당한 버퍼를 제공합니다. 버퍼가 부족하면 `-1`을 반환하고
`errno = ENOBUFS`, `*count`에 필요한 용량을 설정합니다.

결과는 `peer_endpoint` 오름차순으로 정렬됩니다.

#### zlink_spot_node_peer_entry_t

```c
typedef struct zlink_spot_node_peer_entry_t
{
    char service_name[256];
    char local_endpoint[256];
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_state_t state;
    uint64_t connected_since_ms;
    uint64_t last_changed_ms;
} zlink_spot_node_peer_entry_t;
```

| 필드 | 설명 |
|------|------|
| `service_name` | null 종료 서비스 이름. |
| `local_endpoint` | null 종료 로컬 엔드포인트. |
| `peer_endpoint` | null 종료 피어 엔드포인트. |
| `source` | `MANUAL`, `DISCOVERY`, 또는 `MIXED`. |
| `state` | `CONFIGURED`, `CONNECTING`, 또는 `CONNECTED`. |
| `connected_since_ms` | 피어 연결 시점 (에포크 ms, 미연결 시 0). |
| `last_changed_ms` | 이 피어의 마지막 상태 변경 시점 (에포크 ms). |

#### zlink_spot_node_peer_filter_t

```c
typedef struct zlink_spot_node_peer_filter_t
{
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_state_t state;
} zlink_spot_node_peer_filter_t;
```

0이 아닌 값으로 설정된 필드를 기준으로 필터링합니다. 0인 필드는
와일드카드입니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

---

### SpotNode Subjects Snapshot

```c
int zlink_spot_node_subjects_snapshot(void *node,
                                      const zlink_spot_node_subject_filter_t *filter,
                                      zlink_spot_node_subject_entry_t *entries,
                                      size_t *count);
```

SUB subject readiness 정보를 반환합니다. v1은 `ZLINK_SPOT_ROLE_SUB`만
지원하며, PUB role을 필터에 지정하면 `ENOTSUP`을 반환합니다.

**버퍼 규약:** peers snapshot과 동일 -- `entries = NULL`로 개수 조회;
버퍼 부족 시 `ENOBUFS`와 필요 개수 반환.

#### zlink_spot_node_subject_entry_t

```c
typedef struct zlink_spot_node_subject_entry_t
{
    zlink_spot_role_t role;
    char subject[256];
    uint32_t subject_kind;
    uint32_t ready_peer_count;
    uint32_t active_peer_count;
    uint64_t last_changed_ms;
} zlink_spot_node_subject_entry_t;
```

| 필드 | 설명 |
|------|------|
| `role` | `ZLINK_SPOT_ROLE_SUB` (v1 전용). |
| `subject` | null 종료 subject 문자열. |
| `subject_kind` | subject 종류 식별자. |
| `ready_peer_count` | 이 subject가 ready 상태인 피어 수. |
| `active_peer_count` | 이 subject를 제공 중인 피어 수. |
| `last_changed_ms` | 마지막 readiness 변경 시점 (에포크 ms). |

#### zlink_spot_node_subject_filter_t

```c
typedef struct zlink_spot_node_subject_filter_t
{
    zlink_spot_role_t role;
    char subject[256];
    uint32_t subject_kind;
} zlink_spot_node_subject_filter_t;
```

0이 아닌 값으로 설정된 필드를 기준으로 필터링합니다. 0인 필드는
와일드카드입니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

---

### 권장 모니터링 순서

1. `zlink_spot_node_status_snapshot()` -- 전체 건강 상태를 먼저 확인합니다.
2. `zlink_spot_node_peers_snapshot()` -- 피어 연결 상태를 점검합니다.
3. `zlink_spot_node_subjects_snapshot()` -- subject readiness를 확인합니다.

## SpotNode Peer Publish Batching 옵션

SpotNode는 peer publish 경로의 선택적 내부 batching을 제공합니다.
이 옵션들은 SpotNode handle에서 `zlink_set_spot_node_option()`으로 설정합니다.

### zlink_spot_node_option_t

```c
typedef enum zlink_spot_node_option_t {
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_ENABLE                  = 0x3601,
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_DELAY_MS                = 0x3602,
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_MAX_MESSAGES            = 0x3603,
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_MAX_BYTES               = 0x3604,
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_BYPASS_BYTES            = 0x3605,
    ZLINK_SPOT_NODE_OPT_PEER_UNBATCH_MAX_MESSAGES_PER_TURN = 0x3606,
    ZLINK_SPOT_NODE_OPT_PEER_UNBATCH_MAX_BYTES_PER_TURN    = 0x3607,
    ZLINK_SPOT_NODE_OPT_TOPIC_SEND_HWM                   = 0x3608,
    ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM                   = 0x3609,
    ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM                  = 0x360A,
    ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM                  = 0x360B
} zlink_spot_node_option_t;
```

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `PEER_BATCH_ENABLE` | `int` (bool) | 0 (비활성) | peer publish batching 활성화. homogeneous deployment에서 운영자 opt-in. |
| `PEER_BATCH_DELAY_MS` | `int` | 20 | topic bucket flush 최대 지연 (ms). |
| `PEER_BATCH_MAX_MESSAGES` | `int` | 32 | topic bucket당 최대 메시지 수. |
| `PEER_BATCH_MAX_BYTES` | `int` | 65536 | topic bucket당 최대 바이트. |
| `PEER_BATCH_BYPASS_BYTES` | `int` | 65536 | 이 encoded 크기 이상 메시지는 batching을 우회하여 즉시 전송. |
| `PEER_UNBATCH_MAX_MESSAGES_PER_TURN` | `int` | 32 | receiver측 I/O turn당 최대 unbatch 메시지 수. |
| `PEER_UNBATCH_MAX_BYTES_PER_TURN` | `int` | 65536 | receiver측 I/O turn당 최대 unbatch 바이트. |
| `TOPIC_SEND_HWM` | `int` | 0 (무제한) | 토픽 (pub/sub) 메시지 전송 high water mark. |
| `TOPIC_RECV_HWM` | `int` | 0 (무제한) | 토픽 (pub/sub) 메시지 수신 high water mark. |
| `ROUTED_SEND_HWM` | `int` | 0 (무제한) | routed (request-reply) 메시지 전송 high water mark. |
| `ROUTED_RECV_HWM` | `int` | 0 (무제한) | routed (request-reply) 메시지 수신 high water mark. |

사용법:

```c
void *node = zlink_spot_node_new(ctx);

int enabled = 1;
zlink_set_spot_node_option(node, ZLINK_SPOT_NODE_OPT_PEER_BATCH_ENABLE,
                 &enabled, sizeof(enabled));

int delay_ms = 10;
zlink_set_spot_node_option(node, ZLINK_SPOT_NODE_OPT_PEER_BATCH_DELAY_MS,
                 &delay_ms, sizeof(delay_ms));

zlink_spot_node_bind(node, "tcp://*:9000");
```

**v1 제약:** mesh의 모든 SpotNode가 동일 세대 binary를 실행해야 합니다
(homogeneous deployment). runtime capability negotiation은 없습니다.

**반환값:** `zlink_set_spot_node_option` / `zlink_get_spot_node_option`은
성공 시 0, 실패 시 -1을 반환합니다 (errno가 설정됨).

**스레드 안전성:** 옵션은 bind/connect 전에 설정해야 합니다.

## 제거된 public API

다음 계열은 현재 public SPOT surface에 포함되지 않습니다.

- `zlink_spot_pub_*`
- `zlink_spot_sub_*`
- `zlink_spot_publish_bytes`
- `zlink_spot_node_publish_bytes`
- `zlink_spot_sub_set_handler`
- `zlink_spot_node_default_pub`
- `zlink_spot_node_default_sub`
- `zlink_spot_set_pub_option` / `zlink_spot_set_sub_option`
- `zlink_spot_node_set_pub_option` / `zlink_spot_node_set_sub_option`
- `zlink_spot_send_ready_handler` / `zlink_spot_node_send_ready_handler`
- `zlink_spot_node_set_tls_server` / `zlink_spot_node_set_tls_client`

## 예시

### Callback 모드

```c
void on_spot_message(const zlink_routing_id_t *source_rid,
                     const char *topic,
                     size_t topic_len,
                     zlink_msg_t *parts,
                     size_t part_count,
                     void *userdata);

void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://127.0.0.1:5555");

void *spot = zlink_spot_new(node);
zlink_subscribe_handler(spot, on_spot_message, NULL);
zlink_set_subscription (spot, "room:lobby");

zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_publish(spot, "room:lobby", &part, 1, 0);

/* zlink_spot_destroy는 빌린 spot facade만 파괴합니다 */
zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
```

### Recv 모드

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://127.0.0.1:5555");

void *spot = zlink_spot_new(node);
zlink_set_subscription (spot, "room:lobby");

/* 발행 */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_publish(spot, "room:lobby", &part, 1, 0);

/* unified spot에서 수신 */
zlink_routing_id_t source_rid;
zlink_msg_t *recv_parts = NULL;
size_t recv_count = 0;
char topic_buf[256];
size_t topic_len = sizeof(topic_buf);
int rc = zlink_subscribe(spot, &source_rid, &recv_parts, &recv_count,
                         topic_buf, &topic_len, 0);
if (rc == 0) {
    printf("토픽: %.*s\n", (int)topic_len, topic_buf);
    for (size_t i = 0; i < recv_count; i++)
        zlink_msg_close(&recv_parts[i]);
}

/* zlink_spot_destroy는 빌린 spot facade만 파괴합니다 */
zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
```
