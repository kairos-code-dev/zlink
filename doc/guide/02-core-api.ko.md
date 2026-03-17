[English](02-core-api.md) | [한국어](02-core-api.ko.md)

# Core C API 상세 가이드

## 1. Context API

Context는 zlink의 최상위 객체로, I/O 스레드 풀과 소켓을 관리한다.

```c
/* 생성 */
void *ctx = zlink_ctx_new();

/* 설정 — 다중 연결 서버에서는 I/O 스레드를 늘린다 */
zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);     /* 기본 1; 연결이 많으면 4가 최적 */

/* 조회 */
int io_threads = zlink_ctx_get(ctx, ZLINK_IO_THREADS);

/* 종료 */
zlink_ctx_term(ctx);  /* 모든 소켓이 닫힌 후 반환 */
```

### Context 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `ZLINK_IO_THREADS` | 1 | I/O 스레드 수 |
| `ZLINK_MAX_SOCKETS` | 4095 | 최대 소켓 수 |
| `ZLINK_MAX_MSGSZ` | -1 | 최대 메시지 크기 (-1: 무제한) |

## 2. Socket API

공개 socket handle API는 기본적으로 thread-safe다. 여러 스레드에서
같은 소켓 핸들을 공유하여 send/recv/bind/connect 등을 호출할 수 있다.

> 세부 스레딩 규칙은 [스레드 안전성 가이드](11-thread-safety.ko.md)를 참고.

### 2.1 소켓 생성 및 닫기

```c
void *socket = zlink_socket(ctx, ZLINK_DEALER);
/* ... 사용 ... */
zlink_close(socket);
```

### 2.2 소켓 타입 상수

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_PAIR` | 0x1001 | 1:1 양방향 |
| `ZLINK_PUB` | 0x1002 | 발행자 |
| `ZLINK_SUB` | 0x1003 | 구독자 |
| `ZLINK_DEALER` | 0x1004 | 비동기 요청 |
| `ZLINK_ROUTER` | 0x1005 | 라우팅 |
| `ZLINK_XPUB` | 0x1006 | 고급 발행자 |
| `ZLINK_XSUB` | 0x1007 | 고급 구독자 |
| `ZLINK_STREAM` | 0x1008 | RAW 통신 |

### 2.3 연결 관리

```c
/* 바인드 (서버) */
zlink_bind(socket, "tcp://*:5555");

/* 연결 (클라이언트) */
zlink_connect(socket, "tcp://127.0.0.1:5555");

/* 해제 */
zlink_unbind(socket, "tcp://*:5555");
zlink_disconnect(socket, "tcp://127.0.0.1:5555");
```

### 2.4 소켓 옵션

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
| `ZLINK_SNDHWM` | int | 1000 | 송신 High Water Mark |
| `ZLINK_RCVHWM` | int | 1000 | 수신 High Water Mark |
| `ZLINK_SNDTIMEO` | int | -1 | 송신 타임아웃 (ms, -1: 무제한) |
| `ZLINK_RCVTIMEO` | int | -1 | 수신 타임아웃 (ms, -1: 무제한) |
| `ZLINK_LINGER` | int | -1 | 소켓 닫기 시 대기 (ms) |
| `ZLINK_ROUTING_ID` | binary | 자동 | 소켓 라우팅 ID |
| `ZLINK_SUBSCRIBE` | binary | - | 구독 필터 (SUB 전용) |

`ZLINK_SUBSCRIBE` / `ZLINK_UNSUBSCRIBE`, `ZLINK_EVENTS`,
`ZLINK_LAST_ENDPOINT`, `ZLINK_RCVMORE` 같은 옵션은 runtime 중간에도
보통 사용한다. 반면 HWM, 타임아웃, TLS 같은 대부분의 튜닝 옵션은
초기 설정 단계에서 사용한다.

## 3. 메시지 송수신

### 3.1 송신

```c
/* 단순 송신 */
zlink_send(socket, "Hello", 5, 0);

/* 멀티파트 송신 */
zlink_send(socket, "header", 6, ZLINK_SNDMORE);
zlink_send(socket, "body", 4, 0);
```

기본적으로 `zlink_send()`는 전송 큐가 가득 차면(HWM 도달) 블로킹한다.
`ZLINK_DONTWAIT` 플래그를 사용하면 블로킹 대신 즉시 `EAGAIN`을 반환한다.
고급 backpressure 패턴은
[성능 가이드](10-performance.ko.md)를 참고.

### 3.2 수신

zlink 소켓은 두 가지 수신 모드를 지원한다:

#### Pull 모드 (동기식)

핸들러를 부착하지 않으면 `zlink_recv()`로 직접 메시지를 받을 수 있다.
소켓은 기본적으로 pull 모드로 시작한다.

```c
void *socket = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(socket, "tcp://*:5556");

/* 블로킹 수신 */
char buf[256];
int nbytes = zlink_recv(socket, buf, sizeof(buf), 0);

/* 논블로킹 수신 */
nbytes = zlink_recv(socket, buf, sizeof(buf), ZLINK_DONTWAIT);
if (nbytes == -1 && zlink_errno() == EAGAIN) {
    /* 현재 사용 가능한 메시지 없음 */
}
```

#### Callback 모드

소켓 생성 후 핸들러 콜백을 부착하면 메시지 도착 시 I/O 스레드에서
비동기로 호출된다. 한번 부착하면 소켓 수명 동안 해제할 수 없다.
핸들러가 부착된 상태에서 `zlink_recv()` 호출 시 `EBUSY`를 반환한다.

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; i++) {
        printf("프레임 %zu: %.*s\n", i,
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}

void *socket = zlink_socket(ctx, ZLINK_PAIR);
zlink_recv_handler(socket, on_message, NULL);
```

> 두 모드의 비교와 고급 패턴은
> [성능 가이드](10-performance.ko.md)를 참고.

### 3.3 송신 플래그

| 플래그 | 설명 |
|--------|------|
| `ZLINK_DONTWAIT` | 논블로킹 모드 (송수신 불가 시 즉시 EAGAIN 반환) |
| `ZLINK_SNDMORE` | 멀티파트 메시지의 중간 프레임 |

## 4. 핸들러 타입

각 소켓 타입은 전용 등록 함수를 사용한다:

| 소켓 타입 | 등록 호출 | 콜백 시그니처 |
|---|---|---|
| PAIR, DEALER, ROUTER, STREAM | `zlink_recv_handler(socket, fn, userdata)` | `void fn(const zlink_routing_id_t *rid, zlink_msg_t *parts, size_t count, void *userdata)` |
| SUB, XSUB | `zlink_recv_spot_handler(socket, fn, userdata)` | `void fn(const zlink_routing_id_t *rid, const char *topic, size_t topic_len, zlink_msg_t *parts, size_t count, void *userdata)` |
| XPUB | `zlink_recv_xpub_handler(socket, fn, userdata)` | `void fn(int subscribed, const uint8_t *topic, size_t topic_len, void *userdata)` |
| PUB | N/A | 송신 전용 소켓 |

콜백은 I/O 스레드에서 호출된다. 콜백 내부에서 블로킹 작업을 피해야 한다.
느린 처리가 필요하면 사용자 큐에 넣고 별도 스레드에서 처리한다.

## 5. 에러 처리

```c
int rc = zlink_send(socket, data, size, 0);
if (rc == -1) {
    int err = zlink_errno();
    printf("에러: %s\n", zlink_strerror(err));
}
```

주요 에러 코드:

| 에러 | 설명 |
|------|------|
| `EAGAIN` | 논블로킹 모드에서 즉시 완료 불가 |
| `ETERM` | 컨텍스트 종료됨 |
| `ENOTSOCK` | 유효하지 않은 소켓 |
| `EINTR` | 시그널로 인터럽트됨 |
| `EFSM` | 현재 상태에서 허용되지 않는 연산 |
| `EHOSTUNREACH` | 호스트 도달 불가 |

## 6. DEALER/ROUTER 예제

```c
#include <zlink.h>
#include <string.h>
#include <stdio.h>

void on_router_message(const zlink_routing_id_t *source_rid,
                       zlink_msg_t *parts, size_t part_count,
                       void *userdata)
{
    printf("[%.*s]로부터 수신: %.*s\n",
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
    printf("응답: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

int main(void) {
    void *ctx = zlink_ctx_new();

    /* ROUTER (서버) */
    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_recv_handler(router, on_router_message, NULL);
    zlink_bind(router, "tcp://*:5555");

    /* DEALER (클라이언트) */
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);
    zlink_recv_handler(dealer, on_dealer_message, NULL);
    zlink_connect(dealer, "tcp://127.0.0.1:5555");

    /* DEALER → ROUTER */
    zlink_send(dealer, "request", 7, 0);

    /* 핸들러 콜백이 비동기로 메시지를 처리 */
    msleep(100);

    zlink_close(dealer);
    zlink_close(router);
    zlink_ctx_term(ctx);
    return 0;
}
```

---
[← 개요](01-overview.ko.md) | [소켓 패턴 →](03-0-socket-patterns.ko.md)
