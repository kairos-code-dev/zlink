[English](spot.md) | [한국어](spot.ko.md)

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [서비스 공통](README.ko.md)

# SPOT

### 용어

| 용어 | 설명 |
|------|------|
| SpotNode | SPOT mesh와 service attachment의 수명, wiring, 운영 상태를 관리하는 소유자 핸들 |
| Spot (facade) | SpotNode 위에 올라가는 routed + pub/sub 통합 데이터 평면 인터페이스 |
| mesh | SpotNode 간 자동 구성되는 PUB/SUB 네트워크 |
| fanout | 수신한 메시지를 로컬 subscriber에게 분배하는 내부 경로 |
| HWM (High Water Mark) | 송신/수신 큐 최대 용량. 초과 시 backpressure 발생 |
| routing_id | 피어를 식별하는 고유 바이트 열 (최대 255바이트) |
| inproc | 동일 프로세스 내 스레드 간 통신 transport |

SPOT public API는 두 계층으로 정리됩니다.

- `SpotNode`: bind/connect/discovery/TLS wiring owner
- `Spot`: `SpotNode`에 attach되는 unified pub/sub facade

현재 public surface에는 standalone `zlink_spot_pub_*` / `zlink_spot_sub_*`
생성자와 destroy, option API가 없습니다. 대신 `SpotNode`와 unified `Spot` 위에
service-aware attach, send/request/publish/subscribe, node monitor recv 표면이
추가되었습니다.

## I/O 모델

`SpotNode`와 unified `Spot` 모두 **recv 모드**로 시작합니다. topic/routed/
timer readable 알림은 `zlink_spot_dispatch_event_handler()` 하나로 받고,
실제 payload는 대응 recv 함수(`zlink_subscribe()` / `zlink_spot_subscribe()`,
`zlink_spot_recv()`, `zlink_timer_recv()`)로 drain합니다. send-ready는 별도
축입니다.

| 동작 | Recv 모드 (기본) | Dispatch callback 모드 |
|------|-----------------|----------------------|
| **SpotNode 수신** | *(미노출 — unified Spot 사용)* | *(미노출 — unified Spot 사용)* |
| **Spot 수신** | `zlink_subscribe()` / `zlink_spot_subscribe()` / `zlink_spot_recv()` | `zlink_spot_dispatch_event_handler()` readable 알림 후 recv |
| **읽기 poller** | `ZLINK_POLLIN` | `EBUSY` |
| **Send-ready** | poller 또는 `zlink_send_ready_handler()` | poller 또는 `zlink_send_ready_handler()` |

- `zlink_send_ready_handler()`는 receive callback 선행 조건이 없습니다.
- send-ready attach 이후 data-plane `ZLINK_POLLOUT` poller는 `EBUSY`로 실패합니다.
- dispatch callback 모드로 전환된 뒤 `zlink_subscribe()`와 data-plane
  `ZLINK_POLLIN`은 일반 문맥에서 `EBUSY`로 실패합니다. 다만 같은 `spot_`의
  활성 `zlink_spot_dispatch_event_handler()` callback 안에서는 해당 readable
  plane을 비우기 위해 호출할 수 있습니다.
- `publish()`는 두 모드 모두에서 동작합니다.

## 현재 public surface

### SpotNode

```c
void *zlink_spot_node_new(void *ctx);
zlink_close_result_t zlink_spot_node_destroy(void **node_p);

zlink_bind_result_t zlink_spot_node_bind(void *node, const char *endpoint);
zlink_connect_result_t zlink_spot_node_connect_peer(void *node, const char *endpoint);
zlink_connect_result_t zlink_spot_node_disconnect_peer(void *node,
                                                       const char *endpoint);
zlink_config_result_t zlink_spot_node_attach_discovery(void *node, void *discovery);

zlink_config_result_t zlink_set_tls_server(void *node,
                                           const char *cert,
                                           const char *key,
                                           int require_client_cert);
zlink_config_result_t zlink_set_tls_client(void *node,
                                           const char *ca_cert,
                                           const char *hostname,
                                           int trust_system);

zlink_config_result_t zlink_set_option(void *node,
                                       zlink_option_t option,
                                       const void *optval,
                                       size_t optvallen);
zlink_config_result_t zlink_get_option(void *node,
                                       zlink_option_t option,
                                       void *optval,
                                       size_t *optvallen);

zlink_config_result_t zlink_set_routing_id(void *node,
                                           const void *data,
                                           size_t size);
zlink_config_result_t zlink_get_routing_id(void *node,
                                           zlink_routing_id_t *out);
```

`SpotNode`는 SPOT mesh 연결 구조와 service attachment 수명 관리를 맡는 기준
핸들입니다. service-aware 모드에서는 `service_name`이 하나의 node 전체에 고정되지
않습니다. node는 연결된 Discovery 인스턴스나 수동 attach API를 통해 여러
`service_name`의 ROUTER/PUB/SUB attachment를 동시에 가질 수 있습니다.
SpotNode는 일반적인 data-plane 함수를 직접 노출하지 않습니다. publish/subscribe/
recv callback API는 `zlink_spot_new(node)`로 만든 통합 `Spot` 핸들을 통해
사용합니다.
TLS/WSS 설정도 `SpotNode`의 책임이며, `zlink_set_tls_server()` /
`zlink_set_tls_client()`는 bind/connect 전에 node handle에 적용해야 합니다.
`zlink_set_routing_id()`로 설정하는 `SpotNode` routing id는 bind endpoint와
별개인 **논리 주소**입니다. 이 값은 "어느 SpotNode인가"를 식별하는 공개 이름일
뿐이며, 특정 IP, 포트, 또는 머신 위치를 뜻하지 않습니다.

### Unified Spot

```c
void *zlink_spot_new(void *node);
zlink_close_result_t zlink_spot_destroy(void **spot_p);

zlink_submit_result_t zlink_publish(void *spot,
                  const char *topic_id,
                  zlink_msg_t *parts,
                  size_t part_count,
                  zlink_send_flags_t flags);
zlink_recv_result_t zlink_subscribe(void *subject_,
                                    zlink_routing_id_t *source_rid_out_,
                                    zlink_msg_t **parts_out_,
                                    size_t *part_count_out_,
                                    char *topic_id_out_,
                                    size_t *topic_id_len_out_,
                                    zlink_send_flags_t flags_);
zlink_config_result_t zlink_set_subscription (void *spot, const char *filter);
zlink_config_result_t zlink_unset_subscription (void *spot, const char *filter);
zlink_config_result_t zlink_subscription_at(void *spot, size_t index,
                                            char *buf, size_t *len,
                                            int *is_pattern);

zlink_handler_result_t zlink_send_ready_handler(
  void *spot,
  zlink_send_ready_handler_fn handler,
  void *userdata);

zlink_config_result_t zlink_set_pub_option(void *spot,
                                           zlink_pub_option_t option,
                                           const void *optval,
                                           size_t optvallen);
zlink_config_result_t zlink_get_pub_option(void *spot,
                                           zlink_pub_option_t option,
                                           void *optval,
                                           size_t *optvallen);
zlink_config_result_t zlink_set_sub_option(void *spot,
                                           zlink_sub_option_t option,
                                           const void *optval,
                                           size_t optvallen);
zlink_config_result_t zlink_get_sub_option(void *spot,
                                           zlink_sub_option_t option,
                                           void *optval,
                                           size_t *optvallen);

zlink_config_result_t zlink_set_option(void *spot,
                                       zlink_option_t option,
                                       const void *optval,
                                       size_t optvallen);
zlink_config_result_t zlink_get_option(void *spot,
                                       zlink_option_t option,
                                       void *optval,
                                       size_t *optvallen);

zlink_config_result_t zlink_set_routing_id(void *spot,
                                           const void *data,
                                           size_t size);
zlink_config_result_t zlink_get_routing_id(void *spot,
                                           zlink_routing_id_t *out);
```

`zlink_spot_new(node)`는 기존 spot node를 빌리는 unified facade를
생성합니다. publish와 subscribe 동작을 모두 제공합니다. publish-only 혹은
subscribe-only 전용 공개 child handle은 더 이상 제공하지 않습니다.
`zlink_set_routing_id()`로 설정하는 `Spot` routing id 역시 transport endpoint가
아닌 **논리 주소**입니다. 호출자는 원하는 이름을 직접 줄 수 있고, routed
경로에서는 이 값이 "어느 Spot인가"를 식별하는 이름으로 사용됩니다.

unified `Spot`은 transport security 설정 surface가 아닙니다. unified `Spot`
handle에 `zlink_set_tls_server()` 또는 `zlink_set_tls_client()`를 호출하면
`ENOTSUP`로 실패합니다. TLS/WSS는 backing `SpotNode`에 먼저 설정해야 합니다.

`zlink_subscribe()`는 recv 모드에서 동기식 pull 방식의 수신을 제공합니다.
다음 메시지와 source routing ID, topic을 반환합니다. 성공 시 `source_rid_out_`,
`parts_out_`, `topic_id_out_`이 채워집니다. non-blocking 동작은 `flags_`에
`ZLINK_DONTWAIT`를 전달합니다. dispatch callback 모드에서는 일반 문맥에서
`EBUSY`로 실패합니다.
다만 같은 `spot_`의 활성 `zlink_spot_dispatch_event_handler()` callback 안에서
readable subscribe plane을 비우는 경우에는 호출할 수 있습니다.

관찰/운영 상태 확인은 `zlink_spot_node_status_snapshot()`,
`zlink_spot_node_peers_snapshot()`, `zlink_spot_node_subjects_snapshot()`,
`zlink_spot_node_service_attachment_count()`,
`zlink_spot_node_service_attachment_at()`,
`zlink_spot_node_monitor_recv()`를 사용합니다.

## Multi-Service Attachment Surface

service-aware SPOT은 한 `SpotNode` 아래에 여러 서비스 경로를 붙이고, unified
`Spot` 하나에서 이 경로들을 함께 다룹니다.

### SpotNode attach API

```c
zlink_config_result_t zlink_spot_node_attach_router (
  void *node_,
  const char *service_name_,
  void *router_);

zlink_config_result_t zlink_spot_node_attach_pubsub (
  void *node_,
  const char *service_name_,
  void *pub_,
  void *sub_);

zlink_config_result_t zlink_spot_node_attach_discovery (
  void *node_,
  void *discovery_);
```

- `attach_router`는 지정한 서비스 아래 ROUTER attachment를 등록합니다.
- `attach_pubsub`는 지정한 서비스 아래 `PUB + SUB` 한 쌍을 함께 등록합니다.
- 같은 소켓을 둘 이상의 서비스에 중복 attach할 수 없습니다.
- attach는 외부 소켓의 소유권을 가져오지 않습니다. node destroy가 수동 attach된
  소켓을 자동으로 destroy하지 않습니다.
- service-aware attachment 또는 socket-service Discovery가 붙은 node는 공개 facade
  `Spot` 하나만 허용합니다. 그런 node에서 두 번째 `zlink_spot_new(node)`는
  `EBUSY`로 실패합니다.
- 반대로 같은 node에 일반 facade가 둘 이상 먼저 만들어진 상태에서는
  `zlink_spot_node_attach_router()`, `zlink_spot_node_attach_pubsub()`,
  `zlink_spot_node_attach_discovery()`가 `EBUSY`로 실패합니다.
- socket-service Discovery attach는 서로 다른 `service_name` 여러 개를 같은 node에
  붙일 수 있지만, 같은 `service_name` Discovery 중복 attach는 허용하지 않습니다.

### Spot service-aware data-plane API

```c
zlink_submit_result_t zlink_spot_send_service (
  void *spot_,
  const char *service_name_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

zlink_submit_result_t zlink_spot_request_service (
  void *spot_,
  const char *service_name_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

zlink_submit_result_t zlink_spot_publish (
  void *spot_,
  const char *service_name_,
  const char *topic_id_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

zlink_recv_result_t zlink_spot_subscribe (
  void *spot_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);

zlink_recv_result_t zlink_spot_subscription_event (
  void *spot_,
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);
```

- `zlink_spot_send_service()`와 `zlink_spot_request_service()`는 같은 서비스의 active
  ROUTER 집합에서 send-ready 후보 하나를 round-robin으로 고릅니다.
- 지정한 서비스 자체가 없으면 `NOT_FOUND`, attachment는 있으나 현재 active 경로가
  없으면 `NOT_CONNECTED`로 정규화합니다.
- `zlink_spot_publish()`는 지정한 서비스의 active pub/sub pair를 사용합니다.
  서비스가 없으면 `NOT_FOUND`, pair가 없거나 현재 inactive이면 `NOT_CONNECTED`를
  반환합니다.
- `zlink_spot_subscribe()`와 `zlink_spot_subscription_event()`는 payload와 함께
  `service_name`을 돌려줍니다. pub/sub 경로의 `source_rid_out_`는 비어 있을 수
  있습니다.
- service-aware subscribe/readable 알림은 `zlink_spot_dispatch_event_handler()`의
  `ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE` plane으로 올라옵니다.

### Service attachment observation

```c
typedef struct zlink_spot_service_attachment_stats_t
{
    char service_name[256];
    uint32_t router_count;
    uint32_t pub_count;
    uint32_t sub_count;
    uint32_t auto_router_count;
    uint32_t auto_pub_count;
    uint32_t auto_sub_count;
} zlink_spot_service_attachment_stats_t;

zlink_config_result_t zlink_spot_node_service_attachment_count (
  void *node_,
  size_t *count_out_);

zlink_config_result_t zlink_spot_node_service_attachment_at (
  void *node_,
  size_t index_,
  zlink_spot_service_attachment_stats_t *out_);

zlink_recv_result_t zlink_spot_node_monitor_recv (
  void *node_,
  zlink_spot_service_monitor_event_t *out_,
  zlink_recv_flags_t flags_);
```

- attachment snapshot은 서비스별 수동 attach 개수와 Discovery가 공급한 자동
  attachment 개수를 함께 보여줍니다.
- `zlink_spot_node_monitor_recv()`는 서비스별 attachment monitor event를
  `service_name + role + monitor_event` 형태로 반환합니다.
- service-aware monitor event는 `Spot` dispatch readable plane에 섞이지 않습니다.
  monitor event의 소유자는 `spot_`이 아니라 `node_`입니다.

### service data-plane 실패 표

`zlink_spot_send_service()` / `zlink_spot_request_service()`:

| 케이스 | public result | 내부 errno |
|------|---------------|------------|
| `spot_ == NULL` 또는 잘못된 핸들 | `ZLINK_SUBMIT_INVALID_HANDLE` | `EFAULT` |
| `service_name_ == NULL` 또는 빈 문자열 | `ZLINK_SUBMIT_INVALID_ARGUMENT` | `EINVAL` |
| `parts_ == NULL`인데 `part_count_ > 0` | `ZLINK_SUBMIT_INVALID_ARGUMENT` | `EINVAL` |
| 지정한 `service_name_`에 attachment가 없음 | `ZLINK_SUBMIT_NOT_FOUND` | `ENOENT` |
| attachment는 있으나 active ROUTER가 없음 | `ZLINK_SUBMIT_NOT_CONNECTED` | `ENOTCONN` 또는 `EHOSTUNREACH` |
| active ROUTER는 있으나 send-ready 후보가 없음 | `ZLINK_SUBMIT_NOT_CONNECTED` | `ENOTCONN` 또는 `EHOSTUNREACH` |
| 선택된 ROUTER가 HWM에 걸림 | `ZLINK_SUBMIT_BACKPRESSURED` | `EAGAIN` |
| request sequence 공간 소진 | `ZLINK_SUBMIT_SEQ_EXHAUSTED` | `EBUSY` |
| context 종료 | `ZLINK_SUBMIT_TERMINATED` | `ETERM` |
| 그 외 내부 submit 실패 | 기존 submit enum 규칙 | 표준 errno-map |

request completion은 `zlink_request_result_t`를 따릅니다. submit 이후 reply가
오지 않으면 `ZLINK_REQUEST_TIMED_OUT`, remote "대상 없음" reply는
`ZLINK_REQUEST_NOT_FOUND`로 정규화됩니다.

`zlink_spot_publish()`:

| 케이스 | public result | 내부 errno |
|------|---------------|------------|
| `spot_ == NULL` 또는 잘못된 핸들 | `ZLINK_SUBMIT_INVALID_HANDLE` | `EFAULT` |
| `service_name_ == NULL` 또는 빈 문자열 | `ZLINK_SUBMIT_INVALID_ARGUMENT` | `EINVAL` |
| `topic_id_ == NULL` 또는 잘못된 topic | `ZLINK_SUBMIT_INVALID_ARGUMENT` | `EINVAL` |
| 지정한 `service_name_`에 attachment가 없음 | `ZLINK_SUBMIT_NOT_FOUND` | `ENOENT` |
| attachment는 있으나 active pub/sub pair가 없음 | `ZLINK_SUBMIT_NOT_CONNECTED` | `ENOTCONN` 또는 `EHOSTUNREACH` |
| 선택된 `PUB`가 HWM에 걸림 | `ZLINK_SUBMIT_BACKPRESSURED` | `EAGAIN` |
| context 종료 | `ZLINK_SUBMIT_TERMINATED` | `ETERM` |
| 그 외 내부 submit 실패 | 기존 submit enum 규칙 | 표준 errno-map |

`zlink_spot_subscribe()` / `zlink_spot_subscription_event()`는 일반 recv
모델을 따릅니다. 읽을 이벤트가 없으면 `ZLINK_RECV_NO_DATA`, 잘못된 핸들이면
`ZLINK_RECV_INVALID_HANDLE`, context 종료이면 `ZLINK_RECV_TERMINATED`,
그 외 내부 실패는 `ZLINK_RECV_INTERNAL_ERROR`입니다. "active attachment
없음" 상태는 recv 결과로 올리지 않고 attachment snapshot 또는 node monitor
에서 관찰합니다.

### attach_discovery 실패 표

| 케이스 | public result | 내부 errno |
|------|---------------|------------|
| `node_ == NULL` 또는 잘못된 핸들 | `ZLINK_CONFIG_INVALID_HANDLE` | `EFAULT` |
| `discovery_ == NULL` 또는 잘못된 핸들 | `ZLINK_CONFIG_INVALID_ARGUMENT` | `EINVAL` |
| Discovery 타입이 SPOT 자동 attach와 맞지 않음 | `ZLINK_CONFIG_NOT_SUPPORTED` | `ENOTSUP` |
| 같은 Discovery가 이미 다른 owner에 attach됨 | `ZLINK_CONFIG_BUSY` | `EBUSY` |
| 같은 node에 같은 `service_name` Discovery가 이미 attach됨 | `ZLINK_CONFIG_BUSY` | `EBUSY` |
| 같은 node에 공개 facade `Spot`이 둘 이상 이미 생성됨 | `ZLINK_CONFIG_BUSY` | `EBUSY` |
| Discovery view에 `pub`만 있음 | `ZLINK_CONFIG_INVALID_ARGUMENT` | `EINVAL` |
| Discovery view에 `sub`만 있음 | `ZLINK_CONFIG_INVALID_ARGUMENT` | `EINVAL` |
| Discovery view에 `router + pub`만 있음 | `ZLINK_CONFIG_INVALID_ARGUMENT` | `EINVAL` |
| Discovery view에 `router + sub`만 있음 | `ZLINK_CONFIG_INVALID_ARGUMENT` | `EINVAL` |
| 모든 서비스가 `router only` 또는 `router + pub + sub`이고 같은 서비스 중복 없음 | `ZLINK_CONFIG_OK` | `0` |

pub/sub 짝은 attach 시점에만 검증합니다. 운용 중 provider 변화로 짝이 깨지면
해당 서비스의 pub/sub pair는 active 집합에서 제외되고, 그 서비스에 대한 publish
또는 subscribe submit은 pair가 다시 완전해질 때까지 `NOT_CONNECTED`로
실패합니다. 짝이 복구되면 subscription filter가 replay된 뒤 active 집합으로
돌아옵니다.

## SpotNode admission 상태

`SpotNode`는 ROUTER와 같은 admission 상태 표면을 공유합니다. 점검 또는
롤링 재시작 같은 운영 상황에서 peer가 새 작업을 이 노드로 보내지 않게
하려면 `zlink_set_admission_state()`를 사용합니다.

```c
zlink_config_result_t zlink_set_admission_state (
  void *handle_,
  zlink_admission_state_t state_);

zlink_config_result_t zlink_get_admission_state (
  void *handle_,
  zlink_admission_state_t *state_out_);
```

특징:

- 기본값은 `ZLINK_ADMISSION_SERVING`이며, 점검 시점에
  `ZLINK_ADMISSION_DRAINING`으로 바꿀 수 있습니다. runtime에 양방향 전환을
  허용합니다.
- 로컬 SpotNode 자체의 recv/send/request/reply/handler-dispatch 동작은
  admission 상태와 무관합니다. `DRAINING`은 "내가 멈춘다"가 아니라 "남이
  나를 새 작업 대상으로 고르지 않게 한다"는 신호입니다.
- 상태는 SpotNode peer control 경로를 통해 연결된 peer에게 best-effort
  runtime 신호로 전파됩니다. peer는 자신의 cache를 갱신하고, 재연결 후에는
  현재 상태를 다시 동기화합니다.

peer 쪽 효과:

- 다른 노드에서 이 SpotNode를 대상으로 하는 SPOT direct request
  (`zlink_spot_request_spot()`, `zlink_router_request_spot()` 등)는
  대상이 `DRAINING`이면 `ZLINK_SUBMIT_NOT_ADMITTED`로 실패합니다.
- service-aware `zlink_spot_request_service()`는 같은 서비스 경로의 active
  ROUTER 후보를 고를 때 `DRAINING` 노드를 제외합니다. 후보가 모두
  `DRAINING`이면 `ZLINK_SUBMIT_NOT_ADMITTED`로 실패합니다.
- `zlink_spot_publish()`는 fan-out 의미를 가지므로 단일 peer admission
  으로 거절하지 않습니다.

관찰 경로:

- 다른 노드 쪽 service monitor에서
  `ZLINK_SERVICE_MONITOR_EVENT_PEER_ADMISSION_CHANGED`로 이 노드의 상태
  변화를 받을 수 있습니다.
- `zlink_spot_node_peers_snapshot()` / `zlink_spot_node_peers_query()`가
  돌려주는 `zlink_spot_node_peer_entry_t.admission_state`로 각 peer의
  현재 admission 상태를 직접 확인할 수 있습니다.

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

### Routed addressing 요약

SPOT direct send/request/reply의 표준 함수는 계속
`dest_node_rid + dest_spot_rid` 두 값을 직접 받습니다. 이 두 값은 실제 전송
단계에서 쓰이는 **최종 목적지 주소 쌍**입니다.

`SpotNode`와 `Spot`의 routing id는 둘 다 **논리 주소**이지만, SPOT 공개 API는
여전히 "어느 `SpotNode`로 보낼 것인가"와 "그 안의 어느 `Spot`으로 보낼 것인가"를
함께 받는 형태입니다.

호출자가 논리 `spot_rid`만 알고 시작하는 경우에도 별도의 SPOT send/request
함수를 추가로 쓰지 않습니다. 먼저 Discovery에 현재 담당 `SpotNode`를 물어본 뒤,
그 결과를 기존 함수에 넣는 방식으로 해석합니다.

```c
zlink_config_result_t zlink_discovery_resolve_spot (
  void *discovery,
  const zlink_routing_id_t *spot_rid,
  zlink_routing_id_t *owner_node_rid_out);
```

이 함수가 성공하면, 호출자는 반환된 `owner_node_rid_out`과 원래의 `spot_rid`를
묶어서 `zlink_spot_send_spot()`, `zlink_spot_request_spot()`,
`zlink_router_send_spot()`, `zlink_router_request_spot()` 같은 기존 함수에
전달하면 됩니다. 즉 `spot_rid`만으로 보내는 것처럼 보이더라도, 실제 제출 단계는
항상 기존 `dest_node_rid + dest_spot_rid` 경로로 내려갑니다.

- 어떤 `SpotNode`가 현재 그 `spot_rid`를 맡고 있는지에 대한 최종 기준:
  [registry.ko.md](registry.ko.md)
- Discovery가 이 값을 어떻게 조회하고 갱신하는지에 대한 규칙:
  [discovery.ko.md](discovery.ko.md)
- 공통 서비스 계층 전제:
  [README.ko.md](README.ko.md)

Discovery나 Registry를 붙이지 않은 수동 구성에서는 위 조회 함수를 쓸 수
없습니다. 이 경우에는 호출자가 `dest_node_rid + dest_spot_rid`를 직접 알고
있어야 합니다.

### reply 주소 규칙

request handler가 받은 `source_rid`, `spot_rid`, `request_seq`는 이미
답장을 보낼 대상이 확정된 **완성된 reply 주소**입니다.

- reply는 callback/recv에서 받은 source 주소를 그대로 사용해야
  합니다.
- reply를 보낼 때는 `zlink_discovery_resolve_spot()` 같은 새 조회를 다시 하면 안
  됩니다.
- 따라서 reply는 "`spot_rid`로 목적지를 다시 찾는 흐름"의 대상이 아닙니다.

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

- `zlink_spot_request_spot()`은 destination node rid와 destination spot rid를
  모두 요구합니다.
- 이 두 값은 실제 routed 전송 단계에서 쓰는 구체 목적지 쌍입니다. 상위 계층이
  논리 `spot_rid` 하나로 대상을 선택하더라도 실제 submit 직전에는 이 두 값으로
  확정되어 있어야 합니다.
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
`zlink_errno()`로 유지됩니다. `zlink_spot_reply_spot()`의 목적지 주소는 새로
조회한 결과가 아니라, request 수신 시 함께 얻은 완성된 reply 주소 쌍을 그대로
써야 합니다.

### Spot typed receive callback

```c
zlink_handler_result_t zlink_spot_handler (
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

구현은 고성능 데이터 경로를 유지할 수 있어야 합니다. 이를 위해 topic,
routed, timer producer 경로와 user callback 실행 경로는 분리됩니다.
`zlink_spot_dispatch_event_handler()` callback 은 I/O thread 에서 직접
실행되지 않으며, 전용 Spot worker runtime 에서 실행됩니다. worker 수는
context 옵션 `ZLINK_SPOT_WORKER_THREADS`로 조절합니다. 값 `0`은 자동 선택이고,
자동값은 `min(visible logical cores, 8)`이며 코어 수를 알 수 없으면 `1`입니다.
이 옵션은 runtime 시작 전에 설정해야 하며, 시작 뒤 변경은
`ZLINK_CONFIG_INVALID_ARGUMENT` (`errno=EINVAL`)로 실패합니다. 이 옵션은
dispatch event callback 경로에만 적용되며 send-ready, monitor, 다른 callback
경로에는 적용되지 않습니다.

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

`ROUTER` 는 모든 routed 트래픽을 `zlink_router_recv()` 단일 직접 수신
표면으로만 드레인합니다.

- ordinary `ROUTER` 메시지는 `source_node_rid_out_` 에 peer routing id 를
  담고, `source_spot_rid_out_` 는 빈 routing id 로 돌려줍니다.
- SPOT에서 온 메시지는 `source_node_rid_out_` 와 `source_spot_rid_out_` 를
  함께 reply 주소로 사용합니다.
- `request_seq_out_ == 0` 이면 ordinary direct send (fire-and-forget) 입니다.
- `request_seq_out_ != 0` 이면 request-reply 트래픽입니다.

별도의 `zlink_router_spot_recv()` 계약은 두지 않습니다.

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

`Spot` facade에서 topic / routed / timer readable 알림은 단일 dispatch
event callback으로 받습니다.

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
```

- callback은 `zlink_spot_dispatch_event_handler(spot, handler, userdata)`로
  설치합니다.
- 같은 `spot_`의 dispatch callback delivery는 직렬화됩니다.
- 이 callback은 readability 알림 전용입니다. payload는 callback 반환 이후
  (또는 callback 문맥 안에서) `zlink_subscribe()` / `zlink_spot_subscribe()`,
  `zlink_spot_recv()`, `zlink_timer_recv()`로 drain합니다.
- routed 축은 `zlink_spot_handler()`(direct routed callback)와
  `zlink_spot_dispatch_event_handler()`를 동시에 설치할 수 없습니다. 두
  번째 설치는 `ZLINK_HANDLER_BUSY`로 실패합니다.
- `zlink_subscribe_handler_fn` typedef는 헤더에 남아 있지만, 이를 설치할
  수 있는 공개 등록 함수는 제공되지 않습니다.

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

SPOT facade(`Spot`)는 더 이상 공개 service-monitor 대상이 아닙니다.
service-aware attachment가 있는 `SpotNode`의 모니터는 별도 recv 표면인
`zlink_spot_node_monitor_recv()`로만 가져옵니다.

- `zlink_monitor_target_kind_t`에는 `ZLINK_MONITOR_TARGET_SPOT_NODE = 5`가
  추가되었습니다. SpotNode에서 monitor 대상을 식별할 때 사용됩니다.
- service monitor event mask에는
  `ZLINK_SERVICE_MONITOR_EVENT_PEER_ADMISSION_CHANGED` (`1u << 8`)가
  추가되었습니다. 소켓 쪽 대응 이벤트는
  `ZLINK_SOCKET_MONITOR_EVENT_PEER_ADMISSION_CHANGED`이며
  `ZLINK_EVENT_PEER_ADMISSION_CHANGED`로도 표면화됩니다.
- `zlink_spot_node_monitor_recv()`가 돌려주는
  `zlink_spot_service_monitor_event_t`는 `service_name`, attachment role,
  원본 `zlink_monitor_event_t`를 함께 싣습니다.
- 일반 node 상태 요약은 여전히 `zlink_spot_node_status_snapshot()`과
  `zlink_spot_node_peers_snapshot()`으로 조회합니다.

## 스냅샷 / 인트로스펙션

SpotNode는 운영 건강 모니터링과 진단을 위한 lock-free point-in-time 스냅샷
API를 제공합니다. 이벤트 기반 모니터를 보완하는 pull 방식의 조회입니다.

### SpotNode Status Snapshot

```c
zlink_config_result_t zlink_spot_node_status_snapshot(void *node,
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

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

---

### SpotNode Peers Snapshot / Query

```c
zlink_config_result_t zlink_spot_node_peers_snapshot(void *node,
                                                     zlink_spot_node_peer_entry_t *entries,
                                                     size_t *count);

zlink_config_result_t zlink_spot_node_peers_query(void *node,
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
    zlink_admission_state_t admission_state;
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
| `admission_state` | 이 peer의 admission 상태. `ZLINK_ADMISSION_SERVING` 또는 `ZLINK_ADMISSION_DRAINING`. peer가 `DRAINING`이면 로컬은 그 peer를 새 outbound 대상으로 사용하지 않습니다. |
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

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

---

### SpotNode Subjects Snapshot

```c
zlink_config_result_t zlink_spot_node_subjects_snapshot(void *node,
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

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

---

### 권장 모니터링 순서

1. `zlink_spot_node_status_snapshot()` -- 전체 건강 상태를 먼저 확인합니다.
2. `zlink_spot_node_peers_snapshot()` -- 피어 연결 상태를 점검합니다.
3. `zlink_spot_node_subjects_snapshot()` -- subject readiness를 확인합니다.

## SpotNode 옵션

SpotNode handle의 node-level 옵션은 `zlink_set_spot_node_option()`으로 설정합니다.

### zlink_spot_node_option_t

```c
typedef enum zlink_spot_node_option_t {
    ZLINK_SPOT_NODE_OPT_TOPIC_SEND_HWM                   = 0x3608,
    ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM                   = 0x3609,
    ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM                  = 0x360A,
    ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM                  = 0x360B
} zlink_spot_node_option_t;
```

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `TOPIC_SEND_HWM` | `int` | 0 (무제한) | 토픽 (pub/sub) 메시지 전송 high water mark. |
| `TOPIC_RECV_HWM` | `int` | 0 (무제한) | 토픽 (pub/sub) 메시지 수신 high water mark. |
| `ROUTED_SEND_HWM` | `int` | 0 (무제한) | routed (request-reply) 메시지 전송 high water mark. |
| `ROUTED_RECV_HWM` | `int` | 0 (무제한) | routed (request-reply) 메시지 수신 high water mark. |

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

### Callback 모드 (dispatch event handler)

```c
static void on_spot_event(void *spot,
                          zlink_spot_dispatch_event_t event,
                          void *userdata)
{
    if (event != ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE)
        return;
    for (;;) {
        zlink_routing_id_t src;
        zlink_msg_t *parts = NULL;
        size_t parts_count = 0;
        char topic[256];
        size_t topic_len = sizeof(topic);
        zlink_recv_result_t rc = zlink_subscribe(
            spot, &src, &parts, &parts_count,
            topic, &topic_len, ZLINK_DONTWAIT);
        if (rc != ZLINK_RECV_OK) break;
        /* 메시지 처리 */
        for (size_t i = 0; i < parts_count; ++i)
            zlink_msg_close(&parts[i]);
    }
}

void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://127.0.0.1:5555");

void *spot = zlink_spot_new(node);
zlink_set_subscription(spot, "room:lobby");
zlink_spot_dispatch_event_handler(spot, on_spot_event, NULL);

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
