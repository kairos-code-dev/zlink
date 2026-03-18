[English](02-core-api.md) | [한국어](02-core-api.ko.md)

# Core C API 상세 가이드

## 1. Context API

Context는 zlink의 최상위 객체로, I/O thread pool과 socket을 관리한다.

```c
/* 생성 */
void *ctx = zlink_ctx_new();

/* 설정 — 다중 연결 서버에서는 I/O thread를 늘린다 */
zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);     /* 기본 1; 연결이 많으면 4가 최적 */

/* 조회 */
int io_threads = zlink_ctx_get(ctx, ZLINK_IO_THREADS);

/* 종료 */
zlink_ctx_term(ctx);  /* 모든 socket이 닫힌 후 반환 */
```

### Context 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `ZLINK_IO_THREADS` | 1 | I/O thread 수 |
| `ZLINK_MAX_SOCKETS` | 4095 | 최대 socket 수 |
| `ZLINK_MAX_MSGSZ` | -1 | 최대 message 크기 (-1: 무제한) |

## 2. Socket API

공개 socket handle API는 기본적으로 thread-safe다. 여러 thread에서
같은 socket handle을 공유하여 send/recv/bind/connect 등을 호출할 수 있다.

> 세부 threading 규칙은 [Thread Safety 가이드](11-thread-safety.ko.md)를 참고.

### 2.1 Socket 생성 및 닫기

```c
void *socket = zlink_socket(ctx, ZLINK_DEALER);
/* ... 사용 ... */
zlink_close(socket);
```

### 2.2 Socket Type 상수

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_PAIR` | 0x1001 | 1:1 bidirectional pair 소켓 |
| `ZLINK_PUB` | 0x1002 | Publisher 소켓 |
| `ZLINK_SUB` | 0x1003 | Subscriber 소켓 |
| `ZLINK_DEALER` | 0x1004 | Async dealer 소켓 |
| `ZLINK_ROUTER` | 0x1005 | Router 소켓 |
| `ZLINK_XPUB` | 0x1006 | Extended publisher 소켓 |
| `ZLINK_XSUB` | 0x1007 | Extended subscriber 소켓 |
| `ZLINK_STREAM` | 0x1008 | Raw 소켓 |

### 2.3 연결 관리

```c
/* Bind (server) */
zlink_bind(socket, "tcp://*:5555");

/* Connect (client) */
zlink_connect(socket, "tcp://127.0.0.1:5555");

/* Unbind / Disconnect */
zlink_unbind(socket, "tcp://*:5555");
zlink_disconnect(socket, "tcp://127.0.0.1:5555");
```

### 2.4 Socket Option

```c
/* 옵션 설정 */
int hwm = 5000;
zlink_setsockopt(socket, ZLINK_SNDHWM, &hwm, sizeof(hwm));

/* 옵션 조회 */
int value;
size_t len = sizeof(value);
zlink_getsockopt(socket, ZLINK_SNDHWM, &value, &len);
```

주요 옵션:

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_SNDHWM` | int | 1000 | Send High Water Mark |
| `ZLINK_RCVHWM` | int | 1000 | Recv High Water Mark |
| `ZLINK_SNDTIMEO` | int | -1 | Send timeout (ms, -1: 무제한) |
| `ZLINK_RCVTIMEO` | int | -1 | Recv timeout (ms, -1: 무제한) |
| `ZLINK_LINGER` | int | -1 | Socket close 시 linger (ms) |
| `ZLINK_ROUTING_ID` | binary | 자동 | Socket routing ID |
| `ZLINK_SUBSCRIBE` | binary | - | Subscribe filter (SUB 전용) |

`ZLINK_SUBSCRIBE` / `ZLINK_UNSUBSCRIBE`, `ZLINK_EVENTS`,
`ZLINK_LAST_ENDPOINT` 같은 option은 runtime 중간에도
보통 사용한다. 반면 HWM, timeout, TLS 같은 대부분의 tuning option은
초기 설정 단계에서 사용한다.

## 3. Message Send/Recv

### 3.1 Send

```c
/* Simple send */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "Hello", 5);
zlink_send(socket, &part, 1, 0);

/* Multipart send */
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);
zlink_send(socket, parts, 2, 0);
```

기본적으로 `zlink_send()`는 send queue가 가득 차면(HWM 도달) blocking한다.
`ZLINK_DONTWAIT` flag를 사용하면 blocking 대신 즉시 `EAGAIN`을 반환한다.
고급 backpressure pattern은
[Performance 가이드](10-performance.ko.md)를 참고.

### 3.2 Recv

zlink socket은 두 가지 recv mode를 지원한다:

#### Pull Mode (synchronous)

Handler를 부착하지 않으면 `zlink_recv()`로 직접 message를 받을 수 있다.
Socket은 기본적으로 pull mode로 시작한다.

```c
void *socket = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(socket, "tcp://*:5556");

/* Blocking recv */
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
int rc = zlink_recv(socket, &source_rid, &parts, &part_count, 0);
if (rc == 0) {
    for (size_t i = 0; i < part_count; i++) {
        printf("frame %zu: %.*s\n", i,
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
    free(parts);
}

/* Non-blocking recv */
rc = zlink_recv(socket, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
if (rc == -1 && zlink_errno() == EAGAIN) {
    /* 현재 사용 가능한 message 없음 */
}
```

#### Callback Mode

Socket 생성 후 handler callback을 부착하면 message 도착 시 I/O thread에서
비동기로 호출된다. 한번 부착하면 socket 수명 동안 해제할 수 없다.
Handler가 부착된 상태에서 `zlink_recv()` 호출 시 `EBUSY`를 반환한다.

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; i++) {
        printf("frame %zu: %.*s\n", i,
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}

void *socket = zlink_socket(ctx, ZLINK_PAIR);
zlink_recv_handler(socket, on_message, NULL);
```

> 두 mode의 비교와 고급 pattern은
> [Performance 가이드](10-performance.ko.md)를 참고.

### 3.3 Send Flag

| Flag | 설명 |
|--------|------|
| `ZLINK_DONTWAIT` | Non-blocking mode (send/recv 불가 시 즉시 EAGAIN 반환) |

## 4. Handler Type

각 socket type은 전용 등록 함수를 사용한다:

| Socket Type | 등록 호출 | Callback Signature |
|---|---|---|
| PAIR, DEALER, ROUTER, STREAM | `zlink_recv_handler(socket, fn, userdata)` | `void fn(const zlink_routing_id_t *rid, zlink_msg_t *parts, size_t count, void *userdata)` |
| SUB, XSUB | `zlink_subscribe_handler(socket, fn, userdata)` | `void fn(const zlink_routing_id_t *rid, const char *topic, size_t topic_len, zlink_msg_t *parts, size_t count, void *userdata)` |
| XPUB | `zlink_subscription_event_handler(socket, fn, userdata)` | `void fn(const zlink_routing_id_t *source_rid, int subscribed, const char *topic, size_t topic_len, void *userdata)` |
| PUB | N/A | Send-only socket |

Callback은 I/O thread에서 호출된다. Callback 내부에서 blocking 작업을 피해야 한다.
느린 처리가 필요하면 user queue에 넣고 별도 thread에서 처리한다.

## 5. Error 처리

```c
zlink_msg_t part;
zlink_msg_init_size(&part, size);
memcpy(zlink_msg_data(&part), data, size);
int rc = zlink_send(socket, &part, 1, 0);
if (rc == -1) {
    int err = zlink_errno();
    printf("error: %s\n", zlink_strerror(err));
}
```

주요 error code:

| Error | 값 | 설명 |
|-------|-----|------|
| `EAGAIN` | POSIX | Non-blocking mode에서 즉시 완료 불가 |
| `EINTR` | POSIX | Signal에 의해 interrupt됨 |
| `ENOTSOCK` | `HAUSNUMERO + 9` | 유효하지 않은 socket |
| `EHOSTUNREACH` | `HAUSNUMERO + 17` | Host 도달 불가 |
| `EFSM` | `HAUSNUMERO + 51` | 현재 state에서 허용되지 않는 연산 |
| `ETERM` | `HAUSNUMERO + 53` | Context terminated |

> `ZLINK_HAUSNUMERO` = 156384712. POSIX errno와 충돌하지 않는 zlink 전용 base 값이다.

## 6. DEALER/ROUTER 예제

```c
#include <zlink.h>
#include <string.h>
#include <stdio.h>

void on_router_message(const zlink_routing_id_t *source_rid,
                       zlink_msg_t *parts, size_t part_count,
                       void *userdata)
{
    printf("[%.*s] recv: %.*s\n",
           (int)source_rid->size, source_rid->data,
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

void on_dealer_message(const zlink_routing_id_t *source_rid,
                       zlink_msg_t *parts, size_t part_count,
                       void *userdata)
{
    printf("reply: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

int main(void) {
    void *ctx = zlink_ctx_new();

    /* ROUTER (server) */
    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_recv_handler(router, on_router_message, NULL);
    zlink_bind(router, "tcp://*:5555");

    /* DEALER (client) */
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);
    zlink_recv_handler(dealer, on_dealer_message, NULL);
    zlink_connect(dealer, "tcp://127.0.0.1:5555");

    /* DEALER → ROUTER */
    zlink_msg_t req;
    zlink_msg_init_size(&req, 7);
    memcpy(zlink_msg_data(&req), "request", 7);
    zlink_send(dealer, &req, 1, 0);

    /* Handler callback이 비동기로 message를 처리 */
    msleep(100);

    zlink_close(dealer);
    zlink_close(router);
    zlink_ctx_term(ctx);
    return 0;
}
```

---
[← 개요](01-overview.ko.md) | [Socket Pattern →](03-0-socket-patterns.ko.md)
