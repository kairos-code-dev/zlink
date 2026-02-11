# DEALER 소켓

## 1. 개요

DEALER 소켓은 비동기 요청 소켓이다. 여러 피어에 **Round-robin** 분배로 송신하고, **Fair-queue**로 수신한다. REQ 소켓과 달리 send/recv 순서 강제가 없어 자유로운 비동기 메시징이 가능하다.

**핵심 특성:**
- 송신: Round-robin (`lb_t`) — 연결된 피어에 순환 분배
- 수신: Fair-queue (`fq_t`) — 모든 피어에서 공정하게 수신
- send/recv 순서 강제 없음 (비동기)
- routing_id 프레임 자동 처리 없음

**유효한 소켓 조합:** DEALER ↔ ROUTER, DEALER ↔ DEALER

```
┌──────────┐                ┌────────┐
│ DEALER 1 │────────────────►│        │
└──────────┘  Round-robin   │ ROUTER │
┌──────────┐                │        │
│ DEALER 2 │────────────────►│        │
└──────────┘                └────────┘
```

## 2. 기본 사용법

### 생성 및 연결

```c
void *dealer = zlink_socket(ctx, ZLINK_DEALER);

/* routing_id 설정 (선택, ROUTER에서 식별용) */
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "client-1", 8);

/* 서버에 연결 */
zlink_connect(dealer, "tcp://127.0.0.1:5558");
```

### 메시지 송수신

```c
/* 요청 전송 — 순서 제약 없이 연속 전송 가능 */
zlink_send(dealer, "request-1", 9, 0);
zlink_send(dealer, "request-2", 9, 0);
zlink_send(dealer, "request-3", 9, 0);

/* 응답 수신 — 순서 제약 없이 연속 수신 가능 */
char buf[256];
zlink_recv(dealer, buf, sizeof(buf), 0);
zlink_recv(dealer, buf, sizeof(buf), 0);
```

## 3. 메시지 형식

DEALER 소켓은 routing_id 프레임을 자동으로 추가하지 않는다. 애플리케이션이 전송하는 프레임이 그대로 전달된다.

```
DEALER 송신: [데이터]
ROUTER 수신: [routing_id][데이터]   ← ROUTER가 routing_id 추가

ROUTER 송신: [routing_id][데이터]
DEALER 수신: [데이터]              ← routing_id 프레임 제거됨
```

### 멀티파트 메시지

```c
/* DEALER → ROUTER: 멀티파트 전송 */
zlink_send(dealer, "header", 6, ZLINK_SNDMORE);
zlink_send(dealer, "body", 4, 0);

/* ROUTER 수신: [routing_id] + [header] + [body] */
```

## 4. 소켓 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_ROUTING_ID` | binary | 자동(UUID) | ROUTER에서 식별할 ID |
| `ZLINK_PROBE_ROUTER` | int | 0 | 연결 시 빈 메시지 전송 (연결 알림) |
| `ZLINK_SNDHWM` | int | 1000 | 송신 큐 최대 메시지 수 |
| `ZLINK_RCVHWM` | int | 1000 | 수신 큐 최대 메시지 수 |
| `ZLINK_LINGER` | int | -1 | close 시 대기 시간 (ms) |
| `ZLINK_SNDTIMEO` | int | -1 | 송신 타임아웃 (ms) |
| `ZLINK_RCVTIMEO` | int | -1 | 수신 타임아웃 (ms) |
| `ZLINK_CONNECT_ROUTING_ID` | binary | — | 다음 connect에 적용할 alias |

### routing_id 설정

ROUTER가 DEALER를 식별하려면 명시적으로 routing_id를 설정한다.

```c
/* bind/connect 전에 설정 */
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer, "tcp://127.0.0.1:5558");
```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — `zlink_setsockopt(dealer1, ZLINK_ROUTING_ID, "D1", 2)`

## 5. 사용 패턴

### 패턴 1: DEALER → ROUTER 요청-응답

가장 기본적인 패턴. DEALER가 요청, ROUTER가 응답.

```c
/* 서버: ROUTER */
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(router, "tcp://*:5558");

/* 클라이언트: DEALER */
void *dealer = zlink_socket(ctx, ZLINK_DEALER);
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer, "tcp://127.0.0.1:5558");

/* 클라이언트 요청 */
zlink_send(dealer, "Hello", 5, 0);

/* 서버 수신: [routing_id="D1"] + [data="Hello"] */
char identity[32], data[256];
int id_size = zlink_recv(router, identity, sizeof(identity), 0);
int data_size = zlink_recv(router, data, sizeof(data), 0);

/* 서버 응답: routing_id를 앞에 붙여 전송 */
zlink_send(router, identity, id_size, ZLINK_SNDMORE);
zlink_send(router, "World", 5, 0);

/* 클라이언트 수신: "World" */
zlink_recv(dealer, data, sizeof(data), 0);
```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — TCP/IPC/inproc 예제

### 패턴 2: 다중 DEALER 로드밸런싱

여러 DEALER가 하나의 ROUTER에 연결. ROUTER가 각 DEALER를 routing_id로 구분.

```c
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(router, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(router, ZLINK_LAST_ENDPOINT, endpoint, &len);

void *dealer1 = zlink_socket(ctx, ZLINK_DEALER);
zlink_setsockopt(dealer1, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer1, endpoint);

void *dealer2 = zlink_socket(ctx, ZLINK_DEALER);
zlink_setsockopt(dealer2, ZLINK_ROUTING_ID, "D2", 2);
zlink_connect(dealer2, endpoint);

/* 각 DEALER가 메시지 전송 */
zlink_send(dealer1, "from_dealer1", 12, 0);
zlink_send(dealer2, "from_dealer2", 12, 0);

/* ROUTER는 routing_id로 각 DEALER를 구분하여 수신 */
char id[32], msg[64];
zlink_recv(router, id, sizeof(id), 0);  /* "D1" 또는 "D2" */
zlink_recv(router, msg, sizeof(msg), 0);

/* 특정 DEALER에게 응답 */
zlink_send(router, "D1", 2, ZLINK_SNDMORE);
zlink_send(router, "reply_to_d1", 11, 0);

zlink_send(router, "D2", 2, ZLINK_SNDMORE);
zlink_send(router, "reply_to_d2", 11, 0);
```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — `test_router_multiple_dealers_tcp()`

### 패턴 3: 프록시 패턴 (ROUTER-DEALER)

ROUTER(프론트엔드) + DEALER(백엔드)로 멀티스레드 서버 구축.

```c
/* 프론트엔드: 클라이언트가 연결 */
void *frontend = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(frontend, "tcp://*:5558");

/* 백엔드: 워커 스레드가 연결 */
void *backend = zlink_socket(ctx, ZLINK_DEALER);
zlink_bind(backend, "inproc://backend");

/* 워커 스레드 시작 후 프록시 실행 */
zlink_proxy(frontend, backend, NULL);
```

```c
/* 워커 스레드 */
void worker_thread(void *arg) {
    void *worker = zlink_socket(ctx, ZLINK_DEALER);
    zlink_connect(worker, "inproc://backend");

    while (1) {
        /* [routing_id][data] 수신 */
        char routing_id[32], content[256];
        int id_size = zlink_recv(worker, routing_id, sizeof(routing_id), 0);
        int msg_size = zlink_recv(worker, content, sizeof(content), 0);

        /* 처리 후 동일 routing_id로 응답 */
        zlink_send(worker, routing_id, id_size, ZLINK_SNDMORE);
        zlink_send(worker, content, msg_size, 0);
    }
}
```

> 참고: `core/tests/test_proxy.cpp` — ROUTER(frontend) + DEALER(backend) + 워커 풀

### 패턴 4: DEALER ↔ DEALER 비동기 통신

양쪽 모두 DEALER를 사용하여 완전 비동기 P2P 통신.

```c
void *a = zlink_socket(ctx, ZLINK_DEALER);
zlink_bind(a, "tcp://*:5558");

void *b = zlink_socket(ctx, ZLINK_DEALER);
zlink_connect(b, "tcp://127.0.0.1:5558");

/* 양방향 자유 전송 */
zlink_send(a, "ping", 4, 0);
zlink_send(b, "pong", 4, 0);

/* 양방향 자유 수신 */
char buf[64];
zlink_recv(b, buf, sizeof(buf), 0);  /* "ping" */
zlink_recv(a, buf, sizeof(buf), 0);  /* "pong" */
```

## 6. 주의사항

### 피어 없으면 큐잉

연결된 피어가 없으면 메시지는 송신 큐에 쌓인다. HWM 초과 시 블록(기본) 또는 `EAGAIN` 반환(`ZLINK_DONTWAIT`).

```c
/* 피어가 없는 상태에서 전송 */
int rc = zlink_send(dealer, "data", 4, ZLINK_DONTWAIT);
if (rc == -1 && errno == EAGAIN) {
    /* HWM 초과 또는 피어 없음 */
}
```

### Round-robin 분배

여러 피어가 연결된 경우 메시지는 순환적으로 분배된다. 특정 피어에게만 전송하려면 ROUTER를 사용한다.

### routing_id는 connect 전에 설정

`ZLINK_ROUTING_ID`는 `zlink_connect()` 호출 전에 설정해야 한다. 연결 후 변경은 적용되지 않는다.

```c
/* 올바른 순서 */
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer, endpoint);  /* D1으로 식별 */
```
