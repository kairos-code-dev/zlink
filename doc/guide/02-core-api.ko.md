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

### 2.1 소켓 생성 및 닫기

```c
void *socket = zlink_socket(ctx, ZLINK_DEALER);
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
| `ZLINK_SNDHWM` | int | 1000 | 송신 High Water Mark |
| `ZLINK_RCVHWM` | int | 1000 | 수신 High Water Mark |
| `ZLINK_SNDTIMEO` | int | -1 | 송신 타임아웃 (ms, -1: 무제한) |
| `ZLINK_RCVTIMEO` | int | -1 | 수신 타임아웃 (ms, -1: 무제한) |
| `ZLINK_LINGER` | int | -1 | 소켓 닫기 시 대기 (ms) |
| `ZLINK_ROUTING_ID` | binary | 자동 | 소켓 라우팅 ID |
| `ZLINK_SUBSCRIBE` | binary | - | 구독 필터 (SUB 전용) |

## 3. 메시지 송수신

### 3.1 간단한 송수신

```c
/* 송신 */
zlink_send(socket, "Hello", 5, 0);

/* 수신 */
char buf[256];
int size = zlink_recv(socket, buf, sizeof(buf), 0);
```

### 3.2 플래그

| 플래그 | 설명 |
|--------|------|
| `ZLINK_DONTWAIT` | 논블로킹 모드 (데이터 없으면 즉시 EAGAIN) |
| `ZLINK_SNDMORE` | 멀티파트 메시지의 중간 프레임 |

### 3.3 논블로킹 수신

```c
int size = zlink_recv(socket, buf, sizeof(buf), ZLINK_DONTWAIT);
if (size == -1 && zlink_errno() == EAGAIN) {
    /* 데이터 없음 */
}
```

## 4. Poller API

### 4.1 zlink_poll

```c
zlink_pollitem_t items[] = {
    { socket1, 0, ZLINK_POLLIN, 0 },
    { socket2, 0, ZLINK_POLLIN, 0 },
};

int rc = zlink_poll(items, 2, 1000); /* 1초 타임아웃 */
if (rc > 0) {
    if (items[0].revents & ZLINK_POLLIN) {
        /* socket1에서 수신 가능 */
    }
    if (items[1].revents & ZLINK_POLLIN) {
        /* socket2에서 수신 가능 */
    }
}
```

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

int main(void) {
    void *ctx = zlink_ctx_new();

    /* ROUTER (서버) */
    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_bind(router, "tcp://*:5555");

    /* DEALER (클라이언트) */
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);
    zlink_connect(dealer, "tcp://127.0.0.1:5555");

    /* DEALER → ROUTER */
    zlink_send(dealer, "request", 7, 0);

    /* ROUTER: routing_id + data 수신 */
    zlink_msg_t id, body;
    zlink_msg_init(&id);
    zlink_msg_init(&body);
    zlink_msg_recv(&id, router, 0);    /* routing_id 프레임 */
    zlink_msg_recv(&body, router, 0);  /* 데이터 프레임 */

    printf("수신: %.*s\n",
           (int)zlink_msg_size(&body),
           (char *)zlink_msg_data(&body));

    /* ROUTER → DEALER (응답) */
    zlink_msg_send(&id, router, ZLINK_SNDMORE);
    zlink_send(router, "reply", 5, 0);

    /* DEALER 응답 수신 */
    char buf[256];
    int size = zlink_recv(dealer, buf, sizeof(buf), 0);
    buf[size] = '\0';
    printf("응답: %s\n", buf);

    zlink_msg_close(&id);
    zlink_msg_close(&body);
    zlink_close(dealer);
    zlink_close(router);
    zlink_ctx_term(ctx);
    return 0;
}
```
