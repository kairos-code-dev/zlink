[English](06-monitoring.md) | [한국어](06-monitoring.ko.md)

# 모니터링 API 사용법

## 1. 개요

zlink 모니터링 API는 소켓의 연결/해제/핸드셰이크 등 이벤트를 실시간으로 관찰할 수 있다. Polling 기반으로 동작하며, PAIR 소켓을 통해 이벤트를 수신한다.

## 2. 모니터 활성화

### 2.1 자동 생성 (권장)

```c
void *server = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(server, "tcp://*:5555");

/* 모니터 소켓 자동 생성 */
void *mon = zlink_socket_monitor_open(server, ZLINK_EVENT_ALL);
```

### 2.2 수동 설정

```c
zlink_socket_monitor(server, "inproc://monitor", ZLINK_EVENT_ALL);

void *mon = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(mon, "inproc://monitor");
```

## 3. 이벤트 수신

```c
zlink_monitor_event_t ev;
int rc = zlink_monitor_recv(mon, &ev, ZLINK_DONTWAIT);
if (rc == 0) {
    printf("이벤트: 0x%llx\n", (unsigned long long)ev.event);
    printf("로컬: %s\n", ev.local_addr);
    printf("원격: %s\n", ev.remote_addr);

    if (ev.routing_id.size > 0) {
        printf("routing_id: ");
        for (uint8_t i = 0; i < ev.routing_id.size; ++i)
            printf("%02x", ev.routing_id.data[i]);
        printf("\n");
    }
}
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

### 타임아웃 설정

모니터 소켓에 `ZLINK_RCVTIMEO`를 설정하여 이벤트 대기 시간을 제어할 수 있다.

```c
int timeout = 1000;  /* 1초 */
zlink_setsockopt(mon, ZLINK_RCVTIMEO, &timeout, sizeof(timeout));

zlink_monitor_event_t ev;
int rc = zlink_monitor_recv(mon, &ev, 0);  /* 최대 1초 대기 */
if (rc == -1 && errno == EAGAIN) {
    /* 타임아웃: 이벤트 없음 */
}
```

> 참고: `core/tests/testutil_monitoring.cpp` — `get_monitor_event_with_timeout()`

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

#### CONNECTION_READY (`0x1000`)

zlink 핸드셰이크가 성공적으로 완료되어 데이터 전송이 가능한 상태가 되었을 때 발생한다. 애플리케이션 수준의 연결 추적에 가장 중요한 이벤트이다.

- **`value`**: 사용되지 않음.
- **`routing_id`**: ROUTER 소켓의 경우 사용 가능 — 피어에 할당된 라우팅 ID를 포함한다.
- **`local_addr`**: 로컬 엔드포인트 주소.
- **`remote_addr`**: 원격 엔드포인트 주소.
- **일반적 용도**: 피어 등록, 메시지 전송 시작, `zlink_socket_peer_info()`를 통한 피어 정보 조회.

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
| `ZLINK_PROTOCOL_ERROR_ZMP_UNSPECIFIED` | `0x10000000` | 불특정 ZMP 프로토콜 오류. |
| `ZLINK_PROTOCOL_ERROR_ZMP_UNEXPECTED_COMMAND` | `0x10000001` | 핸드셰이크 중 예기치 않은 ZMP 커맨드 수신. |
| `ZLINK_PROTOCOL_ERROR_ZMP_INVALID_SEQUENCE` | `0x10000002` | ZMP 커맨드가 잘못된 순서로 도착. |
| `ZLINK_PROTOCOL_ERROR_ZMP_KEY_EXCHANGE` | `0x10000003` | ZMP 핸드셰이크의 키 교환 단계 실패. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_UNSPECIFIED` | `0x10000011` | 잘못된 형식의 ZMP 커맨드 (불특정). |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_MESSAGE` | `0x10000012` | 잘못된 형식의 ZMP MESSAGE 커맨드. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | `0x10000013` | 잘못된 형식의 ZMP HELLO 커맨드. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_INITIATE` | `0x10000014` | 잘못된 형식의 ZMP INITIATE 커맨드. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_ERROR` | `0x10000015` | 잘못된 형식의 ZMP ERROR 커맨드. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_READY` | `0x10000016` | 잘못된 형식의 ZMP READY 커맨드. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_WELCOME` | `0x10000017` | 잘못된 형식의 ZMP WELCOME 커맨드. |
| `ZLINK_PROTOCOL_ERROR_ZMP_INVALID_METADATA` | `0x10000018` | ZMP 핸드셰이크의 유효하지 않은 메타데이터. |
| `ZLINK_PROTOCOL_ERROR_ZMP_CRYPTOGRAPHIC` | `0x11000001` | ZMP 핸드셰이크 중 암호화 검증 실패. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MECHANISM_MISMATCH` | `0x11000002` | 클라이언트와 서버의 보안 메커니즘 불일치. |
| `ZLINK_PROTOCOL_ERROR_WS_UNSPECIFIED` | `0x30000000` | 불특정 WebSocket 프로토콜 오류. |

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
zlink_monitor_event_t ev;
zlink_monitor_recv(mon, &ev, 0);

if (ev.event == ZLINK_EVENT_DISCONNECTED) {
    switch (ev.value) {
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
```

## 7. 이벤트 필터링 및 구독 프리셋

### 특정 이벤트만 구독

```c
/* 연결/해제 이벤트만 */
void *mon = zlink_socket_monitor_open(server,
    ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
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

void *mon = zlink_socket_monitor_open(server, MONITOR_PRESET_SECURITY);
```

## 8. 피어 정보 조회

### 연결된 피어 수

```c
int count = zlink_socket_peer_count(socket);
printf("연결된 피어 수: %d\n", count);
```

### 특정 피어 정보

```c
/* 인덱스로 routing_id 조회 */
zlink_routing_id_t rid;
zlink_socket_peer_routing_id(socket, 0, &rid);

/* routing_id로 상세 정보 조회 */
zlink_peer_info_t info;
zlink_socket_peer_info(socket, &rid, &info);
printf("원격: %s, 연결시간: %llu\n", info.remote_addr, info.connected_time);
```

### 전체 피어 목록

```c
zlink_peer_info_t peers[64];
size_t peer_count = 64;
zlink_socket_peers(socket, peers, &peer_count);

for (size_t i = 0; i < peer_count; i++) {
    printf("피어 %zu: remote=%s\n", i, peers[i].remote_addr);
}
```

### 피어 정보와 모니터링 결합

```c
/* CONNECTION_READY 이벤트 수신 시 피어 정보 조회 */
zlink_monitor_event_t ev;
zlink_monitor_recv(mon, &ev, 0);

if (ev.event == ZLINK_EVENT_CONNECTION_READY && ev.routing_id.size > 0) {
    zlink_peer_info_t info;
    zlink_socket_peer_info(socket, &ev.routing_id, &info);
    printf("새 연결: remote=%s\n", info.remote_addr);
}
```

## 9. 다중 소켓 모니터링 (Poller 활용)

여러 소켓의 이벤트를 하나의 루프에서 처리.

```c
void *mon_a = zlink_socket_monitor_open(sock_a, ZLINK_EVENT_ALL);
void *mon_b = zlink_socket_monitor_open(sock_b, ZLINK_EVENT_ALL);

zlink_pollitem_t items[] = {
    {mon_a, 0, ZLINK_POLLIN, 0},
    {mon_b, 0, ZLINK_POLLIN, 0},
};

while (1) {
    int rc = zlink_poll(items, 2, 1000);
    if (rc <= 0) continue;

    zlink_monitor_event_t ev;

    if (items[0].revents & ZLINK_POLLIN) {
        zlink_monitor_recv(mon_a, &ev, ZLINK_DONTWAIT);
        printf("소켓 A 이벤트: 0x%llx\n", (unsigned long long)ev.event);
    }
    if (items[1].revents & ZLINK_POLLIN) {
        zlink_monitor_recv(mon_b, &ev, ZLINK_DONTWAIT);
        printf("소켓 B 이벤트: 0x%llx\n", (unsigned long long)ev.event);
    }
}

/* 정리 */
zlink_socket_monitor(sock_a, NULL, 0);
zlink_socket_monitor(sock_b, NULL, 0);
zlink_close(mon_a);
zlink_close(mon_b);
```

### 모니터 + 데이터 소켓 동시 폴링

```c
zlink_pollitem_t items[] = {
    {data_socket, 0, ZLINK_POLLIN, 0},  /* 데이터 수신 */
    {mon_socket, 0, ZLINK_POLLIN, 0},   /* 모니터 이벤트 */
};

while (1) {
    zlink_poll(items, 2, 1000);

    if (items[0].revents & ZLINK_POLLIN) {
        /* 데이터 처리 */
        char buf[256];
        zlink_recv(data_socket, buf, sizeof(buf), 0);
    }
    if (items[1].revents & ZLINK_POLLIN) {
        /* 이벤트 처리 */
        zlink_monitor_event_t ev;
        zlink_monitor_recv(mon_socket, &ev, ZLINK_DONTWAIT);
    }
}
```

## 10. 주의사항

### 모니터 스레드 안전성

모니터 설정은 **소켓 소유 스레드에서만** 호출해야 한다.

```c
/* 올바른 사용: 소켓 생성 스레드에서 모니터 설정 */
void *socket = zlink_socket(ctx, ZLINK_ROUTER);
void *mon = zlink_socket_monitor_open(socket, ZLINK_EVENT_ALL);

/* 잘못된 사용: 다른 스레드에서 모니터 설정 */
/* → 정의되지 않은 동작 */
```

### 동시 모니터 제한

동일 소켓에 동시에 여러 모니터를 설정할 수 없다.

### 모니터 속도

모니터 소켓의 수신이 느리면 이벤트가 **드롭될 수 있다**. DONTWAIT 사용 시 즉시 처리하거나, 별도 스레드에서 처리를 권장한다.

### 모니터 종료 절차

```c
/* 1. 모니터링 중지 */
zlink_socket_monitor(socket, NULL, 0);

/* 2. 모니터 소켓 닫기 */
zlink_close(mon);
```

반드시 두 단계를 모두 수행해야 한다. `zlink_close(mon)`만 호출하면 내부 리소스가 정리되지 않을 수 있다.

---
[← TLS 보안](05-tls-security.ko.md) | [서비스 개요 →](07-0-services.ko.md)
