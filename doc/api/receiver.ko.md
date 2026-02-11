[English](receiver.md) | [한국어](receiver.ko.md)

# 수신자

Receiver는 Gateway의 서버 측 대응입니다. Gateway로부터 요청을 수신하고,
응답을 보내며, Registry에 서비스를 등록하여 Gateway가 자동으로 검색하고
연결할 수 있도록 합니다.

## 상수

```c
#define ZLINK_RECEIVER_SOCKET_ROUTER 1
#define ZLINK_RECEIVER_SOCKET_DEALER 2
```

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_RECEIVER_SOCKET_ROUTER` | 1 | 요청 수신 및 응답 전송에 사용되는 ROUTER 소켓 |
| `ZLINK_RECEIVER_SOCKET_DEALER` | 2 | Registry와의 통신에 사용되는 DEALER 소켓 |

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
int zlink_receiver_bind(void *provider,
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
int zlink_receiver_connect_registry(void *provider,
                                    const char *registry_endpoint);
```

Receiver의 내부 DEALER 소켓을 Registry의 ROUTER 엔드포인트에 연결합니다.
이 연결은 Registry에 등록, 등록 해제, 하트비트 및 가중치 업데이트 메시지를
전송하는 데 사용됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_receiver_register`

---

### zlink_receiver_register

Registry에 서비스를 등록합니다.

```c
int zlink_receiver_register(void *provider,
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
int zlink_receiver_update_weight(void *provider,
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
int zlink_receiver_unregister(void *provider,
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
int zlink_receiver_register_result(void *provider,
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
int zlink_receiver_set_tls_server(void *provider,
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

### zlink_receiver_setsockopt

내부 Receiver 소켓의 소켓 옵션을 설정합니다.

```c
int zlink_receiver_setsockopt(void *provider,
                              int socket_role,
                              int option,
                              const void *optval,
                              size_t optvallen);
```

`socket_role`로 식별되는 Receiver 내부 소켓 중 하나에 저수준 소켓 옵션을
적용합니다. 요청/응답 소켓에는 `ZLINK_RECEIVER_SOCKET_ROUTER`를, Registry
통신 소켓에는 `ZLINK_RECEIVER_SOCKET_DEALER`를 사용합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 잘못된 소켓 역할 또는 알 수 없는 옵션.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_receiver_bind`

---

### zlink_receiver_router

내부 ROUTER 소켓 핸들을 반환합니다.

```c
void *zlink_receiver_router(void *provider);
```

Receiver가 내부적으로 사용하는 원시 ROUTER 소켓 핸들을 반환합니다. 이는
진단 및 커스텀 폴링과 같은 고급 사용 사례를 위한 것입니다. 호출자는 소켓을
닫거나 수정해서는 안 됩니다.

**반환값:** ROUTER 소켓 핸들, Receiver가 유효하지 않으면 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_receiver_new`

---

### zlink_receiver_destroy

Receiver를 파괴하고 모든 리소스를 해제합니다.

```c
int zlink_receiver_destroy(void **provider_p);
```

모든 소켓을 닫고, 내부 상태를 해제하며, Receiver를 해제합니다. 파괴 후
`*provider_p`의 포인터는 `NULL`로 설정됩니다. 등록된 모든 서비스는
Receiver가 파괴될 때 암묵적으로 등록 해제됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. 다른 Receiver 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_receiver_new`
