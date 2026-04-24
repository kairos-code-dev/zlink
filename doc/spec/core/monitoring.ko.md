[English](monitoring.md) | [한국어](monitoring.ko.md)

[스펙 목차](../README.ko.md) · [코어 목차](README.ko.md)

# 모니터링 API 레퍼런스

canonical 이벤트 카탈로그는 [events.ko.md](events.ko.md)에 정리합니다.
이 문서는 monitor API, callback, monitor snapshot 중심으로 봅니다.

## API 구조

모니터링은 두 가지 클래스로 분리됩니다.

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

transport/socket 진단은 소켓 모니터를 사용하고, 공개 service-monitor
surface를 유지하는 서비스의 상태 전이는 서비스 모니터를 사용합니다.
현재 대표 대상은 Discovery입니다.

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
| `value` | 이벤트별 값 (아래 참조). |
| `routing_id` | peer-bound 이벤트의 피어 라우팅 아이덴티티. peer-less 이벤트에서도 초기화되며 0일 수 있음. |
| `local_addr` | null 종료 로컬 엔드포인트 주소 문자열. 항상 초기화됨. |
| `remote_addr` | null 종료 원격 엔드포인트 주소 문자열. 항상 초기화됨. |

`value` 필드 해석:
- 다수 failure 이벤트: errno 또는 프로토콜 에러 코드
- 연결 해제 이벤트: `ZLINK_DISCONNECT_*` 사유
- `CONNECTION_READY`: 예약된 필드이며 aggregate ready count로 해석하지 않음

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
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
    uint32_t auto_hwm_enabled;
    uint32_t auto_hwm_role;
    uint32_t auto_hwm_managed_connections;
    uint32_t auto_hwm_active_hwm_connections;
    uint32_t auto_hwm_planning_transport_connections;
    uint32_t auto_hwm_base_floor_per_connection;
    int32_t auto_hwm_applied_sndhwm;
    int32_t auto_hwm_applied_rcvhwm;
    int32_t auto_hwm_requested_sndbuf;
    int32_t auto_hwm_requested_rcvbuf;
    int32_t auto_hwm_effective_sndbuf;
    int32_t auto_hwm_effective_rcvbuf;
    uint64_t auto_hwm_total_memory_budget_bytes;
    uint64_t auto_hwm_queue_budget_bytes;
    uint64_t auto_hwm_transport_budget_bytes;
    uint64_t auto_hwm_runtime_reserve_bytes;
    uint64_t auto_hwm_group_budget_bytes;
    uint64_t auto_hwm_group_message_slots;
    uint64_t auto_hwm_effective_message_bytes;
} zlink_monitor_snapshot_t;
```

| 필드 | 설명 |
|------|------|
| `source_kind` | snapshot source(`SOCKET`, `SPOT_PUB`, `SPOT_SUB`) |
| `state_flags` | `READY`, `BOUND_READY`, `CLOSED` 같은 aggregate 상태. `READY`는 raw socket monitor source에서만 지원되며 `CONNECTION_READY`와 같은 계약을 가짐 |
| `detail_flags` | 어떤 numeric field가 채워졌는지 표시 |
| `snd_pending_msgs` | 지원되는 경우 aggregate 로컬 송신 backlog |
| `rcv_pending_msgs` | 지원되는 경우 aggregate 로컬 수신 backlog snapshot |
| `auto_hwm_enabled` | 이 source가 자동 HWM 정책을 기준으로 계산 중이면 `1`, 아니면 `0` |
| `auto_hwm_role` | 자동 HWM 진단용 역할 묶음 번호. 현재 `1=control`, `2=routed`, `3=fanout`, `4=recv_ingress`이며 새 값이 추가될 수 있음 |
| `auto_hwm_managed_connections` | 현재 정책이 계산에 사용한 연결 수 |
| `auto_hwm_active_hwm_connections` | HWM 분배에 실제로 나눈 연결 수 |
| `auto_hwm_planning_transport_connections` | transport buffer 계획에 사용한 연결 수 |
| `auto_hwm_base_floor_per_connection` | 역할별 최소 floor 값 |
| `auto_hwm_applied_sndhwm` | 현재 소켓에 적용된 송신 HWM |
| `auto_hwm_applied_rcvhwm` | 현재 소켓에 적용된 수신 HWM |
| `auto_hwm_requested_sndbuf` | 자동 정책이 요청한 `SNDBUF` 값 |
| `auto_hwm_requested_rcvbuf` | 자동 정책이 요청한 `RCVBUF` 값 |
| `auto_hwm_effective_sndbuf` | 현재 snapshot에서 보는 유효 `SNDBUF` 값 |
| `auto_hwm_effective_rcvbuf` | 현재 snapshot에서 보는 유효 `RCVBUF` 값 |
| `auto_hwm_total_memory_budget_bytes` | context 총 메모리 예산 |
| `auto_hwm_queue_budget_bytes` | HWM 계산에 쓴 큐 예산 |
| `auto_hwm_transport_budget_bytes` | transport buffer 계산에 쓴 예산 |
| `auto_hwm_runtime_reserve_bytes` | runtime reserve 예산 |
| `auto_hwm_group_budget_bytes` | 현재 역할 묶음에 배정된 큐 예산 |
| `auto_hwm_group_message_slots` | 현재 역할 묶음 예산을 실효 메시지 크기로 나눈 슬롯 수 |
| `auto_hwm_effective_message_bytes` | 정책이 계산에 사용한 실효 메시지 바이트 |

## 상수

### Monitor Source Kind

```c
typedef enum zlink_monitor_source_kind_t
{
    ZLINK_MONITOR_SOURCE_SOCKET   = 1,
    ZLINK_MONITOR_SOURCE_SPOT_PUB = 3,
    ZLINK_MONITOR_SOURCE_SPOT_SUB = 4
} zlink_monitor_source_kind_t;
```

### Monitor State Mask

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_MONITOR_STATE_READY` | `1 << 0` | raw socket의 ready level. `SOCKET`은 usable connection 존재를 뜻하며 SPOT source는 `READY`를 사용하지 않음. |
| `ZLINK_MONITOR_STATE_BOUND_READY` | `1 << 1` | source에 성공적인 bind가 있음. |
| `ZLINK_MONITOR_STATE_CLOSED` | `1 << 3` | source가 닫힘. |

### Monitor Snapshot Detail Mask

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS` | `1 << 1` | `snd_pending_msgs` 필드가 채워짐. |
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS` | `1 << 2` | `rcv_pending_msgs` 필드가 채워짐. |
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_AUTO_HWM_BUDGET` | `1 << 3` | auto HWM budget/role/HWM 관련 필드가 채워짐. |
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_AUTO_HWM_BUFFERS` | `1 << 4` | auto HWM transport buffer 관련 필드가 채워짐. |

### 이벤트 플래그

관찰할 이벤트를 선택하기 위해 `zlink_socket_monitor_open_options_t.events`에
전달되는 비트마스크 상수입니다. 여러 플래그를 비트 OR로 결합할 수 있습니다.

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_EVENT_CONNECTED` | `0x0001` | 원격 피어에 대한 연결이 수립됨. |
| `ZLINK_EVENT_CONNECT_DELAYED` | `0x0002` | 동기 연결 실패; 비동기 재시도 예약됨. |
| `ZLINK_EVENT_CONNECT_RETRIED` | `0x0004` | 비동기 연결 재시도 진행 중. |
| `ZLINK_EVENT_LISTENING` | `0x0008` | 소켓이 성공적으로 바인딩되어 수신 대기 중. |
| `ZLINK_EVENT_BIND_FAILED` | `0x0010` | 바인딩 시도 실패. |
| `ZLINK_EVENT_ACCEPTED` | `0x0020` | 수신 연결 수락됨. |
| `ZLINK_EVENT_ACCEPT_FAILED` | `0x0040` | 수신 연결 수락 실패. |
| `ZLINK_EVENT_CLOSED` | `0x0080` | 연결이 정상적으로 닫힘. |
| `ZLINK_EVENT_CLOSE_FAILED` | `0x0100` | 연결 닫기 실패. |
| `ZLINK_EVENT_DISCONNECTED` | `0x0200` | 세션 연결 해제됨. |
| `ZLINK_EVENT_MONITOR_STOPPED` | `0x0400` | 모니터 중지됨. |
| `ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL` | `0x0800` | 핸드셰이크 실패 (상세 없음). |
| `ZLINK_EVENT_CONNECTION_READY` | `0x1000` | raw socket의 ready edge. 지원 raw socket 패밀리에서는 이 이벤트 이후 즉시 메시징을 시작할 수 있음. |
| `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL` | `0x2000` | 프로토콜 에러로 핸드셰이크 실패. |
| `ZLINK_EVENT_HANDSHAKE_FAILED_AUTH` | `0x4000` | 인증 실패로 핸드셰이크 실패. |
| `ZLINK_EVENT_PEER_WEIGHT_CHANGED` | `0x8000` | 연결된 raw peer의 가중치 변화. `value`에 새 `0..100` 가중치가 들어간다. `ZLINK_SOCKET_MONITOR_EVENT_PEER_WEIGHT_CHANGED`의 별칭이다. |
| `ZLINK_EVENT_ALL` | `0xFFFF` | 모든 이벤트 구독. |

### 연결 해제 사유

`ZLINK_EVENT_DISCONNECTED` 이벤트의 `value` 필드 값입니다.

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_DISCONNECT_UNKNOWN` | `0` | 사유를 확인할 수 없음. |
| `ZLINK_DISCONNECT_HANDSHAKE_FAILED` | `3` | 핸드셰이크 실패. |
| `ZLINK_DISCONNECT_TRANSPORT_ERROR` | `4` | 트랜스포트 계층 에러. |
| `ZLINK_DISCONNECT_CTX_TERM` | `5` | Context 종료로 인한 연결 해제. |

### 프로토콜 에러

`ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL` 이벤트의 `value` 필드 값입니다.

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | `0x10000013` | 잘못된 ZMP HELLO 명령. |

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
zlink_handler_result_t zlink_socket_monitor_handler (
  void *monitor_,
  zlink_socket_monitor_handler_fn handler_,
  void *userdata_);
```

모니터를 recv 모델에서 **callback-only 모델**로 전환합니다. 부착 후
`zlink_socket_monitor_recv()`는 callback 모드임을 나타내는
`zlink_recv_result_t` 값을 반환합니다. 이 전환은 단방향이며 되돌릴 수
없습니다.

**반환값:** 성공 시 `ZLINK_HANDLER_OK`, 실패 시 `zlink_handler_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

---

### zlink_socket_monitor_recv

recv 모델에서 소켓 모니터의 다음 이벤트를 수신합니다.

```c
zlink_recv_result_t zlink_socket_monitor_recv (
  void *monitor_, zlink_socket_monitor_event_t *out_,
  zlink_recv_flags_t flags_);
```

다음 pending 이벤트를 `out_`에 읽어옵니다. `flags_` 매개변수는 논블로킹
동작을 위해 `ZLINK_DONTWAIT`를 받습니다.
`zlink_socket_monitor_handler()`를 통해 callback 모델로 전환된 경우
callback 모드임을 나타내는 `zlink_recv_result_t` 값을 반환합니다.

**반환값:** 성공 시 `ZLINK_RECV_OK`, 실패 시 `zlink_recv_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

---

### zlink_monitor_snapshot

```c
zlink_config_result_t zlink_monitor_snapshot(void *monitor_, zlink_monitor_snapshot_t *out_);
```

socket/service monitor handle의 현재 aggregate snapshot을 읽습니다.
queue 값은 조회 시점에 source에서 직접 읽어오며, `rcv_pending_msgs`는
approximate 값입니다. recv 모델과 callback 모델 모두에서 동작합니다.
자동 HWM이 켜진 source라면 같은 snapshot에서 HWM floor, 계산에 사용한
예산, 요청한 transport buffer 값을 함께 읽을 수 있습니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

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
zlink_close_result_t zlink_monitor_close (void **monitor_p_);
```

모니터를 닫고 `*monitor_p_`를 `NULL`로 설정합니다. 다른 스레드가 모니터
콜백을 실행 중이면 `errno = EBUSY`로 실패합니다. 콜백 내에서의 self-close는
성공하며, 콜백 반환 후까지 지연됩니다.

**모든** 모니터 타입 -- 소켓 모니터와 서비스 모니터 모두 -- 을 위한 통합 close
함수입니다.

**반환값:** 성공 시 `ZLINK_CLOSE_OK`, 실패 시 `zlink_close_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_socket_monitor_open`, `zlink_service_monitor_open`

---

## 서비스 모니터 API

서비스 모니터는 공개 service-monitor surface를 유지하는 서비스 계층
컴포넌트의 상태 전이 이벤트를 제공합니다. transport 레벨 이벤트를 보고하는
소켓 모니터와 달리, 서비스 모니터는 Discovery membership 변화 같은
상위 수준 이벤트를 보고합니다.

`zlink_service_monitor_open()`의 target은 공개 service monitor를 제공하는
서비스 핸들입니다. 현재 대상은 Discovery, Spot, SpotNode입니다. 서비스
종류는 핸들의 runtime tag에서 결정되며, per-service open 함수나 `role`
파라미터는 없습니다.

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
| `detail_flags` | `ZLINK_EVENT_DETAIL_*` 플래그 비트마스크. |
| `service_name` | null 종료 서비스 이름 (`DETAIL_SERVICE_NAME` 시 유효). |
| `endpoint` | null 종료 엔드포인트 (`DETAIL_ENDPOINT` 시 유효). |
| `routing_id` | 주체/피어 라우팅 아이덴티티 (해당 detail 플래그 시 유효). |
| `subject` | null 종료 subject (`DETAIL_SUBJECT` 시 유효). |
| `subject_kind` | subject 종류 (`DETAIL_SUBJECT_KIND` 시 유효). |

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
| `events` | 관찰할 이벤트 플래그 비트마스크 (`zlink_service_monitor_event_mask_t`). |

### 지원되는 서비스 모니터 대상

`zlink_service_monitor_open()`은 현재 공개 service-monitor surface를 가진
handle에 대해서만 정의된다. 모니터 대상 식별자는
`zlink_monitor_target_kind_t`로 정의된다.

| 대상 | `zlink_monitor_target_kind_t` | 공개 recv surface |
|------|-------------------------------|------------------|
| `Discovery` handle | `ZLINK_MONITOR_TARGET_DISCOVERY = 2` | `zlink_service_monitor_recv()` |
| raw socket | `ZLINK_MONITOR_TARGET_SOCKET = 1` | `zlink_socket_monitor_recv()` |
| `Spot` facade | `ZLINK_MONITOR_TARGET_SPOT = 4` | `zlink_service_monitor_recv()` |
| `SpotNode` handle | `ZLINK_MONITOR_TARGET_SPOT_NODE = 5` | `zlink_service_monitor_recv()` |

`Spot`과 `SpotNode`는 generic service monitor surface를 통해 운영형 이벤트를
노출합니다. `SpotNode`를 대상으로 `zlink_service_monitor_open()`을 열고
`zlink_service_monitor_recv()`로 이벤트를 꺼냅니다. 반환되는
`zlink_service_monitor_event_t`에 표준 monitor event 필드가 담깁니다.
`SpotNode` 전용 별도 monitor recv API는 없습니다.

### 서비스 종류 상수

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_SERVICE_KIND_DISCOVERY` | 1 | Discovery 컴포넌트 |
| `ZLINK_SERVICE_KIND_SPOT_SUB` | 3 | SPOT sub-side service monitor event source |
| `ZLINK_SERVICE_KIND_SPOT_PUB` | 4 | SPOT pub-side service monitor event source |
| `ZLINK_SERVICE_KIND_SOCKET` | 5 | 소켓 패밀리 서비스 |

### 서비스 이벤트 상수

#### 공통 이벤트

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_MONITOR_EVENT_ERROR` | `1 << 4` | 에러 발생 |
| `ZLINK_MONITOR_EVENT_CLOSED` | `1 << 17` | 모니터 닫힘 |

#### Discovery 이벤트

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_DISCOVERY_SERVICE_UP` | `1 << 5` | 검색된 서비스가 활성화됨 |
| `ZLINK_DISCOVERY_SERVICE_DOWN` | `1 << 6` | 검색된 서비스가 비활성화됨 |
| `ZLINK_DISCOVERY_PROVIDERS_CHANGED` | `1 << 7` | 서비스 provider 집합이 변경됨 |

#### 서비스 모니터 이벤트 마스크 상수

`ZLINK_SERVICE_MONITOR_EVENT_*` 상수는
`zlink_service_monitor_open_options_t.events` 비트마스크 구성에 사용합니다.
위 per-service 상수와 동일한 비트에 매핑됩니다.

**공통:**
- `..._EVENT_ERROR` -> `ZLINK_MONITOR_EVENT_ERROR`
- `..._EVENT_CLOSED` -> `ZLINK_MONITOR_EVENT_CLOSED`

**Discovery:**
- `..._DISCOVERY_SERVICE_UP` -> `ZLINK_DISCOVERY_SERVICE_UP`
- `..._DISCOVERY_SERVICE_DOWN` -> `ZLINK_DISCOVERY_SERVICE_DOWN`
- `..._DISCOVERY_PROVIDERS_CHANGED` -> `ZLINK_DISCOVERY_PROVIDERS_CHANGED`

**서비스 공통:**
- `ZLINK_SERVICE_MONITOR_EVENT_PEER_WEIGHT_CHANGED` (`1u << 8`) --
  Discovery service monitor가 추적 중인 peer의 가중치가 바뀌면
  이 비트를 사용합니다. 현재 `Spot` / `SpotNode` generic service monitor는
  이 이벤트를 방출하지 않습니다.

- `ZLINK_SERVICE_MONITOR_EVENT_ALL` -> 모든 서비스 이벤트

### Spot / SpotNode Generic Events

`zlink_service_monitor_open(spot, ...)`와
`zlink_service_monitor_open(spot_node, ...)`는 SPOT pub/sub
monitor bridge가 제공하는 운영형 이벤트만 지원합니다.

| 이벤트 | `Spot` | `SpotNode` | 설명 |
|--------|--------|------------|------|
| `ZLINK_MONITOR_EVENT_ERROR` | 지원 | 지원 | runtime / bridge 에러 |
| `ZLINK_MONITOR_EVENT_CLOSED` | 지원 | 지원 | 종료 이벤트 |
| `peer up` (`1u << 2`) | 지원 | 지원 | peer 사용 가능 상태 진입 |
| `peer down` (`1u << 3`) | 지원 | 지원 | peer 사용 불가 상태 진입 |
| `connection ready` (`1u << 14`) | 지원 | 지원 | data path 준비 완료 |
| `sub filter applied` (`1u << 13`) | 지원 | 지원 | sub filter 적용 완료 |
| `pub queue full` (`1u << 15`) | 지원 | 지원 | pub queue 포화 |
| `pub queue drained` (`1u << 16`) | 지원 | 지원 | pub queue 회복 |

`SpotNode` monitor event는 generic `zlink_service_monitor_recv()`
surface로 꺼냅니다. 반환되는 이벤트에 표준 monitor 필드가 담기며,
peer 수준 상세는 snapshot/query API(`zlink_spot_node_peers_snapshot()`,
`zlink_spot_node_peers_query()`)에서 별도로 확인합니다.

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

공개 service monitor를 제공하는 서비스 핸들에 서비스 모니터를 생성하고
핸들을 반환합니다. `target_`은 Discovery 핸들, Spot facade,
SpotNode handle을 받을 수 있습니다.

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
zlink_handler_result_t zlink_service_monitor_handler (
  void *monitor_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_);
```

모니터를 recv 모델에서 **callback-only 모델**로 전환합니다. 부착 후
`zlink_service_monitor_recv()`는 callback 모드임을 나타내는
`zlink_recv_result_t` 값을 반환합니다. 이 전환은 단방향이며 되돌릴 수
없습니다.

**반환값:** 성공 시 `ZLINK_HANDLER_OK`, 실패 시 `zlink_handler_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

---

### zlink_service_monitor_recv

recv 모델에서 서비스 모니터의 다음 이벤트를 수신합니다.

```c
zlink_recv_result_t zlink_service_monitor_recv (
  void *monitor_, zlink_service_monitor_event_t *out_,
  zlink_recv_flags_t flags_);
```

다음 pending 이벤트를 `out_`에 읽어옵니다. `flags_` 매개변수는 논블로킹
동작을 위해 `ZLINK_DONTWAIT`를 받습니다.
`zlink_service_monitor_handler()`를 통해 callback 모델로 전환된 경우
callback 모드임을 나타내는 `zlink_recv_result_t` 값을 반환합니다.

**반환값:** 성공 시 `ZLINK_RECV_OK`, 실패 시 `zlink_recv_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.
