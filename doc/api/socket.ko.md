[English](socket.md) | [한국어](socket.ko.md)

# 소켓 API 레퍼런스

소켓 API는 zlink 소켓의 생성, 구성, 바인딩, 연결, I/O 수행을 위한 함수를
제공합니다. 모든 메시지 수신은 소켓 생성 시점에 등록하는 핸들러 콜백을 통해
처리됩니다. `recv()` 함수는 없습니다. 소켓은 게시/구독, 요청/응답, raw stream을
포함한 여러 메시징 패턴을 지원합니다.

## 스레드 안전성 요약

공개 socket handle API는 기본적으로 thread-safe합니다. 다만 모든 API가 같은
비용 모델을 갖는 것은 아닙니다.

- `send`는 same-handle concurrent 호출을 허용하는 hot path입니다.
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
  size_t part_count_);
```

PAIR, DEALER, ROUTER 소켓에서 멀티파트 메시지 dispatch 콜백.
소유 I/O 스레드에서 호출됩니다. 모든 메시지 파트의 소유권이 콜백으로
이전되며, 각 파트는 정확히 한 번 close하거나 소비해야 합니다.

### zlink_spot_handler_fn

```c
typedef void (*zlink_spot_handler_fn) (const zlink_routing_id_t *source_rid_,
                                       const char *topic_,
                                       size_t topic_len_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_);
```

SUB, XSUB 소켓에서 토픽 기반 메시지 dispatch 콜백.
소유 I/O 스레드에서 호출되며, 파트의 소유권이 이전됩니다.

### zlink_xpub_handler_fn

```c
typedef void (*zlink_xpub_handler_fn) (int subscribed_,
                                       const uint8_t *topic_,
                                       size_t topic_len_);
```

XPUB 소켓에서 구독 알림 콜백.

### zlink_socket_handler_t

```c
typedef enum zlink_socket_handler_kind_t
{
    ZLINK_SOCKET_HANDLER_MSG  = 0x1201,
    ZLINK_SOCKET_HANDLER_SPOT = 0x1202,
    ZLINK_SOCKET_HANDLER_XPUB = 0x1203
} zlink_socket_handler_kind_t;

typedef struct zlink_socket_handler_t
{
    zlink_socket_handler_kind_t kind;
    union
    {
        zlink_socket_msg_handler_fn msg;
        zlink_spot_handler_fn spot;
        zlink_xpub_handler_fn xpub;
    } fn;
} zlink_socket_handler_t;
```

`zlink_socket()`에 전달하는 핸들러 디스크립터. `kind` 필드로 콜백 변형을
선택합니다. 소켓 타입별 핸들러 kind 매핑:

| 소켓 타입 | 핸들러 Kind | 콜백 |
|---|---|---|
| PAIR, DEALER, ROUTER | `ZLINK_SOCKET_HANDLER_MSG` | `zlink_socket_msg_handler_fn` |
| SUB, XSUB | `ZLINK_SOCKET_HANDLER_SPOT` | `zlink_spot_handler_fn` |
| XPUB | `ZLINK_SOCKET_HANDLER_XPUB` | `zlink_xpub_handler_fn` |
| PUB | N/A | 송신 전용; `NULL` 핸들러 전달 |
| STREAM | `ZLINK_SOCKET_HANDLER_MSG` | 아래 STREAM 콜백 API 참고 |

### zlink_send_ready_handler_fn

```c
typedef void (*zlink_send_ready_handler_fn) (void *subject_);
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
#define ZLINK_SNDMORE   ((zlink_send_flags_t) 0x0002u)
```

| 상수 | 설명 |
|---|---|
| `ZLINK_DONTWAIT` | 논블로킹 작업; 작업이 블로킹될 경우 즉시 `EAGAIN`과 함께 반환 |
| `ZLINK_SNDMORE` | 멀티파트 메시지에서 더 많은 파트가 뒤따를 것임을 나타냄 |

### 보안 메커니즘

| 상수 | 값 | 설명 |
|---|---|---|
| `ZLINK_NULL` | 0 | 보안 메커니즘 없음 (기본값) |
| `ZLINK_PLAIN` | 1 | PLAIN 사용자명/비밀번호 인증 |

### 소켓 옵션

소켓 옵션은 `zlink_setsockopt()`으로 설정하고 `zlink_getsockopt()`으로
조회합니다. 옵션은 `zlink_socket_option_t` enum을 사용합니다.
단축 별칭(예: `ZLINK_LINGER`)도 사용 가능합니다.

#### 일반

| 상수 | 설명 |
|---|---|
| `ZLINK_SOCKOPT_AFFINITY` | I/O 스레드 어피니티 비트마스크 (`uint64_t`) |
| `ZLINK_SOCKOPT_ROUTING_ID` | ROUTER 주소 지정을 위한 소켓 아이덴티티 (`binary`, 최대 255바이트) |
| `ZLINK_SOCKOPT_TYPE` | 소켓 타입 (읽기 전용, `int`) |
| `ZLINK_SOCKOPT_LINGER` | 소켓 종료 시 대기 기간 (밀리초, `int`; -1 = 무한, 0 = 즉시 폐기) |
| `ZLINK_SOCKOPT_BACKLOG` | 대기 중인 연결 큐의 최대 길이 (`int`) |
| `ZLINK_SOCKOPT_LAST_ENDPOINT` | 마지막으로 바인딩된 엔드포인트 (읽기 전용, `string`) |
| `ZLINK_SOCKOPT_FD` | 외부 이벤트 루프 통합을 위한 파일 디스크립터 (읽기 전용, `zlink_fd_t`) |
| `ZLINK_SOCKOPT_EVENTS` | 이벤트 상태 비트마스크 (읽기 전용, `int`) |

#### 하이 워터 마크

| 상수 | 설명 |
|---|---|
| `ZLINK_SOCKOPT_SNDHWM` | 송신 하이 워터 마크; 송신 대기열의 최대 메시지 수 (`int`; 0 = 무제한) |
| `ZLINK_SOCKOPT_RCVHWM` | 수신 하이 워터 마크; 수신 대기열의 최대 메시지 수 (`int`; 0 = 무제한) |
| `ZLINK_SOCKOPT_MAXMSGSIZE` | 최대 인바운드 메시지 크기 (바이트, `int64_t`; -1 = 무제한) |

#### 버퍼

| 상수 | 설명 |
|---|---|
| `ZLINK_SOCKOPT_SNDBUF` | 커널 송신 버퍼 크기 (바이트, `int`; 0 = OS 기본값) |
| `ZLINK_SOCKOPT_RCVBUF` | 커널 수신 버퍼 크기 (바이트, `int`; 0 = OS 기본값) |

#### 타이밍

| 상수 | 설명 |
|---|---|
| `ZLINK_SOCKOPT_SNDTIMEO` | 송신 타임아웃 (밀리초, `int`; -1 = 무한) |
| `ZLINK_SOCKOPT_RECONNECT_IVL` | 초기 재연결 간격 (밀리초, `int`) |
| `ZLINK_SOCKOPT_RECONNECT_IVL_MAX` | 최대 재연결 간격 (밀리초, `int`; 0 = `RECONNECT_IVL`만 사용) |
| `ZLINK_SOCKOPT_CONNECT_TIMEOUT` | 연결 타임아웃 (밀리초, `int`) |
| `ZLINK_SOCKOPT_TCP_MAXRT` | 최대 TCP 재전송 타임아웃 (밀리초, `int`) |
| `ZLINK_SOCKOPT_HANDSHAKE_IVL` | ZMTP 핸드셰이크 타임아웃 (밀리초, `int`) |

#### TCP

| 상수 | 설명 |
|---|---|
| `ZLINK_SOCKOPT_TCP_KEEPALIVE` | SO_KEEPALIVE 재정의 (`int`; -1 = OS 기본값, 0 = 끄기, 1 = 켜기) |
| `ZLINK_SOCKOPT_TCP_KEEPALIVE_CNT` | TCP_KEEPCNT 재정의 (`int`; -1 = OS 기본값) |
| `ZLINK_SOCKOPT_TCP_KEEPALIVE_IDLE` | TCP_KEEPIDLE 재정의 (초, `int`; -1 = OS 기본값) |
| `ZLINK_SOCKOPT_TCP_KEEPALIVE_INTVL` | TCP_KEEPINTVL 재정의 (초, `int`; -1 = OS 기본값) |
| `ZLINK_SOCKOPT_TCP_NODELAY` | TCP_NODELAY 활성화 (`int`; 0 또는 1) |

#### Pub/Sub

| 상수 | 설명 |
|---|---|
| `ZLINK_SOCKOPT_SUBSCRIBE` | 토픽 접두사 구독 (`binary`) |
| `ZLINK_SOCKOPT_UNSUBSCRIBE` | 토픽 접두사 구독 해제 (`binary`) |
| `ZLINK_SOCKOPT_XPUB_VERBOSE` | 모든 구독 메시지를 업스트림으로 전달 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_XPUB_NODROP` | HWM에서 메시지를 자동 삭제하지 않고 `EAGAIN` 반환 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_XPUB_MANUAL` | XPUB에서 수동 구독 관리 활성화 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_XPUB_WELCOME_MSG` | 새 서브스크라이버 연결 시 전송되는 메시지 (`binary`) |
| `ZLINK_SOCKOPT_XPUB_VERBOSER` | 모든 구독 및 구독 해제 메시지를 업스트림으로 전달 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_XPUB_MANUAL_LAST_VALUE` | 수동 XPUB 모드에서 최신 값 캐싱 활성화 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_INVERT_MATCHING` | 토픽 매칭 반전 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_CONFLATE` | 토픽당 가장 최근 메시지만 유지 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_ONLY_FIRST_SUBSCRIBE` | 토픽 접두사당 첫 번째 구독만 처리 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_TOPICS_COUNT` | 구독된 토픽 수 (읽기 전용, `int`) |

#### Router

| 상수 | 설명 |
|---|---|
| `ZLINK_SOCKOPT_ROUTER_MANDATORY` | 연결되지 않은 피어로 라우팅 시 `EHOSTUNREACH` 반환 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_ROUTER_HANDOVER` | 새 연결이 기존 라우팅 아이덴티티를 인수하도록 허용 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_PROBE_ROUTER` | 연결 시 빈 메시지를 보내 ROUTER 피어에서 아이덴티티 설정 (`int`; 0 또는 1) |

#### 하트비트

| 상수 | 설명 |
|---|---|
| `ZLINK_SOCKOPT_HEARTBEAT_IVL` | ZMTP 하트비트 간격 (밀리초, `int`; 0 = 비활성화) |
| `ZLINK_SOCKOPT_HEARTBEAT_TTL` | ZMTP 하트비트 TTL (밀리초, `int`) |
| `ZLINK_SOCKOPT_HEARTBEAT_TIMEOUT` | ZMTP 하트비트 타임아웃 (밀리초, `int`) |

#### TLS

| 상수 | 설명 |
|---|---|
| `ZLINK_SOCKOPT_TLS_CERT` | PEM 인코딩된 TLS 인증서 경로 (`string`) |
| `ZLINK_SOCKOPT_TLS_KEY` | PEM 인코딩된 TLS 개인 키 경로 (`string`) |
| `ZLINK_SOCKOPT_TLS_CA` | PEM 인코딩된 CA 인증서 번들 경로 (`string`) |
| `ZLINK_SOCKOPT_TLS_VERIFY` | TLS 피어 인증서 검증 활성화 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_TLS_REQUIRE_CLIENT_CERT` | 서버 소켓에서 TLS 클라이언트 인증서 요구 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_TLS_HOSTNAME` | TLS SNI 및 인증서 검증을 위한 예상 호스트명 (`string`) |
| `ZLINK_SOCKOPT_TLS_TRUST_SYSTEM` | 시스템 CA 인증서 저장소 신뢰 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_TLS_PASSWORD` | 암호화된 TLS 개인 키의 비밀번호 (`string`) |

#### 기타

| 상수 | 설명 |
|---|---|
| `ZLINK_SOCKOPT_IPV6` | 소켓에서 IPv6 활성화 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_IMMEDIATE` | 완료된 연결에만 메시지 대기열 사용 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_BLOCKY` | 레거시 옵션: context 종료 시 블로킹 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_BINDTODEVICE` | 소켓을 특정 네트워크 인터페이스에 바인딩 (`string`) |
| `ZLINK_SOCKOPT_CONNECT_ROUTING_ID` | 다음 발신 연결의 라우팅 아이덴티티 설정 (`binary`) |
| `ZLINK_SOCKOPT_STREAM_NOTIFY` | STREAM 연결/해제 알림 활성화 (`int`; 0 또는 1) |
| `ZLINK_SOCKOPT_RATE` | 멀티캐스트 데이터 전송률 (kbps, `int`) |
| `ZLINK_SOCKOPT_RECOVERY_IVL` | 멀티캐스트 복구 간격 (밀리초, `int`) |
| `ZLINK_SOCKOPT_MULTICAST_HOPS` | 최대 멀티캐스트 홉 수 (TTL) (`int`) |
| `ZLINK_SOCKOPT_TOS` | IP Type-of-Service 값 (`int`) |
| `ZLINK_SOCKOPT_MULTICAST_MAXTPDU` | 최대 멀티캐스트 전송 데이터 유닛 크기 (바이트, `int`) |
| `ZLINK_SOCKOPT_ZMP_METADATA` | 발신 연결에 ZMP 메타데이터 속성 첨부 (`binary`) |

## 함수

### zlink_socket

수신 핸들러와 함께 소켓을 생성합니다.

```c
void *zlink_socket (void *context_,
                    zlink_socket_type_t type_,
                    const zlink_socket_handler_t *handler_);
```

지정된 context 내에서 새 소켓을 생성합니다. `type_` 매개변수는 메시징 패턴을
선택합니다. `handler_` 매개변수는 메시지 도착 시 I/O 스레드에서 호출되는 수신
콜백을 지정합니다. 송신 전용 소켓(PUB)의 경우 `NULL`을 전달합니다. 수신 가능한
모든 타입에서 핸들러는 non-NULL이어야 합니다.

콜백은 생성 시점에 고정되며 교체할 수 없습니다. 소켓은 context가 종료되기 전에
`zlink_close()`로 닫아야 합니다.

**반환값:** 성공 시 소켓 핸들, 실패 시 `NULL` (errno가 설정됨).

**에러:** 소켓 타입이 유효하지 않거나 수신 가능한 타입에서 핸들러가 NULL이면
`EINVAL`. 최대 소켓 수에 도달하면 `EMFILE`. Context가 종료된 경우 `ETERM`.

**스레드 안전성:** Context에 대해 스레드 안전합니다.

**참고:** `zlink_close`, `zlink_ctx_new`

---

### zlink_close

소켓을 닫고 리소스를 해제합니다.

```c
int zlink_close (void *s_);
```

소켓을 닫고 관련된 모든 리소스를 해제합니다. 송신 대기열에 남아 있는 메시지는
`ZLINK_LINGER` 설정에 따라 폐기되거나 송신됩니다. 다른 스레드에서 동일 핸들에
대해 콜백이나 API 호출이 진행 중이면 `errno=EBUSY`로 실패합니다. send-ready
또는 monitor 콜백 내에서의 self-close는 콜백 에필로그까지 지연됩니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 핸들이 유효한 소켓이 아니면 `ENOTSOCK`. 콜백이나 작업이 진행 중이면
`EBUSY`.

**참고:** `zlink_socket`

---

### zlink_setsockopt

소켓 옵션을 설정합니다.

```c
int zlink_setsockopt (void *s_,
                      zlink_socket_option_t option_,
                      const void *optval_,
                      size_t optvallen_);
```

소켓 옵션을 구성합니다. `option_` 매개변수는 옵션을 식별합니다 (예:
`ZLINK_SNDHWM`, `ZLINK_LINGER`, `ZLINK_SUBSCRIBE`). `optval_` 포인터는 값을
제공하고 `optvallen_`은 크기를 바이트 단위로 지정합니다.

일부 옵션은 소켓을 바인딩하거나 연결하기 전에 설정해야 합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 옵션이 알 수 없거나 값이 범위를 벗어나면 `EINVAL`. Context가 종료된
경우 `ETERM`.

**참고:** `zlink_getsockopt`

---

### zlink_getsockopt

소켓 옵션을 조회합니다.

```c
int zlink_getsockopt (void *s_,
                      zlink_socket_option_t option_,
                      void *optval_,
                      size_t *optvallen_);
```

소켓 옵션의 현재 값을 가져옵니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_setsockopt`

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
`ZLINK_LAST_ENDPOINT`를 사용하세요.

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

소켓에서 버퍼 데이터를 송신합니다.

```c
int zlink_send (void *s_,
                const void *buf_,
                size_t len_,
                zlink_send_flags_t flags_);
```

소켓 `s_`에서 `buf_`의 `len_` 바이트를 송신합니다. 데이터는 전송 전에 내부
메시지로 복사됩니다. `flags_` 매개변수는 0, `ZLINK_DONTWAIT`, `ZLINK_SNDMORE`,
또는 이들의 비트 조합일 수 있습니다. 멀티파트 메시지를 보내려면
`ZLINK_SNDMORE`를 사용하세요. 마지막 파트에서만 이 플래그를 생략합니다.

**반환값:** 성공 시 송신된 바이트 수, 실패 시 -1 (errno가 설정됨).

**에러:** 작업이 블로킹되고 `ZLINK_DONTWAIT`가 설정된 경우 `EAGAIN`. Context가
종료된 경우 `ETERM`.

**참고:** `zlink_msg_send`

---

### zlink_socket_set_send_ready_handler

send-ready 콜백을 설정하거나 교체합니다.

```c
int zlink_socket_set_send_ready_handler (
  void *s_, zlink_send_ready_handler_fn handler_);
```

핸들러는 교체 전용입니다. NULL 전달은 유효하지 않습니다. 교체 성공 시 다음 쓰기
가능 전환부터 반영됩니다. 동일 핸들의 send-ready 콜백 내에서 재진입 호출하면
`errno=EDEADLK`로 실패합니다.

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

## STREAM 콜백 Dispatch API

다음 함수들은 STREAM 소켓을 위한 콜백 기반 인터페이스를 제공합니다.
STREAM 수신은 콜백 전용이며, `recv()`는 지원되지 않습니다. 애플리케이션이
raw 콜백을 등록하면 I/O 스레드에서 데이터 도착 시 직접 호출됩니다.

### zlink_stream_on_raw_fn

```c
typedef int (*zlink_stream_on_raw_fn) (const zlink_routing_id_t *rid_,
                                       zlink_msg_t *msg_);
```

STREAM I/O 스레드에서 데이터 도착 시 호출되는 콜백. `rid_`는 피어를 식별합니다.
`msg_`는 raw 스트림 청크이며, 소유권이 콜백으로 이전됩니다. 콜백은 반환 전에
정확히 1회 해제해야 합니다 (예: `zlink_msg_close()` 또는
`zlink_stream_send_msg()`로 소비). 반환 후 포인터를 유지하면 안 됩니다.
0을 반환하면 dispatch 계속, 0이 아니면 종료를 요청합니다.

---

### zlink_stream_attach_raw

STREAM 소켓에 raw 콜백 dispatch를 등록합니다.

```c
int zlink_stream_attach_raw (void *s_, zlink_stream_on_raw_fn on_raw_);
```

STREAM 소켓 `s_`에 `on_raw_`를 dispatch 콜백으로 등록합니다. 한 번에 하나의
콜백만 등록 가능하며, 이미 등록된 상태에서 호출하면 `errno=EBUSY`와 함께
-1을 반환합니다. attach/detach는 애플리케이션 스레드에서 호출할 수 있으며
STREAM send/close와 직렬화됩니다. raw 콜백 내부에서 attach/detach를 호출하는
것은 지원되지 않습니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `s_`가 STREAM 소켓이 아니거나 `on_raw_`가 NULL이면 `EINVAL`.
이미 콜백이 등록된 경우 `EBUSY`.

**참고:** `zlink_stream_detach`, `zlink_stream_send`

---

### zlink_stream_detach

STREAM 소켓에서 콜백 dispatch를 해제합니다.

```c
int zlink_stream_detach (void *s_);
```

이전에 등록된 dispatch 콜백을 제거합니다. 애플리케이션 스레드에서 호출할 수
있으며 STREAM send/close와 직렬화됩니다. raw 콜백 내부에서 detach를 호출하는
것은 지원되지 않습니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `s_`가 STREAM 소켓이 아니면 `EINVAL`.

**참고:** `zlink_stream_attach_raw`

---

### zlink_stream_send

routing id로 특정 STREAM 피어에게 페이로드를 전송합니다.

```c
int zlink_stream_send (void *s_,
                       const zlink_routing_id_t *rid_,
                       const void *data_,
                       size_t size_,
                       zlink_send_flags_t flags_);
```

`rid_`로 식별되는 피어에게 `data_`의 `size_` 바이트를 전송합니다. 내부적으로
routing id를 첫 번째 프레임으로, 페이로드를 두 번째 프레임으로 전송합니다.
STREAM send API는 애플리케이션 스레드와 STREAM dispatch 콜백에서 호출할 수
있으며, 내부적으로 발신 상태가 직렬화됩니다.

**반환값:** 수락된 페이로드 바이트 수(`size_`), 또는 실패 시 -1.

**에러:** `s_`가 STREAM 소켓이 아니거나 `rid_`가 유효하지 않으면 `EINVAL`.
작업이 블로킹되고 `ZLINK_DONTWAIT`가 설정된 경우 `EAGAIN`.

**참고:** `zlink_stream_send_msg`, `zlink_stream_attach_raw`

---

### zlink_stream_send_msg

routing id로 특정 STREAM 피어에게 메시지를 전송합니다.

```c
int zlink_stream_send_msg (void *s_,
                           const zlink_routing_id_t *rid_,
                           zlink_msg_t *msg_,
                           zlink_send_flags_t flags_);
```

`zlink_stream_send()`와 동일하지만 raw 버퍼 대신 `zlink_msg_t`를 받습니다.
메시지 `msg_`는 이 호출에 의해 소비(move)되며 반환 전에 재초기화됩니다.

**반환값:** 수락된 페이로드 바이트 수, 또는 실패 시 -1.

**참고:** `zlink_stream_send`, `zlink_stream_attach_raw`

---

## 소켓 모니터

### zlink_socket_monitor_open

고정 콜백과 함께 소켓 모니터 핸들을 열고 반환합니다.

```c
void *zlink_socket_monitor_open (void *s_,
                                 zlink_socket_monitor_event_mask_t events_,
                                 zlink_monitor_handler_fn handler_);
```

소켓 `s_`에 대한 모니터를 생성하고 핸들을 반환합니다. `events_` 비트마스크에
해당하는 이벤트가 `handler_` 콜백을 통해 I/O 스레드에서 전달됩니다. 반환된
핸들은 더 이상 필요하지 않을 때 `zlink_close()`로 닫아야 합니다.

**반환값:** 성공 시 모니터 핸들, 실패 시 `NULL` (errno가 설정됨).

**참고:** `zlink_close`
