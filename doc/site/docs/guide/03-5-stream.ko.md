
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
외부 raw client  <---- RAW 바이트 스트림 ---->  STREAM(server)
```

> STREAM은 zlink 내부 소켓(PAIR/PUB/SUB/DEALER/ROUTER)과 직접 호환되지 않는다.

---

## 2. 서버 생성/바인드

=== "C"

    ```c
    void *stream = zlink_socket(ctx, ZLINK_STREAM);
    int linger = 0;
    zlink_set_option(stream, ZLINK_OPT_LINGER, &linger, sizeof(linger));
    zlink_bind(stream, "tcp://0.0.0.0:8080");
    ```

=== "C++"

    ```cpp
    zlink::context_t ctx;
    zlink::stream_socket_t stream(ctx);
    stream.set_option(zlink::opt::linger, 0);
    stream.bind("tcp://0.0.0.0:8080");
    ```

=== "Java"

    ```java
    Context ctx = new Context();
    StreamSocket stream = new StreamSocket(ctx);
    stream.setLinger(0);
    stream.bind("tcp://0.0.0.0:8080");
    ```

=== "Python"

    ```python
    ctx = zlink.Context()
    stream = zlink.StreamSocket(ctx)
    stream.set_option(zlink.OPT_LINGER, 0)
    stream.bind("tcp://0.0.0.0:8080")
    ```

=== "Node/TypeScript"

    ```typescript
    const ctx = new zlink.Context();
    const stream = new zlink.StreamSocket(ctx);
    stream.setOption(zlink.OPT_LINGER, 0);
    stream.bind("tcp://0.0.0.0:8080");
    ```

=== "C#/.NET"

    ```csharp
    using var ctx = new Context();
    using var stream = new StreamSocket(ctx);
    stream.Linger = 0;
    stream.Bind("tcp://0.0.0.0:8080");
    ```

=== "Rust"

    ```rust
    let ctx = zlink::Context::new()?;
    let stream = ctx.stream_socket()?;
    stream.set_linger(0)?;
    stream.bind("tcp://0.0.0.0:8080")?;
    ```

=== "Go"

    ```go
    ctx, err := zlink.NewContext()
    if err != nil { panic(err) }
    stream, err := ctx.StreamSocket()
    if err != nil { panic(err) }
    stream.SetOption(zlink.OptionLinger, 0)
    stream.Bind("tcp://0.0.0.0:8080")
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

### Recv 모드

완전한 recv 모드 에코 서버: STREAM 소켓을 바인드하고, raw 클라이언트로부터
데이터를 수신하여 출력한 뒤 에코로 응답한다.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void)
    {
        void *ctx = zlink_ctx_new();
        void *stream = zlink_socket(ctx, ZLINK_STREAM);
        int notify = 0;
        zlink_set_option(stream, ZLINK_OPT_STREAM_NOTIFY, &notify, sizeof(notify));
        zlink_bind(stream, "tcp://*:8080");

        /* raw 클라이언트로부터 수신 */
        zlink_routing_id_t rid;
        zlink_msg_t *parts;
        size_t count;
        zlink_recv(stream, &rid, &parts, &count, 0);
        printf("Received: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));

        /* 에코 응답 */
        size_t sz = zlink_msg_size(&parts[0]);
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, sz);
        memcpy(zlink_msg_data(&reply), zlink_msg_data(&parts[0]), sz);
        zlink_send_rid(stream, &rid, &reply, 1, 0);

        zlink_multipart_close(parts, count);
        zlink_close(stream);
        zlink_ctx_term(ctx);
        return 0;
    }
    ```

=== "C++"

    ```cpp
    #include <zlink/socket.hpp>
    #include <iostream>

    int main()
    {
        zlink::context_t ctx;
        zlink::stream_socket_t stream(ctx);
        stream.set_option(zlink::opt::stream_notify, 0);
        stream.bind("tcp://*:8080");

        // raw 클라이언트로부터 수신
        auto [rid, parts] = stream.recv();
        std::cout << "Received: " << parts[0].str() << std::endl;

        // 에코 응답
        stream.send_rid(rid, parts[0]);

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class StreamRecvExample {
        public static void main(String[] args) {
            Context ctx = new Context();
            StreamSocket stream = new StreamSocket(ctx);
            stream.setStreamNotify(0);
            stream.bind("tcp://*:8080");

            // raw 클라이언트로부터 수신
            RecvResult result = stream.recv();
            System.out.println("Received: "
                + new String(result.parts()[0].data()));

            // 에코 응답
            stream.sendRid(result.routingId(), result.parts()[0]);

            stream.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()
    stream = zlink.StreamSocket(ctx)
    stream.set_option(zlink.OPT_STREAM_NOTIFY, 0)
    stream.bind("tcp://*:8080")

    # raw 클라이언트로부터 수신
    rid, parts = stream.recv()
    print(f"Received: {parts[0].decode()}")

    # 에코 응답
    stream.send_rid(rid, parts[0])

    stream.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();
    const stream = new zlink.StreamSocket(ctx);
    stream.setOption(zlink.OPT_STREAM_NOTIFY, 0);
    stream.bind('tcp://*:8080');

    // raw 클라이언트로부터 수신
    const { sourceRid, parts } = stream.recv();
    console.log(`Received: ${parts[0].toString()}`);

    // 에코 응답
    stream.sendRid(sourceRid, parts[0]);

    stream.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new Context();
    using var stream = new StreamSocket(ctx);
    stream.StreamNotify = 0;
    stream.Bind("tcp://*:8080");

    // raw 클라이언트로부터 수신
    var (rid, parts) = stream.Recv();
    Console.WriteLine($"Received: {parts[0].GetString()}");

    // 에코 응답
    stream.SendRid(rid, parts[0]);
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();
        let stream = ctx.stream_socket()?;
        stream.set_stream_notify(0)?;
        stream.bind("tcp://*:8080")?;

        // raw 클라이언트로부터 수신
        let (rid, parts) = stream.recv()?;
        println!("Received: {}", String::from_utf8_lossy(parts[0].as_bytes()));

        // 에코 응답
        stream.send_rid(&rid, &parts[0])?;

        Ok(())
    }
    ```

=== "Go"

    ```go
    package main

    import (
        "fmt"
        "log"
        "github.com/kairos-code-dev/zlink-go"
    )

    func main() {
        ctx, err := zlink.NewContext()
        if err != nil { log.Fatal(err) }
        defer ctx.Close()

        stream, err := ctx.StreamSocket()
        if err != nil { log.Fatal(err) }
        defer stream.Close()
        stream.SetOption(zlink.OptionStreamNotify, 0)
        stream.Bind("tcp://*:8080")

        // raw 클라이언트로부터 수신
        rid, parts, err := stream.Recv()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Received: %s\n", string(parts[0].Data()))

        // 에코 응답
        stream.SendTo(rid, parts[0])
    }
    ```

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

STREAM의 콜백에서는 connect/disconnect 이벤트와 데이터를 구분해야 한다.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>
    #include <stdint.h>

    static void *g_stream;

    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        for (size_t i = 0; i < part_count; i++) {
            void *data = zlink_msg_data(&parts[i]);
            size_t size = zlink_msg_size(&parts[i]);

            if (size == 1 && ((uint8_t *)data)[0] == 0x01) {
                printf("Client connected\n");
            } else if (size == 1 && ((uint8_t *)data)[0] == 0x00) {
                printf("Client disconnected\n");
            } else {
                printf("Data: %.*s\n", (int)size, (char *)data);
                zlink_msg_t reply;
                zlink_msg_init_size(&reply, size);
                memcpy(zlink_msg_data(&reply), data, size);
                zlink_send_rid(g_stream, source_rid, &reply, 1, 0);
            }
            zlink_msg_close(&parts[i]);
        }
    }

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        g_stream = zlink_socket(ctx, ZLINK_STREAM);
        int notify = 0;
        zlink_set_option(g_stream, ZLINK_OPT_STREAM_NOTIFY, &notify, sizeof(notify));
        zlink_bind(g_stream, "tcp://*:8080");

        /* 에코 콜백 attach (영구, 해제 불가) */
        zlink_recv_handler(g_stream, on_message, NULL);

        /* 콜백이 연결을 처리하는 동안 블록.
           프로덕션에서는 zlink_poll 또는 이벤트 루프를 사용한다. */
        getchar();

        zlink_close(g_stream);
        zlink_ctx_term(ctx);
        return 0;
    }
    ```

=== "C++"

    ```cpp
    #include <zlink/socket.hpp>
    #include <iostream>

    int main()
    {
        zlink::context_t ctx;
        zlink::stream_socket_t stream(ctx);
        stream.set_option(zlink::opt::linger, 0);
        stream.bind("tcp://*:8080");

        stream.on_message([&](const zlink::routing_id_t& source_rid,
                              std::span<zlink::message_t> parts) {
            for (auto& part : parts) {
                auto data = part.data();
                if (data.size() == 1 && data[0] == 0x01) {
                    std::cout << "Client connected" << std::endl;
                } else if (data.size() == 1 && data[0] == 0x00) {
                    std::cout << "Client disconnected" << std::endl;
                } else {
                    std::cout << "Data: " << part.str() << std::endl;
                    stream.send_rid(source_rid, part);
                }
            }
        });

        std::cin.get();
        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class StreamCallbackExample {
        public static void main(String[] args) {
            Context ctx = new Context();
            StreamSocket stream = new StreamSocket(ctx);
            stream.setLinger(0);
            stream.bind("tcp://*:8080");

            stream.onMessage((sourceRid, parts) -> {
                for (Message part : parts) {
                    byte[] data = part.data();
                    if (data.length == 1 && data[0] == 0x01) {
                        System.out.println("Client connected");
                    } else if (data.length == 1 && data[0] == 0x00) {
                        System.out.println("Client disconnected");
                    } else {
                        System.out.println("Data: " + new String(data));
                        stream.sendRid(sourceRid, part);
                    }
                }
            });

            System.out.println("Echo server running on :8080");
            try { Thread.sleep(Long.MAX_VALUE); } catch (InterruptedException e) {}
            stream.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink
    import time

    ctx = zlink.Context()
    stream = zlink.StreamSocket(ctx)
    stream.set_option(zlink.OPT_LINGER, 0)
    stream.bind("tcp://*:8080")

    def on_message(source_rid, parts):
        for part in parts:
            data = part.data()
            if data == b"\x01":
                print("Client connected")
            elif data == b"\x00":
                print("Client disconnected")
            else:
                print(f"Data: {data.decode()}")
                stream.send_rid(source_rid, data)

    stream.on_message(on_message)

    print("Echo server running on :8080")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass

    stream.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();
    const stream = new zlink.StreamSocket(ctx);
    stream.setOption(zlink.OPT_LINGER, 0);
    stream.bind('tcp://*:8080');

    stream.onMessage((sourceRid: Buffer, parts: Buffer[]) => {
        for (const part of parts) {
            if (part.length === 1 && part[0] === 0x01) {
                console.log('Client connected');
            } else if (part.length === 1 && part[0] === 0x00) {
                console.log('Client disconnected');
            } else {
                console.log(`Data: ${part.toString()}`);
                stream.sendRid(sourceRid, part);
            }
        }
    });

    console.log('Echo server running on :8080');
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new Context();
    using var stream = new StreamSocket(ctx);
    stream.Linger = 0;
    stream.Bind("tcp://*:8080");

    stream.OnMessage((sourceRid, parts) => {
        foreach (var part in parts) {
            var data = part.Data;
            if (data.Length == 1 && data.Span[0] == 0x01) {
                Console.WriteLine("Client connected");
            } else if (data.Length == 1 && data.Span[0] == 0x00) {
                Console.WriteLine("Client disconnected");
            } else {
                Console.WriteLine($"Data: {part.GetString()}");
                stream.SendRid(sourceRid, part);
            }
        }
    });

    Console.WriteLine("Echo server running on :8080");
    Console.ReadLine();
    ```

=== "Rust"

    ```rust
    use zlink::Context;
    use std::io;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();
        let stream = ctx.stream_socket()?;
        stream.set_linger(0)?;
        stream.bind("tcp://*:8080")?;

        stream.on_message(|source_rid, parts| {
            for part in parts {
                let data = part.as_bytes();
                if data == [0x01] {
                    println!("Client connected");
                } else if data == [0x00] {
                    println!("Client disconnected");
                } else {
                    println!("Data: {}", String::from_utf8_lossy(data));
                    stream.send_rid(source_rid, part)?;
                }
            }
            Ok(())
        })?;

        println!("Echo server running on :8080");
        let mut buf = String::new();
        io::stdin().read_line(&mut buf)?;
        Ok(())
    }
    ```

=== "Go"

    ```go
    package main

    import (
        "fmt"
        "os"
        "os/signal"
        "github.com/kairos-code-dev/zlink-go"
    )

    func main() {
        ctx, _ := zlink.NewContext()
        stream, _ := ctx.StreamSocket()
        stream.SetOption(zlink.OptionLinger, 0)
        stream.Bind("tcp://*:8080")

        stream.RecvHandler(func(sourceRid zlink.RoutingID, parts []zlink.Message) {
            for _, part := range parts {
                data := part.Data()
                if len(data) == 1 && data[0] == 0x01 {
                    fmt.Println("Client connected")
                } else if len(data) == 1 && data[0] == 0x00 {
                    fmt.Println("Client disconnected")
                } else {
                    fmt.Printf("Data: %s\n", string(data))
                    stream.SendTo(sourceRid, part)
                }
            }
        })

        fmt.Println("Echo server running on :8080")
        c := make(chan os.Signal, 1)
        signal.Notify(c, os.Interrupt)
        <-c
    }
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
> `ZLINK_DONTWAIT`로 `EAGAIN`을 반환한다. 고급 backpressure 패턴은
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

!!! note "C-only: raw POSIX 소켓 프레이밍"
    이 예시는 클라이언트 측 raw POSIX 소켓 호출을 보여준다.
    각 언어는 자체 네이티브 TCP/WebSocket 클라이언트 라이브러리를 사용한다.

```c
// 사용자 정의 프레이밍 예시: [4B length(big-endian)][body]
// send
uint32_t len_be = htonl(body_len);
send(fd, &len_be, 4, 0);
send(fd, body, body_len, 0);

// recv
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
