[English](spot.md) | [한국어](spot.ko.md)

# SPOT

SPOT public API는 두 계층으로 정리됩니다.

- `SpotNode`: bind/connect/discovery/TLS wiring owner
- `Spot`: `SpotNode`에 attach되는 unified pub/sub facade

현재 public surface에는 standalone `zlink_spot_pub_*` / `zlink_spot_sub_*`
생성자와 destroy, option, monitor API가 없습니다.

## I/O 모델

`SpotNode`와 unified `Spot` 모두 **recv 모드**로 시작하고,
`zlink_subscribe_handler()`로 callback 모드로 **일방 전환**됩니다. 두 모델은
handle 수명 동안 상호 배타적입니다.

| | Recv 모드 (기본) | Callback 모드 |
|---|---|---|
| **SpotNode 수신** | `zlink_subscribe()` | `zlink_subscribe_handler()` 콜백 |
| **Spot 수신** | `zlink_subscribe()` | `zlink_subscribe_handler()` 콜백 |
| **Send-ready** | 사용 불가 (`EBUSY`) | `zlink_send_ready_handler()` |
| **전환** | `zlink_subscribe_handler()` 호출로 전환 | 영구, 되돌릴 수 없음 |

- recv 모드에서 `send_ready_handler()`는 `EBUSY`로 실패합니다.
- callback 모드에서 `recv()`는 `EBUSY`로 실패합니다.
- `publish()`는 두 모드 모두에서 동작합니다.

## 현재 public surface

### SpotNode

```c
void *zlink_spot_node_new(void *ctx,
                          const char *service_name);
int zlink_spot_node_destroy(void **node_p);

int zlink_spot_node_bind(void *node, const char *endpoint);
int zlink_spot_node_connect_peer_pub(void *node, const char *endpoint);
int zlink_spot_node_disconnect_peer_pub(void *node,
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

int zlink_publish(void *node,
                  const char *topic_id,
                  zlink_msg_t *parts,
                  size_t part_count,
                  zlink_send_flags_t flags);
int zlink_set_subscription (void *node, const char *filter);
int zlink_unset_subscription (void *node, const char *filter);
int zlink_subscription_at(void *node, size_t index,
                          char *buf, size_t *len,
                          int *is_pattern);

int zlink_send_ready_handler(
  void *node,
  zlink_send_ready_handler_fn handler,
  void *userdata);
int zlink_set_pub_option(void *node,
                         zlink_pub_option_t option,
                         const void *optval,
                         size_t optvallen);
int zlink_get_pub_option(void *node,
                         zlink_pub_option_t option,
                         void *optval,
                         size_t *optvallen);
int zlink_set_sub_option(void *node,
                         zlink_sub_option_t option,
                         const void *optval,
                         size_t optvallen);
int zlink_get_sub_option(void *node,
                         zlink_sub_option_t option,
                         void *optval,
                         size_t *optvallen);

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

int zlink_subscribe(void *subject_,
                    zlink_routing_id_t *source_rid_out_,
                    zlink_msg_t **parts_out_,
                    size_t *part_count_out_,
                    char *topic_id_out_,
                    size_t *topic_id_len_out_,
                    zlink_send_flags_t flags_);
```

`SpotNode`는 service-bound owner입니다. `service_name`은 생성 시점에
고정됩니다. recv 모드에서는 `zlink_subscribe()`, callback 모드에서는
`zlink_subscribe_handler()`를 사용합니다.

`zlink_subscribe()`는 recv 모드에서 다음 메시지, source routing ID, topic을
반환합니다. 성공 시 `source_rid_out_`, `parts_out_`, `topic_id_out_`이
채워집니다. non-blocking 동작은 `flags_`에 `ZLINK_DONTWAIT`를 전달합니다.
callback 모드에서는 `EBUSY`로 실패합니다.

### Unified Spot

```c
void *zlink_spot_new(void *spot_node);
int zlink_spot_destroy(void **spot_p);

int zlink_publish(void *spot,
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

`zlink_spot_new()`는 항상 pub/sub가 합쳐진 facade를 생성합니다.
publish-only 혹은 subscribe-only public child handle은 더 이상 제공하지 않습니다.

`zlink_subscribe()`는 recv 모드에서 동기식 pull 방식의 수신을 제공합니다.
다음 메시지와 source routing ID, topic을 반환합니다. 성공 시 `source_rid_out_`,
`parts_out_`, `topic_id_out_`이 채워집니다. non-blocking 동작은 `flags_`에
`ZLINK_DONTWAIT`를 전달합니다. callback 모드에서는 `EBUSY`로 실패합니다.

aggregate ready-peer / queue 조회는 `zlink_spot_monitor_open()`과
`zlink_monitor_snapshot()` 조합을 사용합니다.

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

| 대상 | 설정/조회 API | 지원 방향 |
|---|---|---|
| unified `spot` publish 쪽 | `zlink_set_pub_option()` / `zlink_get_pub_option()` | `ZLINK_PUB_OPT_*` |
| unified `spot` subscribe 쪽 | `zlink_set_sub_option()` / `zlink_get_sub_option()` | `ZLINK_SUB_OPT_*` |
| `spot_node` default publish 쪽 | `zlink_set_pub_option()` / `zlink_get_pub_option()` | `ZLINK_PUB_OPT_*` |
| `spot_node` default subscribe 쪽 | `zlink_set_sub_option()` / `zlink_get_sub_option()` | `ZLINK_SUB_OPT_*` |
| common 옵션 (pub 쪽) | `zlink_set_option()` / `zlink_get_option()` | `ZLINK_OPT_SNDHWM`, `ZLINK_OPT_SNDTIMEO`, `ZLINK_OPT_LINGER`, `ZLINK_OPT_SNDBUF`, `ZLINK_OPT_RCVBUF` |
| common 옵션 (sub 쪽) | `zlink_set_option()` / `zlink_get_option()` | `ZLINK_OPT_RCVHWM`, `ZLINK_OPT_RCVTIMEO`, `ZLINK_OPT_LINGER`, `ZLINK_OPT_SNDBUF`, `ZLINK_OPT_RCVBUF` |
| routing_id (pub 쪽) | `zlink_set_routing_id()` / `zlink_get_routing_id()` | — |
| subscription 관리 | `zlink_set_subscription()` / `zlink_unset_subscription()` / `zlink_subscription_at()` | — |

## 모니터링

SPOT monitor는 Spot과 SpotNode 모두에 대해 unified entrypoint를 제공합니다.

```c
void *zlink_spot_monitor_open(void *spot,
                              zlink_spot_role_t role,
                              zlink_spot_monitor_event_mask_t events,
                              zlink_service_monitor_handler_fn handler,
                              void *userdata);

void *zlink_spot_node_monitor_open(void *node,
                                   zlink_spot_role_t role,
                                   zlink_spot_monitor_event_mask_t events,
                                   zlink_service_monitor_handler_fn handler,
                                   void *userdata);
```

- `role`은 `ZLINK_SPOT_ROLE_PUB` 또는 `ZLINK_SPOT_ROLE_SUB`입니다.
- `zlink_spot_monitor_open()`은 unified Spot facade를 모니터합니다.
- `zlink_spot_node_monitor_open()`은 node-owned default pub/sub를 모니터합니다.
- split `zlink_spot_pub_monitor_open()` / `zlink_spot_sub_monitor_open()`는
  public API가 아닙니다.
- 상세 event 정의와 readiness 의미는 [events.ko.md](events.ko.md)를 참고합니다.

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

void *node = zlink_spot_node_new(ctx, "svc-chat");
zlink_subscribe_handler(node, on_spot_message, NULL);
zlink_spot_node_bind(node, "tcp://127.0.0.1:5555");

void *spot = zlink_spot_new(node);
zlink_subscribe_handler(spot, on_spot_message, NULL);
zlink_set_subscription (spot, "room:lobby");

zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_publish(spot, "room:lobby", &part, 1, 0);

zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
```

### Recv 모드

```c
void *node = zlink_spot_node_new(ctx, "svc-chat");
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

zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
```
