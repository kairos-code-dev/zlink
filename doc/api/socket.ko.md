[English](socket.md) | [한국어](socket.ko.md)

# 소켓 API 레퍼런스

소켓 API는 zlink 소켓의 생성, 구성, 바인딩, 연결, I/O 수행을 위한 함수를
제공합니다. 메시지 수신은 두 가지 모드를 지원합니다: 부착된 핸들러를 통한
콜백 dispatch와 `zlink_recv()`를 통한 동기식 pull. 소켓은 recv 모드로
시작하며, 핸들러를 부착하면 콜백 모드로 전환됩니다. 소켓은 게시/구독,
요청/응답, raw stream을 포함한 여러 메시징 패턴을 지원합니다.

## 스레드 안전성 요약

공개 socket handle API는 기본적으로 thread-safe합니다. 다만 모든 API가 같은
비용 모델을 갖는 것은 아닙니다.

- `send`는 여러 스레드에서 동시 호출을 허용하는 hot path입니다.
- `bind/connect/disconnect`, subscribe/unsubscribe, option/query, monitor는
  runtime에 호출 가능한 control path입니다. correctness는 보장되지만 실행
  순서는 내부 직렬화에 따라 결정될 수 있습니다.
- `close`는 fail-fast lifecycle gate를 사용합니다. 다른 스레드가 같은 handle에서
  admitted API나 callback을 실행 중이면 `EBUSY`, close가 accepted된 뒤 새 API
  진입은 `ESHUTDOWN`입니다.
- 예외는 소수만 남깁니다. init-only 설정, callback context에서 금지된 일부
  reentrant API, 같은 `zlink_msg_t` 인스턴스의 동시 공유는 기본 허용 범위
  밖입니다.

## 콜백 타입

### zlink_socket_msg_handler_fn

```c
typedef void (*zlink_socket_msg_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);
```

PAIR, DEALER, ROUTER 소켓에서 멀티파트 메시지 dispatch 콜백.
소유 I/O 스레드에서 호출됩니다. 모든 메시지 파트의 소유권이 콜백으로
이전되며, 각 파트는 정확히 한 번 close하거나 소비해야 합니다.
`zlink_recv_handler()`와 함께 사용합니다.

### zlink_subscribe_handler_fn

```c
typedef void (*zlink_subscribe_handler_fn) (const zlink_routing_id_t *source_rid_,
                                       const char *topic_,
                                       size_t topic_len_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_,
                                       void *userdata_);
```

SUB, XSUB 소켓에서 토픽 기반 메시지 dispatch 콜백.
소유 I/O 스레드에서 호출되며, 파트의 소유권이 이전됩니다.
`zlink_subscribe_handler()`와 함께 사용합니다.

### zlink_subscription_event_handler_fn

```c
typedef void (*zlink_subscription_event_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  int subscribed_,
  const char *topic_,
  size_t topic_len_,
  void *userdata_);
```

XPUB 소켓에서 구독 알림 콜백. `source_rid_`는 구독하는 피어를 식별합니다.
`subscribed_`는 구독 시 0이 아닌 값, 구독 해제 시 0입니다.
`zlink_subscription_event_handler()`와 함께 사용합니다.

각 콜백 타입은 전용 함수를 통해 등록합니다. 소켓 타입별 등록 함수 매핑:

| 소켓 타입 | 등록 함수 | 콜백 |
|---|---|---|
| PAIR, DEALER, ROUTER | `zlink_recv_handler` | `zlink_socket_msg_handler_fn` |
| SUB, XSUB | `zlink_subscribe_handler` | `zlink_subscribe_handler_fn` |
| XPUB | `zlink_subscription_event_handler` | `zlink_subscription_event_handler_fn` |
| PUB | N/A | 송신 전용; 핸들러 불필요 |

### zlink_send_ready_handler_fn

```c
typedef void (*zlink_send_ready_handler_fn) (void *subject_, void *userdata_);
```

송신 가능한 핸들이 쓰기 가능 상태로 전환될 때 호출되는 콜백.

## 상수

### 소켓 타입

```c
typedef enum zlink_socket_type_t
{
    ZLINK_SOCKET_PAIR   = 0x1001,
    ZLINK_SOCKET_PUB    = 0x1002,
    ZLINK_SOCKET_SUB    = 0x1003,
    ZLINK_SOCKET_DEALER = 0x1004,
    ZLINK_SOCKET_ROUTER = 0x1005,
    ZLINK_SOCKET_XPUB   = 0x1006,
    ZLINK_SOCKET_XSUB   = 0x1007,
    ZLINK_SOCKET_STREAM = 0x1008
} zlink_socket_type_t;
```

단축 별칭도 사용 가능: `ZLINK_PAIR`, `ZLINK_PUB`, `ZLINK_SUB`,
`ZLINK_DEALER`, `ZLINK_ROUTER`, `ZLINK_XPUB`, `ZLINK_XSUB`, `ZLINK_STREAM`.

### 송신 플래그

```c
typedef uint32_t zlink_send_flags_t;

#define ZLINK_DONTWAIT  ((zlink_send_flags_t) 0x0001u)
```

| 상수 | 설명 |
|---|---|
| `ZLINK_DONTWAIT` | 논블로킹 작업; 작업이 블로킹될 경우 즉시 `EAGAIN`과 함께 반환 |

### 보안 메커니즘

| 상수 | 값 | 설명 |
|---|---|---|
| `ZLINK_NULL` | 0 | 보안 메커니즘 없음 (기본값) |
| `ZLINK_PLAIN` | 1 | PLAIN 사용자명/비밀번호 인증 |

### 소켓 옵션

소켓 옵션은 타입별 전용 enum과 함수를 사용합니다. 공통 옵션은
`zlink_set_option()` / `zlink_get_option()`으로, 소켓 타입별 옵션은
`zlink_set_router_option()`, `zlink_set_dealer_option()`,
`zlink_set_pub_option()`, `zlink_set_sub_option()`,
`zlink_set_stream_option()` 등 전용 함수로 설정합니다.
ROUTING_ID는 `zlink_set_routing_id()` / `zlink_get_routing_id()` 전용
함수를, TLS는 `zlink_set_tls_server()` / `zlink_set_tls_client()` 전용
함수를, SUBSCRIBE/UNSUBSCRIBE는 `zlink_set_subscription()` /
`zlink_unset_subscription()` 전용 함수를 사용합니다.

#### 공통 옵션 (`zlink_option_t`)

`zlink_set_option()` / `zlink_get_option()`과 함께 사용합니다.
모든 소켓 타입, gateway, discovery(fan-out)에 적용됩니다.

##### Transport/Buffer

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_AFFINITY` | I/O 스레드 어피니티 비트마스크 (`uint64_t`) |
| `ZLINK_OPT_RATE` | 멀티캐스트 데이터 전송률 (kbps, `int`) |
| `ZLINK_OPT_RECOVERY_IVL` | 멀티캐스트 복구 간격 (밀리초, `int`) |
| `ZLINK_OPT_SNDBUF` | 커널 송신 버퍼 크기 (바이트, `int`; 0 = OS 기본값) |
| `ZLINK_OPT_RCVBUF` | 커널 수신 버퍼 크기 (바이트, `int`; 0 = OS 기본값) |
| `ZLINK_OPT_SNDHWM` | 송신 하이 워터 마크 (`int`; 0 = 무제한) |
| `ZLINK_OPT_RCVHWM` | 수신 하이 워터 마크 (`int`; 0 = 무제한) |
| `ZLINK_OPT_MAXMSGSIZE` | 최대 인바운드 메시지 크기 (바이트, `int64_t`; -1 = 무제한) |

##### Timing

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_LINGER` | 소켓 종료 시 대기 기간 (밀리초, `int`; -1 = 무한, 0 = 즉시 폐기) |
| `ZLINK_OPT_RCVTIMEO` | 수신 타임아웃 (밀리초, `int`; -1 = 무한) |
| `ZLINK_OPT_SNDTIMEO` | 송신 타임아웃 (밀리초, `int`; -1 = 무한) |
| `ZLINK_OPT_CONNECT_TIMEOUT` | 연결 타임아웃 (밀리초, `int`) |
| `ZLINK_OPT_RECONNECT_IVL` | 초기 재연결 간격 (밀리초, `int`) |
| `ZLINK_OPT_RECONNECT_IVL_MAX` | 최대 재연결 간격 (밀리초, `int`; 0 = RECONNECT_IVL만 사용) |
| `ZLINK_OPT_HANDSHAKE_IVL` | ZMTP 핸드셰이크 타임아웃 (밀리초, `int`) |

##### TCP

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_TCP_KEEPALIVE` | SO_KEEPALIVE 재정의 (`int`; -1 = OS 기본값, 0 = 끄기, 1 = 켜기) |
| `ZLINK_OPT_TCP_KEEPALIVE_CNT` | TCP_KEEPCNT 재정의 (`int`; -1 = OS 기본값) |
| `ZLINK_OPT_TCP_KEEPALIVE_IDLE` | TCP_KEEPIDLE 재정의 (초, `int`; -1 = OS 기본값) |
| `ZLINK_OPT_TCP_KEEPALIVE_INTVL` | TCP_KEEPINTVL 재정의 (초, `int`; -1 = OS 기본값) |
| `ZLINK_OPT_TCP_MAXRT` | 최대 TCP 재전송 타임아웃 (밀리초, `int`) |
| `ZLINK_OPT_TCP_NODELAY` | TCP_NODELAY 활성화 (`int`; 0 또는 1) |

##### Heartbeat

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_HEARTBEAT_IVL` | ZMTP 하트비트 간격 (밀리초, `int`; 0 = 비활성화) |
| `ZLINK_OPT_HEARTBEAT_TTL` | ZMTP 하트비트 TTL (밀리초, `int`) |
| `ZLINK_OPT_HEARTBEAT_TIMEOUT` | ZMTP 하트비트 타임아웃 (밀리초, `int`) |

##### Network

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_IPV6` | 소켓에서 IPv6 활성화 (`int`; 0 또는 1) |
| `ZLINK_OPT_TOS` | IP Type-of-Service 값 (`int`) |
| `ZLINK_OPT_MULTICAST_HOPS` | 최대 멀티캐스트 홉 수 (TTL) (`int`) |
| `ZLINK_OPT_MULTICAST_MAXTPDU` | 최대 멀티캐스트 전송 데이터 유닛 크기 (바이트, `int`) |
| `ZLINK_OPT_BINDTODEVICE` | 소켓을 특정 네트워크 인터페이스에 바인딩 (`string`) |
| `ZLINK_OPT_BACKLOG` | 대기 중인 연결 큐의 최대 길이 (`int`) |

##### Behavior

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_IMMEDIATE` | 완료된 연결에만 메시지 대기열 사용 (`int`; 0 또는 1) |
| `ZLINK_OPT_CONFLATE` | 토픽당 가장 최근 메시지만 유지 (`int`; 0 또는 1) |
| `ZLINK_OPT_BLOCKY` | 레거시 옵션: context 종료 시 블로킹 (`int`; 0 또는 1) |
| `ZLINK_OPT_INVERT_MATCHING` | 토픽 매칭 반전 (`int`; 0 또는 1) |
| `ZLINK_OPT_ZMP_METADATA` | 발신 연결에 ZMP 메타데이터 속성 첨부 (`binary`) |

##### Read-only

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_FD` | 파일 디스크립터 (읽기 전용, `zlink_fd_t`) |
| `ZLINK_OPT_EVENTS` | 이벤트 상태 비트마스크 (읽기 전용, `int`) |
| `ZLINK_OPT_TYPE` | 소켓 타입 (읽기 전용, `int`) |
| `ZLINK_OPT_LAST_ENDPOINT` | 마지막으로 바인딩된 엔드포인트 (읽기 전용, `string`) |

#### Router 옵션 (`zlink_router_option_t`)

`zlink_set_router_option()` / `zlink_get_router_option()`과 함께 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_ROUTER_OPT_MANDATORY` | 연결되지 않은 피어로 라우팅 시 `EHOSTUNREACH` 반환 (`int`; 0 또는 1) |
| `ZLINK_ROUTER_OPT_HANDOVER` | 새 연결이 기존 라우팅 아이덴티티를 인수하도록 허용 (`int`; 0 또는 1) |
| `ZLINK_ROUTER_OPT_PROBE` | 연결 시 빈 메시지를 보내 ROUTER 피어에서 아이덴티티 설정 (`int`; 0 또는 1) |
| `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` | 다음 발신 연결의 라우팅 아이덴티티 설정 (`binary`) |

#### Dealer 옵션 (`zlink_dealer_option_t`)

`zlink_set_dealer_option()`과 함께 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_DEALER_OPT_PROBE` | 연결 시 빈 메시지를 보내 ROUTER 피어에서 아이덴티티 설정 (`int`; 0 또는 1) |

#### Pub 옵션 (`zlink_pub_option_t`)

`zlink_set_pub_option()` / `zlink_get_pub_option()`과 함께 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_PUB_OPT_VERBOSE` | 모든 구독 메시지를 업스트림으로 전달 (`int`; 0 또는 1) |
| `ZLINK_PUB_OPT_VERBOSER` | 모든 구독 및 구독 해제 메시지를 업스트림으로 전달 (`int`; 0 또는 1) |
| `ZLINK_PUB_OPT_MANUAL` | XPUB에서 수동 구독 관리 활성화 (`int`; 0 또는 1) |
| `ZLINK_PUB_OPT_MANUAL_LAST_VALUE` | 수동 XPUB 모드에서 최신 값 캐싱 활성화 (`int`; 0 또는 1) |
| `ZLINK_PUB_OPT_NODROP` | HWM에서 메시지를 자동 삭제하지 않고 `EAGAIN` 반환 (`int`; 0 또는 1) |
| `ZLINK_PUB_OPT_WELCOME_MSG` | 새 서브스크라이버 연결 시 전송되는 메시지 (`binary`) |
| `ZLINK_PUB_OPT_TOPICS_COUNT` | 구독된 토픽 수 (읽기 전용, `int`) |
| `ZLINK_PUB_OPT_APPROVE_SUBSCRIBE` | XPUB manual 모드에서 구독 승인 (`binary`) |
| `ZLINK_PUB_OPT_REJECT_SUBSCRIBE` | XPUB manual 모드에서 구독 거부 (`binary`) |

#### Sub 옵션 (`zlink_sub_option_t`)

`zlink_set_sub_option()` / `zlink_get_sub_option()`과 함께 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_SUB_OPT_TOPICS_COUNT` | 구독된 토픽 수 (읽기 전용, `int`) |

#### Stream 옵션 (`zlink_stream_option_t`)

`zlink_set_stream_option()` / `zlink_get_stream_option()`과 함께 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_STREAM_OPT_NOTIFY` | STREAM 연결/해제 알림 활성화 (`int`; 0 또는 1) |

## 함수

### zlink_socket

소켓을 생성합니다.

```c
void *zlink_socket (void *context_, zlink_socket_type_t type_);
```

지정된 context 내에서 새 소켓을 생성합니다. `type_` 매개변수는 메시징 패턴을
선택합니다. 소켓은 recv 모드로 시작합니다. `zlink_recv_handler()`로
콜백 모드로 전환할 수 있습니다. 소켓은 context가 종료되기 전에
`zlink_close()`로 닫아야 합니다.

**반환값:** 성공 시 소켓 핸들, 실패 시 `NULL` (errno가 설정됨).

**에러:** 소켓 타입이 유효하지 않으면 `EINVAL`. 최대 소켓 수에 도달하면
`EMFILE`. Context가 종료된 경우 `ETERM`.

**스레드 안전성:** Context에 대해 스레드 안전합니다.

**참고:** `zlink_close`, `zlink_ctx_new`, `zlink_recv_handler`

---

### zlink_recv_handler

소켓에 메시지 수신 핸들러를 부착합니다.

```c
int zlink_recv_handler (void *s_,
                        zlink_socket_msg_handler_fn handler_,
                        void *userdata_);
```

PAIR, DEALER, ROUTER 소켓에 메시지 수신 핸들러를 부착합니다. 소켓은
recv 모드로 시작합니다. 이 호출은 핸들을 콜백 모드로 전환하며, 소켓의 수명 동안
되돌릴 수 없습니다. 동일 핸들에 대한 두 번째 attach는 `errno=EBUSY`로
실패합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 핸들러가 NULL이거나 소켓 타입이 메시지 핸들러를 허용하지 않으면
`EINVAL`. 핸들러가 이미 부착된 경우 `EBUSY`.

**참고:** `zlink_subscribe_handler`, `zlink_subscription_event_handler`,
`zlink_socket`, `zlink_close`

---

### zlink_subscribe_handler

소켓에 토픽 기반 수신 핸들러를 부착합니다.

```c
int zlink_subscribe_handler (void *s_,
                             zlink_subscribe_handler_fn handler_,
                             void *userdata_);
```

SUB 또는 XSUB 소켓에 토픽 기반 수신 핸들러를 부착합니다. 소켓은 recv 모드로
시작합니다. 이 호출은 핸들을 콜백 모드로 전환하며, 소켓의 수명 동안 되돌릴 수
없습니다. 동일 핸들에 대한 두 번째 attach는 `errno=EBUSY`로 실패합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 핸들러가 NULL이거나 소켓 타입이 spot 핸들러를 허용하지 않으면
`EINVAL`. 핸들러가 이미 부착된 경우 `EBUSY`.

**참고:** `zlink_recv_handler`, `zlink_subscription_event_handler`,
`zlink_socket`, `zlink_close`

---

### zlink_subscription_event_handler

XPUB 소켓에 구독 알림 핸들러를 부착합니다.

```c
int zlink_subscription_event_handler (void *s_,
                             zlink_subscription_event_handler_fn handler_,
                             void *userdata_);
```

XPUB 소켓에 구독 알림 핸들러를 부착합니다. 콜백은 구독하는 피어를 식별하는
`source_rid_`, `subscribed_` 플래그, 토픽 바이트를 수신합니다. 소켓은 recv
모드로 시작합니다. 이 호출은 핸들을 콜백 모드로 전환하며, 소켓의 수명 동안
되돌릴 수 없습니다. 동일 핸들에 대한 두 번째 attach는 `errno=EBUSY`로
실패합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 핸들러가 NULL이거나 소켓이 XPUB가 아니면 `EINVAL`. 핸들러가 이미
부착된 경우 `EBUSY`.

**참고:** `zlink_recv_handler`, `zlink_subscribe_handler`,
`zlink_socket`, `zlink_close`

---

### zlink_close

소켓을 닫고 리소스를 해제합니다.

```c
int zlink_close (void *s_);
```

소켓을 닫고 관련된 모든 리소스를 해제합니다. 송신 대기열에 남아 있는 메시지는
`ZLINK_OPT_LINGER` 설정에 따라 폐기되거나 송신됩니다. 다른 스레드에서 동일 핸들에
대해 콜백이나 API 호출이 진행 중이면 `errno=EBUSY`로 실패합니다. send-ready
또는 monitor 콜백 내에서의 self-close는 콜백 에필로그까지 지연됩니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 핸들이 유효한 소켓이 아니면 `ENOTSOCK`. 콜백이나 작업이 진행 중이면
`EBUSY`.

**참고:** `zlink_socket`

---

### zlink_set_option

공통 옵션을 설정합니다.

```c
int zlink_set_option (void *handle_,
                      zlink_option_t option_,
                      const void *optval_,
                      size_t optvallen_);
```

공통 옵션을 설정합니다. 모든 소켓 타입, gateway, discovery(fan-out)에서
사용합니다. `option_` 매개변수는 `zlink_option_t` enum 값입니다. `optval_`
포인터는 값을 제공하고 `optvallen_`은 크기를 바이트 단위로 지정합니다.

일부 옵션은 소켓을 바인딩하거나 연결하기 전에 설정해야 합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 옵션이 알 수 없거나 값이 범위를 벗어나면 `EINVAL`. Context가 종료된
경우 `ETERM`.

**참고:** `zlink_get_option`

---

### zlink_get_option

공통 옵션을 조회합니다.

```c
int zlink_get_option (void *handle_,
                      zlink_option_t option_,
                      void *optval_,
                      size_t *optvallen_);
```

공통 옵션의 현재 값을 가져옵니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_set_option`

---

### zlink_set_routing_id

소켓의 라우팅 아이덴티티를 설정합니다.

```c
int zlink_set_routing_id (void *handle_,
                           const void *data_,
                           size_t size_);
```

ROUTER 주소 지정을 위한 소켓 아이덴티티를 설정합니다. 최대 255바이트.
바인딩 또는 연결하기 전에 설정해야 합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_get_routing_id`

---

### zlink_get_routing_id

소켓의 라우팅 아이덴티티를 조회합니다.

```c
int zlink_get_routing_id (void *handle_,
                           zlink_routing_id_t *out_);
```

소켓에 설정된 라우팅 아이덴티티를 가져옵니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_set_routing_id`

---

### zlink_set_tls_server

서버 측 TLS를 구성합니다.

```c
int zlink_set_tls_server (void *handle_,
                           const char *cert_,
                           const char *key_,
                           int require_client_cert_);
```

서버 소켓에 TLS 인증서, 개인 키를 설정하고, 클라이언트 인증서 요구 여부를
지정합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_set_tls_client`, `zlink_bind`

---

### zlink_set_tls_client

클라이언트 측 TLS를 구성합니다.

```c
int zlink_set_tls_client (void *handle_,
                           const char *ca_cert_,
                           const char *hostname_,
                           int trust_system_);
```

클라이언트 소켓에 CA 인증서, 호스트명(SNI 및 인증서 검증용), 시스템 CA
저장소 신뢰 여부를 설정합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_set_tls_server`, `zlink_connect`

---

### zlink_set_router_option

ROUTER 소켓 전용 옵션을 설정합니다.

```c
int zlink_set_router_option (void *handle_,
                              zlink_router_option_t option_,
                              const void *optval_,
                              size_t optvallen_);
```

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_get_router_option`

---

### zlink_get_router_option

ROUTER 소켓 전용 옵션을 조회합니다.

```c
int zlink_get_router_option (void *handle_,
                              zlink_router_option_t option_,
                              void *optval_,
                              size_t *optvallen_);
```

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_set_router_option`

---

### zlink_set_dealer_option

DEALER 소켓 전용 옵션을 설정합니다.

```c
int zlink_set_dealer_option (void *handle_,
                              zlink_dealer_option_t option_,
                              const void *optval_,
                              size_t optvallen_);
```

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

---

### zlink_set_stream_option

STREAM 소켓 전용 옵션을 설정합니다.

```c
int zlink_set_stream_option (void *handle_,
                              zlink_stream_option_t option_,
                              const void *optval_,
                              size_t optvallen_);
```

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_get_stream_option`

---

### zlink_get_stream_option

STREAM 소켓 전용 옵션을 조회합니다.

```c
int zlink_get_stream_option (void *handle_,
                              zlink_stream_option_t option_,
                              void *optval_,
                              size_t *optvallen_);
```

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_set_stream_option`

---

### zlink_set_pub_option

PUB/XPUB 소켓, spot-pub, spotnode-pub 전용 옵션을 설정합니다.

```c
int zlink_set_pub_option (void *handle_,
                           zlink_pub_option_t option_,
                           const void *optval_,
                           size_t optvallen_);
```

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_get_pub_option`

---

### zlink_get_pub_option

PUB/XPUB 소켓, spot-pub, spotnode-pub 전용 옵션을 조회합니다.

```c
int zlink_get_pub_option (void *handle_,
                           zlink_pub_option_t option_,
                           void *optval_,
                           size_t *optvallen_);
```

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_set_pub_option`

---

### zlink_set_sub_option

SUB/XSUB 소켓, spot-sub, spotnode-sub 전용 옵션을 설정합니다.

```c
int zlink_set_sub_option (void *handle_,
                           zlink_sub_option_t option_,
                           const void *optval_,
                           size_t optvallen_);
```

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_get_sub_option`

---

### zlink_get_sub_option

SUB/XSUB 소켓, spot-sub, spotnode-sub 전용 옵션을 조회합니다.

```c
int zlink_get_sub_option (void *handle_,
                           zlink_sub_option_t option_,
                           void *optval_,
                           size_t *optvallen_);
```

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_set_sub_option`

---

### zlink_bind

소켓을 주소에 바인딩합니다.

```c
int zlink_bind (void *s_, const char *addr_);
```

소켓을 로컬 엔드포인트에 바인딩합니다. 엔드포인트 문자열은
`transport://address` 형식을 사용하며, 지원되는 트랜스포트는 다음과 같습니다:

- `tcp://interface:port` 또는 `tcp://*:port`
- `inproc://name` (프로세스 내)
- `ipc://pathname` (프로세스 간, POSIX 전용)
- `ws://interface:port` (WebSocket)
- `tls://interface:port` (TLS 암호화 TCP)

소켓은 여러 엔드포인트에 바인딩할 수 있습니다. TCP의 경우 포트 0을 지정하면
시스템이 임시 포트를 할당합니다. 실제 엔드포인트를 가져오려면
`ZLINK_OPT_LAST_ENDPOINT`를 사용하세요.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 주소가 이미 사용 중이면 `EADDRINUSE`. 인터페이스가 존재하지 않으면
`EADDRNOTAVAIL`. 트랜스포트가 지원되지 않으면 `EPROTONOSUPPORT`.

**참고:** `zlink_connect`, `zlink_unbind`

---

### zlink_connect

소켓을 원격 주소에 연결합니다.

```c
int zlink_connect (void *s_, const char *addr_);
```

소켓을 원격 엔드포인트에 연결합니다. 엔드포인트 형식은 `zlink_bind()`와
동일합니다. 소켓은 여러 엔드포인트에 연결할 수 있으며, 피어가 사용 불가능해지면
라이브러리가 자동으로 재연결을 처리합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_bind`, `zlink_disconnect`

---

### zlink_unbind

소켓의 주소 바인딩을 해제합니다.

```c
int zlink_unbind (void *s_, const char *addr_);
```

이전에 설정된 바인딩을 제거합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_bind`

---

### zlink_disconnect

소켓의 원격 주소 연결을 해제합니다.

```c
int zlink_disconnect (void *s_, const char *addr_);
```

이전에 설정된 연결을 제거합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_connect`

---

### zlink_send

소켓에서 멀티파트 메시지를 송신합니다.

```c
int zlink_send (void *s_,
                zlink_msg_t *parts_,
                size_t part_count_,
                zlink_send_flags_t flags_);
```

소켓 `s_`에서 `parts_` 배열의 `part_count_`개 파트로 구성된 멀티파트 메시지를
송신합니다. 성공 시 배열 내 모든 파트의 소유권이 라이브러리로 이전되며, 호출자는
이후 접근할 수 없습니다. 실패 시 소유권은 호출자에게 유지됩니다. `flags_`
매개변수는 0 또는 `ZLINK_DONTWAIT`일 수 있습니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 작업이 블로킹되고 `ZLINK_DONTWAIT`가 설정된 경우 `EAGAIN`. Context가
종료된 경우 `ETERM`.

**참고:** `zlink_recv`

---

### zlink_recv

소켓에서 멀티파트 메시지를 수신합니다.

```c
int zlink_recv (void *s_,
                zlink_routing_id_t *source_rid_out_,
                zlink_msg_t **parts_out_,
                size_t *part_count_out_,
                zlink_send_flags_t flags_);
```

소켓 `s_`에서 완전한 멀티파트 메시지를 수신합니다. 성공 시 `*parts_out_`는
라이브러리가 할당한 `*part_count_out_`개 메시지 파트 배열을 가리키며,
`*source_rid_out_`는 송신자의 routing id로 설정됩니다 (해당하는 경우). 파트
배열과 각 파트의 소유권이 호출자에게 이전되며, 호출자는 모든 파트를 close하거나
`zlink_multipart_close()`를 호출하고 배열을 해제해야 합니다. 소켓이 recv
모드여야 합니다 (핸들러 미부착). `zlink_recv_handler()`로 수신 핸들러가
부착된 경우 `errno=EBUSY`로 실패합니다. 메시지가 없을 때 즉시 반환하려면
`ZLINK_DONTWAIT`를 전달하세요.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 작업이 블로킹되고 `ZLINK_DONTWAIT`가 설정된 경우, 또는
`ZLINK_OPT_RCVTIMEO`가 만료된 경우 `EAGAIN`. 수신 핸들러가 부착된 경우 `EBUSY`.
Context가 종료된 경우 `ETERM`.

**참고:** `zlink_send`, `zlink_recv_handler`, `zlink_multipart_close`

---

### zlink_send_rid

routing id로 특정 피어에게 멀티파트 메시지를 송신합니다.

```c
int zlink_send_rid (void *s_,
                    const zlink_routing_id_t *target_rid_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    zlink_send_flags_t flags_);
```

`target_rid_`로 식별되는 피어에게 멀티파트 메시지를 송신합니다. 성공 시
모든 파트의 소유권이 라이브러리로 이전됩니다. 실패 시 소유권은 호출자에게
유지됩니다.

적용 대상: ROUTER (directed reply), STREAM (피어 지정 send), Gateway
(directed request/reply).

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `subject_`가 NULL이면 `EFAULT`. 작업이 블로킹되고
`ZLINK_DONTWAIT`가 설정된 경우 `EAGAIN`. 대상 피어가 연결되지 않은 경우
(`ROUTER_MANDATORY` 활성 시) `EHOSTUNREACH`. Context가 종료된 경우 `ETERM`.

**참고:** `zlink_send`, `zlink_recv`

---

## Pub/Sub 데이터 플레인 API

다음 함수들은 raw PUB, SUB, XSUB, XPUB 소켓을 위한 canonical pub/sub
데이터 플레인을 제공합니다. 동일한 함수가 `spot` 및 `spot_node` 서비스
핸들에도 적용됩니다 ([spot.ko.md](spot.ko.md) 참고).

### zlink_publish

멀티파트 메시지를 발행합니다.

```c
int zlink_publish (void *subject_,
                   const char *topic_id_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   zlink_send_flags_t flags_);
```

지정된 subject에서 멀티파트 메시지를 발행합니다. 성공 시 모든 파트의
소유권이 라이브러리로 이전됩니다.

- `spot` / `spot_node`: `topic_id_`는 non-NULL이어야 합니다 (topic-bearing
  publish). NULL이면 `EINVAL`.
- raw `PUB` / `XPUB`: `topic_id_`는 NULL이어야 합니다 (raw pub publish).
  토픽 매칭은 wire first-frame prefix 규칙을 따릅니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `subject_`가 NULL이면 `EFAULT`. `topic_id_`가 spot/spot_node에서
NULL이거나 지원하지 않는 타입이면 `EINVAL`. subject 타입이 publish를
지원하지 않으면 `ENOTSUP`.

**참고:** `zlink_set_subscription`, `zlink_subscribe`

---

### zlink_set_subscription

토픽 필터를 구독합니다.

```c
int zlink_set_subscription (void *handle_, const char *filter_);
```

`filter_`에 매칭되는 메시지를 구독합니다. 필터 해석: `filter_`가 `*`로
끝나면 prefix-match 패턴이고, 그 외는 exact topic입니다.

적용 대상: raw SUB, raw XSUB, `spot`, `spot_node`.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `subject_`가 NULL이면 `EFAULT`. `filter_`가 NULL이거나 비어있거나
유효하지 않은 패턴 구문(복수 `*`, 중간 `*`)이면 `EINVAL`. subject 타입이
구독을 지원하지 않으면 `ENOTSUP`.

**참고:** `zlink_unset_subscription`, `zlink_subscribe`

---

### zlink_unset_subscription

토픽 필터 구독을 해제합니다.

```c
int zlink_unset_subscription (void *handle_, const char *filter_);
```

이전에 등록된 구독을 제거합니다. `zlink_set_subscription()`과 동일한 문자열
해석 규칙이 적용됩니다: trailing `*`는 패턴 해제, 그 외는 exact topic 해제.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `subject_`가 NULL이면 `EFAULT`. `filter_`가 NULL이거나 비어있으면
`EINVAL`. subject 타입이 구독 해제를 지원하지 않으면 `ENOTSUP`.

**참고:** `zlink_set_subscription`

---

### zlink_subscription_at

인덱스로 구독 필터를 조회합니다.

```c
int zlink_subscription_at (void *handle_,
                            size_t index_,
                            char *filter_out_,
                            size_t *filter_len_inout_,
                            int *is_pattern_out_);
```

`index_` 위치의 구독 필터를 가져옵니다. `filter_out_`에 필터 문자열이,
`*filter_len_inout_`에 길이가, `*is_pattern_out_`에 패턴 여부(trailing `*`
구독이면 1, exact topic이면 0)가 설정됩니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_set_subscription`, `zlink_unset_subscription`

---

### zlink_subscribe

토픽 기반 멀티파트 메시지를 수신합니다.

```c
int zlink_subscribe (void *subject_,
                     zlink_routing_id_t *source_rid_out_,
                     zlink_msg_t **parts_out_,
                     size_t *part_count_out_,
                     char *topic_id_out_,
                     size_t *topic_id_len_out_,
                     zlink_send_flags_t flags_);
```

recv 모드에서 다음 토픽 기반 메시지를 수신합니다. 성공 시
`*source_rid_out_`는 송신자의 routing id (transport가 identity를 전달하지
않으면 zeroed), `*topic_id_out_` / `*topic_id_len_out_`는 토픽 바이트
(binary-safe), `*parts_out_` / `*part_count_out_`는 페이로드 프레임을
받습니다. 파트 배열의 소유권은 호출자에게 이전됩니다.

subject가 recv 모드여야 합니다 (핸들러 미부착). subscribe handler가 부착된
경우 `EBUSY`로 실패합니다.

적용 대상: raw SUB, raw XSUB, `spot`, `spot_node`.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `subject_`가 NULL이면 `EFAULT`. `ZLINK_DONTWAIT`가 설정되고
메시지가 없으면 `EAGAIN`. subscribe handler가 부착된 경우 `EBUSY`. 토픽
버퍼가 작으면 `EMSGSIZE`. subject 타입이 subscribe recv를 지원하지 않으면
`ENOTSUP`.

**참고:** `zlink_subscribe_handler`, `zlink_set_subscription`

---

### zlink_subscription_event

XPUB 소켓에서 구독 이벤트를 수신합니다.

```c
int zlink_subscription_event (void *subject_,
                               zlink_routing_id_t *source_rid_out_,
                               int *subscribed_out_,
                               char *topic_id_out_,
                               size_t *topic_id_len_out_,
                               zlink_send_flags_t flags_);
```

recv 모드에서 다음 구독 이벤트를 수신합니다. 성공 시
`*source_rid_out_`는 구독하는 피어를 식별하고, `*subscribed_out_`는
subscribe이면 1, unsubscribe이면 0이며, `*topic_id_out_` /
`*topic_id_len_out_`는 토픽 바이트를 받습니다 (`zlink_subscribe()`와
동일한 binary-safe buffer 계약).

subject가 recv 모드여야 합니다. subscription event handler가 부착된 경우
`EBUSY`로 실패합니다.

적용 대상: raw XPUB만.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `subject_`가 NULL이면 `EFAULT`. `ZLINK_DONTWAIT`가 설정되고
이벤트가 없으면 `EAGAIN`. subscription event handler가 부착된 경우
`EBUSY`. subject가 XPUB가 아니면 `ENOTSUP`.

**참고:** `zlink_subscription_event_handler`, `zlink_publish`

---

### zlink_send_ready_handler

send-ready 콜백을 설정하거나 교체합니다.

```c
int zlink_send_ready_handler (void *s_,
                               zlink_send_ready_handler_fn handler_,
                               void *userdata_);
```

핸들러는 교체 전용입니다. NULL 전달은 유효하지 않습니다. 교체 성공 시 다음 쓰기
가능 전환부터 반영됩니다. 동일 핸들의 send-ready 콜백 내에서 재진입 호출하면
`errno=EDEADLK`로 실패합니다.

지원: PAIR, PUB, XPUB, DEALER, ROUTER, gateway, unified spot, unified
spot_node.
미지원: SUB, XSUB, STREAM.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_send`

---

### zlink_multipart_close

멀티파트 메시지 배열의 모든 파트를 close합니다.

```c
void zlink_multipart_close (zlink_msg_t *parts, size_t part_count);
```

각 원소에 대해 `zlink_msg_close()`를 호출하는 편의 함수입니다.

**참고:** `zlink_msg_close`

---

## 소켓 모니터

### zlink_socket_monitor_open

recv 모드로 소켓 모니터 핸들을 열고 반환합니다.

```c
void *zlink_socket_monitor_open (void *s_,
                                 const zlink_socket_monitor_open_options_t *options_);
```

소켓 `s_`에 대한 모니터를 생성하고 핸들을 반환합니다. `options_->events`
비트마스크로 관찰할 이벤트를 선택합니다. 모니터는 **recv 모드**로 시작합니다.
`zlink_socket_monitor_recv()`로 이벤트를 직접 수신하거나,
`zlink_socket_monitor_handler()`로 callback-only 모드로 전환할 수 있습니다.
반환된 핸들은 더 이상 필요하지 않을 때 `zlink_monitor_close()`로 닫아야 합니다.

**반환값:** 성공 시 모니터 핸들, 실패 시 `NULL` (errno가 설정됨).

**참고:** `zlink_socket_monitor_handler`, `zlink_socket_monitor_recv`,
`zlink_monitor_snapshot`, `zlink_monitor_close`
