[English](receiver.md) | [한국어](receiver.ko.md)

# 수신자

Receiver는 Gateway의 서버 측 대응입니다. Gateway로부터 요청을 수신하고,
응답을 보내며, Registry에 서비스를 등록하여 Gateway가 자동으로 검색하고
연결할 수 있도록 합니다.

## 현재 권장 API 방향

- 공개 서비스 옵션 설정은 `zlink_receiver_set_option()`을 사용합니다.
- 대표 identity는 `zlink_receiver_set_routing_id()` /
  `zlink_receiver_routing_id()`로 다룹니다.
- `ZLINK_RECEIVER_REGISTER_OK` 같은 상태 전이는
  `zlink_receiver_monitor_open()`으로 관찰합니다.
- 데이터 readiness는 `zlink_poller_add_receiver()`, monitor readiness는
  `zlink_poller_add_monitor()`으로 처리합니다.
- `zlink_receiver_register_result()`는 저수준 호환/디버그 경로로 보고,
  새 코드의 기본 setup/readiness 경로로는 사용하지 않는 것이 좋습니다.

## 함수

### zlink_receiver_new

Receiver를 생성합니다.

```c
void *zlink_receiver_new(void *ctx, const char *routing_id);
```

새 Receiver 인스턴스를 할당하고 초기화합니다. `routing_id`는 Gateway 및
Registry에 대해 이 Receiver를 고유하게 식별합니다. 컨텍스트 핸들은
Receiver의 수명 동안 유효해야 합니다.

**반환값:** 성공 시 Receiver 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_receiver_bind`, `zlink_receiver_register`, `zlink_receiver_destroy`

---

### zlink_receiver_bind

ROUTER 소켓을 엔드포인트에 바인딩합니다.

```c
int zlink_receiver_bind(void *receiver,
                        const char *bind_endpoint);
```

Receiver의 내부 ROUTER 소켓을 지정된 엔드포인트에 바인딩합니다. Gateway는
이 엔드포인트에 연결하여 요청을 전송합니다. 엔드포인트는 일반적으로 TCP
주소입니다 (예: `tcp://*:5555`).

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EADDRINUSE` -- 엔드포인트가 이미 사용 중입니다.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_receiver_register`

---

### zlink_receiver_connect_registry

Registry ROUTER 엔드포인트에 연결합니다.

```c
int zlink_receiver_connect_registry(void *receiver,
                                    const char *registry_endpoint);
```

Receiver의 내부 DEALER 소켓을 Registry의 ROUTER 엔드포인트에 연결합니다.
이 연결은 Registry에 등록, 등록 해제, 가중치 업데이트 메시지를 전송하는 데
사용됩니다. Receiver topology entry는 이 control path에서 직접 파생됩니다.
discovery-driven summary에 대한 Registry heartbeat/topology uplink ownership은
Discovery가 담당합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_receiver_register`

---

### zlink_receiver_register

Registry에 서비스를 등록합니다.

```c
int zlink_receiver_register(void *receiver,
                            const char *service_name,
                            const char *advertise_endpoint,
                            uint32_t weight);
```

지정된 서비스 이름에 대한 등록 요청을 Registry에 전송합니다.
`advertise_endpoint`는 Gateway가 연결할 엔드포인트입니다 (일반적으로
`zlink_receiver_bind`에 전달된 것과 동일한 엔드포인트). `weight` 값은
가중 로드 밸런싱이 구성된 Gateway에서 사용됩니다. Receiver는 여러 서비스
이름을 등록할 수 있습니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_receiver_unregister`, `zlink_receiver_update_weight`, `zlink_receiver_register_result`

---

### zlink_receiver_update_weight

등록된 서비스의 가중치를 업데이트합니다.

```c
int zlink_receiver_update_weight(void *receiver,
                                 const char *service_name,
                                 uint32_t weight);
```

이전에 등록된 서비스에 대한 가중치 업데이트 메시지를 Registry에 전송합니다.
가중 로드 밸런싱을 사용하는 Gateway는 다음 브로드캐스트 주기 이후 새 가중치를
반영합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_receiver_register`

---

### zlink_receiver_unregister

서비스 등록을 해제합니다.

```c
int zlink_receiver_unregister(void *receiver,
                              const char *service_name);
```

지정된 서비스 이름에 대한 등록 해제 요청을 Registry에 전송합니다. 다음
브로드캐스트 주기 이후 Gateway는 더 이상 지정된 서비스에 대해 이 Receiver를
볼 수 없습니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_receiver_register`

---

### zlink_receiver_register_result

등록 결과를 조회합니다.

```c
int zlink_receiver_register_result(void *receiver,
                                   const char *service_name,
                                   int *status,
                                   char *resolved_endpoint,
                                   char *error_message);
```

지정된 서비스 이름에 대한 Registry의 비동기 등록 확인을 검색합니다. `status`
출력은 등록 상태 코드를 수신합니다. `resolved_endpoint` 출력(256바이트 버퍼)은
Registry가 확인한 엔드포인트를 수신합니다. `error_message` 출력(256바이트
버퍼)은 등록이 실패한 경우 사람이 읽을 수 있는 에러 설명을 수신합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_receiver_register`

---

### zlink_receiver_set_tls_server

TLS 서버 인증서를 설정합니다.

```c
int zlink_receiver_set_tls_server(void *receiver,
                                  const char *cert,
                                  const char *key);
```

Receiver의 ROUTER 소켓이 지정된 서버 인증서 및 개인 키를 사용하여 TLS를
사용하도록 구성합니다. `cert` 매개변수는 인증서 파일 경로이고 `key`는 개인
키 파일 경로입니다. `zlink_receiver_bind` 호출 전에 설정해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_gateway_set_tls_client`

---

### zlink_receiver_recv

Receiver 서비스 surface에서 multipart 요청 하나를 수신합니다.

```c
int zlink_receiver_recv(void *receiver,
                        zlink_msg_t **parts,
                        size_t *part_count,
                        int flags,
                        zlink_routing_id_t *routing_id_out);
```

현재 Receiver에 도착한 요청 하나를 multipart 배열로 반환합니다. 필요하면
보낸 쪽 routing id도 함께 돌려줍니다. 사용 후에는 `zlink_multipart_close()`로
`parts`를 정리해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EAGAIN` -- `ZLINK_DONTWAIT`가 설정되었고 준비된 요청이 없음.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_poller_add_receiver`, `zlink_multipart_close`

---

### zlink_receiver_last_endpoint

Receiver의 실제 bind endpoint를 조회합니다.

```c
int zlink_receiver_last_endpoint(void *receiver,
                                 char *endpoint,
                                 size_t *size);
```

Receiver 서비스 소켓의 마지막 유효 bind endpoint를 반환합니다. raw 내부
ROUTER 소켓에서 `ZLINK_LAST_ENDPOINT`를 직접 읽는 대신 사용하는 service-level
API입니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_receiver_bind`, `zlink_receiver_register`

---

### zlink_receiver_router_peers

Receiver ROUTER 소켓의 peer queue 정보를 조회합니다.

```c
int zlink_receiver_router_peers(void *receiver,
                                zlink_peer_info_t *peers,
                                size_t *count);
```

내부 ROUTER 소켓의 peer 단위 queue 통계(송신/수신 pending 메시지 수 포함)를
반환합니다. 먼저 `peers = NULL`로 필요한 개수를 조회한 뒤, 버퍼를 할당하여
다시 호출합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_socket_peers`, `zlink_receiver_peer_info`

---

### zlink_receiver_peer_info

Receiver ROUTER 소켓에서 라우팅 아이덴티티로 피어 정보를 가져옵니다.

```c
int zlink_receiver_peer_info(void *receiver,
                              const zlink_routing_id_t *routing_id,
                              zlink_peer_info_t *info);
```

Receiver 내부 ROUTER 소켓에서 `routing_id`로 식별되는 피어를 조회하고
`info` 구조체에 주소, 연결 시간, 메시지 카운터를 채웁니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 라우팅 아이덴티티를 찾을 수 없음.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_receiver_router_peers`, `zlink_socket_peer_info`

---

### zlink_receiver_set_option

Receiver 서비스 옵션을 설정합니다.

```c
int zlink_receiver_set_option(void *receiver,
                               int option,
                               const void *optval,
                               size_t optvallen);
```

Receiver에 서비스 레벨 옵션을 적용합니다. 사용 가능한 옵션 상수:

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_RECEIVER_OPT_SNDHWM` | 1 | 송신 고수위 마크 |
| `ZLINK_RECEIVER_OPT_RCVHWM` | 2 | 수신 고수위 마크 |
| `ZLINK_RECEIVER_OPT_SNDTIMEO` | 3 | 송신 타임아웃 (ms) |
| `ZLINK_RECEIVER_OPT_RCVTIMEO` | 4 | 수신 타임아웃 (ms) |
| `ZLINK_RECEIVER_OPT_LINGER` | 5 | Linger 기간 (ms) |
| `ZLINK_RECEIVER_OPT_SNDBUF` | 6 | 커널 송신 버퍼 크기 (바이트) |
| `ZLINK_RECEIVER_OPT_RCVBUF` | 7 | 커널 수신 버퍼 크기 (바이트) |

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 알 수 없는 옵션.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_receiver_new`

---

### zlink_receiver_set_routing_id

첫 사용 전에 대표 routing id를 재정의합니다.

```c
int zlink_receiver_set_routing_id(void *receiver,
                                   const void *data,
                                   size_t size);
```

이 Receiver의 커스텀 라우팅 아이덴티티를 설정합니다.
`zlink_receiver_bind` 호출 전에 설정해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_receiver_routing_id`

---

### zlink_receiver_routing_id

이 Receiver의 대표 routing id를 반환합니다.

```c
int zlink_receiver_routing_id(void *receiver,
                               zlink_routing_id_t *out);
```

현재 Receiver의 라우팅 아이덴티티를 가져옵니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_receiver_set_routing_id`

---

### zlink_receiver_destroy

Receiver를 파괴하고 모든 리소스를 해제합니다.

```c
int zlink_receiver_destroy(void **receiver_p);
```

모든 소켓을 닫고, 내부 상태를 해제하며, Receiver를 해제합니다. 파괴 후
`*receiver_p`의 포인터는 `NULL`로 설정됩니다. 등록된 모든 서비스는
Receiver가 파괴될 때 암묵적으로 등록 해제됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. 다른 Receiver 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_receiver_new`
