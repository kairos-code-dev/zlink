[English](gateway.md) | [한국어](gateway.ko.md)

# 게이트웨이

Gateway는 Discovery를 통해 서비스 위치를 자동으로 확인하는 클라이언트 측
로드 밸런싱 요청/응답 프록시입니다. 필요에 따라 Receiver에 연결하고 구성
가능한 로드 밸런싱 전략을 사용하여 메시지를 분배합니다.

## 상수

```c
#define ZLINK_GATEWAY_LB_ROUND_ROBIN 0
#define ZLINK_GATEWAY_LB_WEIGHTED    1
#define ZLINK_GATEWAY_SOCKET_ROUTER  1
```

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_GATEWAY_LB_ROUND_ROBIN` | 0 | 라운드 로빈 로드 밸런싱 (기본값) |
| `ZLINK_GATEWAY_LB_WEIGHTED` | 1 | 수신자 가중치 기반 가중 로드 밸런싱 |
| `ZLINK_GATEWAY_SOCKET_ROUTER` | 1 | 통신에 사용되는 내부 ROUTER 소켓 |

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

**참고:** `zlink_gateway_recv`, `zlink_gateway_send_rid`, `zlink_gateway_set_lb_strategy`

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

**참고:** `zlink_gateway_send`

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

### zlink_gateway_setsockopt

Gateway 소켓 옵션을 설정합니다.

```c
int zlink_gateway_setsockopt(void *gateway,
                             int option,
                             const void *optval,
                             size_t optvallen);
```

Gateway의 내부 ROUTER 소켓에 저수준 소켓 옵션을 적용합니다. 일반적으로
송수신 하이워터마크, 타임아웃 또는 keep-alive 설정을 구성하는 데 사용됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 알 수 없는 옵션.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_gateway_new`

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

### zlink_gateway_router

내부 ROUTER 소켓 핸들을 반환합니다.

```c
void *zlink_gateway_router(void *gateway);
```

Gateway가 내부적으로 사용하는 원시 ROUTER 소켓 핸들을 반환합니다. 이는
진단 및 커스텀 폴링과 같은 고급 사용 사례를 위한 것입니다. 호출자는 소켓을
닫거나 수정해서는 안 됩니다.

**반환값:** ROUTER 소켓 핸들, Gateway가 유효하지 않으면 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_gateway_new`

---

### zlink_gateway_connection_count

서비스에 연결된 수신자 수를 반환합니다.

```c
int zlink_gateway_connection_count(void *gateway,
                                   const char *service_name);
```

지정된 서비스 이름에 대해 현재 연결된 Receiver 수를 반환합니다. 이는
Discovery가 보고하는 수가 아닌 활성 전송 수준 연결을 반영합니다.

**반환값:** 성공 시 연결 수(0 이상), 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_discovery_receiver_count`

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
