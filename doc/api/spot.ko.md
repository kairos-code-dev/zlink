[English](spot.md) | [한국어](spot.ko.md)

# SPOT

SPOT public API는 두 계층으로 정리됩니다.

- `SpotNode`: bind/connect/discovery/TLS wiring owner
- `Spot`: `SpotNode`에 attach되는 unified pub/sub facade

현재 public surface에는 standalone `zlink_spot_pub_*` / `zlink_spot_sub_*`
생성자와 destroy, option, monitor API가 없습니다. direct receive는 polling이 아니라
생성 시점 callback 고정 모델을 사용합니다.

## 현재 public surface

### SpotNode

```c
void *zlink_spot_node_new(void *ctx,
                          const char *service_name,
                          zlink_spot_handler_fn handler,
                          void *userdata);
int zlink_spot_node_destroy(void **node_p);

int zlink_spot_node_bind(void *node, const char *endpoint);
int zlink_spot_node_connect_peer_pub(void *node, const char *endpoint);
int zlink_spot_node_disconnect_peer_pub(void *node,
                                        const char *endpoint);
int zlink_spot_node_attach_discovery(void *node, void *discovery);

int zlink_spot_node_set_tls_server(void *node,
                                   const char *cert,
                                   const char *key);
int zlink_spot_node_set_tls_client(void *node,
                                   const char *ca_cert,
                                   const char *hostname,
                                   int trust_system);

int zlink_spot_node_publish(void *node,
                            const char *topic_id,
                            zlink_msg_t *parts,
                            size_t part_count,
                            zlink_send_flags_t flags);
int zlink_spot_node_subscribe(void *node, const char *topic_id);
int zlink_spot_node_subscribe_pattern(void *node, const char *pattern);
int zlink_spot_node_unsubscribe(void *node,
                                       const char *topic_id_or_pattern);

int zlink_spot_node_send_ready_handler(
  void *node,
  zlink_send_ready_handler_fn handler,
  void *userdata);
int zlink_spot_node_set_pub_option(void *node,
                                   zlink_spot_pub_option_t option,
                                   const void *optval,
                                   size_t optvallen);
int zlink_spot_node_set_sub_option(void *node,
                                   zlink_spot_sub_option_t option,
                                   const void *optval,
                                   size_t optvallen);
```

`SpotNode`는 service-bound owner입니다. `service_name`과 recv callback은
생성 시점에 고정됩니다. node direct API는 node-owned default pub/sub facade를
통해 publish/subscribe를 수행하지만, child handle 자체를 public으로 노출하지는
않습니다.

### Unified Spot

```c
void *zlink_spot_new(void *spot_node,
                     zlink_spot_handler_fn handler,
                     void *userdata);
int zlink_spot_destroy(void **spot_p);

int zlink_spot_publish(void *spot,
                       const char *topic_id,
                       zlink_msg_t *parts,
                       size_t part_count,
                       zlink_send_flags_t flags);
int zlink_spot_sub_recv(void *sub,
                       zlink_msg_t **parts,
                       size_t *part_count,
                       int flags,
                       char *topic_id_out,
                       size_t *topic_id_len);
int zlink_spot_subscribe(void *spot, const char *topic_id);
int zlink_spot_subscribe_pattern(void *spot, const char *pattern);
int zlink_spot_unsubscribe(void *spot,
                           const char *topic_id_or_pattern);

int zlink_spot_send_ready_handler(
  void *spot,
  zlink_send_ready_handler_fn handler,
  void *userdata);

int zlink_spot_set_pub_option(void *spot,
                              zlink_spot_pub_option_t option,
                              const void *optval,
                              size_t optvallen);
int zlink_spot_set_sub_option(void *spot,
                              zlink_spot_sub_option_t option,
                              const void *optval,
                              size_t optvallen);
```

`zlink_spot_new()`는 항상 pub/sub가 합쳐진 facade를 생성합니다.
publish-only 혹은 subscribe-only public child handle은 더 이상 제공하지 않습니다.

`zlink_spot_sub_recv()`는 callback 모델 대신 동기식 pull 방식의 수신을
제공합니다. 다음 메시지와 topic을 반환합니다. 성공 시 `parts`와
`topic_id_out`이 채워집니다. non-blocking 동작은 `flags`에
`ZLINK_DONTWAIT`를 전달합니다.

aggregate ready-peer / queue 조회는 `zlink_spot_monitor_open()`과
`zlink_monitor_snapshot()` 조합을 사용합니다.

## callback 계약

```c
typedef void (*zlink_spot_handler_fn)(const zlink_routing_id_t *source_rid,
                                      const char *topic,
                                      size_t topic_len,
                                      zlink_msg_t *parts,
                                      size_t part_count,
                                      void *userdata);
```

- `zlink_spot_node_new(..., handler)`와 `zlink_spot_new(..., handler)`는
  handler callback을 받습니다. callback dispatch가 불필요하면 `NULL`을 전달합니다.
- callback은 생성 시점에 고정되며 이후 교체할 수 없습니다.
- callback은 전달받은 `parts`의 ownership을 소비해야 합니다.

## 옵션 요약

| 대상 | 설정 API | 지원 방향 |
|---|---|---|
| unified `spot` publish 쪽 | `zlink_spot_set_pub_option()` | `ZLINK_SPOT_PUB_OPT_*` |
| unified `spot` subscribe 쪽 | `zlink_spot_set_sub_option()` | `ZLINK_SPOT_SUB_OPT_*` |
| `spot_node` default publish 쪽 | `zlink_spot_node_set_pub_option()` | `ZLINK_SPOT_PUB_OPT_*` |
| `spot_node` default subscribe 쪽 | `zlink_spot_node_set_sub_option()` | `ZLINK_SPOT_SUB_OPT_*` |

`ZLINK_SPOT_PUB_OPT_MODE`, `ZLINK_SPOT_PUB_OPT_QUEUE_HWM`,
`ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY`는 현재 public runtime에서 지원하지 않으며
설정 시 `ENOTSUP`를 반환합니다.

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

## 예시

```c
void on_spot_message(const zlink_routing_id_t *source_rid,
                     const char *topic,
                     size_t topic_len,
                     zlink_msg_t *parts,
                     size_t part_count,
                     void *userdata);

void *node = zlink_spot_node_new(ctx, "svc-chat", on_spot_message, NULL);
zlink_spot_node_bind(node, "tcp://127.0.0.1:5555");

void *spot = zlink_spot_new(node, on_spot_message, NULL);
zlink_spot_subscribe(spot, "room:lobby");

zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_spot_publish(spot, "room:lobby", &part, 1, 0);

zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
```
