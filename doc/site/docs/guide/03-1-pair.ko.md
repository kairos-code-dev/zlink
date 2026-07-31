[English](03-1-pair.md) | [한국어](03-1-pair.ko.md)

<!-- zlink-nav:start -->
[← 소켓 패턴](03-0-socket-patterns.ko.md) | [PUB/SUB →](03-2-pubsub.ko.md)
<!-- zlink-nav:end -->

# PAIR 소켓

## 1. 개요

PAIR 소켓은 정확히 하나의 피어와 1:1 양방향 독점 연결을 맺는다. 두 번째 피어가 연결하면 첫 번째 연결은 끊어진다.

**핵심 특성:**
- 단일 파이프만 허용 (1:1 독점)
- 양방향 자유 메시징 (send/recv 순서 무관)
- 가장 단순한 소켓 타입

**유효한 소켓 조합:** PAIR ↔ PAIR

```mermaid
flowchart LR
    A[PAIR A] <-->|Bidirectional| B[PAIR B]
```

## 2. 기본 사용법

### 생성 및 연결

```c
void *ctx = zlink_ctx_new();

/* Server side */
void *server = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
zlink_bind(server, "tcp://*:5555");

/* Client side */
void *client = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
zlink_connect(client, "tcp://127.0.0.1:5555");
```

### 메시지 교환

PAIR의 수신 API는 recv/poller 전용이다. 수신은 `zlink_recv()`와 poller를 조합해서
처리한다. 양쪽 모두 send와 recv를 자유롭게 호출할 수 있다.

```c
/* Client → Server */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 5);
memcpy(zlink_msg_data(&msg), "Hello", 5);
zlink_send(client, &msg, 1, 0);

/* Server receives with zlink_recv() (typically inside a poller loop) */
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
if (zlink_recv(server, &source_rid, &parts, &part_count, 0) == ZLINK_RECV_OK) {
    printf("Received: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    zlink_multipart_close(parts, part_count);
    free(parts);
}

/* Server → Client (bidirectional; client uses the same recv+poller pattern) */
zlink_msg_t reply;
zlink_msg_init_size(&reply, 5);
memcpy(zlink_msg_data(&reply), "World", 5);
zlink_send(server, &reply, 1, 0);
```

### 멀티파트 데이터 전송

멀티파트 데이터는 단일 `zlink_send` 호출로 parts 배열을 전송한다.

```c
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 3);
memcpy(zlink_msg_data(&parts[0]), "foo", 3);
zlink_msg_init_size(&parts[1], 6);
memcpy(zlink_msg_data(&parts[1]), "foobar", 6);
zlink_send(server, parts, 2, 0);

/* Receiver pulls both frames from one zlink_recv() call:
   parts[0] = "foo", parts[1] = "foobar", part_count = 2 */
```

> 참고: `core/tests/integration/test_pair_inproc.cpp` — `test_zlink_send_multipart()` 테스트

### 수신 모드

PAIR의 공개 수신 API는 recv/poller 전용이다.
`zlink_recv()`로 동기 수신한다.

```c
void *pair = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
zlink_bind(pair, "tcp://*:5556");

zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
zlink_recv_result_t rc = zlink_recv(
    pair, &source_rid, &parts, &part_count, 0 /* flags */);
if (rc == ZLINK_RECV_OK) {
    /* process parts[0..part_count-1] */
    zlink_multipart_close(parts, part_count);
}
```

> HWM(High-Water Mark, 큐 최대 허용 메시지 수) 도달 시 `zlink_send()`는 대기(기본) 또는 `ZLINK_DONTWAIT`로
> `ZLINK_SUBMIT_BACKPRESSURED`를 반환한다. 고급 배압(backpressure) 패턴은
> [성능 가이드](10-performance.ko.md)를 참고.

## 3. 메시지 형식

PAIR 소켓의 메시지 프레임에는 **애플리케이션 데이터만** 들어간다.

```
Single frame:     [data]
Multipart frame:  [frame1][frame2]...[frameN]
```

> `source_rid` 등 공통 수신 인터페이스는
> [소켓 패턴 개요](03-0-socket-patterns.ko.md#8-공통-수신-인터페이스)를 참고.

멀티파트 전송:

```c
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);
zlink_send(server, parts, 2, 0);
```

## 4. 소켓 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_OPT_SNDHWM` | int | 자동 (기본 floor 4) | control 역할의 자동 HWM 기본값. 수동 설정 시 자동값보다 우선 |
| `ZLINK_OPT_RCVHWM` | int | 자동 (기본 floor 4) | control 역할의 자동 HWM 기본값. 수동 설정 시 자동값보다 우선 |
| `ZLINK_OPT_LINGER` | int | -1 | close 시 미전송 메시지 대기 시간 (ms), -1=무한 |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | 송신 타임아웃 (ms), -1=무한 |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | 수신 타임아웃 (ms), -1=무한 |

```c
int hwm = 5000;
zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

int linger = 0;  /* return immediately on close */
zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
```

## 5. 사용 패턴

### 패턴 1: 스레드 간 시그널링 (inproc)

가장 일반적인 PAIR 사용 사례. inproc transport로 스레드 간 제로카피(zero-copy, 메모리 복사 없이 전달) 통신.

```c
/* Main thread */
void *signal = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
zlink_bind(signal, "inproc://signal");

/* Worker thread */
void *worker_signal = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
zlink_connect(worker_signal, "inproc://signal");

/* Worker → Main: task completion signal */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 4);
memcpy(zlink_msg_data(&msg), "DONE", 4);
zlink_send(worker_signal, &msg, 1, 0);

/* Main: on_signal callback receives "DONE" asynchronously */
```

> 참고: `core/tests/integration/test_pair_inproc.cpp` — bind → connect → bounce 패턴

### 패턴 2: TCP 통신

네트워크를 통한 1:1 통신. 와일드카드 바인드로 포트를 자동 할당할 수 있다.

```c
/* Server: wildcard port */
void *server = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
zlink_bind(server, "tcp://127.0.0.1:*");

/* Query the assigned endpoint */
char endpoint[256];
size_t len = sizeof(endpoint);
zlink_get_option(server, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

/* Client: connect using the queried endpoint */
void *client = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
zlink_connect(client, endpoint);
```

> 참고: `core/tests/integration/test_pair_tcp.cpp` — `bind_loopback_ipv4()` + 와일드카드 바인드

### 패턴 3: DNS 이름 연결

호스트명으로도 연결할 수 있다.

```c
void *client = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
zlink_connect(client, "tcp://localhost:5555");
```

> 참고: `core/tests/integration/test_pair_tcp.cpp` — `test_pair_tcp_connect_by_name()`

### 패턴 4: IPC 통신

같은 머신의 프로세스 간 통신 (Linux/macOS).

```c
void *server = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
zlink_bind(server, "ipc:///tmp/myapp.ipc");

void *client = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
zlink_connect(client, "ipc:///tmp/myapp.ipc");
```

> 참고: `core/tests/integration/test_pair_ipc.cpp` — IPC 경로 길이 검증 포함

## 6. 주의사항

### 단일 피어만 허용

PAIR 소켓은 하나의 연결만 유지한다. 두 번째 피어가 연결하면 첫 번째 연결이 끊어진다.

```
 Allowed:  PAIR A ↔ PAIR B      (1:1)
 Invalid:  PAIR A ← PAIR B      (N:1 attempt drops existing connection)
               ← PAIR C
```

N:1 통신이 필요하면 DEALER/ROUTER를 사용한다.

### inproc bind 순서

inproc transport는 **반드시 bind가 connect보다 먼저** 호출되어야 한다.

```c
/* Correct order */
zlink_bind(a, "inproc://signal");     /* 1. bind first */
zlink_connect(b, "inproc://signal");  /* 2. connect */

/* Wrong order -- fails */
zlink_connect(b, "inproc://signal");  /* fails because bind has not been called yet */
zlink_bind(a, "inproc://signal");
```

### IPC 경로 길이

IPC 엔드포인트의 파일 경로는 시스템 제한(보통 108자)을 넘을 수 없다.

```c
/* Path too long → ENAMETOOLONG error */
zlink_bind(socket, "ipc:///very/long/path/.../endpoint.ipc");
```

> 참고: `core/tests/integration/test_pair_ipc.cpp` — `test_endpoint_too_long()`

### HWM 동작

피어가 없거나 느릴 때 송신 메시지는 HWM까지 큐에 쌓인다. HWM을 넘으면 `zlink_send()`가 대기(기본) 또는 `ZLINK_SUBMIT_BACKPRESSURED`를 반환한다(`ZLINK_DONTWAIT`).

### LINGER 설정

`zlink_close()`를 호출할 때 미전송 메시지가 남아 있으면 LINGER 시간만큼 대기한다. 테스트나 빠른 종료가 필요한 경우:

```c
int linger = 0;
zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
```

---
[← 소켓 패턴](03-0-socket-patterns.ko.md) | [PUB/SUB →](03-2-pubsub.ko.md)

## 언어별 완전한 예제

PAIR 소켓으로 메시지를 주고받는 자립형 예제다(모든 바인딩, 빌드·실행 검증됨).

=== "C++"

    ```cpp
    --8<-- "bindings/cpp/samples/pair_recv_sample.cpp:doc"
    ```

=== "C#/.NET"

    ```csharp
    --8<-- "bindings/dotnet/samples/PairRecv/Program.cs:doc"
    ```

=== "Java"

    ```java
    --8<-- "bindings/java/samples/Zlink.Samples/src/main/java/systems/zlink/samples/PairRecvSample.java:doc"
    ```

=== "Kotlin"

    ```kotlin
    --8<-- "bindings/kotlin/samples/src/main/kotlin/systems/zlink/samples/PairRecvSample.kt:doc"
    ```

=== "Python"

    ```python
    --8<-- "bindings/python/samples/pair_recv_sample.py:doc"
    ```

=== "Node/TypeScript"

    ```typescript
    --8<-- "bindings/node/samples/pair_recv_sample.ts:doc"
    ```

=== "JavaScript"

    ```javascript
    --8<-- "bindings/javascript/samples/pair_recv_sample.js:doc"
    ```

=== "Go"

    ```go
    --8<-- "bindings/go/samples/pair_recv_sample/main.go:doc"
    ```

=== "Rust"

    ```rust
    --8<-- "bindings/rust/samples/pair_recv_sample.rs:doc"
    ```

---
<!-- zlink-nav:bottom:start -->
[← 소켓 패턴](03-0-socket-patterns.ko.md) | [PUB/SUB →](03-2-pubsub.ko.md)
<!-- zlink-nav:bottom:end -->
