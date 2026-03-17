[English](gateway.md) | [한국어](gateway.ko.md)

# Gateway

Gateway는 서비스 바인딩된 로드 밸런싱 요청/응답 핸들입니다. Discovery가
연결된 경우 서비스 위치를 자동으로 확인하고, 구성 가능한 로드 밸런싱 전략을
사용하여 연결된 피어에 메시지를 분배합니다. 모든 수신은 생성 시 등록된 핸들러
콜백을 통해 디스패치됩니다. `recv()` 함수는 없습니다.

## 스레드 안전성 요약

공개 Gateway handle API는 기본적으로 same-handle operational use 기준
thread-safe합니다.

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

- `zlink_gateway_new()`로 서비스 이름, 라우팅 ID, 핸들러를 고정하여 생성합니다.
- `zlink_gateway_attach_discovery()`로 자동 피어 관리를 연결합니다.
- `zlink_gateway_bind()`로 서버 측 동작을 설정합니다.
- `zlink_gateway_connect()` / `zlink_gateway_disconnect()`로 수동 피어 관리를
  합니다 (discovery 연결 전에만 허용).
- `zlink_gateway_set_option()`으로 서비스 레벨 튜닝을 합니다.
- `zlink_gateway_set_send_ready_handler()`로 송신 측 백프레셔를 처리합니다.
- `zlink_gateway_monitor_open()`으로 `ZLINK_GATEWAY_SEND_READY_CHANGED`,
  `ZLINK_GATEWAY_ROUTE_UP` 같은 edge 전이를 관찰합니다.
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

### Gateway 옵션

```c
typedef enum zlink_gateway_option_t
{
    ZLINK_GATEWAY_OPT_SNDHWM  = 0x2101,
    ZLINK_GATEWAY_OPT_RCVHWM  = 0x2102,
    ZLINK_GATEWAY_OPT_SNDTIMEO = 0x2103,
    ZLINK_GATEWAY_OPT_LINGER  = 0x2104,
    ZLINK_GATEWAY_OPT_SNDBUF  = 0x2105,
    ZLINK_GATEWAY_OPT_RCVBUF  = 0x2106
} zlink_gateway_option_t;
```

| 상수 | 설명 |
|------|------|
| `ZLINK_GATEWAY_OPT_SNDHWM` | 송신 고수위 마크 |
| `ZLINK_GATEWAY_OPT_RCVHWM` | 수신 고수위 마크 |
| `ZLINK_GATEWAY_OPT_SNDTIMEO` | 송신 타임아웃 (ms) |
| `ZLINK_GATEWAY_OPT_LINGER` | Linger 기간 (ms) |
| `ZLINK_GATEWAY_OPT_SNDBUF` | 커널 송신 버퍼 크기 (바이트) |
| `ZLINK_GATEWAY_OPT_RCVBUF` | 커널 수신 버퍼 크기 (바이트) |

## 함수

### zlink_gateway_new

Gateway를 생성합니다.

```c
void *zlink_gateway_new (void *ctx,
                         const char *service_name,
                         const char *routing_id,
                         zlink_socket_msg_handler_fn handler,
                         void *userdata);
```

새 Gateway 인스턴스를 할당하고 초기화합니다. `service_name`은 생성 시 고정되는
서비스 아이덴티티입니다. `routing_id`는 이 Gateway를 고유하게 식별합니다.
`handler` 콜백은 메시지가 도착하면 I/O 스레드에서 호출됩니다.

**반환값:** 성공 시 Gateway 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_gateway_send`, `zlink_gateway_destroy`

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

**참고:** `zlink_gateway_last_endpoint`

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

### zlink_gateway_set_send_ready_handler

send-ready 콜백을 설치하거나 교체합니다.

```c
int zlink_gateway_set_send_ready_handler (
  void *gateway, zlink_send_ready_handler_fn handler, void *userdata);
```

Gateway가 쓰기 가능 상태로 전이할 때 핸들러가 호출됩니다. 시작 후에 핸들러를
설치하는 경우, 열린 Gateway monitor에서 `zlink_monitor_snapshot()`으로 초기
상태를 seed하세요.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

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

### zlink_gateway_set_option

Gateway 서비스 옵션을 설정합니다.

```c
int zlink_gateway_set_option (void *gateway,
                              zlink_gateway_option_t option,
                              const void *optval,
                              size_t optvallen);
```

서비스 레벨 옵션을 적용합니다. 위의 Gateway 옵션을 참조하세요.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

---

### zlink_gateway_set_routing_id

첫 bind/connect 전에 대표 routing id를 재정의합니다.

```c
int zlink_gateway_set_routing_id (void *gateway,
                                  const void *data,
                                  size_t size);
```

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_gateway_routing_id`

---

### zlink_gateway_routing_id

이 Gateway의 대표 routing id를 반환합니다.

```c
int zlink_gateway_routing_id (void *gateway,
                              zlink_routing_id_t *out);
```

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_gateway_set_routing_id`

---

### zlink_gateway_set_tls_client

Gateway의 TLS 클라이언트 설정을 구성합니다.

```c
int zlink_gateway_set_tls_client (void *gateway,
                                  const char *ca_cert,
                                  const char *hostname,
                                  int trust_system);
```

발신 연결에 대해 TLS를 활성화합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_gateway_set_tls_server`

---

### zlink_gateway_set_tls_server

Gateway의 TLS 서버 설정을 구성합니다.

```c
int zlink_gateway_set_tls_server (void *gateway,
                                  const char *cert,
                                  const char *key);
```

바인드된 엔드포인트의 수신 연결에 대해 TLS를 활성화합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_gateway_set_tls_client`

---

### zlink_gateway_last_endpoint

이 Gateway의 바인드된 엔드포인트를 확인합니다.

```c
int zlink_gateway_last_endpoint (void *gateway,
                                 char *endpoint,
                                 size_t *size);
```

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_gateway_bind`

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
`send`/`send_rid`는 same-handle 병행 사용을 허용하고, attach/option/monitor는
serialized control path로 동작하며, `zlink_gateway_destroy()`는 stricter
lifecycle gate를 사용합니다. 다른 스레드가 같은 handle에서 callback 또는
admitted API를 실행 중이면 `errno=EBUSY`로 실패하고, destroy가 accepted된 뒤
새 API 진입은 `errno=ESHUTDOWN`로 실패합니다. destroy가 성공한 경우에만
`*gateway_p`가 `NULL`로 정리됩니다.

**참고:** `zlink_gateway_new`
