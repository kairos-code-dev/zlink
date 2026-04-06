# zlink Overview and Getting Started

## 1. What Is zlink?

zlink is a modern messaging library based on [libzmq](https://github.com/zeromq/libzmq) v4.3.5. It focuses on core patterns and provides Boost.Asio-based I/O with a developer-friendly API.

### Changes Compared to libzmq

| | libzmq | zlink |
|---|--------|-------|
| **Socket Types** | 17 (including draft) | **8** — PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER, STREAM |
| **I/O Engine** | Custom poll/epoll/kqueue | **Boost.Asio** (bundled, no external dependencies) |
| **Encryption** | CURVE (libsodium) | **TLS** (OpenSSL) — `tls://`, `wss://` |
| **Transport** | 10+ (PGM, TIPC, VMCI, etc.) | **6** — `tcp`, `ipc`, `inproc`, `ws`, `wss`, `tls` |
| **Dependencies** | libsodium, libbsd, etc. | **OpenSSL only** |

Note: `pgm://` and `epgm://` are currently disabled and unsupported in zlink.

## 2. Architecture Overview

```
+------------------------------------------------------+
|  Application / Bindings                              |
|  C callers · cpp · dotnet · java · node · python     |
+------------------------------------------------------+
|  Public API Facade  (core/src/api/)                  |
|  context_api · socket_api · message_api              |
|  service_api · poller_api · monitor_api              |
|  validate + delegate, per-handle admission guard     |
+------------------------------------------------------+
|  Service Layer                                       |
|  Discovery · SPOT · Registry                         |
|  service access seam (*_access) · lifecycle · runtime|
+------------------------------------------------------+
|  Socket Semantic / Runtime                           |
|  PAIR · PUB/SUB · XPUB/XSUB · DEALER/ROUTER · STREAM |
|  semantic entrypoint + runtime components            |
|  (dispatch · monitor · endpoint · lifecycle)         |
+------------------------------------------------------+
|  Runtime Core  (core/src/core/)                      |
|  ctx · own · reaper · multipart_send_txn             |
|  options dispatch (core_socket · transport · protocol|
|  close/drain/finalization contract                   |
+------------------------------------------------------+
|  Engine Layer (Boost.Asio)                           |
|  asio_zmp_engine — ZMP v1.0 Protocol (8B fixed hdr)  |
|  Proactor pattern · Speculative I/O · Backpressure   |
+------------------------------------------------------+
|  Transport / Protocol                                |
|  tcp · ipc · inproc · ws — plaintext                 |
|  tls · wss             — OpenSSL encrypted           |
+------------------------------------------------------+
|  Core Infrastructure                                 |
|  msg_t(64B fixed) · pipe_t(Lock-free YPipe)          |
|  ctx_t(I/O Thread Pool) · session_base_t(Bridge)     |
+------------------------------------------------------+
```

Key roles per layer:

| Layer | Role |
|-------|------|
| Public API Facade | C API entry point. Validate + delegate only; does not know concrete service/socket details |
| Service Layer | Discovery/SPOT/Registry semantics and lifecycle. Connected to the API layer via service-local access seams |
| Socket Semantic/Runtime | Socket family semantics (semantic) and common mechanism (runtime components) are separated |
| Runtime Core | Context, shutdown, close/drain orchestration, option dispatch, logical multipart send |
| Engine Layer | Boost.Asio-based poller, io_context, mailbox execution backbone |
| Transport/Protocol | Wire format, TLS handshake, address scheme details |

## 3. Core Design

| Design Principle | Description |
|------------------|-------------|
| **Zero-Copy** | VSM (33B or less) stored inline; large messages use reference counting |
| **Lock-Free** | YPipe (CAS-based FIFO) used for inter-thread communication |
| **True Async** | Asynchronous I/O based on the Proactor pattern |
| **Protocol Agnostic** | Clear separation between Transport and Protocol |

## 4. Socket Types

| Socket Type | Pattern | Description |
|-------------|---------|-------------|
| PAIR | 1:1 Bidirectional | Inter-thread signaling, simple communication |
| PUB/SUB | Publish-Subscribe | Topic-based message distribution |
| XPUB/XSUB | Advanced Pub-Sub | Subscription message access, proxying |
| DEALER/ROUTER | Async Routing | Request-reply, load balancing |
| STREAM | RAW Communication | External client integration (tcp/tls/ws/wss) |

## 5. Transport

| Transport | URI Format | Description |
|-----------|------------|-------------|
| tcp | `tcp://host:port` | Standard TCP |
| ipc | `ipc://path` | Unix domain socket |
| inproc | `inproc://name` | In-process communication |
| ws | `ws://host:port` | WebSocket |
| wss | `wss://host:port` | WebSocket + TLS |
| tls | `tls://host:port` | Native TLS |

## 6. Quick Start

### Requirements

- CMake 3.10+, C++17 compiler, OpenSSL

### Build

```bash
cmake -B build -DWITH_TLS=ON -DBUILD_TESTS=ON
cmake --build build
```

### First Program

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void) {
        void *ctx = zlink_ctx_new();

        /* Server */
        void *server = zlink_socket(ctx, ZLINK_PAIR);
        zlink_bind(server, "tcp://*:5555");

        /* Client */
        void *client = zlink_socket(ctx, ZLINK_PAIR);
        zlink_connect(client, "tcp://127.0.0.1:5555");

        /* Send */
        zlink_msg_t part;
        zlink_msg_init_size(&part, 12);
        memcpy(zlink_msg_data(&part), "Hello zlink!", 12);
        zlink_send(client, &part, 1, 0);

        /* Receive */
        zlink_routing_id_t source_rid;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        int rc = zlink_recv(server, &source_rid, &parts, &part_count, 0);
        if (rc == 0)
            printf("Received: %.*s\n",
                   (int)zlink_msg_size(&parts[0]),
                   (char *)zlink_msg_data(&parts[0]));
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);

        zlink_close(client);
        zlink_close(server);
        zlink_ctx_term(ctx);
        return 0;
    }
    ```

=== "C++"

    ```cpp
    #include <zlink/socket.hpp>
    #include <iostream>

    int main() {
        zlink::context_t ctx;

        // Server
        zlink::pair_socket_t server(ctx);
        server.bind("tcp://*:5555");

        // Client
        zlink::pair_socket_t client(ctx);
        client.connect("tcp://127.0.0.1:5555");

        // Send
        zlink::message_t msg("Hello zlink!", 12);
        client.send(msg);

        // Receive
        auto [source_rid, parts] = server.recv();
        if (!parts.empty())
            std::cout << "Received: " << parts[0].to_string() << "\n";

        client.close();
        server.close();
        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class HelloZlink {
        public static void main(String[] args) throws Exception {
            try (Context ctx = new Context()) {
                // Server
                PairSocket server = new PairSocket(ctx);
                server.bind("tcp://*:5555");

                // Client
                PairSocket client = new PairSocket(ctx);
                client.connect("tcp://127.0.0.1:5555");

                // Send
                Message msg = new Message("Hello zlink!".getBytes());
                client.send(msg);

                // Receive
                RecvResult result = server.recv();
                System.out.println("Received: "
                    + new String(result.parts()[0].data()));

                client.close();
                server.close();
            }
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    # Server
    server = zlink.PairSocket(ctx)
    server.bind("tcp://*:5555")

    # Client
    client = zlink.PairSocket(ctx)
    client.connect("tcp://127.0.0.1:5555")

    # Send
    client.send(b"Hello zlink!")

    # Receive
    source_rid, parts = server.recv()
    print(f"Received: {parts[0].data().decode()}")

    client.close()
    server.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from "zlink";

    const ctx = new zlink.Context();

    // Server
    const server = new zlink.PairSocket(ctx);
    server.bind("tcp://*:5555");

    // Client
    const client = new zlink.PairSocket(ctx);
    client.connect("tcp://127.0.0.1:5555");

    // Send
    client.send(Buffer.from("Hello zlink!"));

    // Receive
    const { sourceRid, parts } = server.recv();
    console.log(`Received: ${parts[0].data().toString()}`);

    client.close();
    server.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new Context();

    // Server
    using var server = new PairSocket(ctx);
    server.Bind("tcp://*:5555");

    // Client
    using var client = new PairSocket(ctx);
    client.Connect("tcp://127.0.0.1:5555");

    // Send
    client.Send(new Message("Hello zlink!"u8));

    // Receive
    var (sourceRid, parts) = server.Recv();
    Console.WriteLine($"Received: {parts[0].DataString()}");
    ```

=== "Rust"

    ```rust
    use zlink::{Context, SocketType};

    fn main() -> zlink::Result<()> {
        let ctx = Context::new()?;

        // Server
        let server = ctx.pair_socket()?;
        server.bind("tcp://*:5555")?;

        // Client
        let client = ctx.pair_socket()?;
        client.connect("tcp://127.0.0.1:5555")?;

        // Send
        client.send(b"Hello zlink!")?;

        // Receive
        let (source_rid, parts) = server.recv()?;
        println!("Received: {}", parts[0].as_str()?);

        client.close()?;
        server.close()?;
        ctx.term()?;
        Ok(())
    }
    ```

=== "Go"

    ```go
    package main

    import (
        "fmt"
        "log"
        zlink "github.com/kairoscode/zlink-go"
    )

    func main() {
        ctx, err := zlink.NewContext()
        if err != nil { log.Fatal(err) }
        defer ctx.Close()

        // Server
        server, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer server.Close()
        server.Bind("tcp://*:5555")

        // Client
        client, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer client.Close()
        client.Connect("tcp://127.0.0.1:5555")

        // Send
        msg := zlink.NewMessage([]byte("Hello zlink!"))
        client.Send(msg)

        // Receive
        received, err := server.Recv()
        if err != nil { log.Fatal(err) }
        defer received.Close()
        part, _ := received.SinglePartOrError()
        fmt.Printf("Received: %s\n", string(part.Data()))
    }
    ```

## 7. Next Steps

- [Core API Details](02-core-api.md)
- [Socket Pattern Usage](03-0-socket-patterns.md)
- [Transport Guide](04-transports.md)
- [TLS Security Configuration](05-tls-security.md)

---
[Core API →](02-core-api.md)
