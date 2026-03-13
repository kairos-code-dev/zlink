[English](gateway.md) | [한국어](gateway.ko.md)

# 게이트웨이

Gateway는 Discovery를 통해 서비스 위치를 자동으로 확인하는 클라이언트 측
로드 밸런싱 요청/응답 프록시입니다. 필요에 따라 Receiver에 연결하고 구성
가능한 로드 밸런싱 전략을 사용하여 메시지를 분배합니다.

## 현재 권장 API 방향

- 공개 서비스 옵션 설정은 `zlink_gateway_set_option()`을 사용합니다.
- 대표 identity는 `zlink_gateway_set_routing_id()` /
  `zlink_gateway_routing_id()`로 다룹니다.
- 현재 로컬 제어 상태는 `zlink_gateway_monitor_snapshot()`으로 읽습니다.
- `ZLINK_GATEWAY_SEND_READY_CHANGED`, `ZLINK_GATEWAY_ROUTE_UP` 같은
  edge 전이는 `zlink_gateway_monitor_open()`으로 관찰합니다.
- 데이터 readiness는 `zlink_poller_add_gateway()`, monitor readiness는
  `zlink_poller_add_monitor()`으로 같은 루프에 통합합니다.
- 운영적인 peer 조회는 registry gateway-peer query를 사용합니다.

## 상수

```c
#define ZLINK_GATEWAY_LB_ROUND_ROBIN 0
#define ZLINK_GATEWAY_LB_WEIGHTED    1
```

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_GATEWAY_LB_ROUND_ROBIN` | 0 | 라운드 로빈 로드 밸런싱 (기본값) |
| `ZLINK_GATEWAY_LB_WEIGHTED` | 1 | 수신자 가중치 기반 가중 로드 밸런싱 |

## 함수

### zlink_gateway_new

Gateway를 생성합니다.

```c
void *zlink_gateway_new(void *ctx,
                        void *discovery,
                        const char *routing_id);
```

새 Gateway 인스턴스를 할당하고 초기화합니다. `discovery` 핸들은
`ZLINK_SERVICE_TYPE_GATEWAY`로 생성되어야 하며 호출자가 소유권을 유지합니다.
`routing_id`는 Receiver에 대해 이 Gateway를 고유하게 식별합니다.

**반환값:** 성공 시 Gateway 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_gateway_send`, `zlink_gateway_recv`, `zlink_gateway_destroy`

---

### zlink_gateway_send

서비스에 메시지를 전송합니다 (로드 밸런싱).

```c
int zlink_gateway_send(void *gateway,
                       const char *service_name,
                       zlink_msg_t *parts,
                       size_t part_count,
                       int flags);
```

`service_name`으로 등록된 Receiver에 멀티파트 메시지를 전송합니다. Gateway는
구성된 로드 밸런싱 전략(기본값은 라운드 로빈)에 따라 Receiver를 선택합니다.
서비스에 사용 가능한 Receiver가 없으면 `EHOSTUNREACH`로 호출이 실패합니다.
성공 시 메시지 파트의 소유권이 Gateway로 이전됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EHOSTUNREACH` -- 서비스에 대한 수신자가 없습니다.
- `EAGAIN` -- `ZLINK_DONTWAIT`가 설정되었으며 작업이 블록됩니다.

**스레드 안전성:** 스레드 안전함. 여러 스레드가 동일한 Gateway 핸들에서
동시에 `zlink_gateway_send`를 호출할 수 있습니다.

**참고:** `zlink_gateway_send_bytes`, `zlink_gateway_recv`, `zlink_gateway_send_rid`, `zlink_gateway_set_lb_strategy`

---

### zlink_gateway_send_bytes

서비스에 단일 파트 바이트 버퍼를 전송합니다 (로드 밸런싱).

```c
int zlink_gateway_send_bytes(void *gateway,
                             const char *service_name,
                             const void *data,
                             size_t size,
                             int flags);
```

`service_name`으로 등록된 Receiver에 단일 파트 payload를 전송합니다.
단일 버퍼 전송에 최적화된 편의 API로, 호출자가 `zlink_msg_t`를 직접
구성/해제할 필요가 없습니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- `size > 0`인데 `data == NULL`.
- `EHOSTUNREACH` -- 서비스에 대한 수신자가 없습니다.
- `EAGAIN` -- `ZLINK_DONTWAIT`가 설정되었으며 작업이 블록됩니다.

**스레드 안전성:** 스레드 안전함.

**참고:** `zlink_gateway_send`, `zlink_gateway_send_rid_bytes`

---

### zlink_gateway_recv

메시지를 수신합니다.

```c
int zlink_gateway_recv(void *gateway,
                       zlink_msg_t **parts,
                       size_t *part_count,
                       int flags,
                       char *service_name_out);
```

연결된 모든 Receiver로부터 멀티파트 응답을 수신합니다. 성공 시 `*parts`는
새로 할당된 메시지 파트 배열로 설정되고 `*part_count`는 파트 수로 설정됩니다.
호출자는 각 파트를 `zlink_msg_close`로 닫고 배열을 해제해야 합니다.
`service_name_out` 매개변수가 `NULL`이 아닌 경우 최소 256바이트 버퍼를
가리켜야 하며, 원본 서비스 이름이 여기에 기록됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EAGAIN` -- `ZLINK_DONTWAIT`가 설정되었으며 사용 가능한 메시지가 없습니다.

**스레드 안전성:** 스레드 안전하지 않음. 한 번에 하나의 스레드만
`zlink_gateway_recv`를 호출해야 합니다.

**참고:** `zlink_gateway_send`

---

### zlink_gateway_send_rid

라우팅 ID로 특정 Receiver에 직접 메시지를 전송합니다.

```c
int zlink_gateway_send_rid(void *gateway,
                           const char *service_name,
                           const zlink_routing_id_t *routing_id,
                           zlink_msg_t *parts,
                           size_t part_count,
                           int flags);
```

로드 밸런싱을 우회하고 지정된 `service_name` 내에서 `routing_id`로 식별되는
Receiver에 멀티파트 메시지를 전송합니다. 이는 상태 유지 프로토콜에서와 같이
특정 Receiver 인스턴스에 응답을 보내야 할 때 유용합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EHOSTUNREACH` -- 지정된 라우팅 ID가 연결되어 있지 않습니다.
- `EAGAIN` -- `ZLINK_DONTWAIT`가 설정되었으며 작업이 블록됩니다.

**스레드 안전성:** 스레드 안전함.

**참고:** `zlink_gateway_send`, `zlink_gateway_send_rid_bytes`

---

### zlink_gateway_send_rid_bytes

라우팅 ID로 특정 Receiver에 단일 파트 바이트 버퍼를 직접 전송합니다.

```c
int zlink_gateway_send_rid_bytes(void *gateway,
                                 const char *service_name,
                                 const zlink_routing_id_t *routing_id,
                                 const void *data,
                                 size_t size,
                                 int flags);
```

로드 밸런싱을 우회하고 지정된 `service_name` 내에서 `routing_id`로 식별되는
Receiver에 단일 파트 payload를 전송합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- `size > 0`인데 `data == NULL`.
- `EHOSTUNREACH` -- 지정된 라우팅 ID가 연결되어 있지 않습니다.
- `EAGAIN` -- `ZLINK_DONTWAIT`가 설정되었으며 작업이 블록됩니다.

**스레드 안전성:** 스레드 안전함.

**참고:** `zlink_gateway_send_rid`, `zlink_gateway_send_bytes`

---

### zlink_gateway_set_lb_strategy

서비스의 로드 밸런싱 전략을 설정합니다.

```c
int zlink_gateway_set_lb_strategy(void *gateway,
                                  const char *service_name,
                                  int strategy);
```

지정된 서비스에 메시지를 전송할 때 사용되는 로드 밸런싱 전략을 변경합니다.
유효한 전략은 `ZLINK_GATEWAY_LB_ROUND_ROBIN`(기본값)과
`ZLINK_GATEWAY_LB_WEIGHTED`입니다. 가중 밸런싱을 사용할 때, 등록 시
Receiver가 보고한 가중치 값이 분배 비율을 결정합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 알 수 없는 전략 값.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_gateway_send`

---

### zlink_gateway_set_tls_client

Gateway의 TLS 클라이언트 설정을 구성합니다.

```c
int zlink_gateway_set_tls_client(void *gateway,
                                 const char *ca_cert,
                                 const char *hostname,
                                 int trust_system);
```

이 Gateway의 발신 연결에 대해 TLS를 활성화합니다. `ca_cert` 매개변수는
Receiver 인증서를 검증하는 데 사용되는 CA 인증서 파일 경로를 지정합니다.
`hostname` 매개변수는 인증서 검증을 위한 예상 서버 이름을 설정합니다.
`trust_system`이 0이 아닌 경우 `ca_cert`에 추가로 시스템 신뢰 저장소가
사용됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. 메시지 전송 전에 호출해야 합니다.

**참고:** `zlink_receiver_set_tls_server`

---

### zlink_gateway_monitor_snapshot

현재 로컬 monitor 상태를 읽습니다.

```c
int zlink_gateway_monitor_snapshot(void *gateway,
                                   zlink_gateway_monitor_snapshot_t *out);
```

`send_ready`는 현재 전송 가능한 route가 하나 이상 있으면 `1`입니다.
`bound_ready`는 로컬 service endpoint가 bind 되어 있으면 `1`입니다.
`ready_peer_count`는 이 handle이 현재 알고 있는 ready route 수입니다.

late subscriber는 monitor를 열기 전에 이 API로 초기 상태를 읽고,
그 이후 edge 전이는 monitor로 받는 패턴을 사용합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

---

### zlink_gateway_set_option

Gateway 서비스 옵션을 설정합니다.

```c
int zlink_gateway_set_option(void *gateway,
                              int option,
                              const void *optval,
                              size_t optvallen);
```

Gateway에 서비스 레벨 옵션을 적용합니다. 사용 가능한 옵션 상수:

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_GATEWAY_OPT_SNDHWM` | 1 | 송신 고수위 마크 |
| `ZLINK_GATEWAY_OPT_RCVHWM` | 2 | 수신 고수위 마크 |
| `ZLINK_GATEWAY_OPT_SNDTIMEO` | 3 | 송신 타임아웃 (ms) |
| `ZLINK_GATEWAY_OPT_RCVTIMEO` | 4 | 수신 타임아웃 (ms) |
| `ZLINK_GATEWAY_OPT_LINGER` | 5 | Linger 기간 (ms) |
| `ZLINK_GATEWAY_OPT_SNDBUF` | 6 | 커널 송신 버퍼 크기 (바이트) |
| `ZLINK_GATEWAY_OPT_RCVBUF` | 7 | 커널 수신 버퍼 크기 (바이트) |

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 알 수 없는 옵션.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_gateway_new`

---

### zlink_gateway_set_routing_id

첫 사용 전에 대표 routing id를 재정의합니다.

```c
int zlink_gateway_set_routing_id(void *gateway,
                                  const void *data,
                                  size_t size);
```

이 Gateway의 커스텀 라우팅 아이덴티티를 설정합니다. 메시지 전송 전에
호출해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_gateway_routing_id`

---

### zlink_gateway_routing_id

이 Gateway의 대표 routing id를 반환합니다.

```c
int zlink_gateway_routing_id(void *gateway,
                              zlink_routing_id_t *out);
```

현재 Gateway의 라우팅 아이덴티티를 가져옵니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_gateway_set_routing_id`

---

### zlink_gateway_destroy

Gateway를 파괴하고 모든 리소스를 해제합니다.

```c
int zlink_gateway_destroy(void **gateway_p);
```

모든 연결을 닫고, 내부 소켓을 해제하며, Gateway를 해제합니다. 파괴 후
`*gateway_p`의 포인터는 `NULL`로 설정됩니다. `zlink_gateway_new`에 전달된
Discovery 핸들은 영향을 받지 않으며 별도로 파괴해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. 다른 Gateway 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_gateway_new`
