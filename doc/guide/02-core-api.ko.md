[English](02-core-api.md) | [한국어](02-core-api.ko.md)

# Core C API 상세 가이드

## 1. Context API

Context는 zlink의 최상위 객체로, I/O thread pool과 socket을 관리한다.

> I/O 스레드가 내부에서 어떤 일을 하는지(이벤트 루프, 명령 처리,
> 소켓 할당) 상세 설명은
> [I/O Thread 내부 구조](../internals/io-thread.ko.md)를 참고.

```c
/* Create */
void *ctx = zlink_ctx_new();

/* Configure — increase I/O threads for multi-connection servers */
zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);     /* default 1; 4 is optimal under heavy load */

/* Query */
int io_threads = zlink_ctx_get(ctx, ZLINK_IO_THREADS);

/* Terminate */
zlink_ctx_term(ctx);  /* Returns after all sockets are closed */
```

### Context 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `ZLINK_IO_THREADS` | 1 | I/O thread 수 |
| `ZLINK_MAX_SOCKETS` | 4095 | 최대 socket 수 |
| `ZLINK_MAX_MSGSZ` | -1 | 최대 message 크기 (-1: 무제한) |

## 2. Socket API

공개 socket handle API는 기본적으로 스레드 안전(thread-safe)하다. 여러 thread에서
같은 socket handle을 공유하여 send/recv/bind/connect 등을 호출할 수 있다.

> 세부 threading 규칙은 [Thread Safety 가이드](11-thread-safety.ko.md)를 참고.

### 2.1 Socket 생성 및 닫기

```c
void *socket = zlink_socket(ctx, ZLINK_DEALER);
/* ... use ... */
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

/* Unbind */
zlink_unbind(socket, "tcp://*:5555");
zlink_disconnect(socket, "tcp://127.0.0.1:5555");
```

### 2.4 Socket Option

```c
/* Set option */
int hwm = 5000;
zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

/* Get option */
int value;
size_t len = sizeof(value);
zlink_get_option(socket, ZLINK_OPT_SNDHWM, &value, &len);
```

주요 옵션:

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_OPT_SNDHWM` | int | 1000 | Send High Water Mark |
| `ZLINK_OPT_RCVHWM` | int | 1000 | Recv High Water Mark |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | Send timeout (ms, -1: 무제한) |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | Recv timeout (ms, -1: 무제한) |
| `ZLINK_OPT_LINGER` | int | -1 | Socket close 시 linger (ms) |

Routing ID는 전용 함수로 설정/조회한다:
`zlink_set_routing_id()` / `zlink_get_routing_id()`.
구독 관리는 `zlink_set_subscription()`을 사용한다.

`ZLINK_OPT_EVENTS`, `ZLINK_OPT_LAST_ENDPOINT` 같은 option은
runtime 중간에도 보통 사용한다. 반면 HWM, timeout, TLS 같은 대부분의
tuning option은 초기 설정 단계에서 사용한다.

상세 option 카테고리와 전체 option 레퍼런스는
[소켓 옵션 가이드](12-socket-options.ko.md)를 참고.

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

#### Logical Multipart Send

`zlink_send()`, `zlink_publish()` 등 공개/서비스 surface의
멀티파트 송신은 내부적으로 공통 **logical multipart send** 모듈을 사용한다.
이 모듈은 다음 의미를 공통으로 보장한다.

- **nonblocking**: one-shot 시도 후 실패 시 partial local state rollback
- **blocking**: `sndtimeo` deadline까지 whole-message 단위 재시도
- **재시도 대상**: `EAGAIN`, `EINTR`만 재시도, 그 외 오류는 즉시 실패
- **whole-message 보장**: 멀티파트 메시지는 전체가 성공하거나 전체가 실패한다

이 보장 덕분에 멀티파트 메시지는 전체가 큐에 들어가거나 전체가 실패한다.
부분만 큐에 들어가는 일은 발생하지 않는다.

> Wire 수준 프레임 구조는
> [ZMP 프로토콜](../internals/protocol-zmp.ko.md)을 참고.

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
        printf("Frame %zu: %.*s\n", i,
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}

/* Non-blocking recv */
rc = zlink_recv(socket, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
if (rc == -1 && zlink_errno() == EAGAIN) {
    /* No message available right now */
}
```

#### Callback Mode

Socket 생성 후 핸들러 콜백을 부착하면 메시지 도착 시 I/O 스레드에서
비동기로 호출된다. 한번 부착하면 socket 수명 동안 해제할 수 없다.
Handler가 부착된 상태에서 `zlink_recv()` 호출 시 `EBUSY`를 반환한다.

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; i++) {
        printf("Frame %zu: %.*s\n", i,
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}

void *socket = zlink_socket(ctx, ZLINK_STREAM);
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
| STREAM | `zlink_recv_handler()` | `fn(rid, parts, count, userdata)` |
| ROUTER (request-reply) | `zlink_router_handler()` | `fn(peer_rid, request_seq, parts, count, userdata)` |
| SPOT (routed) | `zlink_spot_handler()` | `fn(source_rid, spot_rid, request_seq, parts, count, userdata)` |
| ROUTER (from SPOT) | `zlink_router_spot_handler()` | `fn(source_node_rid, source_spot_rid, request_seq, parts, count, userdata)` |
| spot, spot_node (topic) | `zlink_subscribe_handler()` | `fn(rid, topic, topic_len, parts, count, userdata)` |
| DEALER (reply) | `zlink_dealer_request()` 에 전달 | `fn(errno, parts, count, userdata)` |
| Timer | `zlink_timer_handler()` | `fn(timer, fire_count, userdata)` |
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
    printf("Error: %s\n", zlink_strerror(err));
}
```

주요 error code:

| Error | 값 | 설명 |
|-------|-----|------|
| `EAGAIN` | POSIX | Non-blocking mode에서 즉시 완료 불가 |
| `EBUSY` | POSIX | recv/callback 모드 충돌 (handler가 이미 부착됨) |
| `EINTR` | POSIX | Signal에 의해 interrupt됨 |
| `ENOTSOCK` | `HAUSNUMERO + 9` | 유효하지 않은 socket |
| `EHOSTUNREACH` | `HAUSNUMERO + 17` | Host 도달 불가 |
| `EFSM` | `HAUSNUMERO + 51` | 현재 state에서 허용되지 않는 연산 |
| `ETERM` | `HAUSNUMERO + 53` | Context terminated |
| `ENOTSUP` | `HAUSNUMERO + 1` | 해당 subject type에서 지원하지 않는 연산 |
| `ENOENT` | — | 대상을 찾을 수 없음 (예: SPOT 대상 미발견) |

> `ZLINK_HAUSNUMERO` = 156384712. POSIX errno와 충돌하지 않는 zlink 전용 base 값이다.

## 6. Timer API

Timer는 socket과 동일한 recv/callback/poller 모델을 지원하는 일급(first-class) 이벤트 소스다.

### 6.1 일반 Timer

```c
void *timer = zlink_timer_new();

/* Start: 100ms 간격, 무한 반복 (0 = infinite) */
zlink_timer_start(timer, 100000000ULL, 0);  /* interval_ns, repeat_count */

/* Pull 모드 */
uint64_t fire_count;
int rc = zlink_timer_recv(timer, &fire_count, 0);

/* Callback 모드 */
void on_fire(void *timer, uint64_t fire_count, void *userdata) {
    /* timer 이벤트 처리 */
}
zlink_timer_handler(timer, on_fire, NULL);

/* 정지 및 해제 */
zlink_timer_stop(timer);
zlink_timer_destroy(&timer);
```

### 6.2 SPOT Timer

SPOT timer는 global scheduler 대신 SpotNode-local shared scheduler를 사용한다.

```c
void *spot_timer = zlink_spot_timer_new(spot);
zlink_timer_start(spot_timer, 50000000ULL, 10);  /* 50ms, 10회 반복 */
```

### 6.3 Poller 통합

Timer를 socket, file descriptor와 함께 poller에 등록할 수 있다.

```c
zlink_poller_add_timer(poller, timer, user_data);
/* ... zlink_poller_wait()가 timer 이벤트를 반환 ... */
zlink_poller_remove_timer(poller, timer);
```

### 핵심 규칙

| 규칙 | 설명 |
|------|------|
| `repeat_count=0` | 무한 반복 |
| `repeat_count=N` | 정확히 N회 fire 후 자동 정지 |
| recv vs callback | 충돌 시 `EBUSY` 반환 (socket과 동일) |
| 일반 timer | global shared scheduler 사용 |
| SPOT timer | SpotNode-local shared scheduler 사용 |

## 7. DEALER/ROUTER 예제

```c
#include <zlink.h>
#include <string.h>
#include <stdio.h>

void on_router_message(const zlink_routing_id_t *source_rid,
                       zlink_msg_t *parts, size_t part_count,
                       void *userdata)
{
    printf("Received from [%.*s]: %.*s\n",
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
    printf("Reply: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

int main(void) {
    void *ctx = zlink_ctx_new();

    /* ROUTER (server) */
    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    /* Receive with zlink_recv() */
    zlink_bind(router, "tcp://*:5555");

    /* DEALER (client) */
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);
    /* Receive with zlink_recv() */
    zlink_connect(dealer, "tcp://127.0.0.1:5555");

    /* DEALER → ROUTER */
    zlink_msg_t req;
    zlink_msg_init_size(&req, 7);
    memcpy(zlink_msg_data(&req), "request", 7);
    zlink_send(dealer, &req, 1, 0);

    /* Handler callbacks process messages asynchronously */
    msleep(100);

    zlink_close(dealer);
    zlink_close(router);
    zlink_ctx_term(ctx);
    return 0;
}
```

---
[← 개요](01-overview.ko.md) | [Socket Pattern →](03-0-socket-patterns.ko.md)
