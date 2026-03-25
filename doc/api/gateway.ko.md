[English](gateway.md) | [한국어](gateway.ko.md)

# Gateway

Gateway는 서비스 바인딩된 로드 밸런싱 요청/응답 핸들입니다. Discovery가
연결된 경우 서비스 위치를 자동으로 확인하고, 구성 가능한 로드 밸런싱 전략을
사용하여 연결된 피어에 메시지를 분배합니다. Gateway는 recv-with-callback
공통 계약을 따르며 receive callback과 send-ready callback은 서로 독립입니다.

## I/O 모델

Gateway handle은 **recv 모드**로 시작합니다.

| 동작 | 기본 Gateway | `recv_handler()` 이후 | `send_ready_handler()` 이후 |
|------|-------------|------------------------|-------------------------------|
| **수신** | `gateway_recv()` / `recv()` | callback dispatch | unchanged |
| **읽기 poller** | `ZLINK_POLLIN` | `EBUSY` | unchanged |
| **쓰기 준비** | poller `ZLINK_POLLOUT` | unchanged | callback dispatch |

- `zlink_recv_handler(gateway, ...)`는 지원됩니다.
- `zlink_send_ready_handler(gateway, ...)`는 지원되며 receive callback 선행 조건이 없습니다.
- `zlink_gateway_send()` / `zlink_gateway_send_rid()`는 두 receive model 모두에서 동작합니다.

## 스레드 안전성 요약

하나의 Gateway handle을 여러 스레드에서 동시에 사용할 수 있습니다
(thread-safe).

- `zlink_gateway_send()` / `zlink_gateway_send_rid()`는 hot path이며 동시 호출을
  허용합니다.
- attach, bind/connect/disconnect, option, query, monitor는 runtime control
  path입니다. correctness는 보장되지만 실행 순서는 내부 직렬화에 따라
  결정될 수 있습니다.
- `zlink_gateway_destroy()`는 fail-fast lifecycle gate를 사용합니다. admitted
  API나 callback이 있으면 `EBUSY`, destroy가 accepted된 뒤 새 API 진입은
  `ESHUTDOWN`입니다.
- init-only 성격의 설정과 callback context 제한은 일반 운영 API와 별도로
  봐야 합니다.

## 현재 권장 API 방향

- `zlink_gateway_new()`로 Gateway 핸들을 생성합니다. 서비스 이름은
  `zlink_discovery_new()` 호출 시 Discovery 생성 시점에 고정됩니다.
- 대표 routing id가 필요하면 첫 bind/connect 전에
  `zlink_set_routing_id()`를 호출합니다.
- `zlink_gateway_recv()`로 메시지를 직접 수신합니다.
- `zlink_gateway_attach_discovery()`로 자동 피어 관리를 연결합니다.
- `zlink_gateway_bind()`로 서버 측 동작을 설정합니다.
- `zlink_gateway_connect()` / `zlink_gateway_disconnect()`로 수동 피어 관리를
  합니다 (discovery 연결 전에만 허용).
- `zlink_set_option()` / `zlink_get_option()`으로 서비스 레벨 튜닝을 합니다.
- 송신 측 백프레셔는 poller `ZLINK_POLLOUT`로 처리합니다.
- `zlink_service_monitor_open(gateway, &options)`으로 edge 전이를 관찰합니다.
  - `ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED`
  - `ZLINK_GATEWAY_MONITOR_EVENT_ROUTE_UP`
  - `zlink_monitor_close()`로 닫습니다.
- monitor handle에 대해 `zlink_monitor_snapshot()`으로 현재 로컬 제어 상태와
  queue depth를 읽습니다.
- 운영적인 peer 조회는 registry gateway-peer query API를 사용합니다.

## 상수

### 로드 밸런싱 전략

```c
typedef enum zlink_gateway_lb_strategy_t
{
    ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN = 0,
    ZLINK_GATEWAY_LB_STRATEGY_WEIGHTED    = 1
} zlink_gateway_lb_strategy_t;
```

| 상수 | 설명 |
|------|------|
| `ZLINK_GATEWAY_LB_ROUND_ROBIN` | 라운드 로빈 로드 밸런싱 (기본값) |
| `ZLINK_GATEWAY_LB_WEIGHTED` | 피어 가중치 기반 가중 로드 밸런싱 |

### 공용 옵션 (generic API 경유)

Gateway는 generic typed option API (`zlink_set_option` /
`zlink_get_option`)를 다음 `zlink_option_t` 상수와 함께 사용합니다:

| 상수 | 설명 |
|------|------|
| `ZLINK_OPT_SNDHWM` | 송신 고수위 마크 |
| `ZLINK_OPT_RCVHWM` | 수신 고수위 마크 |
| `ZLINK_OPT_SNDTIMEO` | 송신 타임아웃 (ms) |
| `ZLINK_OPT_LINGER` | Linger 기간 (ms) |
| `ZLINK_OPT_SNDBUF` | 커널 송신 버퍼 크기 (바이트) |
| `ZLINK_OPT_RCVBUF` | 커널 수신 버퍼 크기 (바이트) |
| `ZLINK_OPT_LAST_ENDPOINT` | 바인드된 엔드포인트 확인 (get 전용) |

전체 `zlink_option_t` 참조는 [socket.ko.md](socket.ko.md)를 참조하세요.

### 라우터 옵션 (generic API 경유)

Gateway는 `zlink_set_router_option` / `zlink_get_router_option`을 통해
라우터 전용 옵션도 지원합니다:

| 상수 | 설명 |
|------|------|
| `ZLINK_ROUTER_OPT_MANDATORY` | 라우팅 불가 시 drop 대신 실패 처리 |
| `ZLINK_ROUTER_OPT_HANDOVER` | 기존 routing id를 새 연결이 인수 허용 |
| `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` | 피어 연결 시 routing id 설정 |

전체 `zlink_router_option_t` 참조는 [socket.ko.md](socket.ko.md)를 참조하세요.

## 함수

### zlink_gateway_new

recv 모드의 Gateway를 생성합니다.

```c
void *zlink_gateway_new (void *ctx);
```

새 Gateway 인스턴스를 할당하고 초기화합니다. 서비스 아이덴티티는 이후
`zlink_gateway_attach_discovery()`로 연결할 Discovery 인스턴스에서 결정됩니다.
필요하면 첫 bind/connect 전에 `zlink_set_routing_id()`로 routing id를
설정합니다.

**반환값:** 성공 시 Gateway 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_gateway_send`, `zlink_gateway_destroy`

### zlink_gateway_recv

recv 모드에서 메시지를 수신합니다.

```c
int zlink_gateway_recv (void *gateway,
                        zlink_routing_id_t *source_rid_out,
                        zlink_msg_t **parts,
                        size_t *part_count,
                        int flags);
```

callback 모드가 전달하던 것과 같은 semantic unit을 반환합니다.
`source_rid_out`에 송신자의 routing ID가 채워집니다. `parts`와
`part_count`에 멀티파트 메시지가 채워지며, 호출자가 반환된 parts의
소유권을 가지고 `zlink_msg_close()`로 해제해야 합니다.

non-blocking 동작은 `flags`에 `ZLINK_DONTWAIT`를 전달합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EBUSY` -- handle이 callback 모드입니다.
- `EAGAIN` -- `ZLINK_DONTWAIT`가 설정되었으며 수신할 메시지가 없습니다.

**스레드 안전성:** recv 모드에서 모든 스레드에서 호출할 수 있습니다.

---

### zlink_gateway_attach_discovery

자동 피어 관리를 위해 Discovery 인스턴스를 연결합니다.

```c
int zlink_gateway_attach_discovery (void *gateway, void *discovery);
```

자동 피어 해석을 위해 Gateway를 Discovery에 연결합니다. Discovery 핸들은
`ZLINK_SERVICE_TYPE_GATEWAY`로 생성되어야 하며 호출자가 소유권을 유지합니다.
연결 후에는 수동 connect/disconnect가 더 이상 허용되지 않습니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_gateway_new`, `zlink_discovery_new`

---

### zlink_gateway_bind

Gateway를 엔드포인트에 바인드합니다.

```c
int zlink_gateway_bind (void *gateway, const char *bind_endpoint);
```

서버 측 동작을 위해 Gateway의 내부 소켓을 지정된 엔드포인트에 바인드합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `ZLINK_OPT_LAST_ENDPOINT`를 사용한 `zlink_get_option`

---

### zlink_gateway_connect

수동 관리되는 원격 피어 라우트에 연결합니다.

```c
int zlink_gateway_connect (void *gateway,
                           const char *endpoint,
                           const zlink_routing_id_t *routing_id);
```

수동 connect는 discovery 연결 전에만 허용됩니다. 원격 라우팅 ID는 요청
디스패치를 위한 피어를 식별합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_gateway_disconnect`

---

### zlink_gateway_disconnect

수동 관리되는 원격 피어 라우트의 연결을 해제합니다.

```c
int zlink_gateway_disconnect (void *gateway, const char *endpoint);
```

수동 disconnect는 discovery 연결 전에만 허용됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_gateway_connect`

---

### zlink_gateway_send

바인드된 서비스에 메시지를 전송합니다 (로드 밸런싱).

```c
int zlink_gateway_send (void *gateway,
                        zlink_msg_t *parts,
                        size_t part_count,
                        zlink_send_flags_t flags);
```

구성된 로드 밸런싱 전략(기본값은 라운드 로빈)에 따라 선택된 피어에 멀티파트
메시지를 전송합니다. 성공 시 메시지 파트의 소유권이 이전됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EHOSTUNREACH` -- 사용 가능한 피어가 없습니다.
- `EAGAIN` -- `ZLINK_DONTWAIT`가 설정되었으며 작업이 블록됩니다.

**스레드 안전성:** 스레드 안전함. 여러 스레드가 동시에 호출할 수 있습니다.

**참고:** `zlink_gateway_send_rid`, `zlink_gateway_set_lb_strategy`

---

### zlink_gateway_send_rid

라우팅 ID로 특정 피어에 직접 메시지를 전송합니다.

```c
int zlink_gateway_send_rid (void *gateway,
                            const zlink_routing_id_t *routing_id,
                            zlink_msg_t *parts,
                            size_t part_count,
                            zlink_send_flags_t flags);
```

로드 밸런싱을 우회하고 `routing_id`로 식별되는 피어에 전송합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EHOSTUNREACH` -- 지정된 라우팅 ID가 연결되어 있지 않습니다.
- `EAGAIN` -- `ZLINK_DONTWAIT`가 설정되었으며 작업이 블록됩니다.

**스레드 안전성:** 스레드 안전함.

**참고:** `zlink_gateway_send`

---

### zlink_gateway_set_lb_strategy

로드 밸런싱 전략을 설정합니다.

```c
int zlink_gateway_set_lb_strategy (
  void *gateway, zlink_gateway_lb_strategy_t strategy);
```

로드 밸런싱 전략을 변경합니다. 유효한 전략은
`ZLINK_GATEWAY_LB_ROUND_ROBIN`(기본값)과 `ZLINK_GATEWAY_LB_WEIGHTED`입니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_gateway_send`, `zlink_gateway_update_peer_weight`

---

### zlink_gateway_update_peer_weight

특정 서비스 피어의 권한 있는 가중치를 업데이트합니다.

```c
int zlink_gateway_update_peer_weight (
  void *gateway,
  const zlink_routing_id_t *routing_id,
  uint32_t weight);
```

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_gateway_set_lb_strategy`

---

### 옵션 — zlink_set_option / zlink_get_option

Gateway는 서비스 레벨 튜닝을 위해 generic typed option API를 사용합니다.

```c
int zlink_set_option (void *gateway, zlink_option_t option, ...);
int zlink_get_option (void *gateway, zlink_option_t option, ...);
```

지원되는 옵션은 위의 공용 옵션을 참조하세요. 이전에
`zlink_gateway_last_endpoint()`로 수행하던 엔드포인트 조회는 다음과 같이
수행합니다:

```c
zlink_get_option (gateway, ZLINK_OPT_LAST_ENDPOINT, buf, &size);
```

generic typed option API의 전체 내용은 [socket.ko.md](socket.ko.md)를 참조하세요.

---

### 라우터 옵션 — zlink_set_router_option / zlink_get_router_option

Gateway는 generic router option API를 통해 라우터 전용 옵션을 지원합니다.

```c
int zlink_set_router_option (void *gateway, zlink_router_option_t option, ...);
int zlink_get_router_option (void *gateway, zlink_router_option_t option, ...);
```

지원되는 옵션: `ZLINK_ROUTER_OPT_MANDATORY`, `ZLINK_ROUTER_OPT_HANDOVER`,
`ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID`.

generic router option API의 전체 내용은 [socket.ko.md](socket.ko.md)를 참조하세요.

---

### Routing ID — zlink_set_routing_id / zlink_get_routing_id

Gateway는 generic routing id API를 사용합니다.

```c
int zlink_set_routing_id (void *gateway, const void *data, size_t size);
int zlink_get_routing_id (void *gateway, zlink_routing_id_t *out);
```

첫 bind/connect 전에 대표 routing id를 설정합니다. get은 현재 routing id를
반환합니다.

전체 내용은 [socket.ko.md](socket.ko.md)를 참조하세요.

---

### TLS — zlink_set_tls_client / zlink_set_tls_server

Gateway는 generic TLS 구성 API를 사용합니다.

```c
int zlink_set_tls_client (void *gateway,
                          const char *ca_cert,
                          const char *hostname,
                          int trust_system);

int zlink_set_tls_server (void *gateway,
                          const char *cert,
                          const char *key,
                          int require_client_cert);
```

`zlink_set_tls_client`는 발신 연결에 대해 TLS를 활성화합니다.
`zlink_set_tls_server`는 바인드된 엔드포인트의 수신 연결에 대해 TLS를
활성화합니다. 참고: `zlink_set_tls_server`는 이전 gateway 전용 API에 비해
`require_client_cert` 파라미터가 추가되었습니다.

전체 내용은 [socket.ko.md](socket.ko.md)를 참조하세요.

---

### Send-Ready — zlink_send_ready_handler

Gateway는 generic send-ready callback 계약을 그대로 사용합니다.

```c
int zlink_send_ready_handler (
  void *gateway, zlink_send_ready_handler_fn handler, void *userdata);
```

attach는 receive callback 모드와 독립적입니다. attach 이후 writable
readiness는 callback으로 전달되고, data-plane poller `ZLINK_POLLOUT`은
`errno=EBUSY`로 실패합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- `handler`가 `NULL`입니다.
- `EBUSY` -- 같은 handle에서 data-plane writable poller surface가 이미 선택되어 있습니다.

전체 내용은 [socket.ko.md](socket.ko.md)를 참조하세요.

---

### zlink_gateway_destroy

Gateway를 파괴하고 모든 리소스를 해제합니다.

```c
int zlink_gateway_destroy (void **gateway_p);
```

모든 연결을 닫고, 내부 소켓을 해제하며, Gateway를 해제합니다. 파괴 후
`*gateway_p`의 포인터는 `NULL`로 설정됩니다. Discovery 핸들(연결된 경우)은
영향을 받지 않으며 별도로 파괴해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** Gateway는 3계층 계약을 따릅니다. hot path
`send`/`send_rid`는 여러 스레드에서 동시 호출을 허용하고, attach/option/monitor는
serialized control path로 동작하며, `zlink_gateway_destroy()`는 stricter
lifecycle gate를 사용합니다. 다른 스레드가 같은 handle에서 callback 또는
admitted API를 실행 중이면 `errno=EBUSY`로 실패하고, destroy가 accepted된 뒤
새 API 진입은 `errno=ESHUTDOWN`로 실패합니다. destroy가 성공한 경우에만
`*gateway_p`가 `NULL`로 정리됩니다.

**참고:** `zlink_gateway_new`

---

## 스냅샷 / 인트로스펙션

### Gateway Status Snapshot

```c
int zlink_gateway_status_snapshot(void *gateway,
                                  zlink_gateway_status_t *out);
```

Gateway의 단일 행 운영 건강 요약을 반환합니다.

#### zlink_gateway_status_t

```c
typedef struct zlink_gateway_status_t
{
    char service_name[256];
    char bind_endpoint[256];
    zlink_routing_id_t gateway_routing_id;
    zlink_gateway_state_t state;
    uint32_t observed_provider_count;
    uint32_t ready_provider_count;
    uint32_t active_route_count;
    uint32_t send_ready;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_gateway_status_t;
```

| 필드 | 설명 |
|------|------|
| `service_name` | 연결된 Discovery의 null 종료 서비스 이름. |
| `bind_endpoint` | null 종료 바인드 엔드포인트. |
| `gateway_routing_id` | 이 Gateway의 라우팅 아이덴티티. |
| `state` | `IDLE`, `CONNECTING`, `PARTIAL_READY`, `READY`, 또는 `ERROR`. |
| `observed_provider_count` | 관찰된 총 provider 수 (연결 + 연결 중). |
| `ready_provider_count` | 현재 ready 상태인 provider 수. |
| `active_route_count` | 로드 밸런싱을 위한 활성 라우트 수. |
| `send_ready` | Gateway가 쓰기 가능하면 0이 아닌 값. |
| `last_error` | 마지막 기록된 에러 코드, 또는 0. |
| `last_changed_ms` | 마지막 상태 변경 시점 (에포크 ms). |

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.
