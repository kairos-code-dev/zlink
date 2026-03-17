[English](03-3-dealer.md) | [한국어](03-3-dealer.ko.md)

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

/* 응답은 생성 시 등록한 핸들러 콜백으로 디스패치됨 */
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
/* 서버: 핸들러가 있는 ROUTER */
void on_request(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    /* source_rid에 DEALER의 routing_id가 포함됨 */
    printf("수신 [%.*s]: %.*s\n",
           (int)source_rid->size, source_rid->data,
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));

    /* 응답: routing_id를 앞에 붙여 전송 */
    zlink_send(router, source_rid->data, source_rid->size, ZLINK_SNDMORE);
    zlink_send(router, "World", 5, 0);

    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

zlink_socket_handler_t router_handler = {
    .kind = ZLINK_SOCKET_HANDLER_MSG,
    .fn.msg = on_request,
    .userdata = NULL
};
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_socket_attach_handler(router, &router_handler);
zlink_bind(router, "tcp://*:5558");

/* 클라이언트: DEALER */
zlink_socket_handler_t dealer_handler = {
    .kind = ZLINK_SOCKET_HANDLER_MSG,
    .fn.msg = on_reply,
    .userdata = NULL
};
void *dealer = zlink_socket(ctx, ZLINK_DEALER);
zlink_socket_attach_handler(dealer, &dealer_handler);
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer, "tcp://127.0.0.1:5558");

/* 클라이언트 요청 */
zlink_send(dealer, "Hello", 5, 0);

/* on_request가 메시지를 수신하여 "World"로 응답
   on_reply가 응답을 수신 */
```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — TCP/IPC/inproc 예제

### 패턴 2: 다중 DEALER 로드밸런싱

여러 DEALER가 하나의 ROUTER에 연결. ROUTER가 각 DEALER를 routing_id로 구분.

```c
/* ROUTER는 핸들러 콜백으로 수신하며 source_rid로 각 DEALER를 구분 */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    /* source_rid->data = "D1" 또는 "D2" */
    /* 특정 DEALER에게 응답 */
    zlink_send(router, source_rid->data, source_rid->size, ZLINK_SNDMORE);
    zlink_send(router, "reply", 5, 0);
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

zlink_socket_handler_t router_handler = {
    .kind = ZLINK_SOCKET_HANDLER_MSG,
    .fn.msg = on_message,
    .userdata = NULL
};
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_socket_attach_handler(router, &router_handler);
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

/* on_message가 각 DEALER의 메시지를 routing_id와 함께 수신 */
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
    void on_work(const zlink_routing_id_t *source_rid,
                 zlink_msg_t *parts, size_t part_count,
                 void *userdata)
    {
        /* 처리 후 동일 routing_id로 응답 */
        zlink_send(worker, source_rid->data, source_rid->size, ZLINK_SNDMORE);
        for (size_t i = 0; i < part_count; i++) {
            int more = (i < part_count - 1) ? ZLINK_SNDMORE : 0;
            zlink_msg_send(&parts[i], worker, more);
        }
    }

    zlink_socket_handler_t handler = {
        .kind = ZLINK_SOCKET_HANDLER_MSG,
        .fn.msg = on_work,
        .userdata = NULL
    };
    void *worker = zlink_socket(ctx, ZLINK_DEALER);
    zlink_socket_attach_handler(worker, &handler);
    zlink_connect(worker, "inproc://backend");

    /* 소켓이 닫힐 때까지 워커 유지 */
}
```

> 참고: `core/tests/test_proxy.cpp` — ROUTER(frontend) + DEALER(backend) + 워커 풀

### 패턴 4: DEALER ↔ DEALER 비동기 통신

양쪽 모두 DEALER를 사용하여 완전 비동기 P2P 통신.

```c
zlink_socket_handler_t handler_a = {
    .kind = ZLINK_SOCKET_HANDLER_MSG,
    .fn.msg = on_message_a,
    .userdata = NULL
};
void *a = zlink_socket(ctx, ZLINK_DEALER);
zlink_socket_attach_handler(a, &handler_a);
zlink_bind(a, "tcp://*:5558");

zlink_socket_handler_t handler_b = {
    .kind = ZLINK_SOCKET_HANDLER_MSG,
    .fn.msg = on_message_b,
    .userdata = NULL
};
void *b = zlink_socket(ctx, ZLINK_DEALER);
zlink_socket_attach_handler(b, &handler_b);
zlink_connect(b, "tcp://127.0.0.1:5558");

/* 양방향 자유 전송 */
zlink_send(a, "ping", 4, 0);
zlink_send(b, "pong", 4, 0);

/* on_message_b가 "ping" 수신, on_message_a가 "pong" 수신 */
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

---
[← PUB/SUB](03-2-pubsub.ko.md) | [ROUTER →](03-4-router.ko.md)
