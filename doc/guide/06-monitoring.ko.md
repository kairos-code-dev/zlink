[English](06-monitoring.md) | [한국어](06-monitoring.ko.md)

# 모니터링 API 사용법

## 1. 개요

zlink 모니터링 API는 소켓의 연결/해제/핸드셰이크 등 이벤트를 실시간으로 관찰할 수 있다. 콜백 기반으로 동작하며, 이벤트 발생 시 등록된 핸들러 함수가 자동으로 호출된다.

패밀리별 control contract와 회귀 테스트 기준은
[socket-family-monitor-contract-spec.ko.md](../plan/direct-callback-recv/socket-family-monitor-contract-spec.ko.md)에
별도로 정리한다. 이 가이드는 실제 사용 시 어떤 event를 gate로 써야 하는지에
집중한다.

## 2. 모니터 활성화

### 2.1 콜백 기반 (권장)

```c
/* 이벤트 핸들러 정의 */
void on_monitor_event(const zlink_monitor_event_t *ev)
{
    printf("이벤트: 0x%llx\n", (unsigned long long)ev->event);
    printf("로컬: %s\n", ev->local_addr);
    printf("원격: %s\n", ev->remote_addr);

    if (ev->routing_id.size > 0) {
        printf("routing_id: ");
        for (uint8_t i = 0; i < ev->routing_id.size; ++i)
            printf("%02x", ev->routing_id.data[i]);
        printf("\n");
    }
}

void *server = zlink_socket(ctx, ZLINK_ROUTER, NULL);
zlink_bind(server, "tcp://*:5555");

/* 모니터 생성 (핸들러 등록) */
void *mon = zlink_socket_monitor_open(server, ZLINK_EVENT_ALL,
                                       on_monitor_event);
```

이벤트 발생 시 `on_monitor_event` 콜백이 자동으로 호출된다.

### 2.2 수동 설정 (레거시)

```c
zlink_socket_monitor(server, "inproc://monitor", ZLINK_EVENT_ALL);

void *mon = zlink_socket(ctx, ZLINK_PAIR, NULL);
zlink_connect(mon, "inproc://monitor");
```

### 이벤트 구조체

```c
typedef struct {
    uint64_t event;               /* 이벤트 타입 */
    uint64_t value;               /* 보조 값 (fd, errno, reason 등) */
    zlink_routing_id_t routing_id; /* 상대방 routing_id */
    char local_addr[256];         /* 로컬 주소 */
    char remote_addr[256];        /* 원격 주소 */
} zlink_monitor_event_t;
```

## 4. 이벤트 타입

### 요약

| 이벤트 | 값 | `value` 필드 | `routing_id` | 발생 측 |
|--------|-----|-------------|:------------:|:-------:|
| `CONNECTED` | `0x0001` | fd | 없음 | 클라이언트 |
| `CONNECT_DELAYED` | `0x0002` | errno | 없음 | 클라이언트 |
| `CONNECT_RETRIED` | `0x0004` | — | 없음 | 클라이언트 |
| `LISTENING` | `0x0008` | fd | 없음 | 서버 |
| `BIND_FAILED` | `0x0010` | errno | 없음 | 서버 |
| `ACCEPTED` | `0x0020` | fd | 없음 | 서버 |
| `ACCEPT_FAILED` | `0x0040` | errno | 없음 | 서버 |
| `CLOSED` | `0x0080` | — | 없음 | 양쪽 |
| `CLOSE_FAILED` | `0x0100` | errno | 없음 | 양쪽 |
| `DISCONNECTED` | `0x0200` | reason 코드 | 가능 | 양쪽 |
| `MONITOR_STOPPED` | `0x0400` | — | 없음 | 양쪽 |
| `HANDSHAKE_FAILED_NO_DETAIL` | `0x0800` | errno | 없음 | 양쪽 |
| `CONNECTION_READY` | `0x1000` | — | 가능 | 양쪽 |
| `HANDSHAKE_FAILED_PROTOCOL` | `0x2000` | 프로토콜 에러 코드 | 없음 | 양쪽 |
| `HANDSHAKE_FAILED_AUTH` | `0x4000` | — | 없음 | 양쪽 |

> 참고: `core/tests/testutil_monitoring.cpp` — `get_zlinkEventName()` 이벤트 이름 매핑

### 4.1 연결 생명주기 이벤트

#### CONNECTED (`0x0001`)

TCP 연결이 성립되었을 때 **클라이언트 측**에서 발생한다. 이 시점에서는 전송 계층 연결만 완료된 상태이며, zlink 핸드셰이크는 아직 수행되지 않았다.

- **`value`**: 새 연결의 파일 디스크립터.
- **`routing_id`**: 사용 불가 (비어 있음).
- **`local_addr`**: 로컬 TCP 엔드포인트 (예: `tcp://192.168.1.10:54321`).
- **`remote_addr`**: 원격 TCP 엔드포인트 (예: `tcp://192.168.1.20:5555`).
- **다음 이벤트**: 성공 시 `CONNECTION_READY`, 실패 시 `HANDSHAKE_FAILED_*` 또는 `DISCONNECTED`.

#### ACCEPTED (`0x0020`)

리스닝 소켓이 수신 TCP 연결을 accept했을 때 **서버 측**에서 발생한다. `CONNECTED`와 마찬가지로 zlink 핸드셰이크는 아직 수행되지 않은 상태이다.

- **`value`**: accept된 연결의 파일 디스크립터.
- **`routing_id`**: 사용 불가 (비어 있음). ID는 핸드셰이크 완료 후 할당된다.
- **`local_addr`**: 리스닝 엔드포인트 주소.
- **`remote_addr`**: 원격 피어 주소.
- **다음 이벤트**: 성공 시 `CONNECTION_READY`, 실패 시 `HANDSHAKE_FAILED_*` 또는 `DISCONNECTED`.
- **제어 규칙**: transport 수락 관찰에는 써도 되지만, business message 송신
  또는 first-delivery gate로 쓰면 안 된다.

#### CONNECTION_READY (`0x1000`)

zlink 핸드셰이크가 성공적으로 완료되어 데이터 전송이 가능한 상태가 되었을 때 발생한다. 애플리케이션 수준의 연결 추적에 가장 중요한 이벤트이다.

- **`value`**: 사용되지 않음.
- **`routing_id`**: ROUTER 소켓의 경우 사용 가능 — 피어에 할당된 라우팅 ID를 포함한다.
- **`local_addr`**: 로컬 엔드포인트 주소.
- **`remote_addr`**: 원격 엔드포인트 주소.
- **일반적 용도**: 피어 등록, 메시지 전송 시작, `zlink_monitor_snapshot()`을
  통한 aggregate queue/readiness 상태 조회.
- **패밀리 규칙**:
  - `PAIR`, `DEALER/ROUTER`, `STREAM`: raw first-I/O gate로 사용 가능
  - `PUB/SUB`: transport/session readiness까지만 뜻하며, 첫 publish delivery
    gate로 사용하면 안 됨

#### DISCONNECTED (`0x0200`)

수립된 세션이 종료될 때 발생한다. 연결 생명주기의 어느 단계에서든 발생할 수 있다.

- **`value`**: `ZLINK_DISCONNECT_*` reason 코드 ([6장](#6-disconnected-reason-코드) 참조).
- **`routing_id`**: 핸드셰이크가 완료된 경우 (즉, 이 피어에 대해 `CONNECTION_READY`가 이전에 발생한 경우) 사용 가능.
- **`local_addr`**: 로컬 엔드포인트 주소.
- **`remote_addr`**: 원격 엔드포인트 주소.
- **일반적 용도**: 재연결 로직 트리거, 피어 상태 업데이트, 해제 사유 로깅.

#### CLOSED (`0x0080`)

`zlink_close()` 또는 `zlink_disconnect()`를 통해 연결이 정상적으로 닫힐 때 발생한다.

- **`value`**: 사용되지 않음.
- **`routing_id`**: 사용 불가 (비어 있음).
- **참고**: `DISCONNECTED`와 달리, 예기치 않은 세션 종료가 아닌 의도적인 로컬 close 작업을 나타낸다.

#### CLOSE_FAILED (`0x0100`)

연결 close 작업이 실패했을 때 발생한다.

- **`value`**: 실패를 설명하는 `errno` 값.
- **`routing_id`**: 사용 불가 (비어 있음).
- **참고**: 실제로는 드물게 발생한다. 리소스 정리 중 내부 오류를 나타낼 수 있다.

### 4.2 클라이언트 측 연결 이벤트

#### CONNECT_DELAYED (`0x0002`)

동기 connect 시도가 즉시 완료되지 못하고 비동기 재시도가 예약되었을 때 **클라이언트 측**에서 발생한다.

- **`value`**: 초기 connect 시도의 `errno` (일반적으로 `EINPROGRESS`).
- **`routing_id`**: 사용 불가 (비어 있음).
- **`remote_addr`**: 대상 엔드포인트 주소.
- **다음 이벤트**: 연결 성공 시 `CONNECTED`, 이후 재시도 시 `CONNECT_RETRIED`.

#### CONNECT_RETRIED (`0x0004`)

비동기 재연결 시도가 진행 중일 때 **클라이언트 측**에서 발생한다. 이전의 `CONNECT_DELAYED` 또는 `DISCONNECTED` 이벤트 이후에 발생한다.

- **`value`**: 사용되지 않음.
- **`routing_id`**: 사용 불가 (비어 있음).
- **`remote_addr`**: 대상 엔드포인트 주소.
- **일반적 순서**: `DISCONNECTED` → `CONNECT_DELAYED` → `CONNECT_RETRIED` → `CONNECTED` → `CONNECTION_READY`.

### 4.3 바인드 측 이벤트

#### LISTENING (`0x0008`)

`zlink_bind()`가 성공하여 소켓이 수신 연결을 대기 중일 때 **서버 측**에서 발생한다.

- **`value`**: 리스닝 소켓의 파일 디스크립터.
- **`routing_id`**: 사용 불가 (비어 있음).
- **`local_addr`**: 바인드된 엔드포인트 주소 (예: `tcp://0.0.0.0:5555`).

#### BIND_FAILED (`0x0010`)

`zlink_bind()`가 실패했을 때 **서버 측**에서 발생한다.

- **`value`**: 실패를 설명하는 `errno` 값 (예: `EADDRINUSE`).
- **`routing_id`**: 사용 불가 (비어 있음).
- **`local_addr`**: 바인드 실패한 주소.
- **일반적 원인**: 포트 사용 중, 권한 부족, 잘못된 주소.

#### ACCEPT_FAILED (`0x0040`)

수신 연결 accept가 실패했을 때 **서버 측**에서 발생한다.

- **`value`**: 실패를 설명하는 `errno` 값.
- **`routing_id`**: 사용 불가 (비어 있음).
- **일반적 원인**: 파일 디스크립터 한도 초과 (`EMFILE`), 리소스 부족.

### 4.4 핸드셰이크 실패 이벤트

TCP 연결이 성립된 후 zlink 프로토콜 핸드셰이크가 실패할 때 발생하는 이벤트들이다.

#### HANDSHAKE_FAILED_NO_DETAIL (`0x0800`)

프로토콜별 정보 없이 발생하는 일반적인 핸드셰이크 실패.

- **`value`**: 실패 시점의 `errno` 값.
- **`routing_id`**: 사용 불가 (비어 있음).
- **일반적 원인**: 핸드셰이크 중 연결 리셋, 예기치 않은 소켓 종료, 타임아웃.

#### HANDSHAKE_FAILED_PROTOCOL (`0x2000`)

ZMP 또는 WebSocket 프로토콜 오류로 핸드셰이크가 실패. `value` 필드에 구체적인 프로토콜 에러 코드가 포함된다.

- **`value`**: `ZLINK_PROTOCOL_ERROR_*` 코드 (아래 [프로토콜 에러 코드](#프로토콜-에러-코드) 참조).
- **`routing_id`**: 사용 불가 (비어 있음).
- **일반적 원인**: 버전 불일치, 잘못된 커맨드, 유효하지 않은 메타데이터, 암호화 오류.

#### HANDSHAKE_FAILED_AUTH (`0x4000`)

인증 또는 보안 메커니즘 실패로 핸드셰이크가 실패.

- **`value`**: 사용되지 않음.
- **`routing_id`**: 사용 불가 (비어 있음).
- **일반적 원인**: TLS 인증서 검증 실패, 보안 메커니즘 불일치, 유효하지 않은 자격 증명.

### 4.5 모니터 제어 이벤트

#### MONITOR_STOPPED (`0x0400`)

`zlink_socket_monitor(socket, NULL, 0)` 호출로 모니터가 중지될 때 발생한다. 이 이벤트 이후에는 더 이상 이벤트가 발생하지 않는다.

- **`value`**: 사용되지 않음.
- **`routing_id`**: 사용 불가 (비어 있음).
- **참고**: 모니터가 마지막으로 발생시키는 이벤트이다. 수신 후 `zlink_close()`로 모니터 핸들을 닫아야 한다.

### 프로토콜 에러 코드

`HANDSHAKE_FAILED_PROTOCOL` 발생 시 `value` 필드에 다음 코드 중 하나가 포함된다:

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | `0x10000013` | 잘못된 형식의 ZMP HELLO 커맨드. |

## 5. 이벤트 흐름 다이어그램

### 연결 성공

```
클라이언트 측:
  CONNECT_DELAYED (선택) → CONNECTED → CONNECTION_READY

서버 측:
  ACCEPTED → CONNECTION_READY
```

### 핸드셰이크 실패

```
클라이언트 측:
  CONNECTED → HANDSHAKE_FAILED_* → DISCONNECTED

서버 측:
  ACCEPTED → HANDSHAKE_FAILED_* → DISCONNECTED
```

### 정상 해제

```
CONNECTION_READY → DISCONNECTED (reason=LOCAL or REMOTE)
```

### 재연결

```
CONNECTED → CONNECTION_READY → DISCONNECTED →
CONNECT_DELAYED → CONNECT_RETRIED → CONNECTED → CONNECTION_READY
```

## 6. DISCONNECTED reason 코드

`DISCONNECTED` 이벤트의 `value` 필드에 해제 사유가 포함된다.

| 코드 | 이름 | 의미 | 대응 방법 |
|------|------|------|-----------|
| 0 | UNKNOWN | 원인 불명 | 로그 기록 후 관찰 |
| 1 | LOCAL | 로컬에서 의도적 종료 | 정상 동작, 처리 불필요 |
| 2 | REMOTE | 원격 피어 정상 종료 | 재연결 로직 실행 |
| 3 | HANDSHAKE_FAILED | 핸드셰이크 실패 | TLS/프로토콜 설정 확인 |
| 4 | TRANSPORT_ERROR | 전송계층 오류 | 네트워크 상태 확인 |
| 5 | CTX_TERM | 컨텍스트 종료 | 종료 처리 |

### reason 코드 처리 예제

```c
void on_monitor(const zlink_monitor_event_t *ev)
{
    if (ev->event == ZLINK_EVENT_DISCONNECTED) {
        switch (ev->value) {
            case 0: printf("원인 불명 해제\n"); break;
            case 1: printf("로컬 종료\n"); break;
            case 2:
                printf("원격 피어 종료 — 재연결 시도\n");
                /* 재연결 로직 */
                break;
            case 3:
                printf("핸드셰이크 실패 — TLS 설정 확인\n");
                break;
            case 4:
                printf("전송 오류 — 네트워크 확인\n");
                break;
            case 5:
                printf("컨텍스트 종료\n");
                break;
        }
    }
}
```

## 7. 이벤트 필터링 및 구독 프리셋

### 특정 이벤트만 구독

```c
/* 연결/해제 이벤트만 */
void *mon = zlink_socket_monitor_open(server,
    ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED,
    on_monitor_event);
```

### 권장 구독 프리셋

| 프리셋 | 이벤트 마스크 | 용도 |
|--------|-------------|------|
| 기본 | `CONNECTION_READY \| DISCONNECTED` | 연결 상태 추적 |
| 디버깅 | 기본 + `CONNECTED \| ACCEPTED \| CONNECT_DELAYED \| CONNECT_RETRIED` | 연결 과정 상세 |
| 보안 | 기본 + `HANDSHAKE_FAILED_*` | 인증 실패 감지 |
| 전체 | `ZLINK_EVENT_ALL` | 모든 이벤트 |

### 프리셋 구현 예제

```c
/* 기본 프리셋 */
#define MONITOR_PRESET_BASIC \
    (ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED)

/* 디버깅 프리셋 */
#define MONITOR_PRESET_DEBUG \
    (MONITOR_PRESET_BASIC | ZLINK_EVENT_CONNECTED | \
     ZLINK_EVENT_ACCEPTED | ZLINK_EVENT_CONNECT_DELAYED | \
     ZLINK_EVENT_CONNECT_RETRIED)

/* 보안 프리셋 */
#define MONITOR_PRESET_SECURITY \
    (MONITOR_PRESET_BASIC | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL | \
     ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL | \
     ZLINK_EVENT_HANDSHAKE_FAILED_AUTH)

void *mon = zlink_socket_monitor_open(server, MONITOR_PRESET_SECURITY,
                                      on_monitor_event);
```

## 8. Monitor Snapshot

### aggregate socket 상태 조회

```c
void *monitor = zlink_socket_monitor_open(socket, ZLINK_EVENT_ALL,
                                          zlink_monitor_ignore_handler);
zlink_monitor_snapshot_t snapshot;
zlink_monitor_snapshot(monitor, &snapshot);
printf("ready peers: %u, sndq=%llu, rcvq=%llu\n",
       snapshot.ready_peer_count,
       (unsigned long long) snapshot.snd_pending_msgs,
       (unsigned long long) snapshot.rcv_pending_msgs);
```

### monitor event와 snapshot 결합

```c
void on_monitor(const zlink_monitor_event_t *ev)
{
    if (ev->event == ZLINK_EVENT_CONNECTION_READY) {
        zlink_monitor_snapshot_t snapshot;
        zlink_monitor_snapshot(g_monitor, &snapshot);
        printf("현재 ready peers: %u\n", snapshot.ready_peer_count);
    }
}
```

### service monitor의 초기 gate

service overlay는 raw socket보다 한 단계 높은 의미를 가진다. 그래서
`Gateway`와 `SPOT`은 `open -> snapshot -> incremental events`를 기본 패턴으로
쓰는 것이 맞다.

- `Gateway`
  - `SERVICE_READY`는 local publication/bind 상태다.
  - 실제 첫 request gate는 monitor handle snapshot에서
    `SEND_READY`와 `ready_peer_count > 0`을 확인하는 것이다.
  - 그 이후 증감은 `SEND_READY_CHANGED`, `ROUTE_UP/DOWN`으로 받는다.
- `SPOT`
  - `FILTER_APPLIED`, `SUBSCRIPTION_READY`는 control-plane progress다.
  - subscriber 쪽 첫 receive gate는 `SUB_DELIVERY_READY_CHANGED`다.
  - publisher 쪽 첫 publish gate는 `PUB_FIRST_DELIVERY_READY_CHANGED`다.
  - snapshot은 aggregate peer/queue 상태를 읽는 용도로 함께 쓴다.

즉 "어떤 event는 제어용이고 어떤 event는 아니냐"가 아니라,
"모든 public event는 자기 레벨의 제어에는 써도 되지만 더 강한 레벨의 gate로
올려 해석하면 안 된다"가 정확한 규칙이다.

## 9. 다중 소켓 모니터링

여러 소켓의 이벤트를 각각의 콜백 핸들러로 처리.

```c
void on_event_a(const zlink_monitor_event_t *ev)
{
    printf("소켓 A 이벤트: 0x%llx\n", (unsigned long long)ev->event);
}

void on_event_b(const zlink_monitor_event_t *ev)
{
    printf("소켓 B 이벤트: 0x%llx\n", (unsigned long long)ev->event);
}

void *mon_a = zlink_socket_monitor_open(sock_a, ZLINK_EVENT_ALL, on_event_a);
void *mon_b = zlink_socket_monitor_open(sock_b, ZLINK_EVENT_ALL, on_event_b);

/* ... 애플리케이션 로직 ... */

/* 정리 */
zlink_socket_monitor(sock_a, NULL, 0);
zlink_socket_monitor(sock_b, NULL, 0);
zlink_close(mon_a);
zlink_close(mon_b);
```

## 10. 주의사항

### 모니터 스레드 안전성

`zlink_socket_monitor_open()`과 monitor handle close는 raw/service handle의
저빈도 control-path 계약에 속한다. 즉 애플리케이션 스레드에서 호출할 수 있고,
같은 handle과 섞여도 correctness가 유지된다. 다만 monitor callback은 I/O 경로
에서 실행되므로 callback 내부의 느린 작업은 사용자 큐로 넘기는 편이 좋다.

```c
/* 애플리케이션 스레드에서 monitor open */
void *socket = zlink_socket(ctx, ZLINK_ROUTER, NULL);
void *mon = zlink_socket_monitor_open(socket, ZLINK_EVENT_ALL,
                                       on_monitor_event);

/* 이후 다른 작업 스레드에서 snapshot 조회 가능 */
zlink_monitor_snapshot_t snapshot;
zlink_monitor_snapshot(mon, &snapshot);
```

### 동시 모니터 제한

동일 소켓에 동시에 여러 모니터를 설정할 수 없다.

### 콜백 처리 속도

콜백 핸들러에서 블로킹 작업을 수행하면 I/O 진행에 영향을 줄 수 있다.
느린 처리가 필요하면 콜백 안에서 사용자 queue로 넘기고 별도 thread에서 처리한다.

### 모니터 종료 절차

```c
/* 1. 모니터링 중지 */
zlink_socket_monitor(socket, NULL, 0);

/* 2. 모니터 소켓 닫기 */
zlink_close(mon);
```

반드시 두 단계를 모두 수행해야 한다. `zlink_close(mon)`만 호출하면 내부 리소스가 정리되지 않을 수 있다.

## 11. 패밀리별 제어 Gate

핵심 기준은 간단하다. public event는 제어에 써도 되지만, 그 event가 보장하는
레벨 안에서만 써야 한다. 이상한 예외 규칙이 있는 것이 아니라, transport,
session, delivery 레벨이 다르기 때문에 gate도 다르게 잡아야 한다.

| 패밀리 | raw/socket monitor에서 제어 gate로 써도 되는 것 | 쓰면 안 되는 것 | 대신 써야 할 것 |
|---|---|---|---|
| `PAIR` | 양쪽 `CONNECTION_READY` 이후 첫 양방향 송수신 시작 | bind-side `ACCEPTED`만 보고 송수신 시작 | 필요 시 snapshot으로 `READY`, `ready_peer_count` 확인 |
| `DEALER/ROUTER` | dealer `CONNECTION_READY`, router `CONNECTION_READY.routing_id` 이후 첫 request/reply 시작 | router `ACCEPTED`만 보고 routed send 시작 | snapshot + `routing_id` 사용 |
| `PUB/SUB` | bind/connect/handshake 상태 관찰 | raw `CONNECTION_READY`를 첫 publish delivery gate로 사용 | application barrier 또는 상위 service event |
| `STREAM` | server `CONNECTION_READY.routing_id` 이후 첫 payload 송수신 시작 | `ACCEPTED`만 보고 payload 송수신 시작 | snapshot + stream `routing_id` |
| `Gateway` | `ZLINK_GATEWAY_SEND_READY_CHANGED(value=1)` 이후 첫 request 시작 | `SERVICE_READY`, `ROUTE_UP`만으로 send gate 판단 | `zlink_monitor_snapshot()` + service monitor |
| `SPOT` | sub는 `SUB_DELIVERY_READY_CHANGED`, pub는 `PUB_FIRST_DELIVERY_READY_CHANGED` 이후 첫 publish/first receive 시작 | raw `CONNECTION_READY`, `PEER_UP`, `FILTER_APPLIED`만으로 delivery gate 판단 | `SPOT` service monitor |

실전 규칙:

- `ACCEPTED`는 transport-progress event다.
- raw `CONNECTION_READY`는 raw session-ready event다.
- first-delivery readiness가 더 필요한 패턴은 상위 service event를 써야 한다.
- gate를 통과한 뒤에도 sleep/retry가 필요하다면, 그 event 의미가 약한 것이고
  더 강한 event를 사용해야 한다.

---
[← TLS 보안](05-tls-security.ko.md) | [서비스 개요 →](07-0-services.ko.md)
