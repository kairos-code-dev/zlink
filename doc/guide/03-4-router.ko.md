[English](03-4-router.md) | [한국어](03-4-router.ko.md)

# ROUTER 소켓

## 1. 개요

ROUTER 소켓은 **routing_id 기반 라우팅** 소켓이다.
수신 메시지에 routing_id 프레임을 자동으로 추가하고,
송신 시 첫 번째 프레임의 routing_id로 대상 피어를 지정한다.

**핵심 특성:**
- 수신 시 routing_id 프레임 자동 추가 (메시지 출처 식별)
- 송신 시 첫 프레임으로 대상 피어 지정 (특정 클라이언트에게 응답)
- 다중 피어 관리 가능 (서버/브로커 역할)

**유효한 소켓 조합:** ROUTER ↔ DEALER, ROUTER ↔ ROUTER

```
┌────────┐              ┌────────┐
│DEALER 1│─────────────►│        │
│ (D1)   │              │ ROUTER │  ← routing_id로 각 DEALER 구분
└────────┘              │        │
┌────────┐              │        │
│DEALER 2│─────────────►│        │
│ (D2)   │              └────────┘
└────────┘
```

## 2. 기본 사용법

### 생성 및 바인드

```c
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(router, "tcp://*:5558");
```

### 메시지 수신

ROUTER는 소켓 생성 후 부착한 핸들러 콜백으로 메시지를 수신한다.

```c
/* DEALER가 "Hello" 전송 → 핸들러가 source_rid + parts를 수신 */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    printf("[%.*s]로부터: %.*s\n",
           (int)source_rid->size, source_rid->data,
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

void *router = zlink_socket(ctx, ZLINK_ROUTER);
/* zlink_recv()로 수신 */
```

### 메시지 송신

응답 시 `zlink_send_rid`에 콜백의 `source_rid`를 전달하여 대상을 지정한다.

```c
/* 콜백의 source_rid를 사용하여 응답 */
zlink_msg_t reply;
zlink_msg_init_size(&reply, 5);
memcpy(zlink_msg_data(&reply), "World", 5);
zlink_send_rid(router, source_rid, &reply, 1, 0);
```

### 수신 모드

**Pull 모드**: 핸들러를 부착하지 않으면 `zlink_recv()`로 동기 수신한다.

```c
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
int rc = zlink_recv(router, &source_rid, &parts, &part_count, 0);
if (rc == 0) {
    /* source_rid로 송신자 식별 */
    /* parts[0..part_count-1] 처리 */
    zlink_multipart_close(parts, part_count);
    free(parts);
}
```

> 피어별 송신 큐가 가득 차면(HWM) `ROUTER_MANDATORY` 활성 시
> `EHOSTUNREACH`를 반환하고, 그렇지 않으면 메시지를 조용히 드롭한다.
> 고급 backpressure 패턴은 [성능 가이드](10-performance.ko.md)를 참고.

## 3. 사용 예제

ROUTER는 `zlink_send_rid()`로 특정 피어에 전송하고,
`zlink_recv()`의 `source_rid`로 송신자를 식별한다.

### 콜백을 사용한 수신/응답

```c
/* 수신: 핸들러 콜백이 routing_id와 데이터를 제공 */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    /* 응답: zlink_send_rid로 원본 피어에게 전송 */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 5);
    memcpy(zlink_msg_data(&reply), "reply", 5);
    zlink_send_rid(router, source_rid, &reply, 1, 0);

    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

## 4. 소켓 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_ROUTER_OPT_MANDATORY` | int | 0 | 미도달 시 `EHOSTUNREACH` 반환 |
| `ZLINK_ROUTER_HANDOVER` | int | 0 | routing_id 충돌 시 기존 연결 대체 |
| `zlink_set_routing_id()` | binary | 자동(UUID) | ROUTER 자신의 routing_id (전용 함수) |
| `ZLINK_OPT_SNDHWM` | int | 1000 | 송신 HWM |
| `ZLINK_OPT_RCVHWM` | int | 1000 | 수신 HWM |
| `ZLINK_OPT_LINGER` | int | -1 | close 시 대기 시간 (ms) |

### ROUTER_MANDATORY

기본적으로 ROUTER는 대상을 찾을 수 없는 메시지를 **조용히 드롭**한다. `ROUTER_MANDATORY`를 활성화하면 `EHOSTUNREACH` 에러를 반환한다.

```c
int mandatory = 1;
zlink_set_router_option(router, ZLINK_ROUTER_OPT_MANDATORY, &mandatory, sizeof(mandatory));

/* 존재하지 않는 대상에게 전송 시도 */
zlink_routing_id_t target_rid = { .data = "UNKNOWN", .size = 7 };
zlink_msg_t msg;
zlink_msg_init_size(&msg, 4);
memcpy(zlink_msg_data(&msg), "data", 4);
int rc = zlink_send_rid(router, &target_rid, &msg, 1, 0);
/* rc == -1, errno == EHOSTUNREACH */
```

> 참고: `core/tests/test_router_mandatory.cpp` — `test_basic()`

## 5. 사용 패턴

### 패턴 1: 다중 DEALER 서버

가장 기본적인 ROUTER 패턴. 여러 DEALER 클라이언트를 routing_id로 구분.

```c
/* 서버: 핸들러가 있는 ROUTER */
void on_request(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    /* 송신자에게 응답 */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 5);
    memcpy(zlink_msg_data(&reply), "reply", 5);
    zlink_send_rid(router, source_rid, &reply, 1, 0);
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

void *router = zlink_socket(ctx, ZLINK_ROUTER);
/* zlink_recv()로 수신 */
zlink_bind(router, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_get_option(router, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

/* 클라이언트 1 */
void *d1 = zlink_socket(ctx, ZLINK_DEALER);
/* zlink_recv()로 응답 수신 */
zlink_set_routing_id(d1, "D1", 2);
zlink_connect(d1, endpoint);

/* 클라이언트 2 */
void *d2 = zlink_socket(ctx, ZLINK_DEALER);
/* zlink_recv()로 응답 수신 */
zlink_set_routing_id(d2, "D2", 2);
zlink_connect(d2, endpoint);

/* 각 클라이언트가 메시지 전송 — on_request가 source_rid와 함께 수신 */
zlink_msg_t m1;
zlink_msg_init_size(&m1, 7);
memcpy(zlink_msg_data(&m1), "from_d1", 7);
zlink_send(d1, &m1, 1, 0);

zlink_msg_t m2;
zlink_msg_init_size(&m2, 7);
memcpy(zlink_msg_data(&m2), "from_d2", 7);
zlink_send(d2, &m2, 1, 0);

/* on_reply가 각 DEALER의 응답을 수신 */
```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — TCP/IPC/inproc 3가지 transport

### 패턴 2: ROUTER_MANDATORY로 전송 실패 감지

```c
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(router, "tcp://*:5558");

/* 기본 동작: 미도달 메시지 조용히 드롭 */
zlink_routing_id_t bad_rid = { .data = "UNKNOWN", .size = 7 };
zlink_msg_t msg;
zlink_msg_init_size(&msg, 4);
memcpy(zlink_msg_data(&msg), "DATA", 4);
zlink_send_rid(router, &bad_rid, &msg, 1, 0);
/* 에러 없음, 메시지 소실 */

/* MANDATORY 모드 활성화 */
int mandatory = 1;
zlink_set_router_option(router, ZLINK_ROUTER_OPT_MANDATORY, &mandatory, sizeof(mandatory));

/* 이제 미도달 시 에러 반환 */
zlink_msg_t msg2;
zlink_msg_init_size(&msg2, 4);
memcpy(zlink_msg_data(&msg2), "DATA", 4);
int rc = zlink_send_rid(router, &bad_rid, &msg2, 1, 0);
if (rc == -1 && errno == EHOSTUNREACH) {
    /* 대상 "UNKNOWN"을 찾을 수 없음 */
}
```

> 참고: `core/tests/test_router_mandatory.cpp` — 기본 드롭 vs MANDATORY 에러

### 패턴 3: 연결 확인 후 전송

DEALER가 먼저 메시지를 전송하여 ROUTER에 연결을 알린 후, ROUTER가 응답.

```c
/* ROUTER 핸들러: DEALER의 초기 메시지로 연결 확인 */
void on_connect(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    /* source_rid->data = "X" — 이제 "X"로 안전하게 전송 가능 */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 7);
    memcpy(zlink_msg_data(&reply), "Welcome", 7);
    zlink_send_rid(router, source_rid, &reply, 1, 0);
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

/* DEALER 연결 및 초기 메시지 전송 */
void *dealer = zlink_socket(ctx, ZLINK_DEALER);
zlink_set_routing_id(dealer, "X", 1);
zlink_connect(dealer, endpoint);
zlink_msg_t hello;
zlink_msg_init_size(&hello, 5);
memcpy(zlink_msg_data(&hello), "Hello", 5);
zlink_send(dealer, &hello, 1, 0);

/* on_connect 수신: source_rid = "X", parts[0] = "Hello"
   "Welcome"으로 응답 */
```

> 참고: `core/tests/test_router_mandatory.cpp` — DEALER 연결 → 메시지 → ROUTER 응답

### 패턴 4: 다중 Transport

같은 ROUTER에 다양한 transport로 DEALER를 연결 가능.

```c
void *router = zlink_socket(ctx, ZLINK_ROUTER);

/* TCP */
zlink_bind(router, "tcp://127.0.0.1:5558");

/* IPC (Linux/macOS) */
zlink_bind(router, "ipc:///tmp/router.ipc");

/* inproc (동일 프로세스) */
zlink_bind(router, "inproc://router");

/* 각 transport의 DEALER가 연결 — ROUTER는 routing_id로 통합 관리 */
```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — TCP/IPC/inproc 테스트

## 6. 주의사항

### 기본 드롭 동작

`ROUTER_MANDATORY`를 설정하지 않으면, 존재하지 않는 routing_id로 전송 시 메시지가 **조용히 드롭**된다. 프로덕션에서는 `ROUTER_MANDATORY` 활성화를 권장한다.

### 재연결 시 routing_id 변경

DEALER가 재연결하면 자동 생성된 routing_id가 변경될 수 있다. 안정적인 통신을 위해 명시적 routing_id 설정을 권장한다.

```c
/* 명시적 routing_id — 재연결 시에도 동일 */
zlink_set_routing_id(dealer, "stable-id", 9);
```

### routing_id 충돌

같은 routing_id를 가진 두 DEALER가 동시에 연결되면, 기본적으로 두 번째 연결이 거부된다. `ROUTER_HANDOVER`를 활성화하면 기존 연결을 대체한다.

> routing_id의 상세 개념은 [08-routing-id.ko.md](08-routing-id.ko.md)를 참고.

---
[← DEALER](03-3-dealer.ko.md) | [STREAM →](03-5-stream.ko.md)
