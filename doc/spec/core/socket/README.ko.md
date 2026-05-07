[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md)

[English](README.md) | [한국어](README.ko.md)

# 소켓 -- 공통 명세

이 문서는 모든 소켓 타입에 적용되는 공통 기반을 다룹니다.
타입별 명세(타입 전용 옵션, 데이터 플레인 API, 동작 세부사항)는
별도 파일에 있습니다.

| 소켓 타입 | 명세 |
|-----------|------|
| PAIR | [pair.ko.md](pair.ko.md) |
| DEALER | [dealer.ko.md](dealer.ko.md) |
| ROUTER | [router.ko.md](router.ko.md) |
| PUB | [pub.ko.md](pub.ko.md) |
| SUB | [sub.ko.md](sub.ko.md) |
| XPUB | [xpub.ko.md](xpub.ko.md) |
| XSUB | [xsub.ko.md](xsub.ko.md) |
| STREAM | [stream.ko.md](stream.ko.md) |

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

## 수신 모델 요약

소켓 타입별 수신 모델은 아래와 같이 고정합니다. 기본 모델은
`recv + poller`이며, 예외 타입만 콜백 기반 수신을 지원합니다.

| 소켓 타입 | 수신 표면 | 비고 |
|-----------|-----------|------|
| PAIR | `zlink_recv()` | recv-only |
| DEALER | `zlink_recv()` (+ `zlink_dealer_request()` completion callback) | recv-only data plane |
| SUB | `zlink_subscribe()` | recv-only |
| XSUB | `zlink_subscribe()` | recv-only |
| ROUTER | `zlink_router_recv()` (+ `zlink_router_request()` completion callback) | recv-only data plane |
| STREAM | `zlink_recv()` / `zlink_recv_handler()` / `zlink_stream_packet_handler()` | 세 모드 중 하나 선택 (예외) |
| PUB | 해당 없음 | 송신 전용 |
| XPUB | `zlink_xpub_recv_part()` (구독 이벤트 recv-only) | 데이터 plane은 송신 |
| monitor / timer | recv / callback 모두 지원 | 관찰/유틸 계층 |

핵심 원칙:

- raw data-plane 수신은 recv + poller 조합이 기본이며, 서버 루프는
  `ZLINK_POLLIN`을 관찰한 뒤 recv 계열 함수로 데이터를 가져오는 방식을 씁니다.
- `DEALER`/`ROUTER`의 request completion callback은 data-plane receive가
  아니라 비동기 작업 완료 통지입니다. 이 둘은 역할이 다르므로 같은 범주로
  묶지 않습니다.
- STREAM만은 예외입니다. raw transport 특성상 세 가지 수신 모드(raw recv,
  raw callback, packet callback) 중 하나를 선택할 수 있습니다. 한 handle
  에서 두 번째 모드로 전환하려 하면 `EBUSY`로 실패합니다.

## 콜백 타입

### zlink_socket_msg_handler_fn

```c
typedef void (*zlink_socket_msg_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);
```

raw `STREAM`의 raw 수신 콜백에 사용되는 타입입니다. 소유 I/O 스레드에서
호출되며, 모든 메시지 파트의 소유권이 콜백으로 이전됩니다. 각 파트는
정확히 한 번 닫거나 소비해야 합니다. `zlink_recv_handler()`와 함께
사용합니다.

### zlink_stream_packet_handler_fn

```c
typedef void (*zlink_stream_packet_handler_fn) (
  void *stream_,
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *header_,
  zlink_msg_t *body_,
  void *userdata_);
```

raw `STREAM`의 packet 단위 수신 콜백 타입입니다. `source_rid_`는 packet을
보낸 client 연결의 routing id를 가리키는 borrowed view이고, `header_`와
`body_`는 고정 framing 규약에 따라 조립된 packet의 header/body payload
입니다. 길이가 0인 경우에도 NULL이 아닌 유효한 `zlink_msg_t`로 전달되며,
두 `msg_t`의 소유권은 콜백으로 이전됩니다. `zlink_stream_packet_handler()`
와 함께 사용합니다.

### zlink_send_ready_handler_fn

```c
typedef void (*zlink_send_ready_handler_fn) (void *subject_, void *userdata_);
```

해당 handle이 backpressure 상태에서 벗어나 송신 재시도를 시도할 가치가
있는 시점에 호출되는 콜백입니다. `ZLINK_POLLOUT`과 같은 send-recovery
readiness 축을 공유하며, 콜백 자체는 재시도 성공을 보장하지 않습니다.

### zlink_reply_handler_fn

```c
typedef void (*zlink_reply_handler_fn) (
  zlink_request_result_t result_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);
```

비동기 request-reply 완료 콜백. 응답이 도착하거나 요청이 타임아웃되면
호출됩니다. 타임아웃 시 `result_`는 `ZLINK_REQUEST_TIMED_OUT`이고 `parts_`는
NULL입니다. 성공 시 `result_`는 `ZLINK_REQUEST_OK`이고 모든 메시지 파트의
소유권이 콜백으로 이전됩니다. `result_`는 submit 실패가 아니라
`zlink_request_result_t` 값으로 request completion 결과를 나타냅니다. 이
콜백은 data-plane receive가 아니라 async operation completion 통지 축이며,
`DEALER`/`ROUTER`의 request API에서만 사용됩니다.

## 상수

### 소켓 타입

```c
typedef enum zlink_socket_type_t
{
    ZLINK_SOCKET_ANY    = 0,
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

`ZLINK_SOCKET_ANY`는 생성할 socket type이 아니다. filter API에서 전체 socket type을
뜻하는 wildcard로만 사용한다. 실제 socket 생성에는 위에 표시된 정규화된
`ZLINK_SOCKET_*` 상수를 사용한다.

### 송신 플래그

```c
typedef enum zlink_send_flags_t
{
    ZLINK_SEND_FLAGS_NONE     = 0,
    ZLINK_SEND_FLAGS_DONTWAIT = 0x0001u
} zlink_send_flags_t;
```

`ZLINK_DONTWAIT` 는 `ZLINK_SEND_FLAGS_DONTWAIT` 를 짧게 쓰는 공개 이름이다.

| 상수 | 설명 |
|---|---|
| `ZLINK_SEND_FLAGS_NONE` | 플래그 없음; 블로킹 송신 동작. |
| `ZLINK_SEND_FLAGS_DONTWAIT` | 논블로킹 모드; 블로킹 시 `ZLINK_SUBMIT_BACKPRESSURED` 반환 |
| `ZLINK_DONTWAIT` | `ZLINK_SEND_FLAGS_DONTWAIT` 를 짧게 쓰는 이름 |

### 수신 플래그

```c
typedef enum zlink_recv_flags_t
{
    ZLINK_RECV_FLAGS_NONE     = 0,
    ZLINK_RECV_FLAGS_DONTWAIT = 0x0001u
} zlink_recv_flags_t;
```

`zlink_recv`, `zlink_subscribe`, 소켓별 `zlink_*_recv` 계열, 그리고
monitor `zlink_*_monitor_recv` 함수들이 이 플래그를 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_RECV_FLAGS_NONE` | 플래그 없음; 블로킹 수신 동작. |
| `ZLINK_RECV_FLAGS_DONTWAIT` | 논블로킹 수신; 수신할 메시지가 없으면 `ZLINK_RECV_NO_DATA` 를 즉시 반환. |

### rid 중복 정책

```c
typedef enum zlink_rid_duplicate_policy_t
{
    ZLINK_RID_DUPLICATE_REJECT = 0,
    ZLINK_RID_DUPLICATE_HANDOVER = 1
} zlink_rid_duplicate_policy_t;
```

`ZLINK_OPT_RID_DUPLICATE_POLICY`는 같은 local socket에 동일한 peer
routing id가 들어왔을 때의 정책을 정합니다. 값은 `int`로 설정하며,
기본값은 `ZLINK_RID_DUPLICATE_REJECT`입니다.

| 값 | 의미 |
|---|---|
| `ZLINK_RID_DUPLICATE_REJECT` | 기존 pipe를 유지하고 새 중복 pipe를 등록하지 않음 |
| `ZLINK_RID_DUPLICATE_HANDOVER` | 새 pipe가 같은 routing id의 기존 pipe를 인수 |

이 옵션은 peer가 광고한 routing id를 관찰할 수 있는 socket에서만 의미가
있습니다. STREAM은 서버가 연결별 4바이트 routing id를 직접 만들기 때문에
이 옵션의 영향을 받지 않습니다.

### 송신 결과

```c
typedef enum zlink_submit_result_t
{
    /* Submit succeeded. */
    ZLINK_SUBMIT_OK = 0,

    /* Normal control-flow result. */
    ZLINK_SUBMIT_BACKPRESSURED = 1,
    ZLINK_SUBMIT_NOT_CONNECTED = 2,
    ZLINK_SUBMIT_NOT_FOUND = 3,
    ZLINK_SUBMIT_NOT_ADMITTED = 13,

    /* Runtime / lifecycle failure. */
    ZLINK_SUBMIT_TERMINATED = 4,

    /* Caller contract violation. */
    ZLINK_SUBMIT_INVALID_HANDLE = 5,
    ZLINK_SUBMIT_INVALID_ARGUMENT = 6,
    ZLINK_SUBMIT_NOT_SUPPORTED = 7,
    ZLINK_SUBMIT_INVALID_STATE = 8,
    ZLINK_SUBMIT_THREAD_VIOLATION = 9,

    /* Internal failure. */
    ZLINK_SUBMIT_OUT_OF_MEMORY = 10,
    ZLINK_SUBMIT_SEQ_EXHAUSTED = 11,
    ZLINK_SUBMIT_INTERNAL_ERROR = 12
} zlink_submit_result_t;
```

send, request submit, reply submit API의 공개 결과를 정규화할 때
사용하는 기준 enum입니다. exported C API는 이 enum을 직접 반환합니다.
내부 구현 경로는 계속 상세 `errno`를 사용하고, exported API 경계에서 그
값을 이 공개 결과 계약으로 정규화합니다.

| 상수 | 값 | 설명 |
|---|---|---|
| `ZLINK_SUBMIT_OK` | 0 | 메시지가 성공적으로 송신됨 |
| `ZLINK_SUBMIT_BACKPRESSURED` | 1 | 송신 큐가 가득 참 (HWM 도달) |
| `ZLINK_SUBMIT_NOT_CONNECTED` | 2 | 대상 경로나 peer가 아직 연결되지 않음 |
| `ZLINK_SUBMIT_NOT_FOUND` | 3 | 대상 peer, spot, routed destination을 찾지 못함 |
| `ZLINK_SUBMIT_NOT_ADMITTED` | 13 | Normal control-flow 결과. 가중치 `0` 대상이 신규 outbound를 거부함; 호출자는 다른 peer를 선택하거나 대기해야 함 |
| `ZLINK_SUBMIT_TERMINATED` | 4 | context가 종료됨 |
| `ZLINK_SUBMIT_INVALID_HANDLE` | 5 | 핸들이 NULL이거나 유효하지 않음 |
| `ZLINK_SUBMIT_INVALID_ARGUMENT` | 6 | API 계약에 맞지 않는 인자 |
| `ZLINK_SUBMIT_NOT_SUPPORTED` | 7 | 지원하지 않는 작업 또는 flags |
| `ZLINK_SUBMIT_INVALID_STATE` | 8 | 핸들이 잘못된 상태에 있음 |
| `ZLINK_SUBMIT_THREAD_VIOLATION` | 9 | 허용된 스레드 모델을 위반함 |
| `ZLINK_SUBMIT_OUT_OF_MEMORY` | 10 | submit 준비 중 메모리 할당 실패 |
| `ZLINK_SUBMIT_SEQ_EXHAUSTED` | 11 | request sequence 공간이 소진됨 |
| `ZLINK_SUBMIT_INTERNAL_ERROR` | 12 | 내부 send/request/reply submit 오류 |

### Request Completion

```c
typedef enum zlink_request_result_t
{
    /* Reply completed successfully. */
    ZLINK_REQUEST_OK = 0,

    /* Completion failure visible to the requester. */
    ZLINK_REQUEST_TIMED_OUT = 101,
    ZLINK_REQUEST_NOT_FOUND = 102,
    ZLINK_REQUEST_TERMINATED = 103,
    ZLINK_REQUEST_PROTOCOL_ERROR = 104,
    ZLINK_REQUEST_INTERNAL_ERROR = 105
} zlink_request_result_t;
```

`zlink_reply_handler_fn`의 completion 결과를 정규화할 때 사용하는 기준
enum입니다. callback은 `result_`를 `zlink_request_result_t` 값으로
직접 전달합니다.

| 상수 | 값 | 설명 |
|---|---|---|
| `ZLINK_REQUEST_OK` | 0 | reply payload를 정상 수신함 |
| `ZLINK_REQUEST_TIMED_OUT` | 101 | 설정된 시간 안에 reply가 도착하지 않음 |
| `ZLINK_REQUEST_NOT_FOUND` | 102 | 대상이 없어 error reply로 완료됨 |
| `ZLINK_REQUEST_TERMINATED` | 103 | request 경로가 명시적인 종료 completion을 방출하기 전까지는 예약값 |
| `ZLINK_REQUEST_PROTOCOL_ERROR` | 104 | reply envelope 또는 error reply payload가 잘못됨 |
| `ZLINK_REQUEST_INTERNAL_ERROR` | 105 | 더 세분화된 public bucket 없이 request completion이 실패함 |

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
모든 소켓 타입과 discovery(fan-out)에 적용됩니다.

내부적으로 옵션은 세 소유권 카테고리로 분류되어 각 도메인 소유자가
validation/apply를 담당합니다. 공개 API surface는 동일하지만, 새 옵션
추가 시 아래 분류에 따라 소유권이 결정됩니다.

| 카테고리 | 대표 옵션 | 내부 소유자 |
|----------|-----------|-------------|
| Core Socket | `SNDHWM`, `RCVHWM`, `AUTO_HWM_MSG_UNIT_BYTES`, `LINGER`, `SNDTIMEO`, `RCVTIMEO` | `options_core_socket` |
| Transport/Network | `RATE`, `RECOVERY_IVL`, `SNDBUF`, `RCVBUF`, `TOS`, `PRIORITY` | `options_transport_network` |
| Protocol/Metadata | ZMP 메타데이터 | `options_protocol_metadata` |

##### Transport/Buffer

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_AFFINITY` | I/O 스레드 어피니티 비트마스크 (`uint64_t`) |
| `ZLINK_OPT_RATE` | 멀티캐스트 전송률 (kbps, `int`) |
| `ZLINK_OPT_RECOVERY_IVL` | 멀티캐스트 복구 간격 (ms, `int`) |
| `ZLINK_OPT_SNDBUF` | 커널 송신 버퍼 크기 (`int`; 0=OS 기본값) |
| `ZLINK_OPT_RCVBUF` | 커널 수신 버퍼 크기 (`int`; 0=OS 기본값) |
| `ZLINK_OPT_SNDHWM` | 송신 하이 워터 마크 (`int`; 0=무제한) |
| `ZLINK_OPT_RCVHWM` | 수신 하이 워터 마크 (`int`; 0=무제한) |
| `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` | 자동 HWM 계산에서 메시지 슬롯으로 환산할 때 쓰는 바이트 단위 (`int`; 0=소켓 타입 기본값, 음수는 `EINVAL`) |
| `ZLINK_OPT_MAXMSGSIZE` | 최대 인바운드 메시지 크기 (`int64_t`; -1=무제한) |

##### Timing

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_LINGER` | 종료 시 대기 (ms, `int`; -1=무한, 0=즉시) |
| `ZLINK_OPT_RCVTIMEO` | 수신 타임아웃 (ms, `int`; -1=무한) |
| `ZLINK_OPT_SNDTIMEO` | 송신 타임아웃 (ms, `int`; -1=무한) |
| `ZLINK_OPT_CONNECT_TIMEOUT` | 연결 타임아웃 (ms, `int`) |
| `ZLINK_OPT_RECONNECT_IVL` | 초기 재연결 간격 (ms, `int`) |
| `ZLINK_OPT_RECONNECT_IVL_MAX` | 최대 재연결 간격 (ms, `int`; 0=IVL만 사용) |
| `ZLINK_OPT_HANDSHAKE_IVL` | ZMTP 핸드셰이크 타임아웃 (ms, `int`) |

##### TCP

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_TCP_KEEPALIVE` | SO_KEEPALIVE (`int`; -1=OS, 0=off, 1=on) |
| `ZLINK_OPT_TCP_KEEPALIVE_CNT` | TCP_KEEPCNT (`int`; -1=OS 기본값) |
| `ZLINK_OPT_TCP_KEEPALIVE_IDLE` | TCP_KEEPIDLE (초, `int`; -1=OS 기본값) |
| `ZLINK_OPT_TCP_KEEPALIVE_INTVL` | TCP_KEEPINTVL (초, `int`; -1=OS 기본값) |
| `ZLINK_OPT_TCP_MAXRT` | 최대 TCP 재전송 타임아웃 (ms, `int`) |
| `ZLINK_OPT_TCP_NODELAY` | TCP_NODELAY 활성화 (`int`; 0 또는 1) |

##### Heartbeat

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_HEARTBEAT_IVL` | ZMTP heartbeat(연결 생존 확인 신호) 간격 (ms, `int`; 0=비활성) |
| `ZLINK_OPT_HEARTBEAT_TTL` | ZMTP heartbeat TTL (ms, `int`) |
| `ZLINK_OPT_HEARTBEAT_TIMEOUT` | ZMTP heartbeat 타임아웃 (ms, `int`) |

##### Network

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_IPV6` | 소켓에서 IPv6 활성화 (`int`; 0 또는 1) |
| `ZLINK_OPT_TOS` | IP Type-of-Service 값 (`int`) |
| `ZLINK_OPT_MULTICAST_HOPS` | 멀티캐스트 TTL (`int`) |
| `ZLINK_OPT_MULTICAST_MAXTPDU` | 최대 멀티캐스트 TPDU 크기 (`int`) |
| `ZLINK_OPT_BINDTODEVICE` | 네트워크 인터페이스 바인딩 (`string`) |
| `ZLINK_OPT_BACKLOG` | listener backlog (`int`) |

##### TLS

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_TLS_CERT` | PEM 인코딩 TLS 인증서 경로 (`string`) |
| `ZLINK_OPT_TLS_KEY` | PEM 인코딩 TLS 개인 키 경로 (`string`) |
| `ZLINK_OPT_TLS_CA` | PEM 인코딩 CA 인증서 번들 경로 (`string`) |
| `ZLINK_OPT_TLS_VERIFY` | TLS 피어 검증 활성화 (`int`; 0 또는 1) |
| `ZLINK_OPT_TLS_REQUIRE_CLIENT_CERT` | 클라이언트 인증서 요구 (`int`; 0 또는 1) |
| `ZLINK_OPT_TLS_HOSTNAME` | SNI 및 인증서 검증용 호스트명 (`string`) |
| `ZLINK_OPT_TLS_TRUST_SYSTEM` | 시스템 CA 인증서 저장소 신뢰 (`int`; 0 또는 1) |
| `ZLINK_OPT_TLS_PASSWORD` | 개인 키 암호 (`string`) |

##### Behavior

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_IMMEDIATE` | 완료된 연결에만 메시지 큐 사용 (`int`) |
| `ZLINK_OPT_CONFLATE` | 토픽당 최신 메시지만 유지 (`int`) |
| `ZLINK_OPT_BLOCKY` | context 종료 시 블로킹 (`int`, 레거시) |
| `ZLINK_OPT_INVERT_MATCHING` | 토픽 매칭 반전 (`int`) |
| `ZLINK_OPT_ZMP_METADATA` | ZMP 메타데이터 첨부 (`binary`) |
| `ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC` | Discovery가 SPOT owner row를 Registry에 publish할지 여부 (`int`; 0=기본값, 1=활성화, 그 외 값은 `EINVAL`) |

##### Read-only

| 상수 | 설명 |
|---|---|
| `ZLINK_OPT_FD` | 파일 디스크립터 (`zlink_fd_t`, 읽기 전용) |
| `ZLINK_OPT_EVENTS` | 이벤트 상태 비트마스크 (`int`, 읽기 전용) |
| `ZLINK_OPT_TYPE` | 소켓 타입 (`int`, 읽기 전용) |
| `ZLINK_OPT_LAST_ENDPOINT` | 바인딩된 엔드포인트 (`string`, 읽기 전용) |
| `ZLINK_OPT_ROUTE_VALUE_MAX_SIZE` | 최대 discovery route value 크기 (`int`, 읽기 전용) |

#### 전용 함수 (옵션 enum이 아님)

- **Routing ID**: `zlink_set_routing_id()` / `zlink_get_routing_id()`
- **TLS**: `zlink_set_tls_server()` / `zlink_set_tls_client()`
- **Subscribe/Unsubscribe**: `zlink_set_subscription()` / `zlink_unset_subscription()`

## 함수

### zlink_socket

소켓을 생성합니다.

```c
void *zlink_socket (void *context_, zlink_socket_type_t type_);
```

지정된 context 내에서 새 소켓을 생성합니다. `type_` 매개변수는 메시징 패턴을
선택합니다. raw socket의 수신 모델은 타입별로 고정됩니다. `PAIR`, `DEALER`,
`SUB`, `XSUB`는 recv-only이며, `ROUTER`는 `zlink_router_recv()`로
수신합니다. `STREAM`만이 예외 타입으로, raw recv / raw callback
(`zlink_recv_handler()`) / packet callback
(`zlink_stream_packet_handler()`) 세 모드 중 하나를 선택해 사용할 수
있습니다. 소켓은 context가 종료되기 전에 `zlink_close()`로 닫아야 합니다.

**반환값:** 성공 시 소켓 핸들, 실패 시 `NULL` (errno가 설정됨).

**에러:** 소켓 타입이 유효하지 않으면 `EINVAL`. 최대 소켓 수에 도달하면
`EMFILE`. Context가 종료된 경우 `ETERM`.

**스레드 안전성:** Context에 대해 스레드 안전합니다.

**참고:** `zlink_close`, `zlink_ctx_new`

---

### zlink_recv_handler

raw `STREAM` 소켓에 raw 수신 콜백을 부착합니다.

```c
zlink_handler_result_t zlink_recv_handler (
  void *s_, zlink_socket_msg_handler_fn handler_, void *userdata_);
```

raw `STREAM` 전용 direct receive callback 등록 함수입니다. 지원 대상은
raw `STREAM` 뿐이며, 다른 subject(PAIR, DEALER 등)는 `ENOTSUP`로 실패합니다.
attach 이후 같은 handle의 `zlink_recv()`, `zlink_stream_packet_handler()`,
data-plane `ZLINK_POLLIN`은 `errno=EBUSY`로 실패합니다. 동일 handle에 대한
두 번째 attach도 `errno=EBUSY`입니다.

자세한 계약은 `stream.ko.md`를 참조하세요.

**반환값:** 성공 시 `ZLINK_HANDLER_OK`. 실패 시에는 `zlink_handler_result_t`
값을 반환합니다. 상세 내부 errno는 진단을 위해 `zlink_errno()`로 유지됩니다.

**참고:** `zlink_stream_packet_handler`, `zlink_socket`, `zlink_close`

---

### zlink_close

소켓을 닫고 리소스를 해제합니다.

```c
zlink_close_result_t zlink_close (void *s_);
```

소켓을 닫고 관련된 모든 리소스를 해제합니다. 송신 대기열에 남아 있는 메시지는
`ZLINK_OPT_LINGER` 설정에 따라 폐기되거나 송신됩니다. 공개 핸들은 계층적 계약을
따릅니다: hot-path send 작업은 여러 스레드에서 동시 호출이 가능하고, 저빈도
제어 경로는 정확성을 위해 직렬화되며, close/destroy는 엄격한 lifecycle gate를
사용합니다. 다른 스레드에서 동일 핸들에 대해 콜백이나 API 호출이 진행 중이면
`errno=EBUSY`로 실패합니다. close가 accepted된 뒤 새 API 진입은
`errno=ESHUTDOWN`으로 실패합니다. send-ready 또는 monitor 콜백 내에서의
self-close는 콜백 에필로그까지 지연됩니다.

**반환값:** 성공 시 `ZLINK_CLOSE_OK`, 실패 시 `zlink_close_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**에러:** 핸들이 유효한 소켓이 아니면 `ENOTSOCK`. 콜백이나 작업이 진행 중이면
`EBUSY`.

**참고:** `zlink_socket`

---

### zlink_set_option

공통 옵션을 설정합니다.

```c
zlink_config_result_t zlink_set_option (void *handle_,
                      zlink_option_t option_,
                      const void *optval_,
                      size_t optvallen_);
```

공통 옵션을 설정합니다. 모든 소켓 타입과 discovery(fan-out)에서
사용합니다. `option_` 매개변수는 `zlink_option_t` enum 값입니다. `optval_`
포인터는 값을 제공하고 `optvallen_`은 크기를 바이트 단위로 지정합니다.

일부 옵션은 소켓을 바인딩하거나 연결하기 전에 설정해야 합니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**에러:** 옵션이 알 수 없거나 값이 범위를 벗어나면 `EINVAL`. Context가 종료된
경우 `ETERM`.

**참고:** `zlink_get_option`

---

### zlink_get_option

공통 옵션을 조회합니다.

```c
zlink_config_result_t zlink_get_option (void *handle_,
                      zlink_option_t option_,
                      void *optval_,
                      size_t *optvallen_);
```

공통 옵션의 현재 값을 가져옵니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_set_option`

---

### zlink_set_routing_id

소켓의 라우팅 아이덴티티를 설정합니다.

```c
zlink_config_result_t zlink_set_routing_id (void *handle_,
                           const void *data_,
                           size_t size_);
```

ROUTER 주소 지정을 위한 소켓 아이덴티티를 설정합니다. 최대 255바이트.
바인딩 또는 연결하기 전에 설정해야 합니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_get_routing_id`

---

### zlink_get_routing_id

소켓의 라우팅 아이덴티티를 조회합니다.

```c
zlink_config_result_t zlink_get_routing_id (void *handle_,
                           zlink_routing_id_t *out_);
```

소켓에 설정된 라우팅 아이덴티티를 가져옵니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_set_routing_id`

---

### zlink_set_tls_server

서버 측 TLS를 구성합니다.

```c
zlink_config_result_t zlink_set_tls_server (void *handle_,
                           const char *cert_,
                           const char *key_,
                           int require_client_cert_);
```

서버 소켓에 TLS 인증서, 개인 키를 설정하고, 클라이언트 인증서 요구 여부를
지정합니다.

service handle의 경우 TLS 지원 범위는 surface마다 다릅니다. Discovery는
client TLS만, Registry는 client/server TLS를 지원하며, SPOT은 `SpotNode`
handle에서만 TLS를 지원합니다. unified `Spot`과 SPOT child pub/sub handle은
`ENOTSUP`로 실패합니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_set_tls_client`, `zlink_bind`

---

### zlink_set_tls_client

클라이언트 측 TLS를 구성합니다.

```c
zlink_config_result_t zlink_set_tls_client (void *handle_,
                           const char *ca_cert_,
                           const char *hostname_,
                           int trust_system_);
```

클라이언트 소켓에 CA 인증서, 호스트명(SNI 및 인증서 검증용), 시스템 CA
저장소 신뢰 여부를 설정합니다.

service handle의 경우 TLS 지원 범위는 surface마다 다릅니다. Discovery는
client TLS만, Registry는 client/server TLS를 지원하며, SPOT은 `SpotNode`
handle에서만 TLS를 지원합니다. unified `Spot`과 SPOT child pub/sub handle은
`ENOTSUP`로 실패합니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_set_tls_server`, `zlink_connect`

---

### zlink_bind

소켓을 주소에 바인딩합니다.

```c
zlink_bind_result_t zlink_bind (void *s_, const char *addr_);
```

소켓을 로컬 엔드포인트에 바인딩합니다. 엔드포인트 문자열은
`transport://address` 형식을 사용하며, 지원되는 트랜스포트는 다음과 같습니다:

- `tcp://interface:port` 또는 `tcp://*:port`
- `inproc://name` (프로세스 내 직접 연결, in-process transport)
- `ipc://pathname` (프로세스 간, POSIX 전용)
- `ws://interface:port` (WebSocket)
- `tls://interface:port` (TLS 암호화 TCP)

소켓은 여러 엔드포인트에 바인딩할 수 있습니다. TCP의 경우 포트 0을 지정하면
시스템이 임시 포트를 할당합니다. 실제 엔드포인트를 가져오려면
`ZLINK_OPT_LAST_ENDPOINT`를 사용하세요.

**반환값:** 성공 시 `ZLINK_BIND_OK`, 실패 시 `zlink_bind_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**에러:** 주소가 이미 사용 중이면 `EADDRINUSE`. 인터페이스가 존재하지 않으면
`EADDRNOTAVAIL`. 트랜스포트가 지원되지 않으면 `EPROTONOSUPPORT`.

**참고:** `zlink_connect`, `zlink_unbind`

---

### zlink_connect

소켓을 원격 주소에 연결합니다.

```c
zlink_connect_result_t zlink_connect (void *s_, const char *addr_);
```

소켓을 원격 엔드포인트에 연결합니다. 엔드포인트 형식은 `zlink_bind()`와
동일합니다. 소켓은 여러 엔드포인트에 연결할 수 있으며, 피어가 사용 불가능해지면
라이브러리가 자동으로 재연결을 처리합니다.

**반환값:** 성공 시 `ZLINK_CONNECT_OK`, 실패 시 `zlink_connect_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_bind`, `zlink_disconnect`

---

### zlink_unbind

소켓의 주소 바인딩을 해제합니다.

```c
zlink_connect_result_t zlink_unbind (void *s_, const char *addr_);
```

이전에 설정된 바인딩을 제거합니다.

**반환값:** 성공 시 `ZLINK_CONNECT_OK`, 실패 시 `zlink_connect_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_bind`

---

### zlink_disconnect

소켓의 원격 주소 연결을 해제합니다.

```c
zlink_connect_result_t zlink_disconnect (void *s_, const char *addr_);
```

이전에 설정된 연결을 제거합니다.

**반환값:** 성공 시 `ZLINK_CONNECT_OK`, 실패 시 `zlink_connect_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_connect`

---

### zlink_disconnect_rid

소켓에 연결된 peer를 routing id로 찾아 종료합니다.

```c
zlink_connect_result_t zlink_disconnect_rid (
  void *s_,
  const zlink_routing_id_t *peer_rid_);
```

`peer_rid_`는 비어 있으면 안 됩니다. 성공하면 해당 peer pipe는 비동기
종료 절차에 들어갑니다. 성공 반환은 remote peer가 종료 이벤트를 이미
처리했다는 뜻이 아닙니다.

ROUTER와 STREAM은 routing map을 사용해 대상을 찾습니다. STREAM에서는
`peer_rid_`가 반드시 4바이트 연결 routing id여야 합니다. 그 외 socket은
현재 연결된 pipe의 source routing id snapshot에서 일치하는 peer를 찾습니다.
동일한 routing id가 둘 이상이면 대상을 확정할 수 없으므로 실패합니다.

Discovery에 attach된 socket에서는 수동 연결 제어를 허용하지 않으므로
`ZLINK_CONNECT_BUSY`로 실패합니다.

**반환값:** 성공 시 `ZLINK_CONNECT_OK`. 대상 없음은
`ZLINK_CONNECT_NOT_FOUND`, 중복 routing id는 `ZLINK_CONNECT_CONFLICT`,
lifecycle 소유권 충돌은 `ZLINK_CONNECT_BUSY`입니다. `zlink_errno()`는
진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_disconnect`, `ZLINK_OPT_RID_DUPLICATE_POLICY`

---

### zlink_socket_attach_discovery

raw 소켓을 discovery 서비스 뷰에 부착합니다.

```c
zlink_config_result_t zlink_socket_attach_discovery (void *socket_, void *discovery_);
```

raw ROUTER, DEALER, PUB, SUB 소켓을 discovery 서비스 뷰에 부착합니다.
부착된 동안 수동 `connect`, `disconnect`, `unbind`, `close` 작업은
실패합니다. discovery 인스턴스를 제거하면 부착된 소켓 생명주기가
종료됩니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_socket`, `zlink_close`

---

### zlink_send_ready_handler

send-ready 콜백을 설정하거나 교체합니다.

```c
zlink_handler_result_t zlink_send_ready_handler (
  void *s_, zlink_send_ready_handler_fn handler_, void *userdata_);
```

핸들러는 교체 전용입니다. NULL 전달은 유효하지 않습니다. 교체 성공 시 다음 쓰기
가능 전환부터 반영됩니다. 동일 핸들의 send-ready 콜백 내에서 재진입 호출하면
`errno=EDEADLK`로 실패합니다.

지원 대상은 raw `PAIR`, `PUB`, `XPUB`, `DEALER`, `ROUTER`, `STREAM`,
`spot`, `spot_node`입니다. send-ready는 수신 모드와 독립적입니다.

이 콜백과 `ZLINK_POLLOUT`은 같은 send-recovery readiness 축을 가리킵니다.
`BACKPRESSURED` 결과를 본 호출자가 재시도할 가치가 있는 시점을 알립니다.
readiness 신호 자체는 재시도 성공을 보장하지 않으며, 알림 뒤 첫 재시도가
다시 `BACKPRESSURED`로 실패할 수 있습니다. 지원하지 않는 subject는
`ENOTSUP`를 반환합니다.

**반환값:** 성공 시 `ZLINK_HANDLER_OK`, 실패 시 `zlink_handler_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

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
