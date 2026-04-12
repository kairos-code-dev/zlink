[English](03-5-stream.md) | [한국어](03-5-stream.ko.md)

# STREAM 소켓

## 1. 개요

STREAM 소켓은 **외부 RAW 클라이언트**와 통신하기 위한 **서버 전용** 소켓이다.

핵심 규칙:
- `ZLINK_STREAM`은 `zlink_bind()`만 지원한다.
- `ZLINK_STREAM`에 `zlink_connect()`를 호출하면 `EOPNOTSUPP`를 반환한다.
- 클라이언트는 zlink STREAM 소켓이 아니라 OS/Asio/WebSocket 등의 **raw client**를 사용해야 한다.
- STREAM은 raw 바이트 스트림을 그대로 전달한다. **프레이밍(패킷 경계)은 사용자가 정의**해야 한다.
- zlink API에서 수신/송신 시 `source_rid`(서버가 자동 할당한 4B 연결 식별자)로 클라이언트를 구분한다.

유효 조합:

```
external raw client  <---- RAW(4B length + body) ---->  STREAM(server)
```

> STREAM은 zlink 내부 소켓(PAIR/PUB/SUB/DEALER/ROUTER)과 직접 호환되지 않는다.

---

## 2. 서버 생성/바인드

```c
void *stream = zlink_socket(ctx, ZLINK_STREAM);
int linger = 0;
zlink_set_option(stream, ZLINK_OPT_LINGER, &linger, sizeof(linger));
zlink_bind(stream, "tcp://0.0.0.0:8080");
```

지원 transport(서버 bind):
- `tcp://`
- `tls://`
- `ws://`
- `wss://`

---

## 3. STREAM 고유 동작

STREAM은 다른 소켓과 동일한 recv/callback 모델을 사용한다.
STREAM만의 고유 동작은 다음과 같다.

- `source_rid`는 서버가 연결별로 자동 할당하며,
  고정 4바이트(`uint32`, big-endian)이다.
- 연결/해제 이벤트가 메시지로 전달된다:

| payload 값 | 의미 |
|------------|------|
| `0x01` (1 byte) | connect 이벤트 |
| `0x00` (1 byte) | disconnect 이벤트 |
| 그 외 | 일반 데이터 |

??? example "Full Sample Code -- Recv"

    | Language | Source |
    |----------|--------|
    | C | [stream_recv_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/stream_recv_sample.c) |
    | C++ | [stream_recv_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/stream_recv_sample.cpp) |
    | Java | [StreamRecvSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/StreamRecvSample.java) |
    | Python | [stream_recv.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/stream_recv.py) |
    | Node | [stream_recv_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/stream_recv_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/StreamRecv/Program.cs) |
    | Rust | [stream_recv_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/stream_recv_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/stream_recv_sample/main.go) |

---

## 4. 콜백 예시

STREAM의 콜백 디스패치(수신 메시지를 콜백으로 전달) 과정에서는
connect/disconnect 이벤트와 데이터를 구분해야 한다.

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; i++) {
        void *data = zlink_msg_data(&parts[i]);
        size_t size = zlink_msg_size(&parts[i]);

        if (size == 1 && ((uint8_t *)data)[0] == 0x01) {
            /* new client connected */
        } else if (size == 1 && ((uint8_t *)data)[0] == 0x00) {
            /* client disconnected */
        } else {
            /* echo reply */
            zlink_msg_t reply;
            zlink_msg_init_size(&reply, size);
            memcpy(zlink_msg_data(&reply), data, size);
            zlink_send_rid(stream, source_rid, &reply, 1, 0);
        }
        zlink_msg_close(&parts[i]);
    }
}

/* Attach callback dispatch (permanent, cannot be undone) */
zlink_recv_handler(stream, on_message, NULL);
```

### 주요 사항

| 항목 | 설명 |
|---|---|
| Attach API | `zlink_recv_handler()` |
| 콜백 | `zlink_socket_msg_handler_fn` |
| 수명 | replace-only attach, detach 없음 |
| 프레이밍 | transport에서 수신된 raw 바이트 |
| 전송 | `zlink_send_rid()` |

> 송신 큐가 가득 차면(HWM) `zlink_send_rid()`는 블록(기본) 또는
> `ZLINK_DONTWAIT` 로 `ZLINK_SUBMIT_BACKPRESSURED` 를 반환한다. 고급 backpressure 패턴은
> [성능 가이드](10-performance.ko.md)를 참고.

- 한 번에 하나의 receive callback만 등록 가능하며, 이미 등록된 상태에서 attach를
  호출하면 `errno=EBUSY`와 함께 `-1`을 반환한다.
- receive callback이 활성인 동안 direct recv 계열과 data-plane `POLLIN`은
  `EBUSY`다.
- 콜백 내부에서 close를 호출하는 것은 지원되지 않는다 (`EBUSY` 실패).

??? example "Full Sample Code -- Callback"

    | Language | Source |
    |----------|--------|
    | C | [stream_callback_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/stream_callback_sample.c) |
    | C++ | [stream_callback_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/stream_callback_sample.cpp) |
    | Java | [StreamCallbackSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/StreamCallbackSample.java) |
    | Python | [stream_callback.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/stream_callback.py) |
    | Node | [stream_callback_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/stream_callback_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/StreamCallback/Program.cs) |
    | Rust | [stream_callback_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/stream_callback_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/stream_callback_sample/main.go) |

---

## 5. 클라이언트 구현 원칙

클라이언트는 raw socket/websocket로 구현한다.
STREAM은 raw 바이트를 그대로 전달하므로, **패킷 경계(프레이밍)는 애플리케이션이 정의**해야 한다.

아래는 `[4B length][body]` 형식을 사용자가 정의한 POSIX TCP 예시(개념):

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

- 지원:
  - `ZLINK_OPT_MAXMSGSIZE`
  - `ZLINK_OPT_SNDHWM` / `ZLINK_OPT_RCVHWM`
  - `ZLINK_OPT_SNDBUF` / `ZLINK_OPT_RCVBUF`
  - `ZLINK_OPT_BACKLOG`
  - `ZLINK_OPT_LINGER`
- TLS/WSS 서버: `zlink_set_tls_server()`
- TLS 클라이언트: `zlink_set_tls_client()`

비지원/변경:
- `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID`를 STREAM에 설정하면 `EOPNOTSUPP`

### 6.1 STREAM 기본 런타임 프로파일

현재 STREAM 내부 기본값:
- `ZLINK_OPT_BACKLOG`: `65536`
- `ZLINK_OPT_SNDBUF`: 미지정(`-1`)이면 `262144`
- `ZLINK_OPT_RCVBUF`: 미지정(`-1`)이면 `262144`
- in/out batch 최소 크기: `12288`
- STREAM accept 동시성 기본값: `4` (최대 `128`로 clamp)
- STREAM 세션 스케줄링 기본값: `rr`

> STREAM 런타임 환경변수 및 내부 튜닝 상수는
> [STREAM 내부 문서](../internals/stream-socket.ko.md)를 참고.

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
[← ROUTER](03-4-router.ko.md) | [Proxy →](03-6-proxy.ko.md) | [Transport →](04-transports.ko.md)
