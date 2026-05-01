[English](03-5-stream.md) | [한국어](03-5-stream.ko.md)

# STREAM 소켓

## 1. 개요

STREAM 소켓은 **외부 RAW 클라이언트**와 통신하기 위한 **서버 전용** 소켓이다.

핵심 규칙:
- `ZLINK_SOCKET_STREAM`은 `zlink_bind()`만 지원한다.
- `ZLINK_SOCKET_STREAM`에 `zlink_connect()`를 호출하면 `EOPNOTSUPP`를 반환한다.
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
void *stream = zlink_socket(ctx, ZLINK_SOCKET_STREAM);
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

STREAM은 raw socket family에서 유일한 예외 타입이다. 한 handle에서 세
가지 수신 모델 중 정확히 하나를 선택한다.

- **raw recv**: `zlink_recv()`로 transport 조각을 직접 가져온다. poller의
  `ZLINK_POLLIN`과 함께 사용한다.
- **raw callback**: `zlink_recv_handler()`로 raw 조각을 콜백으로 받는다.
  event-driven 서버에 적합하다.
- **packet callback**: `zlink_stream_packet_handler()`로 고정 framing
  규약(2B header size + 4B body size + header + body, big-endian)을 따르는
  packet을 조립된 header/body 로 받는다.

세 모델은 상호 배타이며, 한 handle에서 두 번째 모드로 전환하려 하면
`EBUSY`로 실패한다. 응용이 필요에 맞는 모드 하나만 선택한다.

STREAM만의 고유 동작은 다음과 같다.

- `source_rid`는 서버가 연결별로 자동 할당하며,
  고정 4바이트(`uint32`, big-endian)이다.
- 특정 client를 끊어야 하면 callback이나 recv에서 받은 `source_rid`를
  `zlink_disconnect_rid()`에 넘긴다. STREAM의 대상 rid는 반드시 4바이트다.
- 연결/해제 이벤트가 메시지로 전달된다:

| payload 값 | 의미 |
|------------|------|
| `0x01` (1 byte) | connect 이벤트 |
| `0x00` (1 byte) | disconnect 이벤트 |
| 그 외 | 일반 데이터 |

??? example "Full Sample Code -- Recv"

    | Language | Source |
    |----------|--------|
    | C | [stream_recv_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/bindings/c/samples/stream_recv_sample.c) |
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
    | C | [stream_packet_callback_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/bindings/c/samples/stream_packet_callback_sample.c) |
    | C++ | [stream_packet_callback_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/stream_packet_callback_sample.cpp) |
    | Java | [StreamPacketCallbackSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/StreamPacketCallbackSample.java) |
    | Python | [stream_packet_callback_sample.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/samples/stream_packet_callback_sample.py) |
    | Node | [stream_packet_callback_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/samples/stream_packet_callback_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/StreamPacketCallback/Program.cs) |
    | Rust | [stream_packet_callback_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/stream_packet_callback_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/stream_packet_callback_sample/main.go) |

---

## 4.1 packet callback 모드

고정 framing 규약(2바이트 big-endian header size + 4바이트 big-endian
body size + header payload + body payload)을 사용하는 상위 프로토콜에서는
`zlink_stream_packet_handler()`로 packet 단위 콜백을 등록할 수 있다.
core가 fragment 누적과 길이 해석을 직접 처리하므로, 응용은 header/body를
그대로 받아 처리한다.

```c
void on_packet(void *stream,
               const zlink_routing_id_t *source_rid,
               zlink_msg_t *header,
               zlink_msg_t *body,
               void *userdata)
{
    /* header_size == 0 이어도 header 는 길이 0 의 유효한 msg_t 로 전달된다.
       body 도 마찬가지. NULL 이 들어오지 않는다. */
    /* source_rid 는 콜백 실행 중에만 유효한 borrowed view 이므로,
       이후에도 유지하려면 값을 복사해 둔다. */

    /* ... header / body 로 상위 프로토콜 처리 ... */

    zlink_msg_close(header);
    zlink_msg_close(body);
}

zlink_stream_packet_handler(stream, on_packet, NULL);
```

packet callback 모드의 규칙은 다음과 같다.

- `header_size` 와 `body_size` 각각 0 도 허용된다. 길이가 0 이어도 msg_t 는
  유효한 객체로 전달된다.
- `header` 와 `body` 의 소유권은 콜백으로 이전된다. 콜백은 두 msg_t 를 각각
  정확히 한 번 close 하거나 소비해야 한다.
- 같은 handle 에서 raw recv (`zlink_recv()`), raw callback
  (`zlink_recv_handler()`), data-plane `ZLINK_POLLIN` 등록은 모두
  `EBUSY` 로 실패한다. 두 번째 packet handler attach 도 마찬가지다.
- framing 규약을 지키지 않는 malformed packet (길이 제한 초과, 조립 실패,
  불완전 상태 연결 종료 등) 은 연결을 닫는 기본 동작으로 이어진다. 이
  이벤트는 socket monitor 경로로 관찰한다.

이 모드는 fragment 누적을 응용 쪽에서 다시 구현하지 않아도 되는 사용성
이득을 주지만, transport fragment 경계와 packet 경계가 다르다는 점 자체를
바꾸지는 않는다.

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
- `ZLINK_OPT_SNDHWM` / `ZLINK_OPT_RCVHWM`: 기본 `1000`, context auto-HWM을 켜면 STREAM profile 값 사용
- `ZLINK_OPT_SNDBUF` / `ZLINK_OPT_RCVBUF`: 미설정이면 호환 기본값 `262144`
- STREAM 배치 크기 기본값: `4096`
- STREAM 읽기 여유 공간 기본값: `64`
- STREAM accept 동시성 기본값: `4` (최대 `128`로 제한)
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
- `core/tests/integration/test_stream_socket.cpp`
- `core/tests/integration/test_stream_fastpath.cpp`
- `core/tests/routing-id/test_connect_rid_string_alias.cpp`
- `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp`

위 테스트들은 STREAM 서버 + raw client 경로를 기준으로 동작한다.

---
[← ROUTER](03-4-router.ko.md) | [Proxy →](03-6-proxy.ko.md) | [Transport →](04-transports.ko.md)
