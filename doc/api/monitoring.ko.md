[English](monitoring.md) | [한국어](monitoring.ko.md)

# 모니터링 API 레퍼런스

canonical 이벤트 카탈로그는 이제 [events.ko.md](events.ko.md)에 정리합니다.
이 문서는 monitor API, callback, monitor snapshot 중심으로 봅니다.

## 현재 권장 API 방향

이제 모니터링은 두 가지 클래스로 분리됩니다.

- 소켓 모니터링:
  `zlink_socket_monitor_open()` + `zlink_socket_monitor_open_options_t`
- 서비스 모니터링:
  `zlink_service_monitor_open()` + `zlink_service_monitor_open_options_t`

두 클래스 모두 동일한 **recv/callback 전달 모델**을 따릅니다.

1. **Open** -- 모니터는 **recv 모델**로 시작합니다. 해당 `*_recv()` 함수로
   이벤트를 가져옵니다.
2. **핸들러 부착** -- `*_handler()`를 호출하면 **callback-only 모델**로 전환됩니다
   (단방향). 전환 후 `*_recv()`는 `EBUSY`를 반환합니다.
3. **Snapshot** -- `zlink_monitor_snapshot()`은 두 모델 모두에서 동작합니다.

모든 모니터는 `zlink_monitor_close()`로 닫습니다.

transport/socket 진단은 소켓 모니터를 사용하고, readiness,
route 변화, registration 결과, SPOT filter 적용 같은 service 상태 전이는
서비스 모니터를 사용합니다.

## 타입

### zlink_monitor_event_t / zlink_socket_monitor_event_t

소켓 모니터 핸들에서 수신된 단일 모니터 이벤트를 설명합니다.
`zlink_socket_monitor_event_t`는 `zlink_monitor_event_t`의 typedef입니다.

```c
typedef struct {
    uint64_t event;
    uint64_t value;
    zlink_routing_id_t routing_id;
    char local_addr[256];
    char remote_addr[256];
} zlink_monitor_event_t;

typedef zlink_monitor_event_t zlink_socket_monitor_event_t;
```

| 필드 | 설명 |
|------|------|
| `event` | 이벤트 타입을 나타내는 비트마스크 (`ZLINK_EVENT_*` 상수 중 하나). |
| `value` | 이벤트별 값. 연결 이벤트의 경우 파일 디스크립터; 에러 이벤트의 경우 errno 또는 프로토콜 에러 코드; 연결 해제 이벤트의 경우 `ZLINK_DISCONNECT_*` 사유. |
| `routing_id` | 해당되는 경우 이벤트에 관련된 피어의 라우팅 아이덴티티. |
| `local_addr` | null 종료 로컬 엔드포인트 주소 문자열. |
| `remote_addr` | null 종료 원격 엔드포인트 주소 문자열. |

### zlink_monitor_handler_fn / zlink_socket_monitor_handler_fn

```c
typedef void (*zlink_monitor_handler_fn) (
  const zlink_monitor_event_t *event_, void *userdata_);

typedef zlink_monitor_handler_fn zlink_socket_monitor_handler_fn;
```

소켓 모니터 이벤트 콜백. I/O 스레드에서 호출됩니다.

### zlink_socket_monitor_open_options_t

```c
typedef struct zlink_socket_monitor_open_options_t
{
    zlink_socket_monitor_event_mask_t events;
} zlink_socket_monitor_open_options_t;
```

| 필드 | 설명 |
|------|------|
| `events` | 관찰할 이벤트를 선택하는 `ZLINK_EVENT_*` 플래그 비트마스크. |

### zlink_monitor_snapshot_t

```c
typedef struct zlink_monitor_snapshot_t
{
    zlink_monitor_source_kind_t source_kind;
    zlink_monitor_state_mask_t state_flags;
    zlink_monitor_snapshot_detail_mask_t detail_flags;
    uint32_t ready_peer_count;
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
} zlink_monitor_snapshot_t;
```

| 필드 | 설명 |
|------|------|
| `source_kind` | snapshot source(`SOCKET`, `GATEWAY`, `SPOT_PUB`, `SPOT_SUB`) |
| `state_flags` | `READY`, `BOUND_READY`, `SEND_READY` 같은 aggregate 상태 |
| `detail_flags` | 어떤 numeric field가 채워졌는지 표시 |
| `ready_peer_count` | 지원되는 경우 aggregate ready/connected peer 수 |
| `snd_pending_msgs` | 지원되는 경우 aggregate 로컬 송신 backlog |
| `rcv_pending_msgs` | 지원되는 경우 aggregate 로컬 수신 backlog snapshot |

## 상수

### Monitor Source Kind

```c
typedef enum zlink_monitor_source_kind_t
{
    ZLINK_MONITOR_SOURCE_SOCKET   = 1,
    ZLINK_MONITOR_SOURCE_GATEWAY  = 2,
    ZLINK_MONITOR_SOURCE_SPOT_PUB = 3,
    ZLINK_MONITOR_SOURCE_SPOT_SUB = 4
} zlink_monitor_source_kind_t;
```

### Monitor State Mask

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_MONITOR_STATE_READY` | `1 << 0` | source가 ready (최소 하나의 연결). |
| `ZLINK_MONITOR_STATE_BOUND_READY` | `1 << 1` | source에 성공적인 bind가 있음. |
| `ZLINK_MONITOR_STATE_SEND_READY` | `1 << 2` | source가 send 연산을 수락할 수 있음. |
| `ZLINK_MONITOR_STATE_CLOSED` | `1 << 3` | source가 닫힘. |

### Monitor Snapshot Detail Mask

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_PEER_COUNT` | `1 << 0` | `ready_peer_count` 필드가 채워짐. |
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS` | `1 << 1` | `snd_pending_msgs` 필드가 채워짐. |
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS` | `1 << 2` | `rcv_pending_msgs` 필드가 채워짐. |

### 이벤트 플래그

관찰할 이벤트를 선택하기 위해 `zlink_socket_monitor_open_options_t.events`에
전달되는 비트마스크 상수입니다. 여러 플래그를 비트 OR로 결합할 수 있습니다.

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_EVENT_CONNECTED` | `0x0001` | 원격 피어에 대한 연결이 수립됨. |
| `ZLINK_EVENT_CONNECT_DELAYED` | `0x0002` | 동기 연결 시도 실패; 비동기 재시도 예약됨. |
| `ZLINK_EVENT_CONNECT_RETRIED` | `0x0004` | 비동기 연결 재시도 진행 중. |
| `ZLINK_EVENT_LISTENING` | `0x0008` | 소켓이 성공적으로 바인딩되어 수신 대기 중. |
| `ZLINK_EVENT_BIND_FAILED` | `0x0010` | 바인딩 시도 실패. |
| `ZLINK_EVENT_ACCEPTED` | `0x0020` | 수신 연결 수락됨. |
| `ZLINK_EVENT_ACCEPT_FAILED` | `0x0040` | 수신 연결 수락 실패. |
| `ZLINK_EVENT_CLOSED` | `0x0080` | 연결이 정상적으로 닫힘. |
| `ZLINK_EVENT_CLOSE_FAILED` | `0x0100` | 연결 닫기 실패. |
| `ZLINK_EVENT_DISCONNECTED` | `0x0200` | 세션 연결 해제됨. 이벤트 값에 `ZLINK_DISCONNECT_*` 사유가 포함됨. |
| `ZLINK_EVENT_MONITOR_STOPPED` | `0x0400` | 모니터가 중지되어 더 이상 이벤트를 생성하지 않음. |
| `ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL` | `0x0800` | 추가 세부 정보 없이 핸드셰이크 실패. |
| `ZLINK_EVENT_CONNECTION_READY` | `0x1000` | 연결이 데이터 전송 준비 완료 (핸드셰이크 완료). |
| `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL` | `0x2000` | 프로토콜 에러로 인한 핸드셰이크 실패. 이벤트 값에 `ZLINK_PROTOCOL_ERROR_*` 코드가 포함됨. |
| `ZLINK_EVENT_HANDSHAKE_FAILED_AUTH` | `0x4000` | 인증 실패로 인한 핸드셰이크 실패. |
| `ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED` | `0x8000` | SUB subscription 전파 완료. `value` `1`=ready, `0`=lost. |
| `ZLINK_EVENT_PUB_DELIVERY_READY_CHANGED` | `0x10000` | PUB subscriber 준비 완료. `value` `1`=ready, `0`=lost. |
| `ZLINK_EVENT_ALL` | `0xFFFF` | 모든 이벤트 구독. |

### 연결 해제 사유

이벤트가 `ZLINK_EVENT_DISCONNECTED`일 때 `zlink_monitor_event_t.value`에 포함되는 값입니다.

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_DISCONNECT_UNKNOWN` | `0` | 사유를 확인할 수 없음. |
| `ZLINK_DISCONNECT_HANDSHAKE_FAILED` | `3` | 핸드셰이크 실패로 인한 연결 해제. |
| `ZLINK_DISCONNECT_TRANSPORT_ERROR` | `4` | 트랜스포트 계층 에러로 인한 연결 해제. |
| `ZLINK_DISCONNECT_CTX_TERM` | `5` | Context 종료로 인한 연결 해제. |

### 프로토콜 에러

이벤트가 `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL`일 때 `zlink_monitor_event_t.value`에 포함되는 값입니다.

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | `0x10000013` | 잘못된 형식의 ZMP HELLO 명령. |

## 소켓 모니터 함수

### zlink_socket_monitor_open

소켓 모니터 핸들을 recv 모델로 엽니다.

```c
void *zlink_socket_monitor_open (
  void *s_, const zlink_socket_monitor_open_options_t *options_);
```

소켓 `s_`에 모니터를 생성하고 핸들을 반환합니다. `options_->events`
비트마스크로 관찰할 이벤트를 선택합니다. 모니터는 **recv 모델**로 시작합니다.
`zlink_socket_monitor_recv()`로 이벤트를 가져오거나,
`zlink_socket_monitor_handler()`를 호출하여 callback 모델로 전환할 수 있습니다.

**반환값:** 성공 시 모니터 핸들, 실패 시 NULL (errno가 설정됨).

**스레드 안전성:** 소켓을 소유한 스레드에서 호출해야 합니다.

**참고:** `zlink_socket_monitor_recv`, `zlink_socket_monitor_handler`,
`zlink_monitor_close`

---

### zlink_socket_monitor_handler

소켓 모니터에 콜백 핸들러를 부착합니다 (단방향 전환).

```c
int zlink_socket_monitor_handler (
  void *monitor_,
  zlink_socket_monitor_handler_fn handler_,
  void *userdata_);
```

모니터를 recv 모델에서 **callback-only 모델**로 전환합니다. 부착 후
`zlink_socket_monitor_recv()`는 `errno = EBUSY`와 함께 `-1`을 반환합니다.
이 전환은 단방향이며 되돌릴 수 없습니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

---

### zlink_socket_monitor_recv

recv 모델에서 소켓 모니터의 다음 이벤트를 수신합니다.

```c
int zlink_socket_monitor_recv (
  void *monitor_, zlink_socket_monitor_event_t *out_);
```

다음 pending 이벤트를 `out_`에 읽어옵니다.
`zlink_socket_monitor_handler()`를 통해 callback 모델로 전환된 경우
`errno = EBUSY`와 함께 `-1`을 반환합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

---

### zlink_monitor_snapshot

```c
int zlink_monitor_snapshot(void *monitor_, zlink_monitor_snapshot_t *out_);
```

socket/service monitor handle의 현재 aggregate snapshot을 읽습니다.
queue 값은 조회 시점에 source에서 직접 읽어오며, `rcv_pending_msgs`는
여전히 approximate 값입니다. recv 모델과 callback 모델 모두에서 동작합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_socket_monitor_open`, `zlink_service_monitor_open`

---

### zlink_monitor_ignore_handler

소켓 모니터 이벤트 콜백을 억제하는 no-op 핸들러입니다.

```c
void zlink_monitor_ignore_handler (
  const zlink_monitor_event_t *event_, void *userdata_);
```

callback 모델로 전환하되 모든 이벤트를 버리고 싶을 때 (예: snapshot 접근만
필요하고 이벤트 스트림을 무시할 때) `zlink_socket_monitor_handler()`에
전달합니다.

---

### zlink_monitor_close

모든 모니터 핸들(소켓 또는 서비스)을 닫고 리소스를 해제합니다.

```c
int zlink_monitor_close (void **monitor_p_);
```

모니터를 닫고 `*monitor_p_`를 `NULL`로 설정합니다. 다른 스레드가 모니터
콜백을 실행 중이면 `errno = EBUSY`로 실패합니다. 콜백 내에서의 self-close는
성공하며, 콜백 반환 후까지 지연됩니다.

**모든** 모니터 타입 -- 소켓 모니터와 서비스 모니터 모두 -- 을 위한 통합 close
함수입니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**참고:** `zlink_socket_monitor_open`, `zlink_service_monitor_open`

---

## 서비스 모니터 API

서비스 모니터는 서비스 계층 컴포넌트(Discovery, Gateway, SPOT)의 상태 전이
이벤트를 제공합니다. transport 레벨 이벤트를 보고하는 소켓 모니터와 달리,
서비스 모니터는 readiness, route 변화, SPOT 필터 적용 등의 상위 수준 이벤트를
보고합니다.

`zlink_service_monitor_open()`의 target은 모든 서비스 핸들(Discovery,
Gateway, Spot, SpotNode)을 받습니다. 서비스 종류는 핸들의 runtime tag에서
결정되며, per-service open 함수나 `role` 파라미터는 없습니다. 내부
pub/sub 구조는 숨겨집니다.

### zlink_service_event_t / zlink_service_monitor_event_t

단일 서비스 모니터 이벤트를 설명합니다.
`zlink_service_monitor_event_t`는 `zlink_service_event_t`의 typedef입니다.

```c
typedef struct zlink_service_event_t
{
    zlink_service_kind_t service_kind;
    uint32_t event_type;
    int32_t status;
    int32_t error_code;
    uint32_t value;
    zlink_service_event_detail_mask_t detail_flags;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    char subject[256];
    uint32_t subject_kind;
} zlink_service_event_t;

typedef zlink_service_event_t zlink_service_monitor_event_t;
```

| 필드 | 설명 |
|------|------|
| `service_kind` | 컴포넌트 타입을 식별하는 `ZLINK_SERVICE_KIND_*` 상수 중 하나. |
| `event_type` | 이벤트를 나타내는 비트마스크 (아래 서비스 이벤트 상수 중 하나). |
| `status` | 이벤트별 상태 코드. |
| `error_code` | `event_type`이 실패를 나타낼 때의 에러 코드. |
| `value` | 이벤트별 숫자 값. |
| `detail_flags` | 선택적 필드가 채워졌는지를 나타내는 `ZLINK_EVENT_DETAIL_*` 플래그 비트마스크. |
| `service_name` | null 종료 서비스 이름. `ZLINK_EVENT_DETAIL_SERVICE_NAME`이 설정될 때 유효. |
| `endpoint` | null 종료 엔드포인트. `ZLINK_EVENT_DETAIL_ENDPOINT`가 설정될 때 유효. |
| `routing_id` | 주체 또는 피어의 라우팅 아이덴티티. 해당 detail 플래그가 설정될 때 유효. |
| `subject` | null 종료 subject 문자열. `ZLINK_EVENT_DETAIL_SUBJECT`가 설정될 때 유효. |
| `subject_kind` | subject 종류. `ZLINK_EVENT_DETAIL_SUBJECT_KIND`가 설정될 때 유효. |

### zlink_service_monitor_handler_fn

```c
typedef void (*zlink_service_monitor_handler_fn) (
  const zlink_service_event_t *event_, void *userdata_);
```

서비스 모니터 이벤트 콜백. I/O 스레드에서 호출됩니다.

### zlink_service_monitor_open_options_t

```c
typedef struct zlink_service_monitor_open_options_t
{
    zlink_service_monitor_event_mask_t events;
} zlink_service_monitor_open_options_t;
```

| 필드 | 설명 |
|------|------|
| `events` | 관찰할 이벤트를 선택하는 서비스 모니터 이벤트 플래그 비트마스크. 모든 서비스 모니터의 통합 마스크 타입인 `zlink_service_monitor_event_mask_t`를 사용합니다. |

### 서비스 종류 상수

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_SERVICE_KIND_DISCOVERY` | 1 | Discovery 컴포넌트 |
| `ZLINK_SERVICE_KIND_GATEWAY` | 2 | Gateway 컴포넌트 |
| `ZLINK_SERVICE_KIND_SPOT_SUB` | 3 | SPOT Subscriber 컴포넌트 |
| `ZLINK_SERVICE_KIND_SPOT_PUB` | 4 | SPOT Publisher 컴포넌트 |

### 서비스 이벤트 상수

#### 공통 이벤트

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_MONITOR_EVENT_READY` | `1 << 0` | 서비스 준비 완료 |
| `ZLINK_MONITOR_EVENT_LOST` | `1 << 1` | 서비스 연결 손실 |
| `ZLINK_MONITOR_EVENT_PEER_UP` | `1 << 2` | 피어 연결됨 |
| `ZLINK_MONITOR_EVENT_PEER_DOWN` | `1 << 3` | 피어 연결 해제됨 |
| `ZLINK_MONITOR_EVENT_ERROR` | `1 << 4` | 에러 발생 |
| `ZLINK_MONITOR_EVENT_CLOSED` | `1 << 17` | 모니터 닫힘 |

#### Discovery 이벤트

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_DISCOVERY_SERVICE_UP` | `1 << 5` | 검색된 서비스가 활성화됨 |
| `ZLINK_DISCOVERY_SERVICE_DOWN` | `1 << 6` | 검색된 서비스가 비활성화됨 |
| `ZLINK_DISCOVERY_PROVIDERS_CHANGED` | `1 << 7` | 서비스 provider 집합이 변경됨 |

#### Gateway 이벤트

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_GATEWAY_SERVICE_READY` | `1 << 8` | 로컬 Gateway service bind/register 준비 완료 |
| `ZLINK_GATEWAY_SERVICE_LOST` | `1 << 9` | 로컬 Gateway service publication 제거됨 |
| `ZLINK_GATEWAY_SEND_READY_CHANGED` | `1 << 10` | aggregate send readiness 변화, `value`는 `0` 또는 `1` |
| `ZLINK_GATEWAY_ROUTE_UP` | `1 << 11` | 피어로의 경로 활성화됨, `value`는 현재 ready route 수 |
| `ZLINK_GATEWAY_ROUTE_DOWN` | `1 << 12` | 피어로의 경로 비활성화됨, `value`는 현재 ready route 수 |

#### SPOT 이벤트

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_SPOT_SUB_FILTER_APPLIED` | `1 << 13` | 구독 필터 적용됨 |
| `ZLINK_SPOT_SUB_SUBSCRIPTION_READY` | `1 << 14` | 구독 수신 준비 완료 |
| `ZLINK_SPOT_PUB_QUEUE_FULL` | `1 << 15` | PUB 큐가 가득 참 |
| `ZLINK_SPOT_PUB_QUEUE_DRAINED` | `1 << 16` | PUB 큐가 비워짐 |
| `ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED` | `1 << 18` | subject별 remote delivery-ready 카운트 변화 |
| `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED` | `1 << 19` | subject별 delivery-ready 상태 변화 |
| `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED` | `1 << 20` | publisher 기준 first-delivery-safe ready 카운트 변화 |

#### 서비스 모니터 이벤트 마스크 상수

다음 `ZLINK_SERVICE_MONITOR_EVENT_*` 상수는
`zlink_service_monitor_open_options_t.events` 비트마스크를 구성할 때 사용하는
canonical 이름입니다. 위 per-service 상수와 동일한 비트에 매핑됩니다.

| 상수 | 매핑 대상 |
|------|----------|
| `ZLINK_SERVICE_MONITOR_EVENT_ERROR` | `ZLINK_MONITOR_EVENT_ERROR` |
| `ZLINK_SERVICE_MONITOR_EVENT_CLOSED` | `ZLINK_MONITOR_EVENT_CLOSED` |
| `ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP` | `ZLINK_DISCOVERY_SERVICE_UP` |
| `ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN` | `ZLINK_DISCOVERY_SERVICE_DOWN` |
| `ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_SERVICE_READY` | `ZLINK_GATEWAY_SERVICE_READY` |
| `ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_SEND_READY_CHANGED` | `ZLINK_GATEWAY_SEND_READY_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_ROUTE_UP` | `ZLINK_GATEWAY_ROUTE_UP` |
| `ZLINK_SERVICE_MONITOR_EVENT_GATEWAY_ROUTE_DOWN` | `ZLINK_GATEWAY_ROUTE_DOWN` |
| `ZLINK_SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED` | `ZLINK_SPOT_SUB_FILTER_APPLIED` |
| `ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY` | `ZLINK_SPOT_SUB_SUBSCRIPTION_READY` |
| `ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUB_DELIVERY_READY_CHANGED` | `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_SPOT_FIRST_DELIVERY_READY_CHANGED` | `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_ALL` | 모든 서비스 이벤트 |

### Detail 플래그 상수

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_EVENT_DETAIL_SERVICE_NAME` | `0x0001` | `service_name` 필드가 채워짐 |
| `ZLINK_EVENT_DETAIL_ENDPOINT` | `0x0002` | `endpoint` 필드가 채워짐 |
| `ZLINK_EVENT_DETAIL_SUBJECT_RID` | `0x0004` | `routing_id`에 주체 아이덴티티 포함 |
| `ZLINK_EVENT_DETAIL_PEER_RID` | `0x0008` | `routing_id`에 피어 아이덴티티 포함 |
| `ZLINK_EVENT_DETAIL_SUBJECT` | `0x0010` | `subject` 필드가 채워짐 |
| `ZLINK_EVENT_DETAIL_SUBJECT_KIND` | `0x0020` | `subject_kind` 필드가 채워짐 |

---

### zlink_service_monitor_open

통합 서비스 모니터를 recv 모델로 엽니다.

```c
void *zlink_service_monitor_open (
  void *target_,
  const zlink_service_monitor_open_options_t *options_);
```

모든 서비스 핸들에 서비스 모니터를 생성하고 핸들을 반환합니다. `target_`은
Discovery, Gateway, Spot, SpotNode 핸들을 받을 수 있으며, 서비스 종류는
핸들의 runtime tag에서 결정됩니다. Spot 및 SpotNode target의 경우 내부
pub/sub 구조가 숨겨지며, `role` 파라미터는 없습니다.

`options_->events` 비트마스크로 관찰할 이벤트를 선택하며, 통합 마스크 타입인
`zlink_service_monitor_event_mask_t`를 사용합니다.

모니터는 **recv 모델**로 시작합니다. `zlink_service_monitor_recv()`로
이벤트를 가져오거나, `zlink_service_monitor_handler()`를 호출하여 callback
모델로 전환할 수 있습니다.

**반환값:** 성공 시 모니터 핸들, 실패 시 `NULL` (errno가 설정됨).

**스레드 안전성:** 모니터 handle 자체는 thread-safe child handle입니다.

**참고:** `zlink_service_monitor_recv`, `zlink_service_monitor_handler`,
`zlink_monitor_close`

---

### zlink_service_monitor_handler

서비스 모니터에 콜백 핸들러를 부착합니다 (단방향 전환).

```c
int zlink_service_monitor_handler (
  void *monitor_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_);
```

모니터를 recv 모델에서 **callback-only 모델**로 전환합니다. 부착 후
`zlink_service_monitor_recv()`는 `errno = EBUSY`와 함께 `-1`을 반환합니다.
이 전환은 단방향이며 되돌릴 수 없습니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

---

### zlink_service_monitor_recv

recv 모델에서 서비스 모니터의 다음 이벤트를 수신합니다.

```c
int zlink_service_monitor_recv (
  void *monitor_, zlink_service_monitor_event_t *out_);
```

다음 pending 이벤트를 `out_`에 읽어옵니다.
`zlink_service_monitor_handler()`를 통해 callback 모델로 전환된 경우
`errno = EBUSY`와 함께 `-1`을 반환합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).
