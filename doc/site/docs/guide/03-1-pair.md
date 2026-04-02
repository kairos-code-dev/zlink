# PAIR Socket

## 1. Overview

The PAIR socket forms an exclusive 1:1 bidirectional connection with exactly one peer. If a second peer connects, the first connection is dropped.

**Key characteristics:**
- Only a single pipe is allowed (1:1 exclusive)
- Bidirectional free messaging (send/recv order does not matter)
- The simplest socket type

**Valid socket combinations:** PAIR ↔ PAIR

```
┌────────┐              ┌────────┐
│ PAIR A │◄────────────►│ PAIR B │
└────────┘   양방향     └────────┘
```

## 2. Basic Usage

### Creation and Connection

=== "C"

    ```c
    void *ctx = zlink_ctx_new();

    /* Server side */
    void *server = zlink_socket(ctx, ZLINK_PAIR);
    zlink_bind(server, "tcp://*:5555");

    /* Client side */
    void *client = zlink_socket(ctx, ZLINK_PAIR);
    zlink_connect(client, "tcp://127.0.0.1:5555");
    ```

=== "C++"

    ```cpp
    zlink::context_t ctx;

    // Server side
    zlink::pair_socket_t server(ctx);
    server.bind("tcp://*:5555");

    // Client side
    zlink::pair_socket_t client(ctx);
    client.connect("tcp://127.0.0.1:5555");
    ```

=== "Java"

    ```java
    Context ctx = new Context();

    // Server side
    PairSocket server = new PairSocket(ctx);
    server.bind("tcp://*:5555");

    // Client side
    PairSocket client = new PairSocket(ctx);
    client.connect("tcp://127.0.0.1:5555");
    ```

=== "Python"

    ```python
    ctx = zlink.Context()

    # Server side
    server = zlink.PairSocket(ctx)
    server.bind("tcp://*:5555")

    # Client side
    client = zlink.PairSocket(ctx)
    client.connect("tcp://127.0.0.1:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    const ctx = new zlink.Context();

    // Server side
    const server = new zlink.PairSocket(ctx);
    server.bind("tcp://*:5555");

    // Client side
    const client = new zlink.PairSocket(ctx);
    client.connect("tcp://127.0.0.1:5555");
    ```

=== "C#/.NET"

    ```csharp
    var ctx = new Context();

    // Server side
    var server = new PairSocket(ctx);
    server.Bind("tcp://*:5555");

    // Client side
    var client = new PairSocket(ctx);
    client.Connect("tcp://127.0.0.1:5555");
    ```

=== "Rust"

    ```rust
    let ctx = Context::new();

    // Server side
    let server = ctx.pair_socket();
    server.bind("tcp://*:5555");

    // Client side
    let client = ctx.pair_socket();
    client.connect("tcp://127.0.0.1:5555");
    ```

=== "Go"

    ```go
    ctx := zlink.NewContext()

    // Server side
    server := ctx.PairSocket()
    server.Bind("tcp://*:5555")

    // Client side
    client := ctx.PairSocket()
    client.Connect("tcp://127.0.0.1:5555")
    ```

### Message Exchange

!!! note "C API Callback Signature"
    The receive handler uses C-specific types (`zlink_routing_id_t`,
    `zlink_msg_t`). Each binding provides its own idiomatic callback or
    receive interface.

    ```c
    /* Define receive handler */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        printf("Received: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C"

    ```c
    /* Server stays in recv model */
    void *server = zlink_socket(ctx, ZLINK_PAIR);

    /* Client (send only) */
    void *client = zlink_socket(ctx, ZLINK_PAIR);

    /* ... bind/connect ... */

    /* Client → Server */
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 5);
    memcpy(zlink_msg_data(&msg), "Hello", 5);
    zlink_send(client, &msg, 1, 0);
    /* Server receives with zlink_recv() or poller + zlink_recv() */

    /* Server → Client (bidirectional, but client needs handler too for receiving) */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 5);
    memcpy(zlink_msg_data(&reply), "World", 5);
    zlink_send(server, &reply, 1, 0);
    ```

=== "C++"

    ```cpp
    // Server stays in recv model
    zlink::pair_socket_t server(ctx);

    // Client (send only)
    zlink::pair_socket_t client(ctx);

    // ... bind/connect ...

    // Client → Server
    client.send("Hello");

    // Server → Client
    server.send("World");
    ```

=== "Java"

    ```java
    // Server stays in recv model
    PairSocket server = new PairSocket(ctx);

    // Client (send only)
    PairSocket client = new PairSocket(ctx);

    // ... bind/connect ...

    // Client → Server
    client.send("Hello");

    // Server → Client
    server.send("World");
    ```

=== "Python"

    ```python
    # Server stays in recv model
    server = zlink.PairSocket(ctx)

    # Client (send only)
    client = zlink.PairSocket(ctx)

    # ... bind/connect ...

    # Client → Server
    client.send(b"Hello")

    # Server → Client
    server.send(b"World")
    ```

=== "Node/TypeScript"

    ```typescript
    // Server stays in recv model
    const server = new zlink.PairSocket(ctx);

    // Client (send only)
    const client = new zlink.PairSocket(ctx);

    // ... bind/connect ...

    // Client → Server
    client.send(Buffer.from("Hello"));

    // Server → Client
    server.send(Buffer.from("World"));
    ```

=== "C#/.NET"

    ```csharp
    // Server stays in recv model
    var server = new PairSocket(ctx);

    // Client (send only)
    var client = new PairSocket(ctx);

    // ... bind/connect ...

    // Client → Server
    client.Send("Hello");

    // Server → Client
    server.Send("World");
    ```

=== "Rust"

    ```rust
    // Server stays in recv model
    let server = ctx.pair_socket();

    // Client (send only)
    let client = ctx.pair_socket();

    // ... bind/connect ...

    // Client → Server
    client.send(b"Hello");

    // Server → Client
    server.send(b"World");
    ```

=== "Go"

    ```go
    // Server stays in recv model
    server := ctx.PairSocket()

    // Client (send only)
    client := ctx.PairSocket()

    // ... bind/connect ...

    // Client → Server
    client.Send([]byte("Hello"))

    // Server → Client
    server.Send([]byte("World"))
    ```

??? example "Full Sample Code"

    | Language | Source |
    |----------|--------|
    | C | [pair_callback_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/pair_callback_sample.c) |
    | C++ | [pair_callback_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/pair_callback_sample.cpp) |
    | Java | [PairCallbackSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/PairCallbackSample.java) |
    | Python | [pair_callback.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/pair_callback.py) |
    | Node | [pair_callback_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/pair_callback_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/PairCallback/Program.cs) |
    | Rust | [pair_callback_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/pair_callback_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/pair_callback_sample/main.go) |

### Sending Multipart Data

Multipart data is sent as a parts array in a single `zlink_send` call.

=== "C"

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

=== "C++"

    ```cpp
    server.send({"foo", "foobar"});

    // Receiver pulls both frames from one recv() call:
    // parts[0] = "foo", parts[1] = "foobar"
    ```

=== "Java"

    ```java
    server.send("foo", "foobar");

    // Receiver pulls both frames from one recv() call:
    // parts[0] = "foo", parts[1] = "foobar"
    ```

=== "Python"

    ```python
    server.send([b"foo", b"foobar"])

    # Receiver pulls both frames from one recv() call:
    # parts[0] = b"foo", parts[1] = b"foobar"
    ```

=== "Node/TypeScript"

    ```typescript
    server.send([Buffer.from("foo"), Buffer.from("foobar")]);

    // Receiver pulls both frames from one receive() call:
    // parts[0] = "foo", parts[1] = "foobar"
    ```

=== "C#/.NET"

    ```csharp
    server.Send("foo", "foobar");

    // Receiver pulls both frames from one Receive() call:
    // parts[0] = "foo", parts[1] = "foobar"
    ```

=== "Rust"

    ```rust
    server.send(&[b"foo", b"foobar"]);

    // Receiver pulls both frames from one recv() call:
    // parts[0] = "foo", parts[1] = "foobar"
    ```

=== "Go"

    ```go
    server.Send([]byte("foo"), []byte("foobar"))

    // Receiver pulls both frames from one recv() call:
    // parts[0] = "foo", parts[1] = "foobar"
    ```

> Reference: `core/tests/test_pair_inproc.cpp` -- `test_zlink_send_multipart()` test

### Receive Modes

PAIR is recv/poller-only in the public API.
Use `zlink_recv()` to receive synchronously.

=== "C"

    ```c
    void *pair = zlink_socket(ctx, ZLINK_PAIR);
    zlink_bind(pair, "tcp://*:5556");

    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = zlink_recv(pair, &source_rid, &parts, &part_count, 0);
    if (rc == 0) {
        /* process parts[0..part_count-1] */
        zlink_multipart_close(parts, part_count);
        free(parts);
    }
    ```

=== "C++"

    ```cpp
    zlink::pair_socket_t pair(ctx);
    pair.bind("tcp://*:5556");

    auto [source_rid, parts] = pair.recv();
    // process parts[0..N-1]
    ```

=== "Java"

    ```java
    PairSocket pair = new PairSocket(ctx);
    pair.bind("tcp://*:5556");

    Message msg = pair.recv();
    // process msg.parts()
    ```

=== "Python"

    ```python
    pair = zlink.PairSocket(ctx)
    pair.bind("tcp://*:5556")

    source_rid, parts = pair.recv()
    # process parts[0..N-1]
    ```

=== "Node/TypeScript"

    ```typescript
    const pair = new zlink.PairSocket(ctx);
    pair.bind("tcp://*:5556");

    const [sourceRid, parts] = pair.receive();
    // process parts[0..N-1]
    ```

=== "C#/.NET"

    ```csharp
    var pair = new PairSocket(ctx);
    pair.Bind("tcp://*:5556");

    var (sourceRid, parts) = pair.Receive();
    // process parts[0..N-1]
    ```

=== "Rust"

    ```rust
    let pair = ctx.pair_socket();
    pair.bind("tcp://*:5556");

    let (source_rid, parts) = pair.recv();
    // process parts[0..N-1]
    ```

=== "Go"

    ```go
    pair := ctx.PairSocket()
    pair.Bind("tcp://*:5556")

    source_rid, parts := pair.Recv()
    // process parts[0..N-1]
    ```

> When HWM is reached, `zlink_send()` blocks (default) or returns
> `EAGAIN` with `ZLINK_DONTWAIT`. For advanced backpressure patterns,
> see [Performance Guide](10-performance.md).

??? example "Full Sample Code"

    | Language | Source |
    |----------|--------|
    | C | [pair_recv_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/pair_recv_sample.c) |
    | C++ | [pair_recv_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/pair_recv_sample.cpp) |
    | Java | [PairRecvSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/PairRecvSample.java) |
    | Python | [pair_recv.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/pair_recv.py) |
    | Node | [pair_recv_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/pair_recv_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/PairRecv/Program.cs) |
    | Rust | [pair_recv_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/pair_recv_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/pair_recv_sample/main.go) |

## 3. Message Format

PAIR socket message frames contain **application data only**.

```
Single frame:     [data]
Multipart frame:  [frame1][frame2]...[frameN]
```

> For `source_rid` and the common receive interface, see
> [Socket Patterns Overview](03-0-socket-patterns.md#7-common-receive-interface).

Multipart send:

=== "C"

    ```c
    zlink_msg_t parts[2];
    zlink_msg_init_size(&parts[0], 6);
    memcpy(zlink_msg_data(&parts[0]), "header", 6);
    zlink_msg_init_size(&parts[1], 4);
    memcpy(zlink_msg_data(&parts[1]), "body", 4);
    zlink_send(server, parts, 2, 0);
    ```

=== "C++"

    ```cpp
    server.send({"header", "body"});
    ```

=== "Java"

    ```java
    server.send("header", "body");
    ```

=== "Python"

    ```python
    server.send([b"header", b"body"])
    ```

=== "Node/TypeScript"

    ```typescript
    server.send([Buffer.from("header"), Buffer.from("body")]);
    ```

=== "C#/.NET"

    ```csharp
    server.Send("header", "body");
    ```

=== "Rust"

    ```rust
    server.send(&[b"header", b"body"]);
    ```

=== "Go"

    ```go
    server.Send([]byte("header"), []byte("body"))
    ```

## 4. Socket Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `ZLINK_OPT_SNDHWM` | int | 1000 | Maximum number of messages in the send queue |
| `ZLINK_OPT_RCVHWM` | int | 1000 | Maximum number of messages in the receive queue |
| `ZLINK_OPT_LINGER` | int | -1 | Wait time for unsent messages on close (ms), -1=infinite |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | Send timeout (ms), -1=infinite |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | Receive timeout (ms), -1=infinite |

=== "C"

    ```c
    int hwm = 5000;
    zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

    int linger = 0;  /* return immediately on close */
    zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
    ```

=== "C++"

    ```cpp
    socket.set_option(ZLINK_OPT_SNDHWM, 5000);

    socket.set_option(ZLINK_OPT_LINGER, 0);  // return immediately on close
    ```

=== "Java"

    ```java
    socket.setOption(ZLINK_OPT_SNDHWM, 5000);

    socket.setOption(ZLINK_OPT_LINGER, 0);  // return immediately on close
    ```

=== "Python"

    ```python
    socket.set_option(ZLINK_OPT_SNDHWM, 5000)

    socket.set_option(ZLINK_OPT_LINGER, 0)  # return immediately on close
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setOption(ZLINK_OPT_SNDHWM, 5000);

    socket.setOption(ZLINK_OPT_LINGER, 0);  // return immediately on close
    ```

=== "C#/.NET"

    ```csharp
    socket.SetOption(ZLINK_OPT_SNDHWM, 5000);

    socket.SetOption(ZLINK_OPT_LINGER, 0);  // return immediately on close
    ```

=== "Rust"

    ```rust
    socket.set_option(ZLINK_OPT_SNDHWM, 5000);

    socket.set_option(ZLINK_OPT_LINGER, 0);  // return immediately on close
    ```

=== "Go"

    ```go
    socket.SetOption(zlink.OptionSndHWM, 5000)

    socket.SetOption(zlink.OptionLinger, 0)  // return immediately on close
    ```

## 5. Usage Patterns

### Pattern 1: Inter-thread Signaling (inproc)

The most common PAIR use case. Zero-copy communication between threads via the inproc transport.

=== "C"

    ```c
    /* Main thread */
    void *signal = zlink_socket(ctx, ZLINK_PAIR);
    zlink_bind(signal, "inproc://signal");

    /* Worker thread */
    void *worker_signal = zlink_socket(ctx, ZLINK_PAIR);
    zlink_connect(worker_signal, "inproc://signal");

    /* Worker → Main: task completion signal */
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 4);
    memcpy(zlink_msg_data(&msg), "DONE", 4);
    zlink_send(worker_signal, &msg, 1, 0);

    /* Main: on_signal callback receives "DONE" asynchronously */
    ```

=== "C++"

    ```cpp
    // Main thread
    zlink::pair_socket_t signal(ctx);
    signal.bind("inproc://signal");

    // Worker thread
    zlink::pair_socket_t worker_signal(ctx);
    worker_signal.connect("inproc://signal");

    // Worker → Main: task completion signal
    worker_signal.send("DONE");
    ```

=== "Java"

    ```java
    // Main thread
    PairSocket signal = new PairSocket(ctx);
    signal.bind("inproc://signal");

    // Worker thread
    PairSocket workerSignal = new PairSocket(ctx);
    workerSignal.connect("inproc://signal");

    // Worker → Main: task completion signal
    workerSignal.send("DONE");
    ```

=== "Python"

    ```python
    # Main thread
    signal = zlink.PairSocket(ctx)
    signal.bind("inproc://signal")

    # Worker thread
    worker_signal = zlink.PairSocket(ctx)
    worker_signal.connect("inproc://signal")

    # Worker → Main: task completion signal
    worker_signal.send(b"DONE")
    ```

=== "Node/TypeScript"

    ```typescript
    // Main thread
    const signal = new zlink.PairSocket(ctx);
    signal.bind("inproc://signal");

    // Worker thread
    const workerSignal = new zlink.PairSocket(ctx);
    workerSignal.connect("inproc://signal");

    // Worker → Main: task completion signal
    workerSignal.send(Buffer.from("DONE"));
    ```

=== "C#/.NET"

    ```csharp
    // Main thread
    var signal = new PairSocket(ctx);
    signal.Bind("inproc://signal");

    // Worker thread
    var workerSignal = new PairSocket(ctx);
    workerSignal.Connect("inproc://signal");

    // Worker → Main: task completion signal
    workerSignal.Send("DONE");
    ```

=== "Rust"

    ```rust
    // Main thread
    let signal = ctx.pair_socket();
    signal.bind("inproc://signal");

    // Worker thread
    let worker_signal = ctx.pair_socket();
    worker_signal.connect("inproc://signal");

    // Worker → Main: task completion signal
    worker_signal.send(b"DONE");
    ```

=== "Go"

    ```go
    // Main thread
    signal := ctx.PairSocket()
    signal.Bind("inproc://signal")

    // Worker thread
    worker_signal := ctx.PairSocket()
    worker_signal.Connect("inproc://signal")

    // Worker → Main: task completion signal
    worker_signal.Send([]byte("DONE"))
    ```

> Reference: `core/tests/test_pair_inproc.cpp` -- bind → connect → bounce pattern

### Pattern 2: TCP Communication

1:1 communication over the network. Wildcard bind enables automatic port assignment.

=== "C"

    ```c
    /* Server: wildcard port */
    void *server = zlink_socket(ctx, ZLINK_PAIR);
    zlink_bind(server, "tcp://127.0.0.1:*");

    /* Query the assigned endpoint */
    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(server, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

    /* Client: connect using the queried endpoint */
    void *client = zlink_socket(ctx, ZLINK_PAIR);
    zlink_connect(client, endpoint);
    ```

=== "C++"

    ```cpp
    // Server: wildcard port
    zlink::pair_socket_t server(ctx);
    server.bind("tcp://127.0.0.1:*");

    // Query the assigned endpoint
    auto endpoint = server.get_option<std::string>(ZLINK_OPT_LAST_ENDPOINT);

    // Client: connect using the queried endpoint
    zlink::pair_socket_t client(ctx);
    client.connect(endpoint);
    ```

=== "Java"

    ```java
    // Server: wildcard port
    PairSocket server = new PairSocket(ctx);
    server.bind("tcp://127.0.0.1:*");

    // Query the assigned endpoint
    String endpoint = server.getOption(ZLINK_OPT_LAST_ENDPOINT);

    // Client: connect using the queried endpoint
    PairSocket client = new PairSocket(ctx);
    client.connect(endpoint);
    ```

=== "Python"

    ```python
    # Server: wildcard port
    server = zlink.PairSocket(ctx)
    server.bind("tcp://127.0.0.1:*")

    # Query the assigned endpoint
    endpoint = server.get_option(ZLINK_OPT_LAST_ENDPOINT)

    # Client: connect using the queried endpoint
    client = zlink.PairSocket(ctx)
    client.connect(endpoint)
    ```

=== "Node/TypeScript"

    ```typescript
    // Server: wildcard port
    const server = new zlink.PairSocket(ctx);
    server.bind("tcp://127.0.0.1:*");

    // Query the assigned endpoint
    const endpoint = server.getOption(ZLINK_OPT_LAST_ENDPOINT);

    // Client: connect using the queried endpoint
    const client = new zlink.PairSocket(ctx);
    client.connect(endpoint);
    ```

=== "C#/.NET"

    ```csharp
    // Server: wildcard port
    var server = new PairSocket(ctx);
    server.Bind("tcp://127.0.0.1:*");

    // Query the assigned endpoint
    var endpoint = server.GetOption(ZLINK_OPT_LAST_ENDPOINT);

    // Client: connect using the queried endpoint
    var client = new PairSocket(ctx);
    client.Connect(endpoint);
    ```

=== "Rust"

    ```rust
    // Server: wildcard port
    let server = ctx.pair_socket();
    server.bind("tcp://127.0.0.1:*");

    // Query the assigned endpoint
    let endpoint = server.get_option::<String>(ZLINK_OPT_LAST_ENDPOINT);

    // Client: connect using the queried endpoint
    let client = ctx.pair_socket();
    client.connect(&endpoint);
    ```

=== "Go"

    ```go
    // Server: wildcard port
    server := ctx.PairSocket()
    server.Bind("tcp://127.0.0.1:*")

    // Query the assigned endpoint
    endpoint, _ := server.GetOption(zlink.OptionLastEndpoint)

    // Client: connect using the queried endpoint
    client := ctx.PairSocket()
    client.Connect(endpoint)
    ```

> Reference: `core/tests/test_pair_tcp.cpp` -- `bind_loopback_ipv4()` + wildcard bind

### Pattern 3: Connection by DNS Name

You can also connect using a hostname.

=== "C"

    ```c
    void *client = zlink_socket(ctx, ZLINK_PAIR);
    zlink_connect(client, "tcp://localhost:5555");
    ```

=== "C++"

    ```cpp
    zlink::pair_socket_t client(ctx);
    client.connect("tcp://localhost:5555");
    ```

=== "Java"

    ```java
    PairSocket client = new PairSocket(ctx);
    client.connect("tcp://localhost:5555");
    ```

=== "Python"

    ```python
    client = zlink.PairSocket(ctx)
    client.connect("tcp://localhost:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    const client = new zlink.PairSocket(ctx);
    client.connect("tcp://localhost:5555");
    ```

=== "C#/.NET"

    ```csharp
    var client = new PairSocket(ctx);
    client.Connect("tcp://localhost:5555");
    ```

=== "Rust"

    ```rust
    let client = ctx.pair_socket();
    client.connect("tcp://localhost:5555");
    ```

=== "Go"

    ```go
    client := ctx.PairSocket()
    client.Connect("tcp://localhost:5555")
    ```

> Reference: `core/tests/test_pair_tcp.cpp` -- `test_pair_tcp_connect_by_name()`

### Pattern 4: IPC Communication

Inter-process communication on the same machine (Linux/macOS).

=== "C"

    ```c
    void *server = zlink_socket(ctx, ZLINK_PAIR);
    zlink_bind(server, "ipc:///tmp/myapp.ipc");

    void *client = zlink_socket(ctx, ZLINK_PAIR);
    zlink_connect(client, "ipc:///tmp/myapp.ipc");
    ```

=== "C++"

    ```cpp
    zlink::pair_socket_t server(ctx);
    server.bind("ipc:///tmp/myapp.ipc");

    zlink::pair_socket_t client(ctx);
    client.connect("ipc:///tmp/myapp.ipc");
    ```

=== "Java"

    ```java
    PairSocket server = new PairSocket(ctx);
    server.bind("ipc:///tmp/myapp.ipc");

    PairSocket client = new PairSocket(ctx);
    client.connect("ipc:///tmp/myapp.ipc");
    ```

=== "Python"

    ```python
    server = zlink.PairSocket(ctx)
    server.bind("ipc:///tmp/myapp.ipc")

    client = zlink.PairSocket(ctx)
    client.connect("ipc:///tmp/myapp.ipc")
    ```

=== "Node/TypeScript"

    ```typescript
    const server = new zlink.PairSocket(ctx);
    server.bind("ipc:///tmp/myapp.ipc");

    const client = new zlink.PairSocket(ctx);
    client.connect("ipc:///tmp/myapp.ipc");
    ```

=== "C#/.NET"

    ```csharp
    var server = new PairSocket(ctx);
    server.Bind("ipc:///tmp/myapp.ipc");

    var client = new PairSocket(ctx);
    client.Connect("ipc:///tmp/myapp.ipc");
    ```

=== "Rust"

    ```rust
    let server = ctx.pair_socket();
    server.bind("ipc:///tmp/myapp.ipc");

    let client = ctx.pair_socket();
    client.connect("ipc:///tmp/myapp.ipc");
    ```

=== "Go"

    ```go
    server := ctx.PairSocket()
    server.Bind("ipc:///tmp/myapp.ipc")

    client := ctx.PairSocket()
    client.Connect("ipc:///tmp/myapp.ipc")
    ```

> Reference: `core/tests/test_pair_ipc.cpp` -- includes IPC path length validation

## 6. Caveats

### Only a Single Peer Allowed

A PAIR socket maintains only one connection. If a second peer connects, the first connection is dropped.

```
 Allowed:  PAIR A ↔ PAIR B      (1:1)
 Invalid:  PAIR A ← PAIR B      (N:1 attempt drops existing connection)
               ← PAIR C
```

Use DEALER/ROUTER if N:1 communication is needed.

### inproc bind Order

With the inproc transport, **bind must be called before connect**.

=== "C"

    ```c
    /* Correct order */
    zlink_bind(a, "inproc://signal");     /* 1. bind first */
    zlink_connect(b, "inproc://signal");  /* 2. connect */

    /* Wrong order -- fails */
    zlink_connect(b, "inproc://signal");  /* fails because bind has not been called yet */
    zlink_bind(a, "inproc://signal");
    ```

=== "C++"

    ```cpp
    // Correct order
    a.bind("inproc://signal");     // 1. bind first
    b.connect("inproc://signal");  // 2. connect

    // Wrong order -- fails
    b.connect("inproc://signal");  // fails because bind has not been called yet
    a.bind("inproc://signal");
    ```

=== "Java"

    ```java
    // Correct order
    a.bind("inproc://signal");     // 1. bind first
    b.connect("inproc://signal");  // 2. connect

    // Wrong order -- fails
    b.connect("inproc://signal");  // fails because bind has not been called yet
    a.bind("inproc://signal");
    ```

=== "Python"

    ```python
    # Correct order
    a.bind("inproc://signal")      # 1. bind first
    b.connect("inproc://signal")   # 2. connect

    # Wrong order -- fails
    b.connect("inproc://signal")   # fails because bind has not been called yet
    a.bind("inproc://signal")
    ```

=== "Node/TypeScript"

    ```typescript
    // Correct order
    a.bind("inproc://signal");     // 1. bind first
    b.connect("inproc://signal");  // 2. connect

    // Wrong order -- fails
    b.connect("inproc://signal");  // fails because bind has not been called yet
    a.bind("inproc://signal");
    ```

=== "C#/.NET"

    ```csharp
    // Correct order
    a.Bind("inproc://signal");     // 1. bind first
    b.Connect("inproc://signal");  // 2. connect

    // Wrong order -- fails
    b.Connect("inproc://signal");  // fails because bind has not been called yet
    a.Bind("inproc://signal");
    ```

=== "Rust"

    ```rust
    // Correct order
    a.bind("inproc://signal");     // 1. bind first
    b.connect("inproc://signal");  // 2. connect

    // Wrong order -- fails
    b.connect("inproc://signal");  // fails because bind has not been called yet
    a.bind("inproc://signal");
    ```

=== "Go"

    ```go
    // Correct order
    a.Bind("inproc://signal")  // 1. bind first
    b.Connect("inproc://signal")  // 2. connect

    // Wrong order -- fails
    b.Connect("inproc://signal")  // fails because bind has not been called yet
    a.Bind("inproc://signal")
    ```

### IPC Path Length

The file path of an IPC endpoint cannot exceed the system limit (typically 108 characters).

=== "C"

    ```c
    /* Path too long → ENAMETOOLONG error */
    zlink_bind(socket, "ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "C++"

    ```cpp
    // Path too long → ENAMETOOLONG error
    socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "Java"

    ```java
    // Path too long → ENAMETOOLONG error
    socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "Python"

    ```python
    # Path too long → ENAMETOOLONG error
    socket.bind("ipc:///very/long/path/.../endpoint.ipc")
    ```

=== "Node/TypeScript"

    ```typescript
    // Path too long → ENAMETOOLONG error
    socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "C#/.NET"

    ```csharp
    // Path too long → ENAMETOOLONG error
    socket.Bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "Rust"

    ```rust
    // Path too long → ENAMETOOLONG error
    socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "Go"

    ```go
    // Path too long → ENAMETOOLONG error
    socket.Bind("ipc:///very/long/path/.../endpoint.ipc")
    ```

> Reference: `core/tests/test_pair_ipc.cpp` -- `test_endpoint_too_long()`

### HWM Behavior

When there is no peer or the peer is slow, outgoing messages are queued up to the HWM. When the HWM is exceeded, `zlink_send()` blocks (default) or returns `EAGAIN` (`ZLINK_DONTWAIT`).

### LINGER Setting

When `zlink_close()` is called and there are unsent messages remaining, it waits for the LINGER duration. For tests or when a fast shutdown is needed:

=== "C"

    ```c
    int linger = 0;
    zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
    ```

=== "C++"

    ```cpp
    socket.set_option(ZLINK_OPT_LINGER, 0);
    ```

=== "Java"

    ```java
    socket.setOption(ZLINK_OPT_LINGER, 0);
    ```

=== "Python"

    ```python
    socket.set_option(ZLINK_OPT_LINGER, 0)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setOption(ZLINK_OPT_LINGER, 0);
    ```

=== "C#/.NET"

    ```csharp
    socket.SetOption(ZLINK_OPT_LINGER, 0);
    ```

=== "Rust"

    ```rust
    socket.set_option(ZLINK_OPT_LINGER, 0);
    ```

=== "Go"

    ```go
    socket.SetOption(zlink.OptionLinger, 0)
    ```

---
[← Socket Patterns](03-0-socket-patterns.md) | [PUB/SUB →](03-2-pubsub.md)
