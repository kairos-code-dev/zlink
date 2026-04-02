
# Core C API Detailed Guide

## 1. Context API

A Context is the top-level object in zlink that manages the I/O thread pool and sockets.

=== "C"

    ```c
    /* Create */
    void *ctx = zlink_ctx_new();

    /* Configure — increase I/O threads for multi-connection servers */
    zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);     /* default 1; 4 is optimal under heavy load */

    /* Query */
    int io_threads = zlink_ctx_get(ctx, ZLINK_IO_THREADS);

    /* Terminate */
    zlink_ctx_term(ctx);  /* Returns after all sockets are closed */
    ```

=== "C++"

    ```cpp
    // Create
    zlink::context_t ctx;
    ctx.set_io_threads(4);       // default 1; 4 is optimal under heavy load
    int io_threads = ctx.io_threads();

    // Terminate — RAII or explicit
    ctx.close();
    ```

=== "Java"

    ```java
    // Create
    Context ctx = new Context();
    ctx.setIoThreads(4);         // default 1; 4 is optimal under heavy load
    int ioThreads = ctx.getIoThreads();

    // Terminate
    ctx.close();  // or use try-with-resources
    ```

=== "Python"

    ```python
    # Create
    ctx = zlink.Context()
    ctx.set_io_threads(4)        # default 1; 4 is optimal under heavy load
    io_threads = ctx.io_threads

    # Terminate
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    // Create
    const ctx = new zlink.Context();
    ctx.setIoThreads(4);         // default 1; 4 is optimal under heavy load
    const ioThreads = ctx.ioThreads;

    // Terminate
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    // Create
    using var ctx = new Context();
    ctx.IoThreads = 4;           // default 1; 4 is optimal under heavy load
    int ioThreads = ctx.IoThreads;

    // Terminate — Dispose pattern
    ```

=== "Rust"

    ```rust
    // Create
    let ctx = zlink::Context::new()?;
    ctx.set_io_threads(4)?;      // default 1; 4 is optimal under heavy load
    let io_threads = ctx.io_threads()?;

    // Terminate — Drop trait
    ```

=== "Go"

    ```go
    // Create
    ctx := zlink.NewContext()
    ctx.SetIoThreads(4) // default 1; 4 is optimal under heavy load
    ioThreads := ctx.IoThreads()

    // Terminate
    ```

### Context Options

| Option | Default | Description |
|--------|---------|-------------|
| `ZLINK_IO_THREADS` | 1 | Number of I/O threads |
| `ZLINK_MAX_SOCKETS` | 4095 | Maximum number of sockets |
| `ZLINK_MAX_MSGSZ` | -1 | Maximum message size (-1: unlimited) |

## 2. Socket API

Public socket handle APIs are thread-safe by default. Multiple threads
can share the same socket handle to call send/recv/bind/connect, etc.

> For detailed threading rules, see [Thread-Safety Guide](11-thread-safety.md).

### 2.1 Socket Creation and Closing

=== "C"

    ```c
    void *socket = zlink_socket(ctx, ZLINK_DEALER);
    /* ... use ... */
    zlink_close(socket);
    ```

=== "C++"

    ```cpp
    zlink::dealer_socket_t socket(ctx);
    // ... use ...
    socket.close();  // or RAII
    ```

=== "Java"

    ```java
    DealerSocket socket = new DealerSocket(ctx);
    // ... use ...
    socket.close();
    ```

=== "Python"

    ```python
    socket = zlink.DealerSocket(ctx)
    # ... use ...
    socket.close()
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = new zlink.DealerSocket(ctx);
    // ... use ...
    socket.close();
    ```

=== "C#/.NET"

    ```csharp
    using var socket = new DealerSocket(ctx);
    // ... use ...
    ```

=== "Rust"

    ```rust
    let socket = ctx.dealer_socket()?;
    // ... use ...
    // Drop trait handles close
    ```

=== "Go"

    ```go
    socket := ctx.DealerSocket()
    // ... use ...
    // Close explicitly
    ```

### 2.2 Socket Type Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_PAIR` | 0x1001 | 1:1 Bidirectional |
| `ZLINK_PUB` | 0x1002 | Publisher |
| `ZLINK_SUB` | 0x1003 | Subscriber |
| `ZLINK_DEALER` | 0x1004 | Asynchronous request |
| `ZLINK_ROUTER` | 0x1005 | Routing |
| `ZLINK_XPUB` | 0x1006 | Advanced publisher |
| `ZLINK_XSUB` | 0x1007 | Advanced subscriber |
| `ZLINK_STREAM` | 0x1008 | RAW communication |

### 2.3 Connection Management

=== "C"

    ```c
    /* Bind (server) */
    zlink_bind(socket, "tcp://*:5555");

    /* Connect (client) */
    zlink_connect(socket, "tcp://127.0.0.1:5555");

    /* Unbind */
    zlink_unbind(socket, "tcp://*:5555");
    zlink_disconnect(socket, "tcp://127.0.0.1:5555");
    ```

=== "C++"

    ```cpp
    socket.bind("tcp://*:5555");
    socket.connect("tcp://127.0.0.1:5555");
    socket.unbind("tcp://*:5555");
    socket.disconnect("tcp://127.0.0.1:5555");
    ```

=== "Java"

    ```java
    socket.bind("tcp://*:5555");
    socket.connect("tcp://127.0.0.1:5555");
    socket.unbind("tcp://*:5555");
    socket.disconnect("tcp://127.0.0.1:5555");
    ```

=== "Python"

    ```python
    socket.bind("tcp://*:5555")
    socket.connect("tcp://127.0.0.1:5555")
    socket.unbind("tcp://*:5555")
    socket.disconnect("tcp://127.0.0.1:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("tcp://*:5555");
    socket.connect("tcp://127.0.0.1:5555");
    socket.unbind("tcp://*:5555");
    socket.disconnect("tcp://127.0.0.1:5555");
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("tcp://*:5555");
    socket.Connect("tcp://127.0.0.1:5555");
    socket.Unbind("tcp://*:5555");
    socket.Disconnect("tcp://127.0.0.1:5555");
    ```

=== "Rust"

    ```rust
    socket.bind("tcp://*:5555")?;
    socket.connect("tcp://127.0.0.1:5555")?;
    socket.unbind("tcp://*:5555")?;
    socket.disconnect("tcp://127.0.0.1:5555")?;
    ```

=== "Go"

    ```go
    socket.Bind("tcp://*:5555")
    socket.Connect("tcp://127.0.0.1:5555")
    socket.Unbind("tcp://*:5555")
    socket.Disconnect("tcp://127.0.0.1:5555")
    ```

### 2.4 Socket Options

=== "C"

    ```c
    /* Set option */
    int hwm = 5000;
    zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

    /* Get option */
    int value;
    size_t len = sizeof(value);
    zlink_get_option(socket, ZLINK_OPT_SNDHWM, &value, &len);
    ```

=== "C++"

    ```cpp
    socket.set_option(zlink::opt::sndhwm, 5000);
    int value = socket.get_option<int>(zlink::opt::sndhwm);
    ```

=== "Java"

    ```java
    socket.setSndHwm(5000);
    int value = socket.getSndHwm();
    ```

=== "Python"

    ```python
    socket.set_option(zlink.OPT_SNDHWM, 5000)
    value = socket.get_option(zlink.OPT_SNDHWM)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setOption(zlink.OPT_SNDHWM, 5000);
    const value = socket.getOption(zlink.OPT_SNDHWM);
    ```

=== "C#/.NET"

    ```csharp
    socket.SndHwm = 5000;
    int value = socket.SndHwm;
    ```

=== "Rust"

    ```rust
    socket.set_sndhwm(5000)?;
    let value = socket.sndhwm()?;
    ```

=== "Go"

    ```go
    socket.SetOption(zlink.OptionSndHwm, 5000)
    value := socket.GetOption(zlink.OptionSndHwm)
    ```

Key options:

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `ZLINK_OPT_SNDHWM` | int | 1000 | Send High Water Mark |
| `ZLINK_OPT_RCVHWM` | int | 1000 | Receive High Water Mark |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | Send timeout (ms, -1: unlimited) |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | Receive timeout (ms, -1: unlimited) |
| `ZLINK_OPT_LINGER` | int | -1 | Wait time on socket close (ms) |

Routing ID is now set/queried via dedicated functions:
`zlink_set_routing_id()` / `zlink_get_routing_id()`.
Subscription management uses `zlink_set_subscription()`.

Options and queries such as `ZLINK_OPT_EVENTS` and
`ZLINK_OPT_LAST_ENDPOINT` are meaningful during normal runtime use.
By contrast, most tuning knobs such as HWM, timeouts, and TLS settings
are usually closer to initial configuration.

### 2.5 Option Ownership Categories

Internally, options are classified into three categories, each with its
own domain owner responsible for validation/apply. The public API surface
(`zlink_set_option` / `zlink_get_option`) remains unchanged, but when
adding new options, ownership is determined by the following classification:

| Category | Representative Options | Description |
|----------|----------------------|-------------|
| **Core Socket** | `SNDHWM`, `RCVHWM`, `LINGER`, `ROUTING_ID`, `SNDTIMEO`, `RCVTIMEO` | Core socket behavior |
| **Transport/Network** | `RATE`, `RECOVERY_IVL`, `SNDBUF`, `RCVBUF`, `TOS`, `PRIORITY`, `MULTICAST_*` | Network/transport layer policies |
| **Protocol/Metadata** | ZMP protocol metadata | Protocol-level metadata |

This classification ensures transport option changes do not affect
socket/service code and makes it immediately clear which module owns
any given option.

## 3. Sending and Receiving Messages

### 3.1 Sending

=== "C"

    ```c
    /* Simple send */
    zlink_msg_t part;
    zlink_msg_init_size(&part, 5);
    memcpy(zlink_msg_data(&part), "Hello", 5);
    zlink_send(socket, &part, 1, 0);

    /* Multipart send */
    zlink_msg_t parts[2];
    zlink_msg_init_size(&parts[0], 6);
    memcpy(zlink_msg_data(&parts[0]), "header", 6);
    zlink_msg_init_size(&parts[1], 4);
    memcpy(zlink_msg_data(&parts[1]), "body", 4);
    zlink_send(socket, parts, 2, 0);
    ```

=== "C++"

    ```cpp
    // Simple send
    socket.send(zlink::message_t("Hello", 5));

    // Multipart send
    std::vector<zlink::message_t> parts;
    parts.emplace_back("header", 6);
    parts.emplace_back("body", 4);
    socket.send(parts);
    ```

=== "Java"

    ```java
    // Simple send
    socket.send(new Message("Hello".getBytes()));

    // Multipart send
    socket.send(new Message("header".getBytes()),
                new Message("body".getBytes()));
    ```

=== "Python"

    ```python
    # Simple send
    socket.send(b"Hello")

    # Multipart send
    socket.send_multipart([b"header", b"body"])
    ```

=== "Node/TypeScript"

    ```typescript
    // Simple send
    socket.send(Buffer.from("Hello"));

    // Multipart send
    socket.send([Buffer.from("header"), Buffer.from("body")]);
    ```

=== "C#/.NET"

    ```csharp
    // Simple send
    socket.Send(new Message("Hello"u8));

    // Multipart send
    socket.Send(new Message("header"u8), new Message("body"u8));
    ```

=== "Rust"

    ```rust
    // Simple send
    socket.send(&zlink::Message::from("Hello"))?;

    // Multipart send
    socket.send_multipart(&[
        zlink::Message::from("header"),
        zlink::Message::from("body"),
    ])?;
    ```

=== "Go"

    ```go
    // Simple send
    socket.Send(zlink.NewMessage([]byte("Hello")))

    // Multipart send
    socket.SendMultipart([]zlink.Message{zlink.NewMessage([]byte("header")), zlink.NewMessage([]byte("body"))})
    ```

By default `zlink_send()` blocks when the send queue is full (HWM reached).
Pass `ZLINK_DONTWAIT` to return `EAGAIN` immediately instead of blocking.
For advanced backpressure patterns, see
[Performance Guide](10-performance.md).

#### Logical Multipart Send

Multipart sends via `zlink_send()`,
`zlink_publish()`, and other public/service surfaces internally use a
shared **logical multipart send** module. This module provides the
following common guarantees:

- **nonblocking**: one-shot attempt with partial local state rollback on failure
- **blocking**: whole-message retry until the `sndtimeo` deadline
- **retry targets**: only `EAGAIN` and `EINTR` are retried; other errors fail immediately
- **whole-message guarantee**: a multipart message either succeeds entirely or fails entirely

This design is based on `libzmq`'s `pipe/router/xpub/dist` lower layer
complete-message accounting and rollback mechanisms.

### 3.2 Receiving

zlink sockets support two receive modes:

#### Pull Mode (Synchronous)

Without attaching a handler, call `zlink_recv()` to receive messages
directly. Sockets start in pull mode by default.

=== "C"

    ```c
    void *socket = zlink_socket(ctx, ZLINK_PAIR);
    zlink_bind(socket, "tcp://*:5556");

    /* Blocking recv */
    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = zlink_recv(socket, &source_rid, &parts, &part_count, 0);
    if (rc == 0) {
        for (size_t i = 0; i < part_count; i++) {
            printf("Frame %zu: %.*s\n", i,
                   (int)zlink_msg_size(&parts[i]),
                   (char *)zlink_msg_data(&parts[i]));
            zlink_msg_close(&parts[i]);
        }
        free(parts);
    }

    /* Non-blocking recv */
    rc = zlink_recv(socket, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
    if (rc == -1 && zlink_errno() == EAGAIN) {
        /* No message available right now */
    }
    ```

=== "C++"

    ```cpp
    zlink::pair_socket_t socket(ctx);
    socket.bind("tcp://*:5556");

    // Blocking recv
    auto [source_rid, parts] = socket.recv();
    for (size_t i = 0; i < parts.size(); i++)
        std::cout << "Frame " << i << ": " << parts[i].to_string() << "\n";

    // Non-blocking recv
    auto result = socket.recv(zlink::dontwait);
    if (!result)  // EAGAIN — no message available
        ;
    ```

=== "Java"

    ```java
    PairSocket socket = new PairSocket(ctx);
    socket.bind("tcp://*:5556");

    // Blocking recv
    RecvResult result = socket.recv();
    for (int i = 0; i < result.parts().length; i++)
        System.out.println("Frame " + i + ": "
            + new String(result.parts()[i].data()));

    // Non-blocking recv
    RecvResult r = socket.recv(DONTWAIT);
    if (r == null)  // EAGAIN — no message available
        ;
    ```

=== "Python"

    ```python
    socket = zlink.PairSocket(ctx)
    socket.bind("tcp://*:5556")

    # Blocking recv
    source_rid, parts = socket.recv()
    for i, part in enumerate(parts):
        print(f"Frame {i}: {part.data().decode()}")

    # Non-blocking recv
    try:
        source_rid, parts = socket.recv(dontwait=True)
    except zlink.Again:
        pass  # No message available right now
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = new zlink.PairSocket(ctx);
    socket.bind("tcp://*:5556");

    // Blocking recv
    const { sourceRid, parts } = socket.recv();
    parts.forEach((part, i) =>
        console.log(`Frame ${i}: ${part.data().toString()}`));

    // Non-blocking recv
    const result = socket.recv({ dontwait: true });
    if (!result)  // EAGAIN — no message available
        ;
    ```

=== "C#/.NET"

    ```csharp
    using var socket = new PairSocket(ctx);
    socket.Bind("tcp://*:5556");

    // Blocking recv
    var (sourceRid, parts) = socket.Recv();
    for (int i = 0; i < parts.Length; i++)
        Console.WriteLine($"Frame {i}: {parts[i].DataString()}");

    // Non-blocking recv
    var result = socket.TryRecv();
    if (result == null)  // EAGAIN — no message available
        ;
    ```

=== "Rust"

    ```rust
    let socket = ctx.pair_socket()?;
    socket.bind("tcp://*:5556")?;

    // Blocking recv
    let (source_rid, parts) = socket.recv()?;
    for (i, part) in parts.iter().enumerate() {
        println!("Frame {}: {}", i, part.as_str()?);
    }

    // Non-blocking recv
    match socket.recv_dontwait() {
        Ok((rid, parts)) => { /* process */ }
        Err(e) if e.kind() == zlink::ErrorKind::Again => {}
        Err(e) => return Err(e),
    }
    ```

=== "Go"

    ```go
    socket := ctx.PairSocket()
    socket.Bind("tcp://*:5556")

    // Blocking recv
    source_rid, parts, _ := socket.Recv()
    for (i, part) in parts.iter().enumerate() {
        fmt.Printf("Frame {}: %v\n", i, part.as_str()?)
    }

    // Non-blocking recv
    received, err := socket.RecvDontWait()
        Ok((rid, parts)) => { /* process */ }
        Err(e) if e.kind() == zlink::ErrorKind::Again => {}
        Err(e) => return Err(e),
    }
    ```

#### Callback Mode

Attach a handler callback after socket creation. Messages are dispatched
asynchronously on the I/O thread. Once attached, the handler cannot be
removed for the lifetime of the socket. If a handler has been attached,
`zlink_recv()` fails with `EBUSY`.

=== "C"

    ```c
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        for (size_t i = 0; i < part_count; i++) {
            printf("Frame %zu: %.*s\n", i,
                   (int)zlink_msg_size(&parts[i]),
                   (char *)zlink_msg_data(&parts[i]));
            zlink_msg_close(&parts[i]);
        }
    }

    void *socket = zlink_socket(ctx, ZLINK_STREAM);
    zlink_recv_handler(socket, on_message, NULL);
    ```

=== "C++"

    ```cpp
    zlink::stream_socket_t socket(ctx);
    socket.on_message([](const zlink::routing_id_t& rid,
                         std::span<zlink::message_t> parts) {
        for (size_t i = 0; i < parts.size(); i++)
            std::cout << "Frame " << i << ": "
                      << parts[i].to_string() << "\n";
    });
    ```

=== "Java"

    ```java
    StreamSocket socket = new StreamSocket(ctx);
    socket.onMessage((rid, parts) -> {
        for (int i = 0; i < parts.length; i++)
            System.out.println("Frame " + i + ": "
                + new String(parts[i].data()));
    });
    ```

=== "Python"

    ```python
    socket = zlink.StreamSocket(ctx)

    def on_message(rid, parts):
        for i, part in enumerate(parts):
            print(f"Frame {i}: {part.data().decode()}")

    socket.on_message(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = new zlink.StreamSocket(ctx);
    socket.onMessage((rid, parts) => {
        parts.forEach((part, i) =>
            console.log(`Frame ${i}: ${part.data().toString()}`));
    });
    ```

=== "C#/.NET"

    ```csharp
    using var socket = new StreamSocket(ctx);
    socket.OnMessage((rid, parts) => {
        for (int i = 0; i < parts.Length; i++)
            Console.WriteLine($"Frame {i}: {parts[i].DataString()}");
    });
    ```

=== "Rust"

    ```rust
    let socket = ctx.stream_socket()?;
    socket.on_message(|rid, parts| {
        for (i, part) in parts.iter().enumerate() {
            println!("Frame {}: {}", i, part.as_str()?);
        }
        Ok(())
    })?;
    ```

=== "Go"

    ```go
    socket := ctx.StreamSocket()
    socket.on_message(|rid, parts| {
        for (i, part) in parts.iter().enumerate() {
            fmt.Printf("Frame {}: %v\n", i, part.as_str()?)
        }

    })
    ```

> For a comparison of the two modes and advanced patterns, see
> [Performance Guide](10-performance.md).

### 3.3 Send Flags

| Flag | Description |
|------|-------------|
| `ZLINK_DONTWAIT` | Non-blocking mode (returns EAGAIN immediately if cannot send/recv) |

## 4. Handler Types

Each socket type uses a dedicated registration function:

| Socket Type | Registration Call | Callback Signature |
|---|---|---|
| STREAM | `zlink_recv_handler(socket, fn, userdata)` | `void fn(const zlink_routing_id_t *rid, zlink_msg_t *parts, size_t count, void *userdata)` |
| spot, spot_node | `zlink_subscribe_handler(socket, fn, userdata)` | `void fn(const zlink_routing_id_t *rid, const char *topic, size_t topic_len, zlink_msg_t *parts, size_t count, void *userdata)` |
| PUB | N/A | Send-only socket |

Callbacks are invoked on the I/O thread. Avoid blocking work inside callbacks.
If slow processing is needed, enqueue to a user queue and handle it on a
separate thread.

## 5. Error Handling

=== "C"

    ```c
    zlink_msg_t part;
    zlink_msg_init_size(&part, size);
    memcpy(zlink_msg_data(&part), data, size);
    int rc = zlink_send(socket, &part, 1, 0);
    if (rc == -1) {
        int err = zlink_errno();
        printf("Error: %s\n", zlink_strerror(err));
    }
    ```

=== "C++"

    ```cpp
    try {
        socket.send(zlink::message_t(data, size));
    } catch (const zlink::error_t& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
    ```

=== "Java"

    ```java
    try {
        socket.send(new Message(data));
    } catch (ZlinkException e) {
        System.err.println("Error: " + e.getMessage());
    }
    ```

=== "Python"

    ```python
    try:
        socket.send(data)
    except zlink.ZlinkError as e:
        print(f"Error: {e}")
    ```

=== "Node/TypeScript"

    ```typescript
    try {
        socket.send(data);
    } catch (e) {
        console.error(`Error: ${e}`);
    }
    ```

=== "C#/.NET"

    ```csharp
    try {
        socket.Send(new Message(data));
    } catch (ZlinkException e) {
        Console.Error.WriteLine($"Error: {e.Message}");
    }
    ```

=== "Rust"

    ```rust
    match socket.send(&zlink::Message::from(data)) {
        Ok(()) => {}
        Err(e) => eprintln!("Error: {}", e),
    }
    ```

=== "Go"

    ```go
    err := socket.Send(zlink.NewMessage(data))
         => {}
        Err(e) => eprintln!("Error: {}", e),
    }
    ```

Key error codes:

| Error | Description |
|-------|-------------|
| `EAGAIN` | Cannot complete immediately in non-blocking mode |
| `ETERM` | Context has been terminated |
| `ENOTSOCK` | Invalid socket |
| `EINTR` | Interrupted by signal |
| `EFSM` | Operation not allowed in current state |
| `EHOSTUNREACH` | Host unreachable |

## 6. DEALER/ROUTER Example

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    void on_router_message(const zlink_routing_id_t *source_rid,
                           zlink_msg_t *parts, size_t part_count,
                           void *userdata)
    {
        printf("Received from [%.*s]: %.*s\n",
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
        printf("Reply: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }

    int main(void) {
        void *ctx = zlink_ctx_new();

        /* ROUTER (server) */
        void *router = zlink_socket(ctx, ZLINK_ROUTER);
        /* Receive with zlink_recv() */
        zlink_bind(router, "tcp://*:5555");

        /* DEALER (client) */
        void *dealer = zlink_socket(ctx, ZLINK_DEALER);
        /* Receive with zlink_recv() */
        zlink_connect(dealer, "tcp://127.0.0.1:5555");

        /* DEALER → ROUTER */
        zlink_msg_t req;
        zlink_msg_init_size(&req, 7);
        memcpy(zlink_msg_data(&req), "request", 7);
        zlink_send(dealer, &req, 1, 0);

        /* Handler callbacks process messages asynchronously */
        msleep(100);

        zlink_close(dealer);
        zlink_close(router);
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

        // ROUTER (server)
        zlink::router_socket_t router(ctx);
        router.bind("tcp://*:5555");

        // DEALER (client)
        zlink::dealer_socket_t dealer(ctx);
        dealer.connect("tcp://127.0.0.1:5555");

        // DEALER → ROUTER
        dealer.send(zlink::message_t("request", 7));

        // Receive and print
        auto [rid, parts] = router.recv();
        std::cout << "Received from [" << rid.to_string()
                  << "]: " << parts[0].to_string() << "\n";

        dealer.close();
        router.close();
        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class DealerRouterExample {
        public static void main(String[] args) throws Exception {
            try (Context ctx = new Context()) {
                // ROUTER (server)
                RouterSocket router = new RouterSocket(ctx);
                router.bind("tcp://*:5555");

                // DEALER (client)
                DealerSocket dealer = new DealerSocket(ctx);
                dealer.connect("tcp://127.0.0.1:5555");

                // DEALER → ROUTER
                dealer.send(new Message("request".getBytes()));

                // Receive and print
                RecvResult result = router.recv();
                System.out.println("Received from ["
                    + result.routingId() + "]: "
                    + new String(result.parts()[0].data()));

                dealer.close();
                router.close();
            }
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    # ROUTER (server)
    router = zlink.RouterSocket(ctx)
    router.bind("tcp://*:5555")

    # DEALER (client)
    dealer = zlink.DealerSocket(ctx)
    dealer.connect("tcp://127.0.0.1:5555")

    # DEALER → ROUTER
    dealer.send(b"request")

    # Receive and print
    rid, parts = router.recv()
    print(f"Received from [{rid}]: {parts[0].data().decode()}")

    dealer.close()
    router.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from "zlink";

    const ctx = new zlink.Context();

    // ROUTER (server)
    const router = new zlink.RouterSocket(ctx);
    router.bind("tcp://*:5555");

    // DEALER (client)
    const dealer = new zlink.DealerSocket(ctx);
    dealer.connect("tcp://127.0.0.1:5555");

    // DEALER → ROUTER
    dealer.send(Buffer.from("request"));

    // Receive and print
    const { sourceRid, parts } = router.recv();
    console.log(`Received from [${sourceRid}]: ${parts[0].data().toString()}`);

    dealer.close();
    router.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new Context();

    // ROUTER (server)
    using var router = new RouterSocket(ctx);
    router.Bind("tcp://*:5555");

    // DEALER (client)
    using var dealer = new DealerSocket(ctx);
    dealer.Connect("tcp://127.0.0.1:5555");

    // DEALER → ROUTER
    dealer.Send(new Message("request"u8));

    // Receive and print
    var (rid, parts) = router.Recv();
    Console.WriteLine($"Received from [{rid}]: {parts[0].DataString()}");
    ```

=== "Rust"

    ```rust
    use zlink::{Context, SocketType};

    fn main() -> zlink::Result<()> {
        let ctx = Context::new()?;

        // ROUTER (server)
        let router = ctx.router_socket()?;
        router.bind("tcp://*:5555")?;

        // DEALER (client)
        let dealer = ctx.dealer_socket()?;
        dealer.connect("tcp://127.0.0.1:5555")?;

        // DEALER → ROUTER
        dealer.send(&zlink::Message::from("request"))?;

        // Receive and print
        let (rid, parts) = router.recv()?;
        println!("Received from [{}]: {}", rid, parts[0].as_str()?);

        Ok(())
    }
    ```

=== "Go"

    ```go
    func main() {
        ctx := zlink.NewContext()

        // ROUTER (server)
        router := ctx.RouterSocket()
        router.Bind("tcp://*:5555")

        // DEALER (client)
        dealer := ctx.DealerSocket()
        dealer.Connect("tcp://127.0.0.1:5555")

        // DEALER → ROUTER
        dealer.Send(zlink.NewMessage([]byte("request")))

        // Receive and print
        rid, parts, _ := router.Recv()
        fmt.Printf("Received from [{}]: %v\n", rid, parts[0].as_str()?)


    }
    ```

---
[← Overview](01-overview.md) | [Socket Patterns →](03-0-socket-patterns.md)
