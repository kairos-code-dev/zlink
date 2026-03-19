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
zlink_set_routing_id(dealer, "client-1", 8);

/* 서버에 연결 */
zlink_connect(dealer, "tcp://127.0.0.1:5558");
```

### 메시지 송수신

```c
/* 요청 전송 — 순서 제약 없이 연속 전송 가능 */
zlink_msg_t msg1, msg2, msg3;
zlink_msg_init_size(&msg1, 9);
memcpy(zlink_msg_data(&msg1), "request-1", 9);
zlink_send(dealer, &msg1, 1, 0);

zlink_msg_init_size(&msg2, 9);
memcpy(zlink_msg_data(&msg2), "request-2", 9);
zlink_send(dealer, &msg2, 1, 0);

zlink_msg_init_size(&msg3, 9);
memcpy(zlink_msg_data(&msg3), "request-3", 9);
zlink_send(dealer, &msg3, 1, 0);

/* 응답은 생성 시 등록한 핸들러 콜백으로 디스패치됨 */
```

### 수신 모드

DEALER는 `zlink_recv_handler()`로 핸들러를 등록한다. 콜백은
`source_rid` (항상 비어 있음 — DEALER는 routing_id 프레임을 제거)와
`parts[]` 배열을 수신한다.

**Callback 모드** (권장): 소켓 생성 시 핸들러를 부착한다. 모든 피어의
메시지가 fair-queue로 도착하며 비동기로 dispatch된다.

**Pull 모드**: 핸들러를 부착하지 않으면 `zlink_recv()`로 동기 수신한다.

```c
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
int rc = zlink_recv(dealer, &source_rid, &parts, &part_count, 0);
if (rc == 0) {
    /* parts[0..part_count-1] 처리 */
    zlink_multipart_close(parts, part_count);
    free(parts);
}
```

> HWM 도달 시 `zlink_send()`는 블록(기본) 또는 `ZLINK_DONTWAIT`로
> `EAGAIN`을 반환한다. 고급 backpressure 패턴은
> [성능 가이드](10-performance.ko.md)를 참고.

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
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);
zlink_send(dealer, parts, 2, 0);

/* ROUTER 수신: [routing_id] + [header] + [body] */
```

## 4. 소켓 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `zlink_set_routing_id()` | binary | 자동(UUID) | ROUTER에서 식별할 ID (전용 함수) |
| `ZLINK_PROBE_ROUTER` | int | 0 | 연결 시 빈 메시지 전송 (연결 알림) |
| `ZLINK_OPT_SNDHWM` | int | 1000 | 송신 큐 최대 메시지 수 |
| `ZLINK_OPT_RCVHWM` | int | 1000 | 수신 큐 최대 메시지 수 |
| `ZLINK_OPT_LINGER` | int | -1 | close 시 대기 시간 (ms) |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | 송신 타임아웃 (ms) |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | 수신 타임아웃 (ms) |
| `ZLINK_CONNECT_ROUTING_ID` | binary | — | 다음 connect에 적용할 alias |

### routing_id 설정

ROUTER가 DEALER를 식별하려면 명시적으로 routing_id를 설정한다.

```c
/* bind/connect 전에 설정 */
zlink_set_routing_id(dealer, "D1", 2);
zlink_connect(dealer, "tcp://127.0.0.1:5558");
```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — `zlink_set_routing_id(dealer1, "D1", 2)`

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

    /* 응답: zlink_send_rid로 원본 피어에게 전송 */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 5);
    memcpy(zlink_msg_data(&reply), "World", 5);
    zlink_send_rid(router, source_rid, &reply, 1, 0);

    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_recv_handler(router, on_request, NULL);
zlink_bind(router, "tcp://*:5558");

/* 클라이언트: DEALER */
void *dealer = zlink_socket(ctx, ZLINK_DEALER);
zlink_recv_handler(dealer, on_reply, NULL);
zlink_set_routing_id(dealer, "D1", 2);
zlink_connect(dealer, "tcp://127.0.0.1:5558");

/* 클라이언트 요청 */
zlink_msg_t req;
zlink_msg_init_size(&req, 5);
memcpy(zlink_msg_data(&req), "Hello", 5);
zlink_send(dealer, &req, 1, 0);

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
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 5);
    memcpy(zlink_msg_data(&reply), "reply", 5);
    zlink_send_rid(router, source_rid, &reply, 1, 0);
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_recv_handler(router, on_message, NULL);
zlink_bind(router, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_get_option(router, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

void *dealer1 = zlink_socket(ctx, ZLINK_DEALER);
zlink_set_routing_id(dealer1, "D1", 2);
zlink_connect(dealer1, endpoint);

void *dealer2 = zlink_socket(ctx, ZLINK_DEALER);
zlink_set_routing_id(dealer2, "D2", 2);
zlink_connect(dealer2, endpoint);

/* 각 DEALER가 메시지 전송 */
zlink_msg_t m1;
zlink_msg_init_size(&m1, 12);
memcpy(zlink_msg_data(&m1), "from_dealer1", 12);
zlink_send(dealer1, &m1, 1, 0);

zlink_msg_t m2;
zlink_msg_init_size(&m2, 12);
memcpy(zlink_msg_data(&m2), "from_dealer2", 12);
zlink_send(dealer2, &m2, 1, 0);

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
        zlink_send_rid(worker, source_rid, parts, part_count, 0);
    }

    void *worker = zlink_socket(ctx, ZLINK_DEALER);
    zlink_recv_handler(worker, on_work, NULL);
    zlink_connect(worker, "inproc://backend");

    /* 소켓이 닫힐 때까지 워커 유지 */
}
```

> 참고: `core/tests/test_proxy.cpp` — ROUTER(frontend) + DEALER(backend) + 워커 풀

### 패턴 4: DEALER ↔ DEALER 비동기 통신

양쪽 모두 DEALER를 사용하여 완전 비동기 P2P 통신.

```c
void *a = zlink_socket(ctx, ZLINK_DEALER);
zlink_recv_handler(a, on_message_a, NULL);
zlink_bind(a, "tcp://*:5558");

void *b = zlink_socket(ctx, ZLINK_DEALER);
zlink_recv_handler(b, on_message_b, NULL);
zlink_connect(b, "tcp://127.0.0.1:5558");

/* 양방향 자유 전송 */
zlink_msg_t ping;
zlink_msg_init_size(&ping, 4);
memcpy(zlink_msg_data(&ping), "ping", 4);
zlink_send(a, &ping, 1, 0);

zlink_msg_t pong;
zlink_msg_init_size(&pong, 4);
memcpy(zlink_msg_data(&pong), "pong", 4);
zlink_send(b, &pong, 1, 0);

/* on_message_b가 "ping" 수신, on_message_a가 "pong" 수신 */
```

## 6. 주의사항

### 피어 없으면 큐잉

연결된 피어가 없으면 메시지는 송신 큐에 쌓인다. HWM 초과 시 블록(기본) 또는 `EAGAIN` 반환(`ZLINK_DONTWAIT`).

```c
/* 피어가 없는 상태에서 전송 */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 4);
memcpy(zlink_msg_data(&msg), "data", 4);
int rc = zlink_send(dealer, &msg, 1, ZLINK_DONTWAIT);
if (rc == -1 && errno == EAGAIN) {
    /* HWM 초과 또는 피어 없음 */
}
```

### Round-robin 분배

여러 피어가 연결된 경우 메시지는 순환적으로 분배된다. 특정 피어에게만 전송하려면 ROUTER를 사용한다.

### routing_id는 connect 전에 설정

`zlink_set_routing_id()`는 `zlink_connect()` 호출 전에 호출해야 한다. 연결 후 변경은 적용되지 않는다.

```c
/* 올바른 순서 */
zlink_set_routing_id(dealer, "D1", 2);
zlink_connect(dealer, endpoint);  /* D1으로 식별 */
```

---
[← PUB/SUB](03-2-pubsub.ko.md) | [ROUTER →](03-4-router.ko.md)
