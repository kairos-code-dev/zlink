[English](monitoring.md) | [한국어](monitoring.ko.md)

[스펙 목차](../README.ko.md) · [코어 목차](README.ko.md)

# 모니터링 API 레퍼런스

canonical 이벤트 카탈로그는 [events.ko.md](events.ko.md)에 정리합니다.
이 문서는 monitor API, callback, monitor snapshot 중심으로 봅니다.

## API 구조

공개 모니터링 클래스는 하나입니다.

- 소켓 모니터링:
  `zlink_socket_monitor_open()` + `zlink_socket_monitor_open_options_t`

소켓 모니터는 같은 **recv/callback 전달 모델**을 따릅니다.

1. **Open** -- 모니터는 **recv 모델**로 시작합니다. 해당 `*_recv()` 함수로
   이벤트를 가져옵니다.
2. **핸들러 부착** -- `*_handler()`를 호출하면 **callback-only 모델**로 전환됩니다
   (단방향). 전환 후 `*_recv()`는 `EBUSY`를 반환합니다.
3. **Snapshot** -- `zlink_monitor_snapshot()`은 두 모델 모두에서 동작합니다.

모든 모니터는 `zlink_monitor_close()`로 닫습니다.

transport/socket 진단은 소켓 모니터를 사용합니다. 서비스 계층 관찰은 별도
공개 monitor handle 대신 Discovery, Registry, SPOT의 snapshot/query API로
처리합니다.

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
    uint32_t auto_hwm_profile;
    uint32_t auto_hwm_role;
    uint32_t auto_hwm_policy_class;
    uint64_t auto_hwm_unit_budget_bytes;
    uint32_t auto_hwm_size_cap;
    uint64_t auto_hwm_socket_message_slots;
    uint64_t auto_hwm_effective_message_bytes;
    int32_t auto_hwm_applied_sndhwm;
    int32_t auto_hwm_applied_rcvhwm;
    uint64_t auto_hwm_last_recalc_ms;
    uint32_t auto_hwm_last_recalc_reason;
    uint32_t auto_hwm_send_blocked_ratio_ppm;
    int32_t auto_hwm_deferred_sndhwm;
    int32_t auto_hwm_deferred_rcvhwm;
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
| `auto_hwm_profile` | 현재 자동 HWM profile. 값은 `zlink_auto_hwm_profile_t`와 같다 |
| `auto_hwm_role` | 자동 HWM 진단용 역할 번호. 현재 `1=control`, `2=routed`, `3=fanout`, `4=recv_ingress`, `5=spot_data`, `6=peer_queue`, `7=stream`이며 새 값이 추가될 수 있음 |
| `auto_hwm_policy_class` | 단위 예산과 size cap 선택에 사용한 planner policy class. 진단용 값이며 새 값이 추가될 수 있음 |
| `auto_hwm_unit_budget_bytes` | 현재 profile과 policy class에서 고른 연결당 단위 예산 |
| `auto_hwm_size_cap` | 현재 profile, policy class, 실효 메시지 크기에서 고른 메시지 수 상한 |
| `auto_hwm_socket_message_slots` | 선택된 단위 예산과 실효 메시지 단위로 계산한 메시지 슬롯 수 |
| `auto_hwm_effective_message_bytes` | 정책이 계산에 사용한 실효 메시지 단위 바이트 |
| `auto_hwm_applied_sndhwm` | 현재 소켓에 적용된 송신 HWM |
| `auto_hwm_applied_rcvhwm` | 현재 소켓에 적용된 수신 HWM |
| `auto_hwm_last_recalc_ms` | 최근 자동 HWM 재계산 시각(ms) |
| `auto_hwm_last_recalc_reason` | 최근 재계산 사유 enum 값 |
| `auto_hwm_send_blocked_ratio_ppm` | 최근 송신 시도 중 backpressure(배압)로 막힌 비율(ppm) |
| `auto_hwm_deferred_sndhwm` | 지연 중인 송신 HWM 축소값. 없으면 `-1` |
| `auto_hwm_deferred_rcvhwm` | 지연 중인 수신 HWM 축소값. 없으면 `-1` |

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
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_AUTO_HWM_BUDGET` | `1 << 3` | auto-HWM role, profile, unit budget, message unit, 적용 HWM 필드가 채워질 수 있음. |
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_AUTO_HWM_BUFFERS` | `1 << 4` | ABI 안정을 위해 남긴 호환 flag. transport buffer 필드는 더 이상 `zlink_monitor_snapshot_t`에 없으므로 현재 snapshot은 이 flag를 설정하지 않음. |

### Auto-HWM 재계산 사유

| 상수 | 값 | 설명 |
|------|----|------|
| `ZLINK_AUTO_HWM_RECALC_REASON_NONE` | `0` | 재계산 사유 없음 |
| `ZLINK_AUTO_HWM_RECALC_REASON_INITIAL` | `1` | 초기 계산 |
| `ZLINK_AUTO_HWM_RECALC_REASON_ROLE_CHANGE` | `2` | 소켓 역할 변경 |
| `ZLINK_AUTO_HWM_RECALC_REASON_POLICY_TOGGLE` | `3` | 자동 HWM 정책 활성/비활성 변경 |
| `ZLINK_AUTO_HWM_RECALC_REASON_REFRESH` | `4` | 일반 refresh |
| `ZLINK_AUTO_HWM_RECALC_REASON_DEFERRED_SHRINK` | `5` | 현재 pending 메시지가 새 HWM보다 많아서 HWM 축소를 지연함 |

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

socket monitor handle의 현재 aggregate snapshot을 읽습니다.
queue 값은 조회 시점에 source에서 직접 읽어오며, `rcv_pending_msgs`는
approximate 값입니다. recv 모델과 callback 모델 모두에서 동작합니다.
자동 HWM이 켜진 source라면 같은 snapshot에서 HWM floor, 계산에 사용한
예산, 요청한 transport buffer 값을 함께 읽을 수 있습니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_socket_monitor_open`

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

소켓 모니터 핸들을 닫고 리소스를 해제합니다.

```c
zlink_close_result_t zlink_monitor_close (void **monitor_p_);
```

모니터를 닫고 `*monitor_p_`를 `NULL`로 설정합니다. 다른 스레드가 모니터
콜백을 실행 중이면 `errno = EBUSY`로 실패합니다. 콜백 내에서의 self-close는
성공하며, 콜백 반환 후까지 지연됩니다.

**반환값:** 성공 시 `ZLINK_CLOSE_OK`, 실패 시 `zlink_close_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_socket_monitor_open`

---

## 서비스 계층 관찰

Discovery, Registry, SPOT runtime 상태는 별도 공개 이벤트 스트림이 아니라
snapshot/query API로 관찰합니다.

- Discovery: `zlink_discovery_member_peers()`,
  `zlink_discovery_resolve_spot()`, `zlink_discovery_resolve_actor()`
- Registry: `zlink_registry_status_snapshot()`,
  `zlink_registry_service_summary_snapshot()`,
  `zlink_registry_topology_snapshot()`,
  `zlink_registry_topology_query()`
- SpotNode: `zlink_spot_node_status_snapshot()`,
  `zlink_spot_node_peers_snapshot()`,
  `zlink_spot_node_peers_query()`,
  `zlink_spot_node_subjects_snapshot()`

상태 전이를 감지하려면 애플리케이션에서 연속된 snapshot 또는 query 결과를
비교합니다. 이렇게 해야 `core/include/zlink.h` 기준의 현재 공개 계약과
문서가 정확히 일치합니다. 공개 monitor handle은 소켓 monitor만 제공합니다.
