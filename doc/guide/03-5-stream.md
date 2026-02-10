# STREAM 소켓

## 1. 개요

STREAM 소켓은 **외부 클라이언트**(웹 브라우저, 게임 클라이언트, 네이티브 TCP 클라이언트)와 RAW 통신하는 소켓이다. zlink 프로토콜(ZMP) 핸드셰이크 없이 즉시 데이터를 교환한다.

**핵심 특성:**
- ZMP 핸드셰이크 없음 — 외부 클라이언트와 직접 통신
- 4바이트 uint32 routing_id로 클라이언트 식별
- tcp, tls, ws, wss transport 지원
- 연결/해제를 빈 payload 이벤트로 감지

**유효한 소켓 조합:** STREAM ↔ 외부 클라이언트 (zlink 내부 소켓과 호환 불가)

```
┌────────────┐     RAW (Length-Prefix)     ┌────────┐
│ 외부 Client │◄──────────────────────────►│ STREAM │
└────────────┘                             └────────┘
```

> zlink 내부 소켓(PAIR, PUB, SUB 등)과는 프로토콜이 다르므로 연결 불가.

## 2. 기본 사용법

### 서버 생성 및 바인드

```c
void *stream = zlink_socket(ctx, ZLINK_STREAM);
zlink_bind(stream, "tcp://*:8080");
```

### 클라이언트 연결 (STREAM ↔ STREAM)

STREAM 소켓끼리도 연결할 수 있다 (양쪽 모두 RAW 모드).

```c
void *server = zlink_socket(ctx, ZLINK_STREAM);
zlink_bind(server, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(server, ZLINK_LAST_ENDPOINT, endpoint, &len);

void *client = zlink_socket(ctx, ZLINK_STREAM);
zlink_connect(client, endpoint);
```

## 3. 메시지 형식

STREAM 소켓의 메시지는 항상 **2프레임 구조**: `[routing_id(4B)][payload]`

### 수신 형식

```
프레임 0: [routing_id]  (4바이트 uint32, 자동 할당)
프레임 1: [payload]     (애플리케이션 데이터)
```

### 특수 이벤트

| payload | 의미 |
|---------|------|
| 1바이트 `0x01` | 연결 이벤트 (새 클라이언트 접속) |
| 1바이트 `0x00` | 해제 이벤트 (클라이언트 연결 끊김) |
| N바이트 데이터 | 일반 데이터 |

### 수신 코드

```c
unsigned char routing_id[4];
unsigned char payload[4096];

/* 프레임 0: routing_id */
int rc = zlink_recv(stream, routing_id, 4, 0);

/* MORE 플래그 확인 */
int more = 0;
size_t more_size = sizeof(more);
zlink_getsockopt(stream, ZLINK_RCVMORE, &more, &more_size);

/* 프레임 1: payload */
int payload_size = zlink_recv(stream, payload, sizeof(payload), 0);

if (payload_size == 1 && payload[0] == 0x01) {
    /* 새 클라이언트 연결 */
} else if (payload_size == 1 && payload[0] == 0x00) {
    /* 클라이언트 연결 해제 */
} else {
    /* 일반 데이터 */
}
```

> 참고: `core/tests/test_stream_socket.cpp` — `recv_stream_event()` 함수

### 송신 코드

```c
/* 응답: routing_id를 앞에 붙여서 전송 */
zlink_send(stream, routing_id, 4, ZLINK_SNDMORE);
zlink_send(stream, "response", 8, 0);
```

> 참고: `core/tests/test_stream_socket.cpp` — `send_stream_msg()` 함수

## 4. 소켓 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_MAXMSGSIZE` | int64 | -1 | 최대 메시지 크기 (초과 시 연결 끊김) |
| `ZLINK_SNDHWM` | int | 1000 | 송신 HWM |
| `ZLINK_RCVHWM` | int | 1000 | 수신 HWM |
| `ZLINK_LINGER` | int | -1 | close 시 대기 시간 (ms) |
| `ZLINK_TLS_CERT` | string | — | TLS/WSS 서버 인증서 경로 |
| `ZLINK_TLS_KEY` | string | — | TLS/WSS 서버 개인키 경로 |
| `ZLINK_TLS_CA` | string | — | TLS/WSS 클라이언트 CA 경로 |
| `ZLINK_TLS_HOSTNAME` | string | — | TLS 호스트명 검증 |
| `ZLINK_TLS_TRUST_SYSTEM` | int | 1 | 시스템 CA 스토어 신뢰 여부 |

### MAXMSGSIZE

지정된 크기를 초과하는 메시지를 수신하면 연결이 끊어진다.

```c
int64_t maxmsg = 1024;  /* 1KB 제한 */
zlink_setsockopt(stream, ZLINK_MAXMSGSIZE, &maxmsg, sizeof(maxmsg));
```

> 참고: `core/tests/test_stream_socket.cpp` — `test_stream_maxmsgsize()`

## 5. 사용 패턴

### 패턴 1: TCP 에코 서버

```c
void *server = zlink_socket(ctx, ZLINK_STREAM);
int linger = 0;
zlink_setsockopt(server, ZLINK_LINGER, &linger, sizeof(linger));
zlink_bind(server, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(server, ZLINK_LAST_ENDPOINT, endpoint, &len);

void *client = zlink_socket(ctx, ZLINK_STREAM);
zlink_setsockopt(client, ZLINK_LINGER, &linger, sizeof(linger));
zlink_connect(client, endpoint);

/* 연결 이벤트 수신 (양쪽 모두) */
unsigned char server_id[4], client_id[4];
unsigned char code;

zlink_recv(server, server_id, 4, 0);  /* routing_id */
zlink_recv(server, &code, 1, 0);      /* 0x01 = 연결 */

zlink_recv(client, client_id, 4, 0);
zlink_recv(client, &code, 1, 0);      /* 0x01 = 연결 */

/* 클라이언트 → 서버 */
zlink_send(client, client_id, 4, ZLINK_SNDMORE);
zlink_send(client, "hello", 5, 0);

/* 서버 수신 */
unsigned char recv_id[4];
char recv_buf[64];
zlink_recv(server, recv_id, 4, 0);
int size = zlink_recv(server, recv_buf, sizeof(recv_buf), 0);

/* 서버 → 클라이언트 응답 (에코) */
zlink_send(server, recv_id, 4, ZLINK_SNDMORE);
zlink_send(server, recv_buf, size, 0);
```

> 참고: `core/tests/test_stream_socket.cpp` — `test_stream_tcp_basic()`

### 패턴 2: WebSocket 서버 (ws://)

웹 브라우저와 통신하는 WebSocket 서버.

```c
void *server = zlink_socket(ctx, ZLINK_STREAM);
zlink_bind(server, "ws://127.0.0.1:*");

char endpoint[256];
size_t endpoint_len = sizeof(endpoint);
zlink_getsockopt(server, ZLINK_LAST_ENDPOINT, endpoint, &endpoint_len);

void *client = zlink_socket(ctx, ZLINK_STREAM);
zlink_connect(client, endpoint);

/* WebSocket 핸드셰이크 자동 처리 후 데이터 교환 */
unsigned char server_id[4], client_id[4];
unsigned char code;

zlink_recv(server, server_id, 4, 0);
zlink_recv(server, &code, 1, 0);  /* 0x01 */

zlink_recv(client, client_id, 4, 0);
zlink_recv(client, &code, 1, 0);  /* 0x01 */

/* 데이터 전송 */
zlink_send(client, client_id, 4, ZLINK_SNDMORE);
zlink_send(client, "ws", 2, 0);
```

> 참고: `core/tests/test_stream_socket.cpp` — `test_stream_ws_basic()`

### 패턴 3: TLS WebSocket 서버 (wss://)

암호화된 WebSocket 통신.

```c
void *server = zlink_socket(ctx, ZLINK_STREAM);
void *client = zlink_socket(ctx, ZLINK_STREAM);

/* 서버 TLS 설정 */
zlink_setsockopt(server, ZLINK_TLS_CERT, cert_path, strlen(cert_path));
zlink_setsockopt(server, ZLINK_TLS_KEY, key_path, strlen(key_path));
zlink_bind(server, "wss://127.0.0.1:*");

/* 클라이언트 TLS 설정 */
int trust_system = 0;
zlink_setsockopt(client, ZLINK_TLS_TRUST_SYSTEM, &trust_system, sizeof(trust_system));
zlink_setsockopt(client, ZLINK_TLS_CA, ca_path, strlen(ca_path));
zlink_setsockopt(client, ZLINK_TLS_HOSTNAME, "localhost", 9);

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(server, ZLINK_LAST_ENDPOINT, endpoint, &len);
zlink_connect(client, endpoint);

/* 이후 ws와 동일하게 데이터 교환 */
```

> 참고: `core/tests/test_stream_socket.cpp` — `test_stream_wss_basic()`

### 패턴 4: 연결/해제 감지

```c
unsigned char routing_id[4];
unsigned char code;

while (1) {
    zlink_recv(stream, routing_id, 4, 0);
    int size = zlink_recv(stream, &code, 1, ZLINK_DONTWAIT);

    if (size == 1 && code == 0x01) {
        printf("클라이언트 연결: id=%02x%02x%02x%02x\n",
               routing_id[0], routing_id[1],
               routing_id[2], routing_id[3]);
    } else if (size == 1 && code == 0x00) {
        printf("클라이언트 해제: id=%02x%02x%02x%02x\n",
               routing_id[0], routing_id[1],
               routing_id[2], routing_id[3]);
    } else {
        /* 일반 데이터 처리 */
    }
}
```

### 패턴 5: MAXMSGSIZE로 악의적 클라이언트 차단

```c
int64_t maxmsg = 4;  /* 4바이트 초과 메시지 거부 */
zlink_setsockopt(server, ZLINK_MAXMSGSIZE, &maxmsg, sizeof(maxmsg));

/* 클라이언트가 "toolarge" (8바이트) 전송 시 연결 끊김 */
/* 서버에서 0x00 (해제) 이벤트 수신 */
```

> 참고: `core/tests/test_stream_socket.cpp` — `test_stream_maxmsgsize()`

## 6. 주의사항

### zlink 내부 소켓과 통신 불가

STREAM 소켓은 ZMP 프로토콜 핸드셰이크를 하지 않으므로 PAIR, PUB, SUB, DEALER, ROUTER 등 zlink 내부 소켓과 연결할 수 없다. 외부 클라이언트 전용이다.

### routing_id는 4바이트 고정

STREAM 소켓의 routing_id는 항상 4바이트 uint32이다. ROUTER의 가변 크기 routing_id와 다르다.

```c
/* STREAM routing_id 크기 확인 */
unsigned char rid[4];
int rc = zlink_recv(stream, rid, 4, 0);
assert(rc == 4);  /* 항상 4바이트 */
```

### 연결 이벤트 처리 필수

새 클라이언트가 연결되면 반드시 연결 이벤트(0x01)를 수신해야 한다. 이벤트를 무시하면 routing_id를 알 수 없어 응답할 수 없다.

### Transport 제한

STREAM 소켓만 ws, wss transport를 지원한다. 다른 소켓 타입에서는 ws, wss를 사용할 수 없다. tls transport는 모든 소켓 타입에서 사용 가능하다.

| Transport | STREAM | 다른 소켓 |
|-----------|:------:|:---------:|
| tcp | O | O |
| ipc | - | O |
| inproc | - | O |
| ws | O | - |
| wss | O | - |
| tls | O | O |

### LINGER 설정

테스트 환경에서는 LINGER를 0으로 설정하여 빠른 정리를 권장한다.

```c
int linger = 0;
zlink_setsockopt(stream, ZLINK_LINGER, &linger, sizeof(linger));
```

> STREAM 소켓의 내부 구현 최적화(WS/WSS copy elimination 등)는 [STREAM 소켓 최적화](../internals/stream-socket.md)를 참고.
