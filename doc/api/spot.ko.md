[English](spot.md) | [한국어](spot.ko.md)

# SPOT

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
| **SpotNode 수신** | `zlink_subscribe()` | `subscribe_handler()` 콜백 |
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

aggregate ready-peer / queue 조회는 `zlink_service_monitor_open()`과
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

| 대상 | 설정/조회 API | 옵션 |
|------|-------------|------|
| spot / node publish 쪽 | `set_pub_option` / `get_pub_option` | `ZLINK_PUB_OPT_*` |
| spot / node subscribe 쪽 | `set_sub_option` / `get_sub_option` | `ZLINK_SUB_OPT_*` |
| common (pub 쪽) | `set_option` / `get_option` | `SNDHWM`, `SNDTIMEO`, `LINGER`, `SNDBUF`, `RCVBUF` |
| common (sub 쪽) | `set_option` / `get_option` | `RCVHWM`, `RCVTIMEO`, `LINGER`, `SNDBUF`, `RCVBUF` |
| routing_id (pub 쪽) | `set_routing_id` / `get_routing_id` | -- |
| subscription 관리 | `set_subscription` / `unset_subscription` / `subscription_at` | -- |

## 모니터링

SPOT 모니터링은 Spot과 SpotNode 모두에 대해 unified `zlink_service_monitor_open()`
entrypoint를 사용합니다.

```c
void *zlink_service_monitor_open(void *target,
                                 const zlink_service_monitor_open_options_t *options);
```

- `target`은 unified Spot 핸들이거나 SpotNode 핸들입니다. target 종류
  (Spot vs SpotNode)는 핸들에서 자동으로 판별됩니다.
- 이전 per-type API의 `role` 파라미터는 제거되었습니다.
- `options->events`로 구독할 이벤트 마스크를 선택합니다.
- 반환된 monitor 핸들은 `zlink_monitor_close()`로 닫습니다.
- split `zlink_spot_pub_monitor_open()` / `zlink_spot_sub_monitor_open()`는
  public API가 아닙니다.
- 상세 event 정의와 readiness 의미는 [events.ko.md](events.ko.md)를 참고합니다.

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
| `service_name` | 생성 시 고정된 null 종료 서비스 이름. |
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
