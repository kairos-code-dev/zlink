[English](spot.md) | [한국어](spot.ko.md)

# SPOT PUB/SUB

SPOT은 Discovery를 통한 자동 메시 형성으로 토픽 기반의 위치 투명한
발행/구독 메시징을 제공합니다. SPOT 배포는 메시를 형성하는 하나 이상의 노드,
메시지를 주입하는 Publisher, 메시지를 소비하는 Subscriber로 구성됩니다.

## 타입

```c
typedef void (*zlink_spot_sub_handler_fn)(const char *topic,
                                         size_t topic_len,
                                         const zlink_msg_t *parts,
                                         size_t part_count,
                                         void *userdata);
```

핸들러 기반 SPOT 구독자 디스패치를 위한 콜백 함수 타입입니다.
`zlink_spot_sub_set_handler`를 통해 등록하면, 수신 메시지가
`zlink_spot_sub_recv` 대신 이 콜백을 통해 자동으로 전달됩니다.

## 상수

```c
#define ZLINK_SPOT_NODE_SOCKET_PUB    1
#define ZLINK_SPOT_NODE_SOCKET_SUB    2
#define ZLINK_SPOT_NODE_SOCKET_DEALER 3
```

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_SPOT_NODE_SOCKET_PUB` | 1 | 구독자 및 피어에 메시지를 게시하는 데 사용되는 PUB 소켓 |
| `ZLINK_SPOT_NODE_SOCKET_SUB` | 2 | 피어 노드로부터 메시지를 수신하는 데 사용되는 SUB 소켓 |
| `ZLINK_SPOT_NODE_SOCKET_DEALER` | 3 | Registry와의 통신에 사용되는 DEALER 소켓 |

## SPOT 노드

SPOT 노드는 메시 토폴로지를 형성하는 기본 PUB, SUB 및 DEALER 소켓을
관리합니다. Publisher와 Subscriber는 노드에 연결하여 메시지를 송수신합니다.

### zlink_spot_node_new

SPOT 노드를 생성합니다.

```c
void *zlink_spot_node_new(void *ctx);
```

새 SPOT 노드를 할당하고 초기화합니다. 노드는 토픽 기반 메시징을 위한 내부
PUB, SUB 및 DEALER 소켓을 관리합니다. 컨텍스트 핸들은 노드의 수명 동안
유효해야 합니다.

**반환값:** 성공 시 SPOT 노드 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_spot_node_bind`, `zlink_spot_node_destroy`

---

### zlink_spot_node_destroy

SPOT 노드를 파괴하고 모든 리소스를 해제합니다.

```c
int zlink_spot_node_destroy(void **node_p);
```

모든 내부 소켓을 닫고, 내부 상태를 해제하며, 노드를 해제합니다. 파괴 후
`*node_p`의 포인터는 `NULL`로 설정됩니다. 이 노드에 연결된 모든 Publisher와
Subscriber는 이 함수를 호출하기 전에 파괴해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음. 다른 노드 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_spot_node_new`

---

### zlink_spot_node_bind

SPOT 노드를 엔드포인트에 바인딩합니다.

```c
int zlink_spot_node_bind(void *node, const char *endpoint);
```

노드의 PUB 소켓을 지정된 엔드포인트에 바인딩합니다. 피어 노드와 로컬
구독자는 이 엔드포인트에 연결하여 게시된 메시지를 수신합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EADDRINUSE` -- 엔드포인트가 이미 사용 중입니다.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_node_register`

---

### zlink_spot_node_connect_registry

서비스 등록을 위해 Registry 엔드포인트에 연결합니다.

```c
int zlink_spot_node_connect_registry(void *node,
                                     const char *registry_endpoint);
```

노드의 내부 DEALER 소켓을 Registry의 ROUTER 엔드포인트에 연결합니다. 이
연결은 등록, 등록 해제, 하트비트 메시지를 전송하는 데 사용됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_node_register`

---

### zlink_spot_node_connect_peer_pub

피어 노드의 PUB 엔드포인트에 연결합니다.

```c
int zlink_spot_node_connect_peer_pub(void *node,
                                     const char *peer_pub_endpoint);
```

노드의 내부 SUB 소켓을 피어 노드의 PUB 엔드포인트에 연결하여 메시 토폴로지의
일부를 형성합니다. 피어에서 게시된 메시지는 이 연결을 통해 로컬 구독자에게
전달됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_node_disconnect_peer_pub`, `zlink_spot_node_set_discovery`

---

### zlink_spot_node_disconnect_peer_pub

피어 노드의 PUB 엔드포인트와의 연결을 해제합니다.

```c
int zlink_spot_node_disconnect_peer_pub(void *node,
                                        const char *peer_pub_endpoint);
```

노드의 내부 SUB 소켓을 이전에 연결된 피어 PUB 엔드포인트에서 연결 해제합니다.
해당 피어에 대한 메시 링크가 해제됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_node_connect_peer_pub`

---

### zlink_spot_node_register

이 노드를 SPOT 서비스로 Registry에 등록합니다.

```c
int zlink_spot_node_register(void *node,
                             const char *service_name,
                             const char *advertise_endpoint);
```

지정된 서비스 이름에 대한 등록 요청을 Registry에 전송합니다.
`advertise_endpoint`는 피어 노드가 연결할 엔드포인트입니다 (일반적으로
`zlink_spot_node_bind`에 전달된 것과 동일한 엔드포인트). 등록되면
Discovery를 사용하는 피어 노드가 자동으로 연결하여 메시를 형성합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_node_unregister`, `zlink_spot_node_set_discovery`

---

### zlink_spot_node_unregister

이 노드의 Registry 등록을 해제합니다.

```c
int zlink_spot_node_unregister(void *node,
                               const char *service_name);
```

지정된 서비스 이름에 대한 등록 해제 요청을 Registry에 전송합니다. 다음
브로드캐스트 주기 이후 피어 노드는 더 이상 지정된 서비스에 대해 이 노드를
검색할 수 없습니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_node_register`

---

### zlink_spot_node_set_discovery

자동 피어 연결을 위해 Discovery 인스턴스를 연결합니다.

```c
int zlink_spot_node_set_discovery(void *node,
                                  void *discovery,
                                  const char *service_name);
```

자동 메시 형성을 위해 이 노드에 Discovery 인스턴스를 연결합니다. Discovery
핸들은 `ZLINK_SERVICE_TYPE_SPOT`으로 생성되어야 합니다. 노드는
`service_name` 하에서 피어 추가 및 제거를 감시하고, 피어 PUB 엔드포인트가
나타나거나 사라질 때 자동으로 연결 또는 연결 해제합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_node_connect_peer_pub`, `zlink_discovery_new_typed`

---

### zlink_spot_node_set_tls_server

노드의 TLS 서버 인증서를 설정합니다.

```c
int zlink_spot_node_set_tls_server(void *node,
                                   const char *cert,
                                   const char *key);
```

노드의 PUB 소켓이 지정된 서버 인증서 및 개인 키를 사용하여 TLS를
사용하도록 구성합니다. `zlink_spot_node_bind` 호출 전에 설정해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_node_set_tls_client`

---

### zlink_spot_node_set_tls_client

노드의 TLS 클라이언트 설정을 구성합니다.

```c
int zlink_spot_node_set_tls_client(void *node,
                                   const char *ca_cert,
                                   const char *hostname,
                                   int trust_system);
```

피어 노드에 대한 발신 SUB 연결에 TLS를 활성화합니다. `ca_cert` 매개변수는
CA 인증서 파일 경로를 지정합니다. `hostname` 매개변수는 인증서 검증을 위한
예상 서버 이름을 설정합니다. `trust_system`이 0이 아닌 경우 `ca_cert`에
추가로 시스템 신뢰 저장소가 사용됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_node_set_tls_server`

---

### zlink_spot_node_pub_socket

내부 PUB 소켓 핸들을 반환합니다.

```c
void *zlink_spot_node_pub_socket(void *node);
```

노드가 내부적으로 사용하는 원시 PUB 소켓 핸들을 반환합니다. 이는 진단 및
고급 사용 사례를 위한 것입니다. 호출자는 소켓을 닫거나 수정해서는 안 됩니다.

**반환값:** PUB 소켓 핸들, 노드가 유효하지 않으면 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_spot_node_sub_socket`

---

### zlink_spot_node_sub_socket

내부 SUB 소켓 핸들을 반환합니다.

```c
void *zlink_spot_node_sub_socket(void *node);
```

노드가 내부적으로 사용하는 원시 SUB 소켓 핸들을 반환합니다. 이는 진단 및
고급 사용 사례를 위한 것입니다. 호출자는 소켓을 닫거나 수정해서는 안 됩니다.

**반환값:** SUB 소켓 핸들, 노드가 유효하지 않으면 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_spot_node_pub_socket`

---

### zlink_spot_node_setsockopt

내부 SPOT 노드 소켓의 소켓 옵션을 설정합니다.

```c
int zlink_spot_node_setsockopt(void *node,
                               int socket_role,
                               int option,
                               const void *optval,
                               size_t optvallen);
```

`socket_role`로 식별되는 노드 내부 소켓 중 하나에 저수준 소켓 옵션을
적용합니다. `ZLINK_SPOT_NODE_SOCKET_*` 상수를 사용하여 대상 소켓을
선택합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 잘못된 소켓 역할 또는 알 수 없는 옵션.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_node_new`

---

## SPOT Pub

SPOT Publisher는 노드에 연결되어 토픽 식별자로 메시지를 게시합니다. 동일한
노드에 여러 Publisher를 연결할 수 있습니다.

### zlink_spot_pub_new

지정된 노드에 연결된 스레드 안전 SPOT publisher를 생성합니다.

```c
void *zlink_spot_pub_new(void *node);
```

새 SPOT Publisher를 할당하고 초기화합니다. Publisher는 지정된 노드에
연결되며 해당 노드의 PUB 소켓을 사용하여 메시지를 분배합니다. 노드는
Publisher의 수명 동안 유효해야 합니다.

**반환값:** 성공 시 SPOT Publisher 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_spot_pub_publish`, `zlink_spot_pub_destroy`

---

### zlink_spot_pub_destroy

SPOT publisher를 파괴합니다.

```c
int zlink_spot_pub_destroy(void **pub_p);
```

Publisher를 해제하고 `*pub_p`를 `NULL`로 설정합니다. 기본 노드는 영향을
받지 않습니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_pub_new`

---

### zlink_spot_pub_publish

토픽으로 멀티파트 메시지를 게시합니다.

```c
int zlink_spot_pub_publish(void *pub,
                           const char *topic_id,
                           zlink_msg_t *parts,
                           size_t part_count,
                           int flags);
```

지정된 토픽 식별자로 노드의 PUB 소켓에 멀티파트 메시지를 게시합니다. 이
토픽(또는 일치하는 패턴)을 구독한 Subscriber가 메시지를 수신합니다. 성공 시
메시지 파트의 소유권이 이전됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전함. 여러 스레드에서의 동시 호출은 뮤텍스를 통해
내부적으로 직렬화됩니다. 이를 통해 여러 publisher 또는 애플리케이션 스레드가
외부 잠금 없이 동일한 노드에서 게시할 수 있습니다.

**참고:** `zlink_spot_sub_subscribe`, `zlink_spot_pub_new`

---

### zlink_spot_pub_setsockopt

SPOT publisher의 소켓 옵션을 설정합니다.

```c
int zlink_spot_pub_setsockopt(void *pub,
                              int option,
                              const void *optval,
                              size_t optvallen);
```

Publisher의 기본 소켓에 저수준 소켓 옵션을 적용합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 알 수 없는 옵션.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_pub_new`

---

## SPOT Sub

SPOT Subscriber는 노드에 연결되어 구독과 일치하는 메시지를 수신합니다.
메시지는 두 가지 방법으로 소비할 수 있습니다:

- **핸들러 기반:** `zlink_spot_sub_set_handler`를 통해 콜백을 등록합니다.
  수신 메시지는 콜백을 통해 자동으로 전달됩니다. 이 모드는 이벤트 기반
  아키텍처에 적합합니다.
- **Recv 기반:** 폴링 루프에서 `zlink_spot_sub_recv`를 호출하여 메시지를
  동기적으로 수신합니다. 이 모드는 메시지 소비 시점을 명시적으로 제어합니다.

두 모드는 상호 배타적입니다. 핸들러가 설정된 경우 `zlink_spot_sub_recv`를
동시에 호출해서는 안 됩니다. 핸들러를 지우고 recv 기반 소비로 되돌리려면
`zlink_spot_sub_set_handler`에 `NULL`을 전달합니다.

### zlink_spot_sub_new

지정된 노드에 연결된 SPOT subscriber를 생성합니다.

```c
void *zlink_spot_sub_new(void *node);
```

새 SPOT Subscriber를 할당하고 초기화합니다. Subscriber는 지정된 노드에
연결되며 노드의 SUB 소켓으로부터 메시지를 수신합니다. 노드는 Subscriber의
수명 동안 유효해야 합니다.

**반환값:** 성공 시 SPOT Subscriber 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_spot_sub_subscribe`, `zlink_spot_sub_destroy`

---

### zlink_spot_sub_destroy

SPOT subscriber를 파괴합니다.

```c
int zlink_spot_sub_destroy(void **sub_p);
```

Subscriber를 해제하고 `*sub_p`를 `NULL`로 설정합니다. 활성 핸들러가 있으면
지워집니다. 기본 노드는 영향을 받지 않습니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_sub_new`

---

### zlink_spot_sub_subscribe

정확한 토픽을 구독합니다.

```c
int zlink_spot_sub_subscribe(void *sub, const char *topic_id);
```

정확한 `topic_id`로 게시된 메시지에 대한 관심을 등록합니다. 이 문자열과
정확히 일치하는 토픽의 메시지만 전달됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_sub_subscribe_pattern`, `zlink_spot_sub_unsubscribe`

---

### zlink_spot_sub_subscribe_pattern

토픽 패턴을 구독합니다 (접두사 매칭).

```c
int zlink_spot_sub_subscribe_pattern(void *sub, const char *pattern);
```

지정된 접두사 패턴으로 시작하는 토픽의 메시지에 대한 관심을 등록합니다.
예를 들어, `"market."`을 구독하면 `"market.price"` 및 `"market.volume"`과
같은 토픽과 일치합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_sub_subscribe`, `zlink_spot_sub_unsubscribe`

---

### zlink_spot_sub_unsubscribe

토픽 또는 패턴 구독을 해제합니다.

```c
int zlink_spot_sub_unsubscribe(void *sub,
                               const char *topic_id_or_pattern);
```

이전에 등록된 구독을 제거합니다. 인수는 `zlink_spot_sub_subscribe` 또는
`zlink_spot_sub_subscribe_pattern`에 전달된 정확한 문자열과 일치해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_sub_subscribe`, `zlink_spot_sub_subscribe_pattern`

---

### zlink_spot_sub_set_handler

자동 메시지 디스패치를 위한 콜백 핸들러를 설정합니다.

```c
int zlink_spot_sub_set_handler(void *sub,
                               zlink_spot_sub_handler_fn handler,
                               void *userdata);
```

수신 메시지마다 자동으로 호출되는 콜백 함수를 등록합니다. 핸들러가 설정되면
메시지는 콜백을 통해 전달되며 `zlink_spot_sub_recv`를 동시에 사용해서는
안 됩니다. 콜백을 지우고 recv 기반 소비로 되돌리려면 `handler`에 `NULL`을
전달합니다. `userdata` 포인터는 각 호출 시 콜백에 전달됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_sub_recv`

---

### zlink_spot_sub_recv

subscriber로부터 메시지를 수신합니다 (폴링 모드).

```c
int zlink_spot_sub_recv(void *sub,
                        zlink_msg_t **parts,
                        size_t *part_count,
                        int flags,
                        char *topic_id_out,
                        size_t *topic_id_len);
```

폴링 모드에서 다음 메시지를 수신합니다. 성공 시 `*parts`는 새로 할당된
메시지 파트 배열로 설정되고 `*part_count`는 파트 수로 설정됩니다. 호출자는
각 파트를 `zlink_msg_close`로 닫고 배열을 해제해야 합니다. `topic_id_out`
버퍼는 토픽 문자열을 수신합니다; 입력 시 `*topic_id_len`은 버퍼 크기를
지정하고, 출력 시 실제 토픽 길이로 설정됩니다. 핸들러가 활성 상태일 때는
호출해서는 안 됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EAGAIN` -- `ZLINK_DONTWAIT`가 설정되었으며 사용 가능한 메시지가 없습니다.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_sub_set_handler`, `zlink_spot_sub_subscribe`

---

### zlink_spot_sub_setsockopt

SPOT subscriber의 소켓 옵션을 설정합니다.

```c
int zlink_spot_sub_setsockopt(void *sub,
                              int option,
                              const void *optval,
                              size_t optvallen);
```

Subscriber의 기본 소켓에 저수준 소켓 옵션을 적용합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 알 수 없는 옵션.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_sub_new`

---

### zlink_spot_sub_socket

원시 SUB 소켓 핸들을 반환합니다.

```c
void *zlink_spot_sub_socket(void *sub);
```

Subscriber가 내부적으로 사용하는 원시 SUB 소켓 핸들을 반환합니다. 이는
커스텀 poll 루프와의 통합과 같은 고급 사용 사례를 위한 것입니다. 호출자는
소켓을 닫거나 수정해서는 안 됩니다.

**반환값:** SUB 소켓 핸들, Subscriber가 유효하지 않으면 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_spot_sub_new`
