[English](registry.md) | [한국어](registry.ko.md)

# 레지스트리

레지스트리는 zlink 서비스 계층의 중앙 서비스 디렉터리입니다. Receiver 및
SPOT 노드로부터 서비스 등록, 등록 해제, 하트비트 요청을 수신하고, 집계된
서비스 목록을 연결된 모든 Discovery 인스턴스에 주기적으로 브로드캐스트합니다.

## 상수

```c
#define ZLINK_REGISTRY_SOCKET_PUB      1
#define ZLINK_REGISTRY_SOCKET_ROUTER   2
#define ZLINK_REGISTRY_SOCKET_PEER_SUB 3
```

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_REGISTRY_SOCKET_PUB` | 1 | 서비스 목록 브로드캐스트에 사용되는 PUB 소켓 |
| `ZLINK_REGISTRY_SOCKET_ROUTER` | 2 | 등록 및 하트비트 수신에 사용되는 ROUTER 소켓 |
| `ZLINK_REGISTRY_SOCKET_PEER_SUB` | 3 | 피어 레지스트리 브로드캐스트 구독에 사용되는 SUB 소켓 |

## 함수

### zlink_registry_new

서비스 레지스트리를 생성합니다.

```c
void *zlink_registry_new(void *ctx);
```

새 Registry 인스턴스를 할당하고 초기화합니다. Registry는 브로드캐스트 및
등록 수신을 위한 내부 PUB 및 ROUTER 소켓을 관리합니다. 컨텍스트 핸들은
Registry의 수명 동안 유효해야 합니다.

**반환값:** 성공 시 Registry 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_registry_set_endpoints`, `zlink_registry_start`, `zlink_registry_destroy`

---

### zlink_registry_set_endpoints

Registry의 PUB 및 ROUTER 엔드포인트를 설정합니다.

```c
int zlink_registry_set_endpoints(void *registry,
                                 const char *pub_endpoint,
                                 const char *router_endpoint);
```

Registry가 바인딩할 엔드포인트를 구성합니다. PUB 엔드포인트는 Discovery
인스턴스에 서비스 목록을 브로드캐스트하는 데 사용됩니다. ROUTER 엔드포인트는
Receiver 및 SPOT 노드로부터 등록, 등록 해제, 하트비트 메시지를 수신하는 데
사용됩니다. `zlink_registry_start` 호출 전에 설정해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. `zlink_registry_start` 호출 전에 설정해야 합니다.

**참고:** `zlink_registry_new`, `zlink_registry_start`

---

### zlink_registry_set_id

레지스트리 고유 ID를 설정합니다.

```c
int zlink_registry_set_id(void *registry, uint32_t registry_id);
```

이 Registry 인스턴스에 고유 식별자를 할당합니다. ID는 여러 레지스트리가
피어 연결을 통해 서로 동기화하는 클러스터 구성에 사용됩니다.
`zlink_registry_start` 호출 전에 설정해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. `zlink_registry_start` 호출 전에 설정해야 합니다.

**참고:** `zlink_registry_add_peer`

---

### zlink_registry_add_peer

클러스터 동기화를 위한 피어 레지스트리 PUB 엔드포인트를 추가합니다.

```c
int zlink_registry_add_peer(void *registry,
                            const char *peer_pub_endpoint);
```

이 Registry를 피어 Registry의 PUB 엔드포인트에 연결하여 클러스터 전체에서
서비스 목록을 동기화할 수 있도록 합니다. 여러 피어를 추가할 수 있습니다.
`zlink_registry_start` 호출 전에 설정해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. `zlink_registry_start` 호출 전에 설정해야 합니다.

**참고:** `zlink_registry_set_id`

---

### zlink_registry_set_heartbeat

하트비트 간격 및 타임아웃을 설정합니다.

```c
int zlink_registry_set_heartbeat(void *registry,
                                 uint32_t interval_ms,
                                 uint32_t timeout_ms);
```

Registry가 등록된 서비스로부터 하트비트 메시지를 기대하는 빈도와 서비스를
만료로 간주하는 시점을 구성합니다. 서비스가 `timeout_ms` 밀리초 이내에
하트비트를 보내지 않으면 Registry는 해당 서비스를 서비스 목록에서 제거합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. `zlink_registry_start` 호출 전에 설정해야 합니다.

**참고:** `zlink_registry_set_broadcast_interval`

---

### zlink_registry_set_broadcast_interval

서비스 목록 브로드캐스트 간격을 설정합니다.

```c
int zlink_registry_set_broadcast_interval(void *registry,
                                          uint32_t interval_ms);
```

Registry가 PUB 소켓을 통해 전체 서비스 목록을 게시하는 빈도를 제어합니다.
PUB 엔드포인트를 구독하는 Discovery 인스턴스는 이 간격으로 업데이트를
수신합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. `zlink_registry_start` 호출 전에 설정해야 합니다.

**참고:** `zlink_registry_set_heartbeat`

---

### zlink_registry_setsockopt

내부 Registry 소켓의 소켓 옵션을 설정합니다.

```c
int zlink_registry_setsockopt(void *registry,
                              int socket_role,
                              int option,
                              const void *optval,
                              size_t optvallen);
```

`socket_role`로 식별되는 Registry 내부 소켓 중 하나에 저수준 소켓 옵션을
적용합니다. `ZLINK_REGISTRY_SOCKET_*` 상수를 사용하여 대상 소켓을 선택합니다.
`zlink_registry_start` 호출 전에 설정해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 잘못된 소켓 역할 또는 알 수 없는 옵션.

**스레드 안전성:** 스레드 안전하지 않음. `zlink_registry_start` 호출 전에 설정해야 합니다.

**참고:** `zlink_registry_set_endpoints`

---

### zlink_registry_start

Registry를 시작합니다.

```c
int zlink_registry_start(void *registry);
```

구성된 엔드포인트를 바인딩하고, 내부 스레드를 생성하며, 등록 수신 및 서비스
목록 브로드캐스트를 시작합니다. 모든 구성(엔드포인트, 하트비트, 브로드캐스트
간격, 소켓 옵션, 피어)은 이 함수를 호출하기 전에 설정해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. Registry당 정확히 한 번만 호출해야 합니다.

**참고:** `zlink_registry_set_endpoints`, `zlink_registry_destroy`

---

### zlink_registry_destroy

Registry를 파괴하고 모든 리소스를 해제합니다.

```c
int zlink_registry_destroy(void **registry_p);
```

내부 스레드를 중지하고, 모든 소켓을 닫고, Registry를 해제합니다. 파괴 후
`*registry_p`의 포인터는 `NULL`로 설정됩니다. Registry가 시작된 경우
이 함수는 내부 스레드가 종료될 때까지 블록합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. 다른 Registry 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_registry_new`
