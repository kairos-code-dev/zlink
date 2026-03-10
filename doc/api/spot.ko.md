[English](spot.md) | [한국어](spot.ko.md)

# SPOT PUB/SUB

SPOT은 Discovery를 통한 자동 메시 형성으로 토픽 기반의 위치 투명한
발행/구독 메시징을 제공합니다. SPOT 배포는 메시를 형성하는 하나 이상의 노드,
메시지를 주입하는 Publisher, 메시지를 소비하는 Subscriber로 구성됩니다.

## 현재 권장 API 방향

- `SpotNode`는 bind/connect/discovery/TLS wiring owner이며, direct publish/recv를
  위한 node-owned 기본 `SpotPub` / `SpotSub` facade도 노출할 수 있습니다.
- 실제 public service surface는 여전히 `SpotPub`과 `SpotSub`이며,
  `zlink_spot_node_default_pub()` / `zlink_spot_node_default_sub()`가 반환하는
  embedded handle도 같은 surface를 재사용합니다.
- 서비스 옵션은 `zlink_spot_pub_set_option()` /
  `zlink_spot_sub_set_option()`으로 설정합니다.
- 대표 identity는 `zlink_spot_pub_set_routing_id()` /
  `zlink_spot_sub_set_routing_id()`로 다룹니다.
- 상태 전이는 `zlink_spot_pub_monitor_open()` /
  `zlink_spot_sub_monitor_open()`으로 관찰합니다. 토폴로지 수준의
  상태 보고는 Discovery가 소유하는 topology summary를 통해 이루어집니다.
- `SpotNode`는 explicit child handle owner로도, `zlink_spot_node_publish*()`,
  `zlink_spot_node_recv()`, `zlink_spot_node_subscribe*()`를 통한 direct facade로도
  사용할 수 있습니다.
- `zlink_spot_node_register()`는 attached Discovery의 uplink runtime을
  통해 등록을 제출합니다. `set_discovery()` 호출이 선행되어야 합니다.

## 옵션 요약

현재 public option surface는 `SpotPub` / `SpotSub` 기준으로 정리되어 있습니다.

| 대상 | 설정 API | 지원 옵션 | 비고 |
|------|----------|-----------|------|
| `SpotPub` | `zlink_spot_pub_set_option()` | `ZLINK_SPOT_PUB_OPT_SNDHWM`, `ZLINK_SPOT_PUB_OPT_SNDTIMEO`, `ZLINK_SPOT_PUB_OPT_LINGER`, `ZLINK_SPOT_PUB_OPT_NODROP`, `ZLINK_SPOT_PUB_OPT_SNDBUF`, `ZLINK_SPOT_PUB_OPT_RCVBUF` | 현재 canonical publish-side option surface |
| `SpotSub` | `zlink_spot_sub_set_option()` | `ZLINK_SPOT_SUB_OPT_RCVHWM`, `ZLINK_SPOT_SUB_OPT_RCVTIMEO`, `ZLINK_SPOT_SUB_OPT_LINGER`, `ZLINK_SPOT_SUB_OPT_QUEUE_NODROP`, `ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY`, `ZLINK_SPOT_SUB_OPT_SNDBUF`, `ZLINK_SPOT_SUB_OPT_RCVBUF` | 현재 canonical subscribe-side option surface |
| `SpotNode` | `zlink_spot_node_set_pub_option()` / `zlink_spot_node_set_sub_option()` | `ZLINK_SPOT_PUB_OPT_SNDHWM`, `ZLINK_SPOT_PUB_OPT_SNDTIMEO`, `ZLINK_SPOT_PUB_OPT_LINGER`, `ZLINK_SPOT_PUB_OPT_NODROP`, `ZLINK_SPOT_PUB_OPT_SNDBUF`, `ZLINK_SPOT_PUB_OPT_RCVBUF`, `ZLINK_SPOT_SUB_OPT_RCVHWM`, `ZLINK_SPOT_SUB_OPT_RCVTIMEO`, `ZLINK_SPOT_SUB_OPT_LINGER`, `ZLINK_SPOT_SUB_OPT_SNDBUF`, `ZLINK_SPOT_SUB_OPT_RCVBUF` | embedded default handle + 이후 생성되는 child handle 기본값 |

제거된 queue/async 계열 옵션:

- `ZLINK_SPOT_PUB_OPT_MODE`
- `ZLINK_SPOT_PUB_OPT_QUEUE_HWM`
- `ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY`

위 세 옵션은 proxy 기반 재작성 이후 지원하지 않으며, 설정 시 `ENOTSUP`을
반환합니다.

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

Proxy 기반 재작성 이후 `ASYNC` 모드 상수는 제거되었습니다. publish는
항상 호출자 스레드에서 내부 inproc PUB facade를 통해 data plane으로
전달됩니다. 동시 호출은 내부에서 직렬화됩니다.

## SPOT 노드

SPOT 노드는 메시 토폴로지를 형성하는 PUB, SUB 소켓과 proxy 기반 data
plane worker를 관리합니다. Publisher와 Subscriber는 노드에 연결하여
메시지를 송수신합니다. Registry 연동은 attached Discovery의 uplink
runtime을 통해 이루어지며, SpotNode 자체는 registry raw socket을
소유하지 않습니다. direct node API를 사용하면 Node는 lazy-init된 embedded
기본 `SpotPub` / `SpotSub`를 소유하고 direct 호출을 그 handle들로 위임합니다.

### zlink_spot_node_new

SPOT 노드를 생성합니다.

```c
void *zlink_spot_node_new(void *ctx);
```

새 SPOT 노드를 할당하고 초기화합니다. 노드는 토픽 기반 메시징을 위한 내부
PUB, SUB 소켓과 proxy 기반 data plane worker를 관리합니다. 컨텍스트
핸들은 노드의 수명 동안 유효해야 합니다.

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

attached Discovery의 registry uplink runtime을 통해 등록 요청을 제출합니다.
`advertise_endpoint`는 피어 노드가 연결할 엔드포인트입니다 (일반적으로
`zlink_spot_node_bind`에 전달된 것과 동일한 엔드포인트). 등록되면
Discovery를 사용하는 피어 노드가 자동으로 연결하여 메시를 형성합니다.

`advertise_endpoint`가 `NULL`이거나 빈 문자열이면, Node는 이미 bind된 public
엔드포인트를 advertise 값으로 유도합니다. 이 동작은 concrete bind가 하나일 때만
허용됩니다. `tcp://*:5555`, `tcp://0.0.0.0:5555`, `tcp://[::]:5555` 같은
wildcard bind는 피어에 광고할 수 없으므로 실패합니다.

**사전 조건:** `zlink_spot_node_set_discovery()`가 먼저 호출되어야 합니다.
Discovery가 attach되지 않은 상태에서 호출하면 `EFSM`으로 실패합니다.

반환값 `0`은 Discovery uplink runtime이 요청을 수락했음을 의미합니다. 피어
가시성은 eventual이며, `register()` 자체를 강한 readiness barrier로
사용하지 말고 Discovery 또는 monitor 이벤트로 확인해야 합니다.

**반환값:** 로컬 수락 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EFSM` -- Discovery가 attach되지 않은 상태에서 호출했습니다.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_node_unregister`, `zlink_spot_node_set_discovery`

---

### zlink_spot_node_unregister

이 노드의 Registry 등록을 해제합니다.

```c
int zlink_spot_node_unregister(void *node,
                               const char *service_name);
```

attached Discovery의 registry uplink runtime을 통해 등록 해제 요청을
제출합니다. 다음 브로드캐스트 주기 이후 피어 노드는 더 이상 지정된 서비스에
대해 이 노드를 검색할 수 없습니다.

**사전 조건:** `zlink_spot_node_set_discovery()`가 먼저 호출되어야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EFSM` -- Discovery가 attach되지 않은 상태에서 호출했습니다.

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

### zlink_spot_pub_set_option

SpotPub 서비스 옵션을 설정합니다.

```c
int zlink_spot_pub_set_option(void *pub,
                               int option,
                               const void *optval,
                               size_t optvallen);
```

Publisher에 서비스 레벨 옵션을 적용합니다. 사용 가능한 옵션 상수:

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_SPOT_PUB_OPT_SNDHWM` | 1 | 송신 고수위 마크 |
| `ZLINK_SPOT_PUB_OPT_SNDTIMEO` | 2 | 송신 타임아웃 (ms) |
| `ZLINK_SPOT_PUB_OPT_LINGER` | 3 | Linger 기간 (ms) |
| `ZLINK_SPOT_PUB_OPT_NODROP` | 4 | HWM 도달 시 메시지 드롭하지 않음 |
| `ZLINK_SPOT_PUB_OPT_SNDBUF` | 8 | 커널 송신 버퍼 크기 (바이트) |
| `ZLINK_SPOT_PUB_OPT_RCVBUF` | 9 | 커널 수신 버퍼 크기 (바이트) |

**제거된 옵션:** `MODE`(5), `QUEUE_HWM`(6), `QUEUE_FULL_POLICY`(7)는
proxy 기반 재작성에서 제거되었습니다. 설정 시 `ENOTSUP`을 반환합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 알 수 없는 옵션.
- `ENOTSUP` -- proxy 기반 구현에서 제거된 queue/filter 옵션입니다.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_pub_new`

---

### zlink_spot_pub_set_routing_id

첫 사용 전에 대표 routing id를 재정의합니다.

```c
int zlink_spot_pub_set_routing_id(void *pub,
                                   const void *data,
                                   size_t size);
```

이 Publisher의 커스텀 라우팅 아이덴티티를 설정합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_pub_routing_id`

---

### zlink_spot_pub_routing_id

이 SpotPub의 대표 routing id를 반환합니다.

```c
int zlink_spot_pub_routing_id(void *pub,
                               zlink_routing_id_t *out);
```

현재 Publisher의 라우팅 아이덴티티를 가져옵니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_spot_pub_set_routing_id`

---

### zlink_spot_pub_peers

SpotPub 피어 큐 통계를 조회합니다.

```c
int zlink_spot_pub_peers(void *pub,
                          zlink_peer_info_t *peers,
                          size_t *count);
```

Publisher의 기본 소켓에서 피어 단위 큐 통계를 반환합니다. 먼저
`peers = NULL`로 필요한 개수를 조회한 뒤, 버퍼를 할당하여 다시 호출합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_socket_peers`

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

**스레드 안전성:** 스레드 안전. 동시 호출은 내부에서 직렬화됩니다. publish는
inproc PUB facade를 통해 data plane worker로 전달됩니다.

**참고:** `zlink_spot_pub_publish_bytes`, `zlink_spot_sub_subscribe`, `zlink_spot_pub_new`

---

### zlink_spot_pub_publish_bytes

토픽으로 단일 파트 바이트 버퍼를 게시합니다.

```c
int zlink_spot_pub_publish_bytes(void *pub,
                                 const char *topic_id,
                                 const void *data,
                                 size_t size,
                                 int flags);
```

지정된 토픽 식별자로 노드의 PUB 소켓에 단일 파트 payload를 게시합니다.
단일 버퍼 게시 경로에서 호출자가 `zlink_msg_t`를 직접 구성하지 않아도 되는
편의 API입니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- `size > 0`인데 `data == NULL`.

**스레드 안전성:** 스레드 안전. 동시 호출은 내부에서 직렬화됩니다.

**참고:** `zlink_spot_pub_publish`, `zlink_spot_pub_new`

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

### zlink_spot_sub_set_option

SpotSub 서비스 옵션을 설정합니다.

```c
int zlink_spot_sub_set_option(void *sub,
                               int option,
                               const void *optval,
                               size_t optvallen);
```

Subscriber에 서비스 레벨 옵션을 적용합니다. 사용 가능한 옵션 상수:

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_SPOT_SUB_OPT_RCVHWM` | 1 | 수신 고수위 마크 |
| `ZLINK_SPOT_SUB_OPT_RCVTIMEO` | 2 | 수신 타임아웃 (ms) |
| `ZLINK_SPOT_SUB_OPT_LINGER` | 3 | Linger 기간 (ms) |
| `ZLINK_SPOT_SUB_OPT_QUEUE_NODROP` | 4 | 큐 가득 참 시 메시지 드롭하지 않음 |
| `ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY` | 5 | 큐 가득 참 정책 |
| `ZLINK_SPOT_SUB_OPT_SNDBUF` | 6 | 커널 송신 버퍼 크기 (바이트) |
| `ZLINK_SPOT_SUB_OPT_RCVBUF` | 7 | 커널 수신 버퍼 크기 (바이트) |

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 알 수 없는 옵션.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_sub_new`

---

### zlink_spot_sub_set_routing_id

첫 사용 전에 대표 routing id를 재정의합니다.

```c
int zlink_spot_sub_set_routing_id(void *sub,
                                   const void *data,
                                   size_t size);
```

이 Subscriber의 커스텀 라우팅 아이덴티티를 설정합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_sub_routing_id`

---

### zlink_spot_sub_routing_id

이 SpotSub의 대표 routing id를 반환합니다.

```c
int zlink_spot_sub_routing_id(void *sub,
                               zlink_routing_id_t *out);
```

현재 Subscriber의 라우팅 아이덴티티를 가져옵니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_spot_sub_set_routing_id`

---

### zlink_spot_sub_peers

SpotSub 피어 큐 통계를 조회합니다.

```c
int zlink_spot_sub_peers(void *sub,
                          zlink_peer_info_t *peers,
                          size_t *count);
```

Subscriber의 기본 소켓에서 피어 단위 큐 통계를 반환합니다. 먼저
`peers = NULL`로 필요한 개수를 조회한 뒤, 버퍼를 할당하여 다시 호출합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_socket_peers`

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

**에러:**
- `EBUSY` -- 동일 subscriber에서 `zlink_spot_sub_recv`가 진행 중입니다.

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
- `EBUSY` -- 동일한 subscriber에 대해 다른 스레드가 이미 `zlink_spot_sub_recv`를 호출 중입니다.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_spot_sub_set_handler`, `zlink_spot_sub_subscribe`

---

## 폴링 통합

SPOT 서비스는 poller에 직접 등록할 수 있습니다. poller는 서비스 인스턴스
자체를 감시하며, 내부 소켓 핸들은 호출자에게 노출되지 않습니다. poller가
readiness를 시그널한 후에도 호출자는 기존 서비스 API(`zlink_spot_sub_recv`,
`zlink_spot_pub_publish` 등)를 사용하여 메시지를 송수신합니다.

**SpotNode는 poller 대상이 아닙니다.** SpotNode는 runtime/config owner
역할만 합니다. poller에는 `spot_sub` 또는 `spot_pub` 인스턴스를 등록하세요.

### Poller 등록 API

```c
int zlink_poller_add_spot_sub(void *poller, void *sub,
                              void *userdata, short events);
int zlink_poller_add_spot_pub(void *poller, void *pub,
                              void *userdata, short events);

int zlink_poller_modify_spot_sub(void *poller, void *sub, short events);
int zlink_poller_modify_spot_pub(void *poller, void *pub, short events);

int zlink_poller_remove_spot_sub(void *poller, void *sub);
int zlink_poller_remove_spot_pub(void *poller, void *pub);
```

### 사용 패턴

```text
1. 서비스 인스턴스 생성 (spot_sub, spot_pub)
2. poller에 서비스 인스턴스 등록
3. poller로 readiness 대기
4. 기존 서비스 API로 send/recv
```

### 예제 -- SPOT subscriber + poller

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://*:9500");

void *sub = zlink_spot_sub_new(node);
zlink_spot_node_connect_peer_pub(node, "tcp://peer:9500");
zlink_spot_sub_subscribe(sub, "bench");

void *poller = zlink_poller_new();
zlink_poller_add_spot_sub(poller, sub, NULL, ZLINK_POLLIN);

zlink_poller_event_t ev;
while (zlink_poller_wait(poller, &ev, 1000) == 1) {
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof(topic);
    // readiness 시그널 후 기존 서비스 API로 수신
    zlink_spot_sub_recv(sub, &parts, &part_count, ZLINK_DONTWAIT,
                        topic, &topic_len);
}
```

### 예제 -- SPOT publisher + poller

```c
void *pub = zlink_spot_pub_new(node);

void *poller = zlink_poller_new();
zlink_poller_add_spot_pub(poller, pub, NULL, ZLINK_POLLOUT);

zlink_poller_event_t ev;
if (zlink_poller_wait(poller, &ev, 1000) == 1) {
    // readiness 시그널 후 기존 서비스 API로 발행
    zlink_spot_pub_publish_bytes(pub, "bench", data, size, 0);
}
```

### 내부 동작

- **spot_sub**: poller는 data plane worker가 관리하는 내부 SUB facade
  소켓의 readiness를 감시합니다. data plane에서 메시지가 도착하면
  readable이 됩니다. readiness 이후 `zlink_spot_sub_recv(...)`를
  평소처럼 호출합니다.
- **spot_pub**: poller는 inproc PUB facade 소켓의 writability를
  감시합니다. readiness 이후 `zlink_spot_pub_publish(...)`를 평소처럼
  호출합니다.

### 스레드 안전성

`spot_pub`은 thread-safe입니다 — 여러 스레드에서 동시에 `publish()`를
호출할 수 있으며, 내부에서 직렬화됩니다. `spot_sub`은 thread-safe가
**아닙니다** — `recv()`, handler, `subscribe()`, `unsubscribe()` 호출은
한 번에 하나의 실행 흐름에서만 수행해야 합니다.

### 요약

| API | 용도 |
|-----|------|
| `zlink_spot_pub_peers` / `zlink_spot_sub_peers` | peer queue 통계 |
| `zlink_spot_pub_publish` | 메시지 발행 |
| `zlink_spot_sub_set_handler` | 콜백 기반 수신 |
| `zlink_spot_sub_recv` | subscriber 큐에서 폴링 기반 수신 |
| `zlink_poller_add_spot_sub` / `zlink_poller_add_spot_pub` | poller에 서비스 등록 |
