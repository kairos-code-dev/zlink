[English](monitoring.md) | [한국어](monitoring.ko.md)

# 모니터링 & 피어 정보 API 레퍼런스

모니터링 API를 사용하면 연결, 연결 해제, 핸드셰이크 실패 등의 소켓 생명주기 이벤트를 관찰할 수 있습니다. 피어 정보 API는 ROUTER 소켓에 현재 연결된 피어 집합에 대한 피어별 메시지 카운터 및 연결 타임스탬프를 포함한 인트로스펙션을 제공합니다.

## 타입

### zlink_monitor_event_t

소켓 모니터 핸들에서 수신된 단일 모니터 이벤트를 설명합니다.

```c
typedef struct {
    uint64_t event;
    uint64_t value;
    zlink_routing_id_t routing_id;
    char local_addr[256];
    char remote_addr[256];
} zlink_monitor_event_t;
```

| 필드 | 설명 |
|------|------|
| `event` | 이벤트 타입을 나타내는 비트마스크 (`ZLINK_EVENT_*` 상수 중 하나). |
| `value` | 이벤트별 값. 연결 이벤트의 경우 파일 디스크립터; 에러 이벤트의 경우 errno 또는 프로토콜 에러 코드; 연결 해제 이벤트의 경우 `ZLINK_DISCONNECT_*` 사유. |
| `routing_id` | 해당되는 경우 이벤트에 관련된 피어의 라우팅 아이덴티티. |
| `local_addr` | null 종료 로컬 엔드포인트 주소 문자열. |
| `remote_addr` | null 종료 원격 엔드포인트 주소 문자열. |

### zlink_peer_info_t

단일 연결된 피어에 대한 정보를 포함합니다.

```c
typedef struct {
    zlink_routing_id_t routing_id;
    char remote_addr[256];
    uint64_t connected_time;
    uint64_t msgs_sent;
    uint64_t msgs_received;
} zlink_peer_info_t;
```

| 필드 | 설명 |
|------|------|
| `routing_id` | 피어의 라우팅 아이덴티티. |
| `remote_addr` | 피어의 null 종료 원격 주소. |
| `connected_time` | 피어가 연결된 시점의 타임스탬프 (에포크 밀리초). |
| `msgs_sent` | 이 피어에 송신된 메시지 수. |
| `msgs_received` | 이 피어로부터 수신된 메시지 수. |

## 상수

### 이벤트 플래그

관찰할 이벤트를 선택하기 위해 `zlink_socket_monitor()` 또는 `zlink_socket_monitor_open()`에 전달되는 비트마스크 상수입니다. 여러 플래그를 비트 OR로 결합할 수 있습니다.

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
| `ZLINK_EVENT_ALL` | `0xFFFF` | 모든 이벤트 구독. |

### 연결 해제 사유

이벤트가 `ZLINK_EVENT_DISCONNECTED`일 때 `zlink_monitor_event_t.value`에 포함되는 값입니다.

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_DISCONNECT_UNKNOWN` | `0` | 사유를 확인할 수 없음. |
| `ZLINK_DISCONNECT_LOCAL` | `1` | 로컬 측에서 연결 해제를 시작함. |
| `ZLINK_DISCONNECT_REMOTE` | `2` | 원격 피어에서 연결 해제를 시작함. |
| `ZLINK_DISCONNECT_HANDSHAKE_FAILED` | `3` | 핸드셰이크 실패로 인한 연결 해제. |
| `ZLINK_DISCONNECT_TRANSPORT_ERROR` | `4` | 트랜스포트 계층 에러로 인한 연결 해제. |
| `ZLINK_DISCONNECT_CTX_TERM` | `5` | Context 종료로 인한 연결 해제. |

### 프로토콜 에러

이벤트가 `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL`일 때 `zlink_monitor_event_t.value`에 포함되는 값입니다.

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_PROTOCOL_ERROR_ZMP_UNSPECIFIED` | `0x10000000` | 지정되지 않은 ZMP 프로토콜 에러. |
| `ZLINK_PROTOCOL_ERROR_ZMP_UNEXPECTED_COMMAND` | `0x10000001` | 예기치 않은 ZMP 명령 수신. |
| `ZLINK_PROTOCOL_ERROR_ZMP_INVALID_SEQUENCE` | `0x10000002` | 유효하지 않은 ZMP 명령 시퀀스. |
| `ZLINK_PROTOCOL_ERROR_ZMP_KEY_EXCHANGE` | `0x10000003` | ZMP 키 교환 실패. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_UNSPECIFIED` | `0x10000011` | 잘못된 형식의 ZMP 명령 (지정되지 않음). |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_MESSAGE` | `0x10000012` | 잘못된 형식의 ZMP MESSAGE 명령. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | `0x10000013` | 잘못된 형식의 ZMP HELLO 명령. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_INITIATE` | `0x10000014` | 잘못된 형식의 ZMP INITIATE 명령. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_ERROR` | `0x10000015` | 잘못된 형식의 ZMP ERROR 명령. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_READY` | `0x10000016` | 잘못된 형식의 ZMP READY 명령. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_WELCOME` | `0x10000017` | 잘못된 형식의 ZMP WELCOME 명령. |
| `ZLINK_PROTOCOL_ERROR_ZMP_INVALID_METADATA` | `0x10000018` | 유효하지 않은 ZMP 메타데이터. |
| `ZLINK_PROTOCOL_ERROR_ZMP_CRYPTOGRAPHIC` | `0x11000001` | ZMP 암호화 에러. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MECHANISM_MISMATCH` | `0x11000002` | ZMP 보안 메커니즘 불일치. |
| `ZLINK_PROTOCOL_ERROR_WS_UNSPECIFIED` | `0x30000000` | 지정되지 않은 WebSocket 프로토콜 에러. |

## 함수

### zlink_socket_monitor

inproc 주소를 통해 소켓 모니터를 시작합니다. 이벤트를 수신하기 위해 별도의 SUB 소켓을 생성해야 하는 레거시 방식입니다.

```c
int zlink_socket_monitor(void *s_, const char *addr_, int events_);
```

소켓 `s_`에 모니터를 등록하여 inproc 엔드포인트 `addr_`에 이벤트를 발행합니다. `events_` 비트마스크와 일치하는 이벤트만 발행됩니다. `ZLINK_PAIR` 소켓을 생성하여 `addr_`에 연결하고 모니터 이벤트 프레임을 수동으로 수신해야 합니다.

새 코드에서는 바로 사용 가능한 모니터 핸들을 반환하는 `zlink_socket_monitor_open()`을 사용하세요.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**스레드 안전성:** 소켓을 소유한 스레드에서 호출해야 합니다.

**참고:** `zlink_socket_monitor_open`, `zlink_monitor_recv`

---

### zlink_socket_monitor_open

소켓 모니터 핸들을 직접 열고 반환합니다. 소켓 이벤트를 모니터링하는 데 권장되는 방식입니다.

```c
void *zlink_socket_monitor_open(void *s_, int events_);
```

소켓 `s_`에 모니터를 생성하고 불투명 모니터 핸들을 반환합니다. 핸들을 `zlink_monitor_recv()`에 직접 전달하여 구조화된 이벤트 데이터를 수신할 수 있습니다. `events_` 비트마스크와 일치하는 이벤트만 전달됩니다. 완료 후 모니터 핸들은 `zlink_close()`로 닫으세요.

**반환값:** 성공 시 모니터 핸들, 실패 시 NULL (errno가 설정됨).

**스레드 안전성:** 소켓을 소유한 스레드에서 호출해야 합니다.

**참고:** `zlink_monitor_recv`, `zlink_close`

---

### zlink_monitor_recv

모니터 핸들에서 이벤트를 수신합니다.

```c
int zlink_monitor_recv(void *monitor_socket_, zlink_monitor_event_t *event_, int flags_);
```

`monitor_socket_`(`zlink_socket_monitor_open()`에서 얻은)에서 모니터 이벤트를 사용할 수 있을 때까지 블로킹한 후 `event_` 구조체를 채웁니다. 대기 중인 이벤트가 없는 경우 즉시 `EAGAIN`과 함께 반환하려면 `flags_`에 `ZLINK_DONTWAIT`를 전달합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:**

- `EAGAIN` -- 사용 가능한 이벤트가 없고 `ZLINK_DONTWAIT`가 지정됨.
- `ETERM` -- Context가 종료되었습니다.

**스레드 안전성:** 모니터 핸들을 소유한 스레드에서 호출해야 합니다.

**참고:** `zlink_socket_monitor_open`, `zlink_monitor_event_t`

---

### zlink_socket_peer_info

라우팅 아이덴티티로 피어 정보를 가져옵니다.

```c
int zlink_socket_peer_info(void *socket_, const zlink_routing_id_t *routing_id_, zlink_peer_info_t *info_);
```

주어진 ROUTER 소켓에서 `routing_id_`로 식별되는 피어를 조회하고 `info_` 구조체에 주소, 연결 시간, 메시지 카운터를 채웁니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:**

- `EINVAL` -- 라우팅 아이덴티티를 찾을 수 없거나 소켓이 ROUTER가 아닙니다.

**스레드 안전성:** 소켓을 소유한 스레드에서 호출해야 합니다.

**참고:** `zlink_socket_peer_routing_id`, `zlink_socket_peers`

---

### zlink_socket_peer_routing_id

인덱스로 피어의 라우팅 아이덴티티를 가져옵니다.

```c
int zlink_socket_peer_routing_id(void *socket_, int index_, zlink_routing_id_t *out_);
```

소켓의 내부 피어 테이블에서 위치 `index_` (0부터 시작)에 있는 피어의 라우팅 아이덴티티를 가져옵니다. 모든 피어를 순회하려면 `zlink_socket_peer_count()`와 함께 사용합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:**

- `EINVAL` -- 인덱스가 범위를 벗어나거나 소켓이 ROUTER가 아닙니다.

**스레드 안전성:** 소켓을 소유한 스레드에서 호출해야 합니다.

**참고:** `zlink_socket_peer_count`, `zlink_socket_peer_info`

---

### zlink_socket_peer_count

연결된 피어 수를 반환합니다.

```c
int zlink_socket_peer_count(void *socket_);
```

ROUTER 소켓 `socket_`에 현재 연결된 피어 수를 반환합니다. 피어가 연결 및 연결 해제됨에 따라 호출 사이에 카운트가 변경될 수 있습니다.

**반환값:** 연결된 피어 수 (>= 0), 또는 실패 시 -1 (errno가 설정됨).

**스레드 안전성:** 소켓을 소유한 스레드에서 호출해야 합니다.

**참고:** `zlink_socket_peer_routing_id`, `zlink_socket_peers`

---

### zlink_socket_peers

연결된 모든 피어의 정보를 배열로 가져옵니다.

```c
int zlink_socket_peers(void *socket_, zlink_peer_info_t *peers_, size_t *count_);
```

ROUTER 소켓에 연결된 모든 피어의 정보를 `peers_` 배열에 채웁니다. 입력 시 `*count_`는 배열의 용량을 포함해야 합니다. 출력 시 `*count_`는 기록된 실제 피어 수로 설정됩니다. 배열이 너무 작은 경우 호출은 성공하지만 처음 `*count_`(입력) 항목만 기록되며 `*count_`(출력)는 연결된 총 피어 수를 반영합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:**

- `EINVAL` -- 소켓이 ROUTER가 아니거나, `peers_` 또는 `count_`가 NULL입니다.

**스레드 안전성:** 소켓을 소유한 스레드에서 호출해야 합니다.

**참고:** `zlink_socket_peer_count`, `zlink_socket_peer_info`
