# Socket Patterns Overview and Selection Guide

## 1. Overview

zlink provides 8 socket types. Each socket implements a unique messaging pattern, and communication is only possible between valid socket combinations.

## 2. Socket Summary

| Socket | Pattern | Direction | Routing Strategy | Primary Use |
|--------|---------|-----------|------------------|-------------|
| **PAIR** | 1:1 Bidirectional | Bidirectional | Single pipe (1:1 exclusive) | Inter-thread signaling, worker coordination |
| **PUB** | Publish | Unidirectional (send) | `dist_t` (Fan-out) | Event broadcast |
| **SUB** | Subscribe | Unidirectional (recv) | `fq_t` (Fair-queue) | Topic-filtered reception |
| **XPUB** | Advanced Publish | Bidirectional | `dist_t` + subscription recv | Proxy/broker, subscription monitoring |
| **XSUB** | Advanced Subscribe | Unidirectional (recv) | `fq_t` (no local filter) | Proxy/broker |
| **DEALER** | Async Request | Bidirectional | Send: `lb_t` (Round-robin), Recv: `fq_t` | Load balancing, async requests |
| **ROUTER** | ID Routing | Bidirectional | Directed send by routing_id | Server, broker, multi-client |
| **STREAM** | RAW Communication | Bidirectional | routing_id based (4B uint32) | External client integration |

## 3. Socket Compatibility Matrix

Only valid socket combinations can be connected. Connecting incompatible sockets causes a handshake failure.

| | PAIR | PUB | SUB | XPUB | XSUB | DEALER | ROUTER | STREAM |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **PAIR** | **O** | | | | | | | |
| **PUB** | | | **O** | | **O** | | | |
| **SUB** | | **O** | | **O** | | | | |
| **XPUB** | | | **O** | | **O** | | | |
| **XSUB** | | **O** | | **O** | | | | |
| **DEALER** | | | | | | **O** | **O** | |
| **ROUTER** | | | | | | **O** | **O** | |
| **STREAM** | | | | | | | | **External** |

> STREAM sockets are not compatible with other zlink sockets; they communicate only with external RAW clients.

## 4. Routing Strategy Summary

| Strategy | Behavior | Used By |
|----------|----------|---------|
| **Single pipe** | Communicates with only one peer (N:1 not possible) | PAIR |
| **Round-robin** (`lb_t`) | Distributes to connected peers in rotation | DEALER send |
| **Fair-queue** (`fq_t`) | Receives fairly from all peers | DEALER/SUB recv |
| **Fan-out** (`dist_t`) | Replicates and sends to all subscribers | PUB/XPUB |
| **ID routing** | Directs to a specific peer via routing_id frame | ROUTER/STREAM |

> For internal implementation details of routing strategies, see [architecture.md](../internals/architecture.md).

## 5. Pattern Selection Guide

### Decision Flow

```
Is the communication peer an external client (browser, game)?
├── Yes → STREAM (ws/wss/tcp/tls)
└── No → Communication between zlink sockets
         ├── Is it 1:1 exclusive?
         │   └── Yes → PAIR
         └── No → N:M communication
              ├── Publish-subscribe (broadcast)?
              │   ├── Proxy/broker needed → XPUB/XSUB
              │   └── Simple pub-sub → PUB/SUB
              └── Request-reply / routing?
                  └── DEALER/ROUTER
```

### Recommendations by Use Case

| Use Case | Recommended Pattern | Description |
|----------|---------------------|-------------|
| Inter-thread signaling | PAIR + inproc | Fastest 1:1 communication |
| Event broadcast | PUB/SUB | Topic-based filtering |
| Message broker/proxy | XPUB/XSUB | Subscription message access and transformation |
| Async request-reply server | DEALER↔DEALER | Async bidirectional communication |
| Load balancing | Multiple DEALERs → ROUTER | Round-robin distribution |
| Targeted peer delivery | ROUTER | Specify target via routing_id |
| Web client integration | STREAM + ws/wss | WebSocket RAW communication |
| External TCP client | STREAM + tcp/tls | Length-prefix RAW communication |

> For location transparency (auto-connect, load balancing, topic mesh),
> use the service layer (SPOT) instead of raw sockets.
> See [Services Overview](07-0-services.md) for details.

## 6. Sub-Documents

See the individual documents for detailed usage of each socket type.

| Document | Socket | Description |
|----------|--------|-------------|
| [03-1-pair.md](03-1-pair.md) | PAIR | 1:1 bidirectional exclusive connection |
| [03-2-pubsub.md](03-2-pubsub.md) | PUB/SUB/XPUB/XSUB | Publish-subscribe family |
| [03-3-dealer.md](03-3-dealer.md) | DEALER | Async request, round-robin |
| [03-4-router.md](03-4-router.md) | ROUTER | ID-based routing |
| [03-5-stream.md](03-5-stream.md) | STREAM | External client RAW communication |

## 7. Common Receive Interface

All socket types use the same interface for `zlink_recv()` and recv callbacks:

!!! note "C API signature — each binding wraps this into its idiomatic recv method."

    ```c
    int zlink_recv (void *socket,
                    zlink_routing_id_t *source_rid,  /* sender routing_id */
                    zlink_msg_t **parts,              /* multipart data */
                    size_t *part_count,               /* frame count */
                    zlink_send_flags_t flags);
    ```

- **`source_rid`**: Populated with the sender peer's routing_id on all
  socket types. This is not a message frame — zlink automatically
  resolves the connected peer's identity as a separate parameter.
- **`parts` / `part_count`**: Multipart is the default on all sockets.
  `part_count=1` for single frame, `part_count=2+` for multipart.

> **Difference from libzmq:** libzmq ROUTER returned routing_id as the
> first frame of `zmq_recv()`. In zlink, routing_id is a separate
> parameter on all socket types.

PUB/SUB sockets use dedicated APIs instead of `zlink_recv()`:
- Receive: `zlink_subscribe()` / `zlink_subscribe_handler()`
- Publish: `zlink_publish()`
- `zlink_send()` / `zlink_recv()` return `ENOTSUP` on all 4 PUB/SUB sockets

## 8. Terminology

Terms used throughout the documentation:

| Term | Meaning |
|------|---------|
| **hot path** | High-frequency call path. Data transfer APIs like `send`, `publish`. Optimized for concurrent calls |
| **control path** | Low-frequency call path. Configuration/management APIs like `bind`, `connect`, `set_option`, `monitor`. Correctness guaranteed via internal serialization |
| **correctness** | Property that multiple threads can use the same handle concurrently without data corruption or crashes |
| **fail-fast lifecycle gate** | On `close`/`destroy`: returns `EBUSY` immediately if other threads are using the handle; after close is accepted, new API calls return `ESHUTDOWN` |
| **admission guard** | Internal gate that checks handle validity and whether shutdown is in progress on API entry |
| **approximate limit** | Not an exact hard limit. HWM allows slight overshoot for lock-free performance |

> For the full thread-safety contract, see the [Thread-Safety Guide](11-thread-safety.md).

## 9. Basic Usage Flow

The basic pattern common to all socket types:

=== "C"

    ```c
    /* 1. Create Context */
    void *ctx = zlink_ctx_new();

    /* 2. Define handler callback */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        /* process received message */
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }

    /* 3. Create Socket (raw STREAM callback example) */
    void *socket = zlink_socket(ctx, ZLINK_STREAM);
    zlink_recv_handler(socket, on_message, NULL);

    /* 4. Set socket options (before bind/connect) */
    zlink_set_option(socket, ZLINK_OPT_<OPTION>, &value, sizeof(value));

    /* 5. Establish connection (bind or connect) */
    zlink_bind(socket, "tcp://*:5555");
    // or
    zlink_connect(socket, "tcp://127.0.0.1:5555");

    /* 6. Send messages (receive is handled by callback) */
    zlink_send(socket, data, size, flags);

    /* 7. Cleanup */
    zlink_close(socket);
    zlink_ctx_term(ctx);
    ```

=== "C++"

    ```cpp
    // 1. Create Context
    zlink::context_t ctx;

    // 2. Define handler callback
    auto on_message = [](const zlink::routing_id_t& source_rid,
                         std::vector<zlink::message_t> parts,
                         void* userdata) {
        // process received message
    };

    // 3. Create Socket (raw STREAM callback example)
    zlink::stream_socket_t socket(ctx);
    socket.recv_handler(on_message, nullptr);

    // 4. Set socket options (before bind/connect)
    socket.set_option(ZLINK_OPT_<OPTION>, value);

    // 5. Establish connection (bind or connect)
    socket.bind("tcp://*:5555");
    // or
    socket.connect("tcp://127.0.0.1:5555");

    // 6. Send messages (receive is handled by callback)
    socket.send(data);

    // 7. Cleanup
    socket.close();
    ```

=== "Java"

    ```java
    // 1. Create Context
    Context ctx = new Context();

    // 2–3. Create Socket with callback (raw STREAM example)
    StreamSocket socket = new StreamSocket(ctx);
    socket.recvHandler((sourceRid, parts) -> {
        // process received message
    });

    // 4. Set socket options (before bind/connect)
    socket.setOption(Option.<OPTION>, value);

    // 5. Establish connection (bind or connect)
    socket.bind("tcp://*:5555");
    // or
    socket.connect("tcp://127.0.0.1:5555");

    // 6. Send messages (receive is handled by callback)
    socket.send(data);

    // 7. Cleanup
    socket.close();
    ctx.close();
    ```

=== "Python"

    ```python
    # 1. Create Context
    ctx = zlink.Context()

    # 2–3. Create Socket with callback (raw STREAM example)
    socket = zlink.StreamSocket(ctx)
    socket.recv_handler(lambda source_rid, parts: ...)

    # 4. Set socket options (before bind/connect)
    socket.set_option(zlink.Option.<OPTION>, value)

    # 5. Establish connection (bind or connect)
    socket.bind("tcp://*:5555")
    # or
    socket.connect("tcp://127.0.0.1:5555")

    # 6. Send messages (receive is handled by callback)
    socket.send(data)

    # 7. Cleanup
    socket.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    // 1. Create Context
    const ctx = new zlink.Context();

    // 2–3. Create Socket with callback (raw STREAM example)
    const socket = new zlink.StreamSocket(ctx);
    socket.recvHandler((sourceRid, parts) => {
        // process received message
    });

    // 4. Set socket options (before bind/connect)
    socket.setOption(zlink.Option.<OPTION>, value);

    // 5. Establish connection (bind or connect)
    socket.bind("tcp://*:5555");
    // or
    socket.connect("tcp://127.0.0.1:5555");

    // 6. Send messages (receive is handled by callback)
    socket.send(data);

    // 7. Cleanup
    socket.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    // 1. Create Context
    using var ctx = new Context();

    // 2–3. Create Socket with callback (raw STREAM example)
    using var socket = new StreamSocket(ctx);
    socket.RecvHandler((sourceRid, parts) => {
        // process received message
    });

    // 4. Set socket options (before bind/connect)
    socket.SetOption(Option.<OPTION>, value);

    // 5. Establish connection (bind or connect)
    socket.Bind("tcp://*:5555");
    // or
    socket.Connect("tcp://127.0.0.1:5555");

    // 6. Send messages (receive is handled by callback)
    socket.Send(data);
    ```

=== "Rust"

    ```rust
    // 1. Create Context
    let ctx = zlink::Context::new()?;

    // 2–3. Create Socket with callback (raw STREAM example)
    let socket = ctx.stream_socket()?;
    socket.recv_handler(|source_rid, parts| {
        // process received message
    })?;

    // 4. Set socket options (before bind/connect)
    socket.set_option(zlink::Option::<OPTION>, value)?;

    // 5. Establish connection (bind or connect)
    socket.bind("tcp://*:5555")?;
    // or
    socket.connect("tcp://127.0.0.1:5555")?;

    // 6. Send messages (receive is handled by callback)
    socket.send(data)?;

    // 7. Cleanup
    socket.close()?;
    ctx.term()?;
    ```

> The following options must be set **before** `zlink_bind()`/`zlink_connect()`
> as they are used during handshake or connection:
> `zlink_set_routing_id()`, `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` (via `zlink_set_router_option()`), `ZLINK_ROUTER_OPT_PROBE` (via `zlink_set_router_option()`), `zlink_set_tls_server()` / `zlink_set_tls_client()`.
> Other options (`ZLINK_OPT_SNDHWM`, `ZLINK_OPT_RCVHWM`, `ZLINK_OPT_LINGER`, `ZLINK_OPT_SNDTIMEO`, etc.) can be changed after bind/connect.

> The example above is the raw `STREAM` callback form. Other raw socket
> families use recv/poller as the canonical model. For a comparison of the
> two modes, see [Core API](02-core-api.md) section 3.2.

> **Callback mode constraints:** After installing `zlink_recv_handler()`,
> `zlink_recv()` returns `EBUSY` (irreversible transition). The following
> recv-related options become ineffective:
>
> | Option | Reason |
> |--------|--------|
> | `ZLINK_OPT_RCVTIMEO` | No `recv()` calls, so timeout has no effect |
> | `ZLINK_RCVMORE` (removed) | Complete multipart delivered atomically as `parts[]` |
>
> `ZLINK_OPT_RCVHWM` remains effective in callback mode (applies to the I/O thread's internal queue).

---
[← Core API](02-core-api.md) | [PAIR →](03-1-pair.md)
