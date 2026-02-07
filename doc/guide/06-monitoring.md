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

| 이벤트 | 값 | 의미 | 발생 시점 | routing_id |
|--------|-----|------|-----------|:----------:|
| `CONNECTED` | — | TCP 연결 완료 | connect 직후 | 없음 |
| `ACCEPTED` | — | accept 완료 | 리스너 accept 직후 | 없음 |
| `CONNECTION_READY` | — | **연결 완료** | 핸드셰이크 성공 | 가능 |
| `DISCONNECTED` | reason | 세션 종료 | 어떤 단계든 | 가능 |
| `CONNECT_DELAYED` | — | 연결 지연 | 첫 시도 실패 | 없음 |
| `CONNECT_RETRIED` | — | 연결 재시도 | 재연결 시도 | 없음 |
| `LISTENING` | — | 리스너 활성화 | bind 성공 | 없음 |
| `BIND_FAILED` | errno | bind 실패 | bind 오류 | 없음 |
| `CLOSED` | — | 소켓 닫힘 | close 직후 | 없음 |
| `MONITOR_STOPPED` | — | 모니터 중지 | monitor(NULL) 호출 | 없음 |
| `HANDSHAKE_FAILED_NO_DETAIL` | errno | 핸드셰이크 실패 | READY 이전 | 없음 |
| `HANDSHAKE_FAILED_PROTOCOL` | — | 프로토콜 오류 | ZMP 핸드셰이크 | 없음 |
| `HANDSHAKE_FAILED_AUTH` | — | 인증 실패 | TLS 핸드셰이크 | 없음 |

> 참고: `core/tests/testutil_monitoring.cpp` — `get_zlinkEventName()` 이벤트 이름 매핑

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
