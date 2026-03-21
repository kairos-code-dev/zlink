[English](events.md) | [한국어](events.ko.md)

# 이벤트 카탈로그

이 문서는 raw socket monitor 이벤트와 service monitor 이벤트의 canonical
catalog입니다.

사용 기준:
- [monitoring.ko.md](monitoring.ko.md): monitor API와 peer inspection API
- 이 문서: 이벤트 의미, payload 필드, 권장 gate
- [socket-family-monitor-contract-spec.ko.md](../plan/direct-callback-recv/socket-family-monitor-contract-spec.ko.md):
  패밀리별 제어 가능 범위와 회귀 테스트 기준

## Service Event 모델

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
```

필드 의미:
- `value`는 이벤트별 숫자 값입니다. 모든 `*_READY_CHANGED` 이벤트에서는
  `current_ready_count` (절대 readiness 카운트)를 뜻합니다.
- `subject`는 `detail_flags`에 `ZLINK_EVENT_DETAIL_SUBJECT`가 있을 때만
  유효합니다.
- `subject_kind`는 `detail_flags`에
  `ZLINK_EVENT_DETAIL_SUBJECT_KIND`가 있을 때만 유효합니다.
- `routing_id`는 `SUBJECT_RID` 또는 `PEER_RID`가 있을 때만 유효합니다.

subject kind 상수:
- `ZLINK_SERVICE_EVENT_SUBJECT_NONE`
- `ZLINK_SERVICE_EVENT_SUBJECT_TOPIC`
- `ZLINK_SERVICE_EVENT_SUBJECT_PATTERN`

detail flag:
- `ZLINK_EVENT_DETAIL_SERVICE_NAME`
- `ZLINK_EVENT_DETAIL_ENDPOINT`
- `ZLINK_EVENT_DETAIL_SUBJECT_RID`
- `ZLINK_EVENT_DETAIL_PEER_RID`
- `ZLINK_EVENT_DETAIL_SUBJECT`
- `ZLINK_EVENT_DETAIL_SUBJECT_KIND`

## semantic level

- `PEER_UP` / `PEER_DOWN`: 연결 수준
- `SUB_FILTER_APPLIED`: local subscriber filter 설치 완료
- `SUBSCRIPTION_READY_CHANGED`: subscriber 쪽 subscription readiness 변화
- `*_DELIVERY_READY_CHANGED`: 특정 subject에 대해 첫 delivery 보장 가능 상태

권장 gate:
- publisher는 `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED`에서 `value >= 1`을
  기다린 뒤 publish 시작
- subscriber는 `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED`에서 `value == 1`을
  기다린 뒤 측정/통신 시작
- `PEER_UP`를 first-delivery gate로 쓰지 않음

## Raw Socket Monitor 이벤트

| 상수 | 의미 |
|---|---|
| `ZLINK_EVENT_CONNECTED` | outbound 연결 수립 |
| `ZLINK_EVENT_CONNECT_DELAYED` | 동기 connect 실패 후 재시도 예약 |
| `ZLINK_EVENT_CONNECT_RETRIED` | 비동기 재시도 진행 중 |
| `ZLINK_EVENT_LISTENING` | bind/listen 활성 |
| `ZLINK_EVENT_BIND_FAILED` | bind 실패 |
| `ZLINK_EVENT_ACCEPTED` | inbound 연결 수락 |
| `ZLINK_EVENT_ACCEPT_FAILED` | accept 실패 |
| `ZLINK_EVENT_CLOSED` | 정상 close |
| `ZLINK_EVENT_CLOSE_FAILED` | close 실패 |
| `ZLINK_EVENT_DISCONNECTED` | 세션 연결 해제 |
| `ZLINK_EVENT_MONITOR_STOPPED` | socket monitor 종료 |
| `ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL` | 상세 정보 없는 handshake 실패 |
| `ZLINK_EVENT_CONNECTION_READY_CHANGED` | transport handshake readiness 변화 |
| `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL` | protocol handshake 오류 |
| `ZLINK_EVENT_HANDSHAKE_FAILED_AUTH` | auth handshake 오류 |

disconnect reason:
- `ZLINK_DISCONNECT_UNKNOWN`
- `ZLINK_DISCONNECT_HANDSHAKE_FAILED`
- `ZLINK_DISCONNECT_TRANSPORT_ERROR`
- `ZLINK_DISCONNECT_CTX_TERM`

## Service Monitor 이벤트

### 공통

| 상수 | 의미 |
|---|---|
| `ZLINK_MONITOR_EVENT_PEER_UP` | 피어 연결 |
| `ZLINK_MONITOR_EVENT_PEER_DOWN` | 피어 연결 해제 |
| `ZLINK_MONITOR_EVENT_ERROR` | 오류 발생 |
| `ZLINK_MONITOR_EVENT_CLOSED` | monitor terminal 이벤트 |

### Discovery

| 상수 | 의미 |
|---|---|
| `ZLINK_DISCOVERY_SERVICE_UP` | provider 가용 |
| `ZLINK_DISCOVERY_SERVICE_DOWN` | provider 소실 |
| `ZLINK_DISCOVERY_PROVIDERS_CHANGED` | provider 집합 변경 |

### Gateway

| 상수 | 의미 |
|---|---|
| `ZLINK_GATEWAY_MONITOR_EVENT_READY_CHANGED` | 로컬 service readiness 변화, `value`는 current_ready_count |
| `ZLINK_GATEWAY_SEND_READY_CHANGED` | Gateway send readiness 변화, `value`는 current_ready_count |
| `ZLINK_GATEWAY_ROUTE_UP` | route 활성화, `value`는 current_ready_count |
| `ZLINK_GATEWAY_ROUTE_DOWN` | route 비활성화, `value`는 current_ready_count |

### SPOT

| 상수 | 발생 주체 | 의미 |
|---|---|---|
| `ZLINK_SPOT_SUB_FILTER_APPLIED` | Spot sub / node-sub monitor | local filter 설치 완료 |
| `ZLINK_SPOT_SUB_SUBSCRIPTION_READY_CHANGED` | Spot sub / node-sub monitor | subscription readiness 변화, `value`는 current_ready_count |
| `ZLINK_SPOT_PUB_QUEUE_FULL` | Spot pub / node-pub monitor | PUB 큐가 가득 참 |
| `ZLINK_SPOT_PUB_QUEUE_DRAINED` | Spot pub / node-pub monitor | PUB 큐가 비워짐 |
| `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED` | Spot sub / node-sub monitor | subject별 delivery-ready 상태 변화, `value`는 current_ready_count |
| `ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED` | Spot pub / node-pub monitor | subject별 remote delivery-ready 카운트 변화, `value`는 current_ready_count |
| `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED` | Spot pub / node-pub monitor | first-delivery-safe ready 카운트 변화, `value`는 current_ready_count; publisher 제어 gate로 사용 |

SPOT subject 규칙:
- sub 쪽은 exact topic / pattern에 대해 `subject_kind`가 채워집니다.
- pub 쪽은 `subject`는 채우지만, remote subscription frame만으로는 exact와
  pattern origin을 항상 복원할 수 없어 `subject_kind`가 비어 있을 수 있습니다.
- pattern subscription은 sub-side monitor에서 public API와 동일하게 trailing
  `*`를 포함한 문자열로 노출됩니다.

## 예시

subscriber gate:

```c
if (event->event_type == ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
    && (event->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) != 0
    && strcmp(event->subject, "bench") == 0
    && event->value == 1) {
    /* 이제 첫 publish를 받을 수 있다 */
}
```

publisher gate:

```c
if (event->event_type == ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED
    && (event->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) != 0
    && strcmp(event->subject, "bench") == 0
    && event->value >= 1) {
    /* 이제 첫 publish를 보낼 수 있다 */
}
```
