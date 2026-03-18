[English](03-5-stream.md) | [한국어](03-5-stream.ko.md)

# STREAM 소켓

## 1. 개요

STREAM 소켓은 **외부 RAW 클라이언트**와 통신하기 위한 **서버 전용** 소켓이다.

핵심 규칙:
- `ZLINK_STREAM`은 `zlink_bind()`만 지원한다.
- `ZLINK_STREAM`에 `zlink_connect()`를 호출하면 `EOPNOTSUPP`를 반환한다.
- 클라이언트는 zlink STREAM 소켓이 아니라 OS/Asio/WebSocket 등의 **raw client**를 사용해야 한다.
- wire 형식은 `4-byte length(big-endian) + body`이다.
- zlink API에서는 `[routing_id(4B)][payload]` 2프레임으로 수신/송신한다.

유효 조합:

```
외부 raw client  <---- RAW(4B length + body) ---->  STREAM(server)
```

> STREAM은 zlink 내부 소켓(PAIR/PUB/SUB/DEALER/ROUTER)과 직접 호환되지 않는다.

---

## 2. 서버 생성/바인드

```c
void *stream = zlink_socket(ctx, ZLINK_STREAM);
int linger = 0;
zlink_setsockopt(stream, ZLINK_LINGER, &linger, sizeof(linger));
zlink_bind(stream, "tcp://0.0.0.0:8080");
```

지원 transport(서버 bind):
- `tcp://`
- `tls://`
- `ws://`
- `wss://`

---

## 3. 메시지 모델

### 3.1 wire (네트워크)

```
+----------------------+-------------------+
| body_len (4B, BE)    | body (N bytes)    |
+----------------------+-------------------+
```

### 3.2 zlink STREAM API (애플리케이션)

STREAM 소켓에서 애플리케이션이 보는 형태:

```
Frame 0: routing_id (4 bytes)
Frame 1: payload (N bytes)
```

- `routing_id`는 서버가 연결별로 자동 할당한다.
- 고정 4바이트(`uint32`, big-endian)이다.

### 3.3 이벤트 payload

| payload | 의미 |
|---|---|
| `0x01` (1 byte) | connect 이벤트 |
| `0x00` (1 byte) | disconnect 이벤트 |
| 그 외 | 일반 데이터 |

---

## 4. 콜백 Dispatch (수신/응답)

STREAM은 모든 수신 작업에 콜백 전용 dispatch를 사용한다.
`recv()` / `zlink_msg_recv()`는 STREAM 소켓에서 지원하지 않는다.
`zlink_recv_handler()`로 콜백을 등록한다. 핸들러는 한 번 등록하면
영구적이며 해제할 수 없다.

### 콜백 Dispatch

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; i++) {
        void *data = zlink_msg_data(&parts[i]);
        size_t size = zlink_msg_size(&parts[i]);

        if (size == 1 && ((uint8_t *)data)[0] == 0x01) {
            /* 새 클라이언트 연결 */
        } else if (size == 1 && ((uint8_t *)data)[0] == 0x00) {
            /* 클라이언트 연결 해제 */
        } else {
            /* 에코 응답 */
            zlink_msg_t reply;
            zlink_msg_init_size(&reply, size);
            memcpy(zlink_msg_data(&reply), data, size);
            zlink_send_rid(stream, source_rid, &reply, 1, 0);
        }
        zlink_msg_close(&parts[i]);
    }
}

/* 콜백 dispatch attach (영구, 해제 불가) */
zlink_recv_handler(stream, on_message, NULL);
```

### 주요 사항

| 항목 | 설명 |
|---|---|
| Attach API | `zlink_recv_handler()` |
| 콜백 | `zlink_socket_msg_handler_fn` |
| 수명 | 한 번 등록하면 영구 (detach 없음) |
| 프레이밍 | transport에서 수신된 raw 바이트 |
| 전송 | `zlink_send_rid()` |

> 송신 큐가 가득 차면(HWM) `zlink_send_rid()`는 블록(기본) 또는
> `ZLINK_DONTWAIT`로 `EAGAIN`을 반환한다. 고급 backpressure 패턴은
> [성능 가이드](10-performance.ko.md)를 참고.

- 한 번에 하나의 콜백만 등록 가능하며, 이미 등록된 상태에서 attach를
  호출하면 `errno=EBUSY`와 함께 `-1`을 반환한다.
- 핸들러는 소켓 수명 동안 영구적이며 해제할 수 없다.
- 콜백 내부에서 close를 호출하는 것은 지원되지 않는다 (`EBUSY` 실패).

---

## 5. 클라이언트 구현 원칙

클라이언트는 raw socket/websocket로 구현한다.

POSIX TCP 예시(개념):

```c
// send: [4B length][body]
uint32_t len_be = htonl(body_len);
send(fd, &len_be, 4, 0);
send(fd, body, body_len, 0);

// recv: [4B length][body]
recv(fd, &len_be, 4, MSG_WAITALL);
uint32_t body_len = ntohl(len_be);
recv(fd, body, body_len, MSG_WAITALL);
```

---

## 6. 옵션 및 런타임 정책

주요 옵션:
- 지원: `ZLINK_MAXMSGSIZE`, `ZLINK_SNDHWM`, `ZLINK_RCVHWM`, `ZLINK_SNDBUF`, `ZLINK_RCVBUF`, `ZLINK_BACKLOG`, `ZLINK_LINGER`
- TLS/WSS 서버 옵션: `ZLINK_TLS_CERT`, `ZLINK_TLS_KEY`, `ZLINK_TLS_CA`, `ZLINK_TLS_HOSTNAME`, `ZLINK_TLS_TRUST_SYSTEM`

비지원/변경:
- `ZLINK_CONNECT_ROUTING_ID`를 STREAM에 설정하면 `EOPNOTSUPP`

### 6.1 STREAM 기본 런타임 프로파일

현재 STREAM 내부 기본값:
- `ZLINK_BACKLOG`: `65536`
- `ZLINK_SNDBUF`: 미지정(`-1`)이면 `262144`
- `ZLINK_RCVBUF`: 미지정(`-1`)이면 `262144`
- in/out batch 최소 크기: `12288`
- STREAM accept 동시성 기본값: `4` (최대 `128`로 clamp)
- STREAM 세션 스케줄링 기본값: `rr`

### 6.2 STREAM 런타임 환경변수 (현재 유지)

- `ZLINK_ASIO_STREAM_ACCEPT_CONCURRENCY` (기본 `4`, STREAM listener 전용)
- `ZLINK_ASIO_STREAM_SESSION_SCHED` (`rr|minload`, 기본 `rr`)
- `ZLINK_ASIO_STREAM_ENABLE_NON_TCP_SPEC_READ` (기본 off)
- `ZLINK_ASIO_STREAM_DISABLE_GATHER` (기본 off, gather on)
- `ZLINK_ASIO_STREAM_NOTIFY_QUEUE_DEQUE` (기본 on)
- `ZLINK_ASIO_STREAM_BATCH_SIZE` (기본 `12288`)

### 6.3 STREAM 튜닝 환경변수 제거(내부 상수 고정)

다음 STREAM env 토글은 제거되었고 코드 상수로 고정됨:
- `ZLINK_ASIO_STREAM_ENABLE_HANDLER_ALLOC` -> 항상 활성
- `ZLINK_ASIO_STREAM_ENABLE_READ_DRAIN` -> 항상 활성
- `ZLINK_ASIO_STREAM_ENABLE_SPECULATIVE_WRITE` -> STREAM/TCP 경로에서 상시 on 고정
- `ZLINK_ASIO_STREAM_ENABLE_RX_SLAB` -> 항상 활성
- `ZLINK_ASIO_STREAM_GATHER_THRESHOLD` -> `8192` 고정
- `ZLINK_ASIO_STREAM_SPEC_WRITE_BUDGET_BYTES` -> `2097152` 고정
- `ZLINK_ASIO_STREAM_READ_DRAIN_MAX_LOOPS` -> `16` 고정
- `ZLINK_ASIO_STREAM_READ_DRAIN_MAX_BYTES` -> `1048576` 고정

---

## 7. 에러/제약

- `zlink_connect(stream, ...)` -> `EOPNOTSUPP`
- STREAM에서 `routing_id` 프레임 크기가 4바이트가 아니면 프로토콜 오류
- `MAXMSGSIZE` 초과 메시지는 연결 종료(disconnect 이벤트)

---

## 8. 테스트 기준 구현

참고 파일:
- `core/tests/test_stream_socket.cpp`
- `core/tests/test_stream_fastpath.cpp`
- `core/tests/routing-id/test_connect_rid_string_alias.cpp`
- `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp`

위 테스트들은 STREAM 서버 + raw client 경로를 기준으로 동작한다.

---
[← ROUTER](03-4-router.ko.md) | [Transport →](04-transports.ko.md)
