# STREAM Socket

## 1. Overview

STREAM is a **server-only** socket for communicating with **external raw clients**.

Core rules:
- `ZLINK_STREAM` supports `zlink_bind()` only.
- Calling `zlink_connect()` on `ZLINK_STREAM` returns `EOPNOTSUPP`.
- Clients must use OS/Asio/WebSocket raw client stacks, not zlink STREAM sockets.
- Wire format is `4-byte length (big-endian) + body`.
- At the zlink API level, messages are exposed as 2 frames: `[routing_id(4B)][payload]`.

Valid combination:

```
external raw client  <---- RAW(4B length + body) ---->  STREAM(server)
```

> STREAM is not directly compatible with zlink internal sockets (PAIR/PUB/SUB/DEALER/ROUTER).

---

## 2. Server Create/Bind

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

Supported server transports:
- `tcp://`
- `tls://`
- `ws://`
- `wss://`

---

## 3. STREAM-Specific Behavior

STREAM uses the same recv/callback model as other sockets.
STREAM-specific behavior:

- `source_rid` is auto-assigned per connection by the server,
  always fixed 4 bytes (`uint32`, big-endian).
- Connect/disconnect events are delivered as messages:

| payload | meaning |
|---|---|
| `0x01` (1 byte) | connect event |
| `0x00` (1 byte) | disconnect event |
| otherwise | regular data |

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

## 4. Callback Example

In STREAM callbacks, connect/disconnect events must be distinguished from data.

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

        /* Attach echo callback (permanent, cannot be undone) */
        zlink_recv_handler(g_stream, on_message, NULL);

        /* Block while the callback handles connections.
           In production, use zlink_poll or an event loop. */
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

### Key Points

| Item | Description |
|---|---|
| Attach API | `zlink_recv_handler()` |
| Callback | `zlink_socket_msg_handler_fn` |
| Lifetime | Permanent once attached (no detach) |
| Framing | Raw bytes as received from the transport |
| Send | `zlink_send_rid()` |

> When the send queue is full (HWM), `zlink_send_rid()` blocks
> (default) or returns `EAGAIN` with `ZLINK_DONTWAIT`. For advanced
> backpressure patterns, see [Performance Guide](10-performance.md).

- Only one callback can be attached at a time; calling attach while a
  callback is already attached returns `-1` with `errno=EBUSY`.
- The handler is permanent and cannot be detached for the lifetime of
  the socket.
- Close from inside the callback is not supported (fails with `EBUSY`).

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

## 5. Client Implementation Rule

Clients must be implemented as raw socket/websocket clients.

Conceptual POSIX TCP example:

!!! note "C-only: raw POSIX socket framing"
    This example shows raw POSIX socket calls for the client side.
    Each language uses its own native TCP/WebSocket client library.

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

## 6. Option and Runtime Policy

Main supported options:
- `ZLINK_OPT_MAXMSGSIZE`, `ZLINK_OPT_SNDHWM`, `ZLINK_OPT_RCVHWM`, `ZLINK_OPT_SNDBUF`, `ZLINK_OPT_RCVBUF`, `ZLINK_OPT_BACKLOG`, `ZLINK_OPT_LINGER`
- TLS/WSS server: `zlink_set_tls_server()` / TLS client: `zlink_set_tls_client()`

Unsupported/changed:
- Setting `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` on STREAM returns `EOPNOTSUPP`.

### 6.1 Default STREAM runtime profile

Defaults currently used by STREAM internals:
- `ZLINK_OPT_BACKLOG`: `65536`
- `ZLINK_OPT_SNDBUF`: `262144` when unset (`-1`)
- `ZLINK_OPT_RCVBUF`: `262144` when unset (`-1`)
- minimum in/out batch size: `12288`
- STREAM accept concurrency default: `4` (clamped to max `128`)
- STREAM session scheduling default: `rr`

> STREAM runtime environment variables and internal tuning constants
> are documented in [STREAM internals](../internals/stream-socket.md).

---

## 7. Errors and Constraints

- `zlink_connect(stream, ...)` -> `EOPNOTSUPP`
- On STREAM, non-4-byte `routing_id` frame is a protocol error
- Messages larger than `MAXMSGSIZE` are dropped and connection is closed (disconnect event)

---

## 8. Reference Tests

- `core/tests/test_stream_socket.cpp`
- `core/tests/test_stream_fastpath.cpp`
- `core/tests/routing-id/test_connect_rid_string_alias.cpp`
- `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp`

These tests use STREAM server + raw client paths.

---
[← ROUTER](03-4-router.md) | [Proxy →](03-6-proxy.md) | [Transport →](04-transports.md)
