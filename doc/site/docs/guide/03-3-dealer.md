# DEALER Socket

## 1. Overview

The DEALER socket is an asynchronous request socket. It sends to multiple peers using **round-robin** distribution and receives using **fair-queue**. There is no enforced send/recv ordering, enabling free asynchronous messaging.

**Key characteristics:**
- Send: Round-robin (`lb_t`) -- cyclic distribution across connected peers
- Receive: Fair-queue (`fq_t`) -- fair reception from all peers
- No enforced send/recv ordering (asynchronous)

**Valid socket combinations:** DEALER ↔ ROUTER, DEALER ↔ DEALER

```
┌──────────┐                ┌────────┐
│ DEALER 1 │────────────────►│        │
└──────────┘  Round-robin   │ ROUTER │
┌──────────┐                │        │
│ DEALER 2 │────────────────►│        │
└──────────┘                └────────┘
```

## 2. Basic Usage

### Creation and Connection

=== "C"

    ```c
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);

    /* Set routing_id (optional, used for identification by ROUTER) */
    zlink_set_routing_id(dealer, "client-1", 8);

    /* Connect to server */
    zlink_connect(dealer, "tcp://127.0.0.1:5558");
    ```

=== "C++"

    ```cpp
    zlink::dealer_socket_t dealer(ctx);

    // Set routing_id (optional, used for identification by ROUTER)
    dealer.set_routing_id("client-1");

    // Connect to server
    dealer.connect("tcp://127.0.0.1:5558");
    ```

=== "Java"

    ```java
    DealerSocket dealer = new DealerSocket(ctx);

    // Set routing_id (optional, used for identification by ROUTER)
    dealer.setRoutingId("client-1");

    // Connect to server
    dealer.connect("tcp://127.0.0.1:5558");
    ```

=== "Python"

    ```python
    dealer = zlink.DealerSocket(ctx)

    # Set routing_id (optional, used for identification by ROUTER)
    dealer.set_routing_id("client-1")

    # Connect to server
    dealer.connect("tcp://127.0.0.1:5558")
    ```

=== "Node/TypeScript"

    ```typescript
    const dealer = new zlink.DealerSocket(ctx);

    // Set routing_id (optional, used for identification by ROUTER)
    dealer.setRoutingId("client-1");

    // Connect to server
    dealer.connect("tcp://127.0.0.1:5558");
    ```

=== "C#/.NET"

    ```csharp
    var dealer = new DealerSocket(ctx);

    // Set routing_id (optional, used for identification by ROUTER)
    dealer.SetRoutingId("client-1");

    // Connect to server
    dealer.Connect("tcp://127.0.0.1:5558");
    ```

=== "Rust"

    ```rust
    let dealer = ctx.dealer_socket();

    // Set routing_id (optional, used for identification by ROUTER)
    dealer.set_routing_id("client-1");

    // Connect to server
    dealer.connect("tcp://127.0.0.1:5558");
    ```

### Sending and Receiving Messages

=== "C"

    ```c
    /* Send requests -- can send consecutively without ordering constraints */
    zlink_msg_t msg1, msg2, msg3;
    zlink_msg_init_size(&msg1, 9);
    memcpy(zlink_msg_data(&msg1), "request-1", 9);
    zlink_send(dealer, &msg1, 1, 0);

    zlink_msg_init_size(&msg2, 9);
    memcpy(zlink_msg_data(&msg2), "request-2", 9);
    zlink_send(dealer, &msg2, 1, 0);

    zlink_msg_init_size(&msg3, 9);
    memcpy(zlink_msg_data(&msg3), "request-3", 9);
    zlink_send(dealer, &msg3, 1, 0);

    /* Responses are dispatched to the handler callback registered at creation */
    ```

=== "C++"

    ```cpp
    // Send requests -- can send consecutively without ordering constraints
    dealer.send("request-1");
    dealer.send("request-2");
    dealer.send("request-3");

    // Responses are dispatched to the handler callback registered at creation
    ```

=== "Java"

    ```java
    // Send requests -- can send consecutively without ordering constraints
    dealer.send("request-1");
    dealer.send("request-2");
    dealer.send("request-3");

    // Responses are dispatched to the handler callback registered at creation
    ```

=== "Python"

    ```python
    # Send requests -- can send consecutively without ordering constraints
    dealer.send(b"request-1")
    dealer.send(b"request-2")
    dealer.send(b"request-3")

    # Responses are dispatched to the handler callback registered at creation
    ```

=== "Node/TypeScript"

    ```typescript
    // Send requests -- can send consecutively without ordering constraints
    dealer.send(Buffer.from("request-1"));
    dealer.send(Buffer.from("request-2"));
    dealer.send(Buffer.from("request-3"));

    // Responses are dispatched to the handler callback registered at creation
    ```

=== "C#/.NET"

    ```csharp
    // Send requests -- can send consecutively without ordering constraints
    dealer.Send("request-1");
    dealer.Send("request-2");
    dealer.Send("request-3");

    // Responses are dispatched to the handler callback registered at creation
    ```

=== "Rust"

    ```rust
    // Send requests -- can send consecutively without ordering constraints
    dealer.send(b"request-1");
    dealer.send(b"request-2");
    dealer.send(b"request-3");

    // Responses are dispatched to the handler callback registered at creation
    ```

### Receive Modes

Use `zlink_recv()` to receive synchronously.

=== "C"

    ```c
    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = zlink_recv(dealer, &source_rid, &parts, &part_count, 0);
    if (rc == 0) {
        /* process parts[0..part_count-1] */
        zlink_multipart_close(parts, part_count);
        free(parts);
    }
    ```

=== "C++"

    ```cpp
    auto [source_rid, parts] = dealer.recv();
    // process parts[0..N-1]
    ```

=== "Java"

    ```java
    Message msg = dealer.recv();
    // process msg.parts()
    ```

=== "Python"

    ```python
    source_rid, parts = dealer.recv()
    # process parts[0..N-1]
    ```

=== "Node/TypeScript"

    ```typescript
    const [sourceRid, parts] = dealer.receive();
    // process parts[0..N-1]
    ```

=== "C#/.NET"

    ```csharp
    var (sourceRid, parts) = dealer.Receive();
    // process parts[0..N-1]
    ```

=== "Rust"

    ```rust
    let (source_rid, parts) = dealer.recv();
    // process parts[0..N-1]
    ```

> When HWM is reached, `zlink_send()` blocks (default) or returns
> `EAGAIN` with `ZLINK_DONTWAIT`. For advanced backpressure patterns,
> see [Performance Guide](10-performance.md).

## 3. Usage Example

=== "C"

    ```c
    /* DEALER → ROUTER send */
    zlink_msg_t parts[2];
    zlink_msg_init_size(&parts[0], 6);
    memcpy(zlink_msg_data(&parts[0]), "header", 6);
    zlink_msg_init_size(&parts[1], 4);
    memcpy(zlink_msg_data(&parts[1]), "body", 4);
    zlink_send(dealer, parts, 2, 0);
    ```

=== "C++"

    ```cpp
    // DEALER → ROUTER send
    dealer.send({"header", "body"});
    ```

=== "Java"

    ```java
    // DEALER → ROUTER send
    dealer.send("header", "body");
    ```

=== "Python"

    ```python
    # DEALER → ROUTER send
    dealer.send([b"header", b"body"])
    ```

=== "Node/TypeScript"

    ```typescript
    // DEALER → ROUTER send
    dealer.send([Buffer.from("header"), Buffer.from("body")]);
    ```

=== "C#/.NET"

    ```csharp
    // DEALER → ROUTER send
    dealer.Send("header", "body");
    ```

=== "Rust"

    ```rust
    // DEALER → ROUTER send
    dealer.send(&[b"header", b"body"]);
    ```

## 4. Socket Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `zlink_set_routing_id()` | binary | Auto (UUID) | ID for identification by ROUTER (dedicated function) |
| `ZLINK_PROBE_ROUTER` | int | 0 | Send empty message on connect (connection notification) |
| `ZLINK_OPT_SNDHWM` | int | 1000 | Maximum number of messages in the send queue |
| `ZLINK_OPT_RCVHWM` | int | 1000 | Maximum number of messages in the receive queue |
| `ZLINK_OPT_LINGER` | int | -1 | Wait time on close (ms) |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | Send timeout (ms) |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | Receive timeout (ms) |
| `ZLINK_CONNECT_ROUTING_ID` | binary | -- | Alias applied to the next connect |

### Setting routing_id

To allow ROUTER to identify a DEALER, explicitly set the routing_id.

=== "C"

    ```c
    /* Set before bind/connect */
    zlink_set_routing_id(dealer, "D1", 2);
    zlink_connect(dealer, "tcp://127.0.0.1:5558");
    ```

=== "C++"

    ```cpp
    // Set before bind/connect
    dealer.set_routing_id("D1");
    dealer.connect("tcp://127.0.0.1:5558");
    ```

=== "Java"

    ```java
    // Set before bind/connect
    dealer.setRoutingId("D1");
    dealer.connect("tcp://127.0.0.1:5558");
    ```

=== "Python"

    ```python
    # Set before bind/connect
    dealer.set_routing_id("D1")
    dealer.connect("tcp://127.0.0.1:5558")
    ```

=== "Node/TypeScript"

    ```typescript
    // Set before bind/connect
    dealer.setRoutingId("D1");
    dealer.connect("tcp://127.0.0.1:5558");
    ```

=== "C#/.NET"

    ```csharp
    // Set before bind/connect
    dealer.SetRoutingId("D1");
    dealer.Connect("tcp://127.0.0.1:5558");
    ```

=== "Rust"

    ```rust
    // Set before bind/connect
    dealer.set_routing_id("D1");
    dealer.connect("tcp://127.0.0.1:5558");
    ```

> Reference: `core/tests/test_router_multiple_dealers.cpp` -- `zlink_set_routing_id(dealer1, "D1", 2)`

## 5. Usage Patterns

### Pattern 1: DEALER → ROUTER Request-Reply

The most basic pattern. DEALER sends requests, ROUTER replies.

!!! note "C API Callback Signature"
    The request handler uses C-specific types (`zlink_routing_id_t`,
    `zlink_msg_t`). Each binding provides its own idiomatic callback or
    receive interface.

    ```c
    /* Server: ROUTER with handler */
    void on_request(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        /* source_rid contains the DEALER's routing_id */
        printf("Received from [%.*s]: %.*s\n",
               (int)source_rid->size, source_rid->data,
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));

        /* Reply: send to the source peer using zlink_send_rid */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 5);
        memcpy(zlink_msg_data(&reply), "World", 5);
        zlink_send_rid(router, source_rid, &reply, 1, 0);

        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C"

    ```c
    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    /* Receive with zlink_recv() */
    zlink_bind(router, "tcp://*:5558");

    /* Client: DEALER */
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);
    /* Receive replies with zlink_recv() */
    zlink_set_routing_id(dealer, "D1", 2);
    zlink_connect(dealer, "tcp://127.0.0.1:5558");

    /* Client request */
    zlink_msg_t req;
    zlink_msg_init_size(&req, 5);
    memcpy(zlink_msg_data(&req), "Hello", 5);
    zlink_send(dealer, &req, 1, 0);

    /* on_request receives the message, replies with "World"
       on_reply receives the reply */
    ```

=== "C++"

    ```cpp
    zlink::router_socket_t router(ctx);
    router.bind("tcp://*:5558");

    // Client: DEALER
    zlink::dealer_socket_t dealer(ctx);
    dealer.set_routing_id("D1");
    dealer.connect("tcp://127.0.0.1:5558");

    // Client request
    dealer.send("Hello");

    // Router receives and replies
    auto [source_rid, parts] = router.recv();
    router.send_rid(source_rid, "World");
    ```

=== "Java"

    ```java
    RouterSocket router = new RouterSocket(ctx);
    router.bind("tcp://*:5558");

    // Client: DEALER
    DealerSocket dealer = new DealerSocket(ctx);
    dealer.setRoutingId("D1");
    dealer.connect("tcp://127.0.0.1:5558");

    // Client request
    dealer.send("Hello");

    // Router receives and replies
    Message msg = router.recv();
    router.sendRid(msg.routingId(), "World");
    ```

=== "Python"

    ```python
    router = zlink.RouterSocket(ctx)
    router.bind("tcp://*:5558")

    # Client: DEALER
    dealer = zlink.DealerSocket(ctx)
    dealer.set_routing_id("D1")
    dealer.connect("tcp://127.0.0.1:5558")

    # Client request
    dealer.send(b"Hello")

    # Router receives and replies
    source_rid, parts = router.recv()
    router.send_rid(source_rid, b"World")
    ```

=== "Node/TypeScript"

    ```typescript
    const router = new zlink.RouterSocket(ctx);
    router.bind("tcp://*:5558");

    // Client: DEALER
    const dealer = new zlink.DealerSocket(ctx);
    dealer.setRoutingId("D1");
    dealer.connect("tcp://127.0.0.1:5558");

    // Client request
    dealer.send(Buffer.from("Hello"));

    // Router receives and replies
    const [sourceRid, parts] = router.receive();
    router.sendRid(sourceRid, Buffer.from("World"));
    ```

=== "C#/.NET"

    ```csharp
    var router = new RouterSocket(ctx);
    router.Bind("tcp://*:5558");

    // Client: DEALER
    var dealer = new DealerSocket(ctx);
    dealer.SetRoutingId("D1");
    dealer.Connect("tcp://127.0.0.1:5558");

    // Client request
    dealer.Send("Hello");

    // Router receives and replies
    var (sourceRid, parts) = router.Receive();
    router.SendRid(sourceRid, "World");
    ```

=== "Rust"

    ```rust
    let router = ctx.router_socket();
    router.bind("tcp://*:5558");

    // Client: DEALER
    let dealer = ctx.dealer_socket();
    dealer.set_routing_id("D1");
    dealer.connect("tcp://127.0.0.1:5558");

    // Client request
    dealer.send(b"Hello");

    // Router receives and replies
    let (source_rid, parts) = router.recv();
    router.send_rid(&source_rid, b"World");
    ```

> Reference: `core/tests/test_router_multiple_dealers.cpp` -- TCP/IPC/inproc examples

### Pattern 2: Multiple DEALER Load Balancing

Multiple DEALERs connect to a single ROUTER. ROUTER distinguishes each DEALER by routing_id.

=== "C"

    ```c
    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    /* ROUTER receives with zlink_recv() and distinguishes each DEALER by source_rid */
    zlink_bind(router, "tcp://127.0.0.1:*");

    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(router, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

    void *dealer1 = zlink_socket(ctx, ZLINK_DEALER);
    zlink_set_routing_id(dealer1, "D1", 2);
    zlink_connect(dealer1, endpoint);

    void *dealer2 = zlink_socket(ctx, ZLINK_DEALER);
    zlink_set_routing_id(dealer2, "D2", 2);
    zlink_connect(dealer2, endpoint);

    /* Each DEALER sends a message */
    zlink_msg_t m1;
    zlink_msg_init_size(&m1, 12);
    memcpy(zlink_msg_data(&m1), "from_dealer1", 12);
    zlink_send(dealer1, &m1, 1, 0);

    zlink_msg_t m2;
    zlink_msg_init_size(&m2, 12);
    memcpy(zlink_msg_data(&m2), "from_dealer2", 12);
    zlink_send(dealer2, &m2, 1, 0);

    /* on_message receives each DEALER's message with its routing_id */
    ```

=== "C++"

    ```cpp
    zlink::router_socket_t router(ctx);
    router.bind("tcp://127.0.0.1:*");

    auto endpoint = router.get_option<std::string>(ZLINK_OPT_LAST_ENDPOINT);

    zlink::dealer_socket_t dealer1(ctx);
    dealer1.set_routing_id("D1");
    dealer1.connect(endpoint);

    zlink::dealer_socket_t dealer2(ctx);
    dealer2.set_routing_id("D2");
    dealer2.connect(endpoint);

    // Each DEALER sends a message
    dealer1.send("from_dealer1");
    dealer2.send("from_dealer2");

    // Router receives each DEALER's message with its routing_id
    ```

=== "Java"

    ```java
    RouterSocket router = new RouterSocket(ctx);
    router.bind("tcp://127.0.0.1:*");

    String endpoint = router.getOption(ZLINK_OPT_LAST_ENDPOINT);

    DealerSocket dealer1 = new DealerSocket(ctx);
    dealer1.setRoutingId("D1");
    dealer1.connect(endpoint);

    DealerSocket dealer2 = new DealerSocket(ctx);
    dealer2.setRoutingId("D2");
    dealer2.connect(endpoint);

    // Each DEALER sends a message
    dealer1.send("from_dealer1");
    dealer2.send("from_dealer2");

    // Router receives each DEALER's message with its routing_id
    ```

=== "Python"

    ```python
    router = zlink.RouterSocket(ctx)
    router.bind("tcp://127.0.0.1:*")

    endpoint = router.get_option(ZLINK_OPT_LAST_ENDPOINT)

    dealer1 = zlink.DealerSocket(ctx)
    dealer1.set_routing_id("D1")
    dealer1.connect(endpoint)

    dealer2 = zlink.DealerSocket(ctx)
    dealer2.set_routing_id("D2")
    dealer2.connect(endpoint)

    # Each DEALER sends a message
    dealer1.send(b"from_dealer1")
    dealer2.send(b"from_dealer2")

    # Router receives each DEALER's message with its routing_id
    ```

=== "Node/TypeScript"

    ```typescript
    const router = new zlink.RouterSocket(ctx);
    router.bind("tcp://127.0.0.1:*");

    const endpoint = router.getOption(ZLINK_OPT_LAST_ENDPOINT);

    const dealer1 = new zlink.DealerSocket(ctx);
    dealer1.setRoutingId("D1");
    dealer1.connect(endpoint);

    const dealer2 = new zlink.DealerSocket(ctx);
    dealer2.setRoutingId("D2");
    dealer2.connect(endpoint);

    // Each DEALER sends a message
    dealer1.send(Buffer.from("from_dealer1"));
    dealer2.send(Buffer.from("from_dealer2"));

    // Router receives each DEALER's message with its routing_id
    ```

=== "C#/.NET"

    ```csharp
    var router = new RouterSocket(ctx);
    router.Bind("tcp://127.0.0.1:*");

    var endpoint = router.GetOption(ZLINK_OPT_LAST_ENDPOINT);

    var dealer1 = new DealerSocket(ctx);
    dealer1.SetRoutingId("D1");
    dealer1.Connect(endpoint);

    var dealer2 = new DealerSocket(ctx);
    dealer2.SetRoutingId("D2");
    dealer2.Connect(endpoint);

    // Each DEALER sends a message
    dealer1.Send("from_dealer1");
    dealer2.Send("from_dealer2");

    // Router receives each DEALER's message with its routing_id
    ```

=== "Rust"

    ```rust
    let router = ctx.router_socket();
    router.bind("tcp://127.0.0.1:*");

    let endpoint = router.get_option::<String>(ZLINK_OPT_LAST_ENDPOINT);

    let dealer1 = ctx.dealer_socket();
    dealer1.set_routing_id("D1");
    dealer1.connect(&endpoint);

    let dealer2 = ctx.dealer_socket();
    dealer2.set_routing_id("D2");
    dealer2.connect(&endpoint);

    // Each DEALER sends a message
    dealer1.send(b"from_dealer1");
    dealer2.send(b"from_dealer2");

    // Router receives each DEALER's message with its routing_id
    ```

> Reference: `core/tests/test_router_multiple_dealers.cpp` -- `test_router_multiple_dealers_tcp()`

### Pattern 3: Proxy Pattern (ROUTER-DEALER)

Build a multi-threaded server using ROUTER (frontend) + DEALER (backend).

=== "C"

    ```c
    /* Frontend: clients connect here */
    void *frontend = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_bind(frontend, "tcp://*:5558");

    /* Backend: worker threads connect here */
    void *backend = zlink_socket(ctx, ZLINK_DEALER);
    zlink_bind(backend, "inproc://backend");

    /* Start worker threads then run proxy */
    zlink_proxy(frontend, backend, NULL);
    ```

=== "C++"

    ```cpp
    // Frontend: clients connect here
    zlink::router_socket_t frontend(ctx);
    frontend.bind("tcp://*:5558");

    // Backend: worker threads connect here
    zlink::dealer_socket_t backend(ctx);
    backend.bind("inproc://backend");

    // Start worker threads then run proxy
    zlink::proxy(frontend, backend);
    ```

=== "Java"

    ```java
    // Frontend: clients connect here
    RouterSocket frontend = new RouterSocket(ctx);
    frontend.bind("tcp://*:5558");

    // Backend: worker threads connect here
    DealerSocket backend = new DealerSocket(ctx);
    backend.bind("inproc://backend");

    // Start worker threads then run proxy
    Proxy.run(frontend, backend);
    ```

=== "Python"

    ```python
    # Frontend: clients connect here
    frontend = zlink.RouterSocket(ctx)
    frontend.bind("tcp://*:5558")

    # Backend: worker threads connect here
    backend = zlink.DealerSocket(ctx)
    backend.bind("inproc://backend")

    # Start worker threads then run proxy
    zlink.proxy(frontend, backend)
    ```

=== "Node/TypeScript"

    ```typescript
    // Frontend: clients connect here
    const frontend = new zlink.RouterSocket(ctx);
    frontend.bind("tcp://*:5558");

    // Backend: worker threads connect here
    const backend = new zlink.DealerSocket(ctx);
    backend.bind("inproc://backend");

    // Start worker threads then run proxy
    zlink.proxy(frontend, backend);
    ```

=== "C#/.NET"

    ```csharp
    // Frontend: clients connect here
    var frontend = new RouterSocket(ctx);
    frontend.Bind("tcp://*:5558");

    // Backend: worker threads connect here
    var backend = new DealerSocket(ctx);
    backend.Bind("inproc://backend");

    // Start worker threads then run proxy
    Proxy.Run(frontend, backend);
    ```

=== "Rust"

    ```rust
    // Frontend: clients connect here
    let frontend = ctx.router_socket();
    frontend.bind("tcp://*:5558");

    // Backend: worker threads connect here
    let backend = ctx.dealer_socket();
    backend.bind("inproc://backend");

    // Start worker threads then run proxy
    zlink::proxy(&frontend, &backend);
    ```

!!! note "C API Worker Thread Callback"
    The worker thread uses C-specific callback types. Each binding
    provides its own idiomatic worker pattern.

    ```c
    /* Worker thread */
    void worker_thread(void *arg) {
        void on_work(const zlink_routing_id_t *source_rid,
                     zlink_msg_t *parts, size_t part_count,
                     void *userdata)
        {
            /* Process and reply with the same routing_id */
            zlink_send_rid(worker, source_rid, parts, part_count, 0);
        }

        void *worker = zlink_socket(ctx, ZLINK_DEALER);
        /* Receive work with zlink_recv() */
        zlink_connect(worker, "inproc://backend");

        /* Worker stays alive until socket is closed */
    }
    ```

> Reference: `core/tests/test_proxy.cpp` -- ROUTER(frontend) + DEALER(backend) + worker pool

### Pattern 4: DEALER ↔ DEALER Asynchronous Communication

Both sides use DEALER for fully asynchronous P2P communication.

=== "C"

    ```c
    void *a = zlink_socket(ctx, ZLINK_DEALER);
    /* Receive with zlink_recv() */
    zlink_bind(a, "tcp://*:5558");

    void *b = zlink_socket(ctx, ZLINK_DEALER);
    /* Receive with zlink_recv() */
    zlink_connect(b, "tcp://127.0.0.1:5558");

    /* Bidirectional free send */
    zlink_msg_t ping;
    zlink_msg_init_size(&ping, 4);
    memcpy(zlink_msg_data(&ping), "ping", 4);
    zlink_send(a, &ping, 1, 0);

    zlink_msg_t pong;
    zlink_msg_init_size(&pong, 4);
    memcpy(zlink_msg_data(&pong), "pong", 4);
    zlink_send(b, &pong, 1, 0);

    /* on_message_b receives "ping", on_message_a receives "pong" */
    ```

=== "C++"

    ```cpp
    zlink::dealer_socket_t a(ctx);
    a.bind("tcp://*:5558");

    zlink::dealer_socket_t b(ctx);
    b.connect("tcp://127.0.0.1:5558");

    // Bidirectional free send
    a.send("ping");
    b.send("pong");
    ```

=== "Java"

    ```java
    DealerSocket a = new DealerSocket(ctx);
    a.bind("tcp://*:5558");

    DealerSocket b = new DealerSocket(ctx);
    b.connect("tcp://127.0.0.1:5558");

    // Bidirectional free send
    a.send("ping");
    b.send("pong");
    ```

=== "Python"

    ```python
    a = zlink.DealerSocket(ctx)
    a.bind("tcp://*:5558")

    b = zlink.DealerSocket(ctx)
    b.connect("tcp://127.0.0.1:5558")

    # Bidirectional free send
    a.send(b"ping")
    b.send(b"pong")
    ```

=== "Node/TypeScript"

    ```typescript
    const a = new zlink.DealerSocket(ctx);
    a.bind("tcp://*:5558");

    const b = new zlink.DealerSocket(ctx);
    b.connect("tcp://127.0.0.1:5558");

    // Bidirectional free send
    a.send(Buffer.from("ping"));
    b.send(Buffer.from("pong"));
    ```

=== "C#/.NET"

    ```csharp
    var a = new DealerSocket(ctx);
    a.Bind("tcp://*:5558");

    var b = new DealerSocket(ctx);
    b.Connect("tcp://127.0.0.1:5558");

    // Bidirectional free send
    a.Send("ping");
    b.Send("pong");
    ```

=== "Rust"

    ```rust
    let a = ctx.dealer_socket();
    a.bind("tcp://*:5558");

    let b = ctx.dealer_socket();
    b.connect("tcp://127.0.0.1:5558");

    // Bidirectional free send
    a.send(b"ping");
    b.send(b"pong");
    ```

## 6. Caveats

### Queuing When No Peer Is Connected

If no peer is connected, messages accumulate in the send queue. When the HWM is exceeded, the call blocks (default) or returns `EAGAIN` (`ZLINK_DONTWAIT`).

=== "C"

    ```c
    /* Send with no peer connected */
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 4);
    memcpy(zlink_msg_data(&msg), "data", 4);
    int rc = zlink_send(dealer, &msg, 1, ZLINK_DONTWAIT);
    if (rc == -1 && errno == EAGAIN) {
        /* HWM exceeded or no peer connected */
    }
    ```

=== "C++"

    ```cpp
    // Send with no peer connected
    try {
        dealer.send("data", zlink::dontwait);
    } catch (const zlink::eagain_error& e) {
        // HWM exceeded or no peer connected
    }
    ```

=== "Java"

    ```java
    // Send with no peer connected
    try {
        dealer.send("data", SendFlags.DONTWAIT);
    } catch (EagainException e) {
        // HWM exceeded or no peer connected
    }
    ```

=== "Python"

    ```python
    # Send with no peer connected
    try:
        dealer.send(b"data", flags=zlink.DONTWAIT)
    except zlink.Again:
        # HWM exceeded or no peer connected
        pass
    ```

=== "Node/TypeScript"

    ```typescript
    // Send with no peer connected
    try {
        dealer.send(Buffer.from("data"), { dontwait: true });
    } catch (e) {
        // HWM exceeded or no peer connected
    }
    ```

=== "C#/.NET"

    ```csharp
    // Send with no peer connected
    try {
        dealer.Send("data", SendFlags.DontWait);
    } catch (EagainException) {
        // HWM exceeded or no peer connected
    }
    ```

=== "Rust"

    ```rust
    // Send with no peer connected
    match dealer.send_dontwait(b"data") {
        Err(ZlinkError::Eagain) => {
            // HWM exceeded or no peer connected
        }
        _ => {}
    }
    ```

### Round-Robin Distribution

When multiple peers are connected, messages are distributed in a round-robin fashion. To send to a specific peer, use ROUTER instead.

### Set routing_id Before connect

`zlink_set_routing_id()` must be called before `zlink_connect()`. Changes after connection are not applied.

=== "C"

    ```c
    /* Correct order */
    zlink_set_routing_id(dealer, "D1", 2);
    zlink_connect(dealer, endpoint);  /* identified as D1 */
    ```

=== "C++"

    ```cpp
    // Correct order
    dealer.set_routing_id("D1");
    dealer.connect(endpoint);  // identified as D1
    ```

=== "Java"

    ```java
    // Correct order
    dealer.setRoutingId("D1");
    dealer.connect(endpoint);  // identified as D1
    ```

=== "Python"

    ```python
    # Correct order
    dealer.set_routing_id("D1")
    dealer.connect(endpoint)  # identified as D1
    ```

=== "Node/TypeScript"

    ```typescript
    // Correct order
    dealer.setRoutingId("D1");
    dealer.connect(endpoint);  // identified as D1
    ```

=== "C#/.NET"

    ```csharp
    // Correct order
    dealer.SetRoutingId("D1");
    dealer.Connect(endpoint);  // identified as D1
    ```

=== "Rust"

    ```rust
    // Correct order
    dealer.set_routing_id("D1");
    dealer.connect(endpoint);  // identified as D1
    ```

---
[← PUB/SUB](03-2-pubsub.md) | [ROUTER →](03-4-router.md)
