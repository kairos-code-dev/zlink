# ROUTER 소켓

## 1. 개요

ROUTER 소켓은 **routing_id 기반 라우팅** 소켓이다. 수신 메시지에 routing_id 프레임을 자동으로 추가하고, 송신 시 첫 번째 프레임의 routing_id로 대상 피어를 지정한다.

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

ROUTER는 수신 메시지에 routing_id 프레임을 자동으로 앞에 추가한다.

```c
/* DEALER가 "Hello" 전송 → ROUTER는 [routing_id][Hello] 수신 */
char identity[32], data[256];
int id_size = zlink_recv(router, identity, sizeof(identity), 0);
int data_size = zlink_recv(router, data, sizeof(data), 0);
```

### 메시지 송신

응답 시 수신한 routing_id를 첫 프레임으로 전송하여 대상을 지정한다.

```c
/* 수신한 routing_id를 그대로 사용하여 응답 */
zlink_send(router, identity, id_size, ZLINK_SNDMORE);
zlink_send(router, "World", 5, 0);
```

## 3. 메시지 형식

### 수신 형식

DEALER가 `[A][B]` 멀티파트를 전송하면, ROUTER는 `[routing_id][A][B]`를 수신한다.

```
DEALER 송신:  [프레임1][프레임2]
                     ↓
ROUTER 수신:  [routing_id][프레임1][프레임2]
```

### 송신 형식

ROUTER가 전송할 때 첫 프레임은 반드시 대상 routing_id여야 한다. routing_id 프레임은 전송되지 않고 라우팅에만 사용된다.

```
ROUTER 송신:  [routing_id][프레임1][프레임2]
                      ↓
DEALER 수신:  [프레임1][프레임2]   ← routing_id 제거됨
```

### zlink_msg_t를 사용한 수신/응답

```c
/* 수신 */
zlink_msg_t rid, data;
zlink_msg_init(&rid);
zlink_msg_init(&data);
zlink_msg_recv(&rid, router, 0);   /* routing_id 프레임 */
zlink_msg_recv(&data, router, 0);  /* 데이터 프레임 */

/* 응답: routing_id를 그대로 재사용 */
zlink_msg_send(&rid, router, ZLINK_SNDMORE);
zlink_send(router, "reply", 5, 0);

zlink_msg_close(&rid);
zlink_msg_close(&data);
```

## 4. 소켓 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_ROUTER_MANDATORY` | int | 0 | 미도달 메시지 시 EHOSTUNREACH 에러 반환 |
| `ZLINK_ROUTER_HANDOVER` | int | 0 | routing_id 충돌 시 기존 연결 대체 |
| `ZLINK_ROUTING_ID` | binary | 자동(UUID) | ROUTER 자신의 routing_id |
| `ZLINK_SNDHWM` | int | 1000 | 송신 HWM |
| `ZLINK_RCVHWM` | int | 1000 | 수신 HWM |
| `ZLINK_LINGER` | int | -1 | close 시 대기 시간 (ms) |

### ROUTER_MANDATORY

기본적으로 ROUTER는 대상을 찾을 수 없는 메시지를 **조용히 드롭**한다. `ROUTER_MANDATORY`를 활성화하면 `EHOSTUNREACH` 에러를 반환한다.

```c
int mandatory = 1;
zlink_setsockopt(router, ZLINK_ROUTER_MANDATORY, &mandatory, sizeof(mandatory));

/* 존재하지 않는 대상에게 전송 시도 */
int rc = zlink_send(router, "UNKNOWN", 7, ZLINK_SNDMORE);
/* rc == -1, errno == EHOSTUNREACH */
```

> 참고: `core/tests/test_router_mandatory.cpp` — `test_basic()`

## 5. 사용 패턴

### 패턴 1: 다중 DEALER 서버

가장 기본적인 ROUTER 패턴. 여러 DEALER 클라이언트를 routing_id로 구분.

```c
/* 서버 */
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(router, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(router, ZLINK_LAST_ENDPOINT, endpoint, &len);

/* 클라이언트 1 */
void *d1 = zlink_socket(ctx, ZLINK_DEALER);
zlink_setsockopt(d1, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(d1, endpoint);

/* 클라이언트 2 */
void *d2 = zlink_socket(ctx, ZLINK_DEALER);
zlink_setsockopt(d2, ZLINK_ROUTING_ID, "D2", 2);
zlink_connect(d2, endpoint);

/* 각 클라이언트에서 메시지 수신 */
char id[32], msg[64];
int id_size = zlink_recv(router, id, sizeof(id), 0);
int msg_size = zlink_recv(router, msg, sizeof(msg), 0);

/* 특정 클라이언트에게 응답 */
zlink_send(router, "D1", 2, ZLINK_SNDMORE);
zlink_send(router, "reply_to_d1", 11, 0);

zlink_send(router, "D2", 2, ZLINK_SNDMORE);
zlink_send(router, "reply_to_d2", 11, 0);

/* 각 DEALER는 자신의 응답만 수신 */
char buf[64];
zlink_recv(d1, buf, sizeof(buf), 0);  /* "reply_to_d1" */
zlink_recv(d2, buf, sizeof(buf), 0);  /* "reply_to_d2" */
```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — TCP/IPC/inproc 3가지 transport

### 패턴 2: ROUTER_MANDATORY로 전송 실패 감지

```c
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(router, "tcp://*:5558");

/* 기본 동작: 미도달 메시지 조용히 드롭 */
zlink_send(router, "UNKNOWN", 7, ZLINK_SNDMORE);
zlink_send(router, "DATA", 4, 0);
/* 에러 없음, 메시지 소실 */

/* MANDATORY 모드 활성화 */
int mandatory = 1;
zlink_setsockopt(router, ZLINK_ROUTER_MANDATORY, &mandatory, sizeof(mandatory));

/* 이제 미도달 시 에러 반환 */
int rc = zlink_send(router, "UNKNOWN", 7, ZLINK_SNDMORE);
if (rc == -1 && errno == EHOSTUNREACH) {
    /* 대상 "UNKNOWN"을 찾을 수 없음 */
}
```

> 참고: `core/tests/test_router_mandatory.cpp` — 기본 드롭 vs MANDATORY 에러

### 패턴 3: 연결 확인 후 전송

DEALER가 먼저 메시지를 전송하여 ROUTER에 연결을 알린 후, ROUTER가 응답.

```c
/* DEALER 연결 및 초기 메시지 전송 */
void *dealer = zlink_socket(ctx, ZLINK_DEALER);
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "X", 1);
zlink_connect(dealer, endpoint);
zlink_send(dealer, "Hello", 5, 0);

/* ROUTER: DEALER의 연결을 확인 */
char id[32];
zlink_recv(router, id, sizeof(id), 0);  /* "X" */
char buf[64];
zlink_recv(router, buf, sizeof(buf), 0);  /* "Hello" */

/* 이제 "X"로 안전하게 전송 가능 */
zlink_send(router, "X", 1, ZLINK_SNDMORE);
zlink_send(router, "Hello", 5, 0);
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
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "stable-id", 9);
```

### routing_id 충돌

같은 routing_id를 가진 두 DEALER가 동시에 연결되면, 기본적으로 두 번째 연결이 거부된다. `ROUTER_HANDOVER`를 활성화하면 기존 연결을 대체한다.

### 멀티파트 메시지 완전성

ROUTER에서 송신할 때 routing_id 프레임과 데이터 프레임을 반드시 `ZLINK_SNDMORE`로 연결해야 한다. routing_id만 전송하고 데이터를 빠뜨리면 예기치 않은 동작이 발생한다.

```c
/* 올바른 전송 */
zlink_send(router, identity, id_size, ZLINK_SNDMORE);  /* 반드시 SNDMORE */
zlink_send(router, data, data_size, 0);

/* 잘못된 전송 — identity만 전송하면 안 됨 */
zlink_send(router, identity, id_size, 0);  /* SNDMORE 없음! */
```

> routing_id의 상세 개념은 [08-routing-id.md](08-routing-id.md)를 참고.
