[English](02-core-api.md) | [한국어](02-core-api.ko.md)

# Core C API 상세 가이드

## 1. Context API

Context는 zlink의 최상위 객체로, I/O 스레드 풀과 소켓을 관리한다.

```c
/* 생성 */
void *ctx = zlink_ctx_new();

/* 설정 */
zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);     /* I/O 스레드 수 (기본 2) */
zlink_ctx_set(ctx, ZLINK_MAX_SOCKETS, 2048); /* 최대 소켓 수 (기본 1023) */

/* 조회 */
int io_threads = zlink_ctx_get(ctx, ZLINK_IO_THREADS);

/* 종료 */
zlink_ctx_term(ctx);  /* 모든 소켓이 닫힌 후 반환 */
```

### Context 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `ZLINK_IO_THREADS` | 2 | I/O 스레드 수 |
| `ZLINK_MAX_SOCKETS` | 1023 | 최대 소켓 수 |
| `ZLINK_MAX_MSGSZ` | -1 | 최대 메시지 크기 (-1: 무제한) |

## 2. Socket API

### 2.0 스레드 안전성 빠른 요약

공개 socket handle API는 기본적으로 thread-safe다. 다만 모든 API가 같은 비용
모델을 갖는 것은 아니다.

- `zlink_send()`는 same-handle concurrent 호출을 허용하는 hot path다.
- `bind/connect/disconnect`, subscribe/unsubscribe, monitor, option/query는
  runtime에 호출할 수 있으며 correctness 우선 직렬화 계층이다.
- `zlink_close()`는 fail-fast lifecycle gate를 사용한다. 다른 스레드가 같은
  handle에서 admitted API나 callback을 실행 중이면 `EBUSY`, close가 accepted된
  뒤 새 API 진입은 `ESHUTDOWN`이다.
- 예외는 소수만 남긴다. init-only 성격의 설정, callback context에서 금지된
  일부 reentrant API, 같은 `zlink_msg_t` 객체의 동시 공유는 기본 허용 범위
  밖이다.

### 2.1 소켓 생성 및 닫기

```c
void *socket = zlink_socket(ctx, ZLINK_DEALER, NULL);
/* ... 사용 ... */
zlink_close(socket);
```

### 2.2 소켓 타입 상수

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_PAIR` | 0 | 1:1 양방향 |
| `ZLINK_PUB` | 1 | 발행자 |
| `ZLINK_SUB` | 2 | 구독자 |
| `ZLINK_DEALER` | 5 | 비동기 요청 |
| `ZLINK_ROUTER` | 6 | 라우팅 |
| `ZLINK_XPUB` | 9 | 고급 발행자 |
| `ZLINK_XSUB` | 10 | 고급 구독자 |
| `ZLINK_STREAM` | 11 | RAW 통신 |

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
| `ZLINK_SNDHWM` | int | 300000 | 송신 High Water Mark |
| `ZLINK_RCVHWM` | int | 300000 | 수신 High Water Mark |
| `ZLINK_SNDTIMEO` | int | -1 | 송신 타임아웃 (ms, -1: 무제한) |
| `ZLINK_RCVTIMEO` | int | -1 | 수신 타임아웃 (ms, -1: 무제한) |
| `ZLINK_LINGER` | int | -1 | 소켓 닫기 시 대기 (ms) |
| `ZLINK_ROUTING_ID` | binary | 자동 | 소켓 라우팅 ID |
| `ZLINK_SUBSCRIBE` | binary | - | 구독 필터 (SUB 전용) |

`ZLINK_SUBSCRIBE` / `ZLINK_UNSUBSCRIBE`, `ZLINK_EVENTS`,
`ZLINK_LAST_ENDPOINT`, `ZLINK_RCVMORE` 같은 옵션과 조회는 runtime 중간에도
의미가 있다. 반면 HWM, 타임아웃, TLS 같은 대부분의 튜닝 옵션은 보통 초기
설정에 가깝다.

## 3. 메시지 송수신

### 3.1 송신

```c
/* 단순 송신 */
zlink_send(socket, "Hello", 5, 0);

/* 멀티파트 송신 */
zlink_send(socket, "header", 6, ZLINK_SNDMORE);
zlink_send(socket, "body", 4, 0);
```

### 3.2 수신 (콜백 핸들러)

모든 수신은 소켓 생성 시 등록한 핸들러 콜백을 통해 디스패치된다.
`recv()` 함수는 없으며, 메시지가 도착하면 콜백이 비동기로 호출된다.

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count)
{
    for (size_t i = 0; i < part_count; i++) {
        printf("프레임 %zu: %.*s\n", i,
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}

zlink_socket_handler_t handler = {
    .kind = ZLINK_SOCKET_HANDLER_MSG,
    .fn.msg = on_message
};
void *socket = zlink_socket(ctx, ZLINK_PAIR, &handler);
```

### 3.3 송신 플래그

| 플래그 | 설명 |
|--------|------|
| `ZLINK_DONTWAIT` | 논블로킹 모드 (송신 불가 시 즉시 EAGAIN 반환) |
| `ZLINK_SNDMORE` | 멀티파트 메시지의 중간 프레임 |

## 4. 핸들러 타입

각 소켓 타입은 생성 시 특정 핸들러 종류를 받는다:

| 소켓 타입 | 핸들러 종류 | 콜백 시그니처 |
|---|---|---|
| PAIR, DEALER, ROUTER | `ZLINK_SOCKET_HANDLER_MSG` | `void fn(const zlink_routing_id_t *rid, zlink_msg_t *parts, size_t count)` |
| SUB | `ZLINK_SOCKET_HANDLER_SPOT` | `void fn(const zlink_routing_id_t *rid, const char *topic, size_t topic_len, zlink_msg_t *parts, size_t count)` |
| XPUB | `ZLINK_SOCKET_HANDLER_XPUB` | `void fn(int subscribed, const uint8_t *topic, size_t topic_len)` |
| PUB, XSUB | N/A (NULL) | 송신 전용 소켓 |

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
                       zlink_msg_t *parts, size_t part_count)
{
    printf("[%.*s]로부터 수신: %.*s\n",
           (int)source_rid->size, source_rid->data,
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

void on_dealer_message(const zlink_routing_id_t *source_rid,
                       zlink_msg_t *parts, size_t part_count)
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
    zlink_socket_handler_t router_handler = {
        .kind = ZLINK_SOCKET_HANDLER_MSG,
        .fn.msg = on_router_message
    };
    void *router = zlink_socket(ctx, ZLINK_ROUTER, &router_handler);
    zlink_bind(router, "tcp://*:5555");

    /* DEALER (클라이언트) */
    zlink_socket_handler_t dealer_handler = {
        .kind = ZLINK_SOCKET_HANDLER_MSG,
        .fn.msg = on_dealer_message
    };
    void *dealer = zlink_socket(ctx, ZLINK_DEALER, &dealer_handler);
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
