# Routing ID Concepts and Usage

## 1. Overview

A Routing ID is binary data that identifies sockets and connections in zlink. It is used for message routing in ROUTER sockets, for identifying external clients in STREAM sockets, and for identifying peers in monitoring.

## 2. zlink_routing_id_t

!!! note "C API struct -- each binding wraps this into its idiomatic routing ID type."

    ```c
    typedef struct {
        uint8_t size;       /* 0~255 */
        uint8_t data[255];
    } zlink_routing_id_t;
    ```

## 3. Auto-Generation Rules

| Type | Format | Size | Description |
|------|--------|------|-------------|
| Socket own routing_id | UUID (binary) | 16B | Auto-generated for all sockets |
| STREAM peer routing_id | uint32 | 4B | Auto-assigned per connection |

- If the user does not call `zlink_set_routing_id()`, it is auto-generated
- Uniqueness is guaranteed based on a process-wide global counter

### own vs peer — Differences Users Should Know

| | own routing_id | peer routing_id |
|---|---|---|
| **Creation time** | At socket creation | At peer connection |
| **Size** | 16B (UUID) | Variable (ROUTER), 4B (STREAM) |
| **Usage** | Sent during handshake | Automatically prepended to received messages |
| **Configuration** | `zlink_set_routing_id()` | Uses value set by the peer |

The own routing_id is automatically assigned a UUID when the socket is created and is sent to the peer during the handshake. The peer routing_id is the own routing_id sent by the peer and is automatically prepended as the first frame of received messages in ROUTER/STREAM sockets.

## 4. User-Defined routing_id

### Setting Socket Identity

=== "C"

    ```c
    /* Set before bind/connect */
    const char *id = "router-A";
    zlink_set_routing_id(socket, id, strlen(id));
    ```

=== "C++"

    ```cpp
    /* Set before bind/connect */
    zlink::routing_id_t rid("router-A");
    dealer.set_routing_id(rid);
    ```

=== "Java"

    ```java
    /* Set before bind/connect */
    var rid = RoutingId.copyOf("router-A".getBytes());
    dealer.setRoutingId(rid);
    ```

=== "Python"

    ```python
    # Set before bind/connect
    socket.set_routing_id(b"router-A")
    ```

=== "Node/TypeScript"

    ```typescript
    // Set before bind/connect
    socket.setRoutingId(Buffer.from("router-A"));
    ```

=== "C#/.NET"

    ```csharp
    // Set before bind/connect
    dealer.DealerOptions.RoutingId = new RoutingId("router-A");
    ```

=== "Rust"

    ```rust
    // Set before bind/connect
    let rid = RoutingId::new(b"router-A")?;
    dealer.set_routing_id(&rid)?;
    ```

=== "Go"

    ```go
    // Set before bind/connect
    rid, _ := zlink.NewRoutingID([]byte("router-A"))
    dealer.SetRoutingID(rid)
    ```

Notes:
- Must be set **before** `zlink_bind()` or `zlink_connect()`
- Cannot be changed after connection
- Empty string ("") is not allowed
- A conflict occurs if two peers with the same routing_id connect to the same ROUTER

### Considerations for User-Defined routing_id

=== "C"

    ```c
    /* Good example: meaningful identifiers */
    zlink_set_routing_id(dealer, "worker-01", 9);
    zlink_set_routing_id(dealer, "D1", 2);

    /* Caution: potential collision with auto-generated routing_ids */
    /* Avoid UUID format (16B binary) */
    ```

=== "C++"

    ```cpp
    /* Good example: meaningful identifiers */
    dealer.set_routing_id(zlink::routing_id_t("worker-01"));
    dealer.set_routing_id(zlink::routing_id_t("D1"));

    /* Caution: potential collision with auto-generated routing_ids */
    /* Avoid UUID format (16B binary) */
    ```

=== "Java"

    ```java
    /* Good example: meaningful identifiers */
    dealer.setRoutingId(RoutingId.copyOf("worker-01".getBytes()));
    dealer.setRoutingId(RoutingId.copyOf("D1".getBytes()));

    /* Caution: potential collision with auto-generated routing_ids */
    /* Avoid UUID format (16B binary) */
    ```

=== "Python"

    ```python
    # Good example: meaningful identifiers
    dealer.set_routing_id(b"worker-01")
    dealer.set_routing_id(b"D1")

    # Caution: potential collision with auto-generated routing_ids
    # Avoid UUID format (16B binary)
    ```

=== "Node/TypeScript"

    ```typescript
    // Good example: meaningful identifiers
    dealer.setRoutingId(Buffer.from("worker-01"));
    dealer.setRoutingId(Buffer.from("D1"));

    // Caution: potential collision with auto-generated routing_ids
    // Avoid UUID format (16B binary)
    ```

=== "C#/.NET"

    ```csharp
    // Good example: meaningful identifiers
    dealer.DealerOptions.RoutingId = new RoutingId("worker-01");
    dealer.DealerOptions.RoutingId = new RoutingId("D1");

    // Caution: potential collision with auto-generated routing_ids
    // Avoid UUID format (16B binary)
    ```

=== "Rust"

    ```rust
    // Good example: meaningful identifiers
    dealer.set_routing_id(&RoutingId::new(b"worker-01")?)?;
    dealer.set_routing_id(&RoutingId::new(b"D1")?)?;

    // Caution: potential collision with auto-generated routing_ids
    // Avoid UUID format (16B binary)
    ```

=== "Go"

    ```go
    // Good example: meaningful identifiers
    rid, _ := zlink.NewRoutingID([]byte("worker-01"))
    dealer.SetRoutingID(rid)
    rid2, _ := zlink.NewRoutingID([]byte("D1"))
    dealer.SetRoutingID(rid2)

    // Caution: potential collision with auto-generated routing_ids
    // Avoid UUID format (16B binary)
    ```

> Reference: `core/tests/test_router_multiple_dealers.cpp` — `zlink_set_routing_id(dealer1, "D1", 2)`

### Querying

=== "C"

    ```c
    zlink_routing_id_t rid;
    zlink_get_routing_id(socket, &rid);

    printf("routing_id (%u bytes): ", rid.size);
    for (size_t i = 0; i < rid.size; ++i)
        printf("%02x", rid.data[i]);
    printf("\n");
    ```

=== "C++"

    ```cpp
    zlink::routing_id_t rid;
    dealer.get_routing_id(rid);

    auto bytes = rid.to_bytes();
    std::print("routing_id ({} bytes): ", bytes.size());
    for (auto b : bytes)
        std::print("{:02x}", b);
    std::println();
    ```

=== "Java"

    ```java
    RoutingId rid = RoutingId.copyOf(dealer.getOption(
        SocketOptions.ROUTING_ID_BYTES));

    byte[] bytes = rid.toByteArray();
    System.out.printf("routing_id (%d bytes): ", bytes.length);
    for (byte b : bytes)
        System.out.printf("%02x", b);
    System.out.println();
    ```

=== "Python"

    ```python
    rid = socket.get_routing_id()

    print(f"routing_id ({len(rid)} bytes): {rid.hex()}")
    ```

=== "Node/TypeScript"

    ```typescript
    const rid = socket.getRoutingId();

    console.log(`routing_id (${rid.length} bytes): ${rid.toString("hex")}`);
    ```

=== "C#/.NET"

    ```csharp
    var rid = dealer.DealerOptions.RoutingId;

    Console.WriteLine($"routing_id: {rid.Value}");
    ```

=== "Rust"

    ```rust
    let rid = dealer.routing_id()?;

    print!("routing_id ({} bytes): ", rid.len());
    for b in rid.data() {
        print!("{:02x}", b);
    }
    println!();
    ```

=== "Go"

    ```go
    rid, _ := dealer.RoutingID()

    fmt.Printf("routing_id (%d bytes): %x\n", len(rid.Bytes()), rid.Bytes())
    ```

## 5. Connection Alias Setting

`ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` is a per-connection alias applied to the next `zlink_connect()` call. It is set via `zlink_set_router_option()` and is used when a ROUTER needs to refer to a specific connection by a meaningful name.

=== "C"

    ```c
    /* Apply alias to the next connect */
    const char *alias = "edge-1";
    zlink_set_router_option(socket, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, alias, strlen(alias));
    zlink_connect(socket, "tcp://server:5555");

    /* Different alias for another connection */
    const char *alias2 = "edge-2";
    zlink_set_router_option(socket, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, alias2, strlen(alias2));
    zlink_connect(socket, "tcp://server2:5556");
    ```

=== "C++"

    ```cpp
    /* Apply alias to the next connect */
    router.set_option(zlink::router_option::connect_routing_id,
                      std::string("edge-1"));
    router.connect("tcp://server:5555");

    /* Different alias for another connection */
    router.set_option(zlink::router_option::connect_routing_id,
                      std::string("edge-2"));
    router.connect("tcp://server2:5556");
    ```

=== "Java"

    ```java
    /* Apply alias to the next connect */
    router.routerOptions().connectRoutingId(
        RoutingId.copyOf("edge-1".getBytes()));
    router.connect("tcp://server:5555");

    /* Different alias for another connection */
    router.routerOptions().connectRoutingId(
        RoutingId.copyOf("edge-2".getBytes()));
    router.connect("tcp://server2:5556");
    ```

=== "Python"

    ```python
    # Apply alias to the next connect
    router.router_options.connect_routing_id = b"edge-1"
    router.connect("tcp://server:5555")

    # Different alias for another connection
    router.router_options.connect_routing_id = b"edge-2"
    router.connect("tcp://server2:5556")
    ```

=== "Node/TypeScript"

    ```typescript
    // Apply alias to the next connect
    router.options.connectRoutingId = Buffer.from("edge-1");
    router.connect("tcp://server:5555");

    // Different alias for another connection
    router.options.connectRoutingId = Buffer.from("edge-2");
    router.connect("tcp://server2:5556");
    ```

=== "C#/.NET"

    ```csharp
    // Apply alias to the next connect
    router.RouterOptions.ConnectRoutingId = new RoutingId("edge-1");
    router.Connect("tcp://server:5555");

    // Different alias for another connection
    router.RouterOptions.ConnectRoutingId = new RoutingId("edge-2");
    router.Connect("tcp://server2:5556");
    ```

=== "Rust"

    ```rust
    // Apply alias to the next connect
    let alias1 = RoutingId::new(b"edge-1")?;
    router.set_connect_routing_id(&alias1)?;
    router.connect("tcp://server:5555")?;

    // Different alias for another connection
    let alias2 = RoutingId::new(b"edge-2")?;
    router.set_connect_routing_id(&alias2)?;
    router.connect("tcp://server2:5556")?;
    ```

=== "Go"

    ```go
    // Apply alias to the next connect
    alias1, _ := zlink.NewRoutingID([]byte("edge-1"))
    router.SetConnectRoutingID(alias1)
    router.Connect("tcp://server:5555")

    // Different alias for another connection
    alias2, _ := zlink.NewRoutingID([]byte("edge-2"))
    router.SetConnectRoutingID(alias2)
    router.Connect("tcp://server2:5556")
    ```

- `zlink_set_routing_id()` applies to the entire socket
- `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` (set via `zlink_set_router_option()`) applies to individual connections
- A single socket can have different aliases for each connection
- `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` is for ROUTER-side connection paths.
- Setting it on `ZLINK_STREAM` returns `EOPNOTSUPP`.

## 6. Using routing_id with ROUTER Sockets

In ROUTER sockets, `zlink_recv()` and recv callbacks return the sender's
routing_id as a **separate parameter** (`source_rid`), not as a message frame.
When replying, pass the same routing_id to `zlink_send_rid()`.

> **Difference from libzmq:** libzmq ROUTER returned routing_id as the
> first frame of `zmq_recv()`. In zlink, routing_id is a separate parameter
> on all socket types.

### Basic Request-Reply

=== "C"

    ```c
    /* ROUTER server (with handler) */
    void on_request(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        /* source_rid = "D1" (2 bytes), parts[0] = "Hello" (5 bytes) */

        /* Reply: use zlink_send_rid for directed send */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 5);
        memcpy(zlink_msg_data(&reply), "World", 5);
        zlink_send_rid(router, source_rid, &reply, 1, 0);

        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }

    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    /* Receive with zlink_recv() */
    zlink_bind(router, "tcp://127.0.0.1:*");

    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(router, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

    /* DEALER client (explicit routing_id) */
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);
    zlink_set_routing_id(dealer, "D1", 2);
    zlink_connect(dealer, endpoint);

    /* DEALER send */
    zlink_msg_t req;
    zlink_msg_init_size(&req, 5);
    memcpy(zlink_msg_data(&req), "Hello", 5);
    zlink_send(dealer, &req, 1, 0);

    /* on_request callback receives the message and replies */
    ```

=== "C++"

    ```cpp
    /* ROUTER server (with handler) */
    zlink::router_socket_t router(ctx);
    router.bind("tcp://127.0.0.1:*");
    std::string endpoint;
    router.get_option(zlink::socket_option::last_endpoint, endpoint);

    auto sender = router.send_handle();
    router.on_receive([sender](const zlink_routing_id_t *source_rid,
                               zlink_msg_t *parts, size_t part_count,
                               void *) {
        /* source_rid = "D1", parts[0] = "Hello" */
        zlink::routing_id_t rid(*source_rid);
        auto reply = zlink::message_t::from_string("World");
        sender.send(rid, reply);
        zlink::detail::close_message_array(parts, part_count);
    }, nullptr);

    /* DEALER client (explicit routing_id) */
    zlink::dealer_socket_t dealer(ctx);
    dealer.set_routing_id(zlink::routing_id_t("D1"));
    dealer.connect(endpoint);

    auto req = zlink::message_t::from_string("Hello");
    dealer.send(req);
    ```

=== "Java"

    ```java
    /* ROUTER server */
    var router = ctx.socket(SocketType.ROUTER);
    router.bind("tcp://127.0.0.1:*");
    String endpoint = router.getOption(SocketOptions.LAST_ENDPOINT);

    router.onReceive((received) -> {
        /* received.routingId() = "D1", received.parts() = ["Hello"] */
        var reply = Message.copyOf("World".getBytes());
        router.send(received.routingId(), reply);
    });

    /* DEALER client (explicit routing_id) */
    var dealer = ctx.socket(SocketType.DEALER);
    dealer.setRoutingId(RoutingId.copyOf("D1".getBytes()));
    dealer.connect(endpoint);

    dealer.send(Message.copyOf("Hello".getBytes()));
    ```

=== "Python"

    ```python
    # ROUTER server (with handler)
    router = ctx.socket(zlink.ROUTER)
    router.bind("tcp://127.0.0.1:*")
    endpoint = router.get_option(zlink.LAST_ENDPOINT)

    def on_request(received):
        # received.routing_id = b"D1", received.parts[0] = b"Hello"
        reply = zlink.Message(b"World")
        router.send(reply, routing_id=received.routing_id)

    router.on_receive(on_request)

    # DEALER client (explicit routing_id)
    dealer = ctx.socket(zlink.DEALER)
    dealer.set_routing_id(b"D1")
    dealer.connect(endpoint)

    dealer.send(zlink.Message(b"Hello"))
    ```

=== "Node/TypeScript"

    ```typescript
    // ROUTER server (with handler)
    const router = new zlink.RouterSocket(ctx);
    router.bind("tcp://127.0.0.1:*");
    const endpoint = router.options.lastEndpoint;

    router.onReceive((routingId, parts) => {
        // routingId = <Buffer "D1">, parts[0] = "Hello"
        router.send(routingId!, Buffer.from("World"));
    });

    // DEALER client (explicit routing_id)
    const dealer = new zlink.DealerSocket(ctx);
    dealer.setRoutingId(Buffer.from("D1"));
    dealer.connect(endpoint);

    dealer.send(Buffer.from("Hello"));
    ```

=== "C#/.NET"

    ```csharp
    // ROUTER server (with handler)
    using var router = new RouterSocket(ctx);
    router.Bind("tcp://127.0.0.1:*");
    var endpoint = router.Options.LastEndpoint;

    router.OnReceive((received) => {
        // received.RoutingId = "D1", received.Parts[0] = "Hello"
        router.Send(received.RoutingId, Message.CopyFrom("World"u8));
    });

    // DEALER client (explicit routing_id)
    using var dealer = new DealerSocket(ctx);
    dealer.DealerOptions.RoutingId = new RoutingId("D1");
    dealer.Connect(endpoint);

    dealer.Send(Message.CopyFrom("Hello"u8));
    ```

=== "Rust"

    ```rust
    // ROUTER server (with handler)
    let mut router = ctx.router_socket()?;
    router.bind("tcp://127.0.0.1:*")?;
    let endpoint = router.last_endpoint()?;

    let sender = router.send_handle();
    router.on_receive(move |received| {
        // received.routing_id() = "D1", received.parts() = ["Hello"]
        let reply = Message::from(b"World" as &[u8]);
        sender.send(received.routing_id(), reply).unwrap();
    })?;

    // DEALER client (explicit routing_id)
    let dealer = ctx.dealer_socket()?;
    dealer.set_routing_id(&RoutingId::new(b"D1")?)?;
    dealer.connect(&endpoint)?;

    dealer.send(Message::from(b"Hello" as &[u8]))?;
    ```

=== "Go"

    ```go
    // ROUTER server (with handler)
    router, _ := ctx.RouterSocket()
    router.Bind("tcp://127.0.0.1:*")
    endpoint, _ := router.LastEndpoint()

    router.OnReceive(func(received *zlink.Received) {
        // received.RoutingID() = "D1", received.Parts() = ["Hello"]
        reply, _ := zlink.NewMessage([]byte("World"))
        router.SendTo(received.RoutingID(), reply)
    })

    // DEALER client (explicit routing_id)
    dealer, _ := ctx.DealerSocket()
    rid, _ := zlink.NewRoutingID([]byte("D1"))
    dealer.SetRoutingID(rid)
    dealer.Connect(endpoint)

    msg, _ := zlink.NewMessage([]byte("Hello"))
    dealer.Send(msg)
    ```

### Distinguishing Multiple Clients

=== "C"

    ```c
    /* DEALER 1: routing_id = "D1" */
    zlink_set_routing_id(dealer1, "D1", 2);
    zlink_connect(dealer1, endpoint);

    /* DEALER 2: routing_id = "D2" */
    zlink_set_routing_id(dealer2, "D2", 2);
    zlink_connect(dealer2, endpoint);

    /* ROUTER handler distinguishes clients by source_rid */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        /* source_rid->data contains "D1" or "D2" */
        /* Reply to specific client */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 5);
        memcpy(zlink_msg_data(&reply), "reply", 5);
        zlink_send_rid(router, source_rid, &reply, 1, 0);
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    /* DEALER 1: routing_id = "D1" */
    dealer1.set_routing_id(zlink::routing_id_t("D1"));
    dealer1.connect(endpoint);

    /* DEALER 2: routing_id = "D2" */
    dealer2.set_routing_id(zlink::routing_id_t("D2"));
    dealer2.connect(endpoint);

    /* ROUTER handler distinguishes clients by source_rid */
    auto sender = router.send_handle();
    router.on_receive([sender](const zlink_routing_id_t *source_rid,
                               zlink_msg_t *parts, size_t part_count,
                               void *) {
        /* source_rid contains "D1" or "D2" */
        zlink::routing_id_t rid(*source_rid);
        auto reply = zlink::message_t::from_string("reply");
        sender.send(rid, reply);
        zlink::detail::close_message_array(parts, part_count);
    }, nullptr);
    ```

=== "Java"

    ```java
    /* DEALER 1: routing_id = "D1" */
    dealer1.setRoutingId(RoutingId.copyOf("D1".getBytes()));
    dealer1.connect(endpoint);

    /* DEALER 2: routing_id = "D2" */
    dealer2.setRoutingId(RoutingId.copyOf("D2".getBytes()));
    dealer2.connect(endpoint);

    /* ROUTER handler distinguishes clients by routing_id */
    router.onReceive((received) -> {
        /* received.routingId() contains "D1" or "D2" */
        router.send(received.routingId(),
            Message.copyOf("reply".getBytes()));
    });
    ```

=== "Python"

    ```python
    # DEALER 1: routing_id = "D1"
    dealer1.set_routing_id(b"D1")
    dealer1.connect(endpoint)

    # DEALER 2: routing_id = "D2"
    dealer2.set_routing_id(b"D2")
    dealer2.connect(endpoint)

    # ROUTER handler distinguishes clients by routing_id
    def on_message(received):
        # received.routing_id contains b"D1" or b"D2"
        reply = zlink.Message(b"reply")
        router.send(reply, routing_id=received.routing_id)

    router.on_receive(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    // DEALER 1: routing_id = "D1"
    dealer1.setRoutingId(Buffer.from("D1"));
    dealer1.connect(endpoint);

    // DEALER 2: routing_id = "D2"
    dealer2.setRoutingId(Buffer.from("D2"));
    dealer2.connect(endpoint);

    // ROUTER handler distinguishes clients by routingId
    router.onReceive((routingId, parts) => {
        // routingId contains "D1" or "D2"
        router.send(routingId!, Buffer.from("reply"));
    });
    ```

=== "C#/.NET"

    ```csharp
    // DEALER 1: routing_id = "D1"
    dealer1.DealerOptions.RoutingId = new RoutingId("D1");
    dealer1.Connect(endpoint);

    // DEALER 2: routing_id = "D2"
    dealer2.DealerOptions.RoutingId = new RoutingId("D2");
    dealer2.Connect(endpoint);

    // ROUTER handler distinguishes clients by RoutingId
    router.OnReceive((received) => {
        // received.RoutingId contains "D1" or "D2"
        router.Send(received.RoutingId, Message.CopyFrom("reply"u8));
    });
    ```

=== "Rust"

    ```rust
    // DEALER 1: routing_id = "D1"
    dealer1.set_routing_id(&RoutingId::new(b"D1")?)?;
    dealer1.connect(endpoint)?;

    // DEALER 2: routing_id = "D2"
    dealer2.set_routing_id(&RoutingId::new(b"D2")?)?;
    dealer2.connect(endpoint)?;

    // ROUTER handler distinguishes clients by routing_id
    let sender = router.send_handle();
    router.on_receive(move |received| {
        // received.routing_id() contains "D1" or "D2"
        let reply = Message::from(b"reply" as &[u8]);
        sender.send(received.routing_id(), reply).unwrap();
    })?;
    ```

=== "Go"

    ```go
    // DEALER 1: routing_id = "D1"
    rid1, _ := zlink.NewRoutingID([]byte("D1"))
    dealer1.SetRoutingID(rid1)
    dealer1.Connect(endpoint)

    // DEALER 2: routing_id = "D2"
    rid2, _ := zlink.NewRoutingID([]byte("D2"))
    dealer2.SetRoutingID(rid2)
    dealer2.Connect(endpoint)

    // ROUTER handler distinguishes clients by RoutingID
    router.OnReceive(func(received *zlink.Received) {
        // received.RoutingID() contains "D1" or "D2"
        reply, _ := zlink.NewMessage([]byte("reply"))
        router.SendTo(received.RoutingID(), reply)
    })
    ```

> Reference: `core/tests/test_router_multiple_dealers.cpp` — Multiple DEALER example

### Handling routing_id with zlink_msg_t

=== "C"

    ```c
    /* Handler callback provides routing_id and data directly */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        /* Check routing_id size and content */
        printf("routing_id: %zu bytes\n", source_rid->size);

        /* Reply: use source_rid */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 5);
        memcpy(zlink_msg_data(&reply), "reply", 5);
        zlink_send_rid(router, source_rid, &reply, 1, 0);

        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    /* Handler callback provides routing_id and data directly */
    auto sender = router.send_handle();
    router.on_receive([sender](const zlink_routing_id_t *source_rid,
                               zlink_msg_t *parts, size_t part_count,
                               void *) {
        /* Check routing_id size and content */
        std::println("routing_id: {} bytes", source_rid->size);

        /* Reply: use source_rid */
        zlink::routing_id_t rid(*source_rid);
        auto reply = zlink::message_t::from_string("reply");
        sender.send(rid, reply);
        zlink::detail::close_message_array(parts, part_count);
    }, nullptr);
    ```

=== "Java"

    ```java
    /* Handler callback provides routing_id and data directly */
    router.onReceive((received) -> {
        /* Check routing_id size and content */
        System.out.printf("routing_id: %d bytes%n",
            received.routingId().size());

        /* Reply: use routing_id from received */
        router.send(received.routingId(),
            Message.copyOf("reply".getBytes()));
    });
    ```

=== "Python"

    ```python
    # Handler callback provides routing_id and data directly
    def on_message(received):
        # Check routing_id size and content
        print(f"routing_id: {len(received.routing_id)} bytes")

        # Reply: use routing_id from received
        reply = zlink.Message(b"reply")
        router.send(reply, routing_id=received.routing_id)

    router.on_receive(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    // Handler callback provides routingId and data directly
    router.onReceive((routingId, parts) => {
        // Check routing_id size and content
        console.log(`routing_id: ${routingId!.length} bytes`);

        // Reply: use routingId
        router.send(routingId!, Buffer.from("reply"));
    });
    ```

=== "C#/.NET"

    ```csharp
    // Handler callback provides RoutingId and data directly
    router.OnReceive((received) => {
        // Check routing_id size and content
        Console.WriteLine($"routing_id: {received.RoutingId.Length} bytes");

        // Reply: use RoutingId from received
        router.Send(received.RoutingId, Message.CopyFrom("reply"u8));
    });
    ```

=== "Rust"

    ```rust
    // Handler callback provides routing_id and data directly
    let sender = router.send_handle();
    router.on_receive(move |received| {
        // Check routing_id size and content
        println!("routing_id: {} bytes", received.routing_id().len());

        // Reply: use routing_id from received
        let reply = Message::from(b"reply" as &[u8]);
        sender.send(received.routing_id(), reply).unwrap();
    })?;
    ```

=== "Go"

    ```go
    // Handler callback provides RoutingID and data directly
    router.OnReceive(func(received *zlink.Received) {
        // Check routing_id size and content
        rid := received.RoutingID()
        fmt.Printf("routing_id: %d bytes\n", len(rid.Bytes()))

        // Reply: use RoutingID from received
        reply, _ := zlink.NewMessage([]byte("reply"))
        router.SendTo(rid, reply)
    })
    ```

## 7. Using routing_id with STREAM Sockets

STREAM sockets identify external clients using a 4B uint32 peer routing_id.

### Basic Usage

=== "C"

    ```c
    /* Callback dispatch */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        for (size_t i = 0; i < part_count; ++i) {
            void *data = zlink_msg_data(&parts[i]);
            size_t size = zlink_msg_size(&parts[i]);

            /* Reply: use the same routing_id */
            zlink_msg_t reply;
            zlink_msg_init_size(&reply, size);
            memcpy(zlink_msg_data(&reply), data, size);
            zlink_send_rid(stream, source_rid, &reply, 1, 0);
            zlink_msg_close(&parts[i]);
        }
    }

    zlink_recv_handler(stream, on_message, NULL);
    ```

=== "C++"

    ```cpp
    /* Callback dispatch */
    auto sender = stream.send_handle();
    stream.on_receive([sender](const zlink_routing_id_t *source_rid,
                               zlink_msg_t *parts, size_t part_count,
                               void *) {
        zlink::routing_id_t rid(*source_rid);
        for (size_t i = 0; i < part_count; ++i) {
            auto reply = zlink::message_t(zlink_msg_data(&parts[i]),
                                          zlink_msg_size(&parts[i]));
            sender.send(rid, reply);
            zlink_msg_close(&parts[i]);
        }
    }, nullptr);
    ```

=== "Java"

    ```java
    /* Callback dispatch */
    stream.onReceive((received) -> {
        for (var part : received.parts()) {
            stream.send(received.routingId(),
                Message.copyOf(part.toByteArray()));
        }
    });
    ```

=== "Python"

    ```python
    # Callback dispatch
    def on_message(received):
        for part in received.parts:
            reply = zlink.Message(part.to_bytes())
            stream.send(reply, routing_id=received.routing_id)

    stream.on_receive(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    // Callback dispatch
    stream.onReceive((routingId, parts) => {
        for (const part of parts) {
            stream.send(routingId!, Buffer.from(part.data));
        }
    });
    ```

=== "C#/.NET"

    ```csharp
    // Callback dispatch
    stream.OnReceive((received) => {
        foreach (var part in received.Parts)
        {
            stream.Send(received.RoutingId,
                Message.CopyFrom(part.ToArray()));
        }
    });
    ```

=== "Rust"

    ```rust
    // Callback dispatch
    let sender = stream.send_handle();
    stream.on_receive(move |received| {
        for part in received.parts() {
            let reply = Message::from(part.data());
            sender.send(received.routing_id(), reply).unwrap();
        }
    })?;
    ```

=== "Go"

    ```go
    // Callback dispatch
    stream.OnReceive(func(received *zlink.Received) {
        for _, part := range received.Parts() {
            reply, _ := zlink.NewMessage(part.Data())
            stream.SendTo(received.RoutingID(), reply)
        }
    })
    ```

### routing_id in Connect/Disconnect Events

=== "C"

    ```c
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        for (size_t i = 0; i < part_count; ++i) {
            uint8_t *data = (uint8_t *)zlink_msg_data(&parts[i]);
            size_t size = zlink_msg_size(&parts[i]);

            if (size == 1 && data[0] == 0x01) {
                /* New client connected: identify by source_rid */
                printf("Connected: ");
                for (size_t j = 0; j < source_rid->size; j++)
                    printf("%02x", source_rid->data[j]);
                printf("\n");
            } else if (size == 1 && data[0] == 0x00) {
                /* Client disconnected: identify by source_rid and clean up */
                printf("Disconnected\n");
            }
            zlink_msg_close(&parts[i]);
        }
    }
    ```

=== "C++"

    ```cpp
    stream.on_receive([](const zlink_routing_id_t *source_rid,
                         zlink_msg_t *parts, size_t part_count,
                         void *) {
        for (size_t i = 0; i < part_count; ++i) {
            auto *data = static_cast<uint8_t *>(zlink_msg_data(&parts[i]));
            size_t size = zlink_msg_size(&parts[i]);

            if (size == 1 && data[0] == 0x01) {
                /* New client connected */
                auto bytes = zlink::routing_id_t(*source_rid).to_bytes();
                std::print("Connected: ");
                for (auto b : bytes) std::print("{:02x}", b);
                std::println();
            } else if (size == 1 && data[0] == 0x00) {
                /* Client disconnected */
                std::println("Disconnected");
            }
            zlink_msg_close(&parts[i]);
        }
    }, nullptr);
    ```

=== "Java"

    ```java
    stream.onReceive((received) -> {
        for (var part : received.parts()) {
            byte[] data = part.toByteArray();

            if (data.length == 1 && data[0] == 0x01) {
                /* New client connected */
                System.out.printf("Connected: %s%n",
                    HexFormat.of().formatHex(
                        received.routingId().toByteArray()));
            } else if (data.length == 1 && data[0] == 0x00) {
                /* Client disconnected */
                System.out.println("Disconnected");
            }
        }
    });
    ```

=== "Python"

    ```python
    def on_message(received):
        for part in received.parts:
            data = part.to_bytes()

            if data == b"\x01":
                # New client connected
                print(f"Connected: {received.routing_id.hex()}")
            elif data == b"\x00":
                # Client disconnected
                print("Disconnected")

    stream.on_receive(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    stream.onReceive((routingId, parts) => {
        for (const part of parts) {
            const data = part.data;

            if (data.length === 1 && data[0] === 0x01) {
                // New client connected
                console.log(`Connected: ${routingId!.toString("hex")}`);
            } else if (data.length === 1 && data[0] === 0x00) {
                // Client disconnected
                console.log("Disconnected");
            }
        }
    });
    ```

=== "C#/.NET"

    ```csharp
    stream.OnReceive((received) => {
        foreach (var part in received.Parts)
        {
            var data = part.ToArray();

            if (data.Length == 1 && data[0] == 0x01)
            {
                // New client connected
                Console.WriteLine($"Connected: {received.RoutingId}");
            }
            else if (data.Length == 1 && data[0] == 0x00)
            {
                // Client disconnected
                Console.WriteLine("Disconnected");
            }
        }
    });
    ```

=== "Rust"

    ```rust
    stream.on_receive(|received| {
        for part in received.parts() {
            let data = part.data();

            if data == [0x01] {
                // New client connected
                print!("Connected: ");
                for b in received.routing_id().data() {
                    print!("{:02x}", b);
                }
                println!();
            } else if data == [0x00] {
                // Client disconnected
                println!("Disconnected");
            }
        }
    })?;
    ```

=== "Go"

    ```go
    stream.OnReceive(func(received *zlink.Received) {
        for _, part := range received.Parts() {
            data := part.Data()

            if len(data) == 1 && data[0] == 0x01 {
                // New client connected
                fmt.Printf("Connected: %x\n",
                    received.RoutingID().Bytes())
            } else if len(data) == 1 && data[0] == 0x00 {
                // Client disconnected
                fmt.Println("Disconnected")
            }
        }
    })
    ```

> Reference: `core/tests/test_stream_socket.cpp` — `recv_stream_event()`, `send_stream_msg()`

### ROUTER vs STREAM routing_id Comparison

| | ROUTER | STREAM |
|---|---|---|
| **Size** | Variable (user-defined or 16B UUID) | Fixed 4B (uint32) |
| **Generation** | Peer's own routing_id | Auto-assigned by the server |
| **Configurable** | Peer sets via `zlink_set_routing_id()` | Auto-assigned only (not configurable) |
| **Frame position** | Automatically prepended on receive | Automatically prepended on receive |

## 8. Debugging Tips for routing_id

### Hex Output

Since routing_id is binary data, printing it as a string may produce garbled output. Use hex format instead.

=== "C"

    ```c
    void print_routing_id(const void *data, size_t size) {
        const uint8_t *bytes = (const uint8_t *)data;
        printf("routing_id[%zu]: ", size);
        for (size_t i = 0; i < size; i++)
            printf("%02x", bytes[i]);
        printf("\n");
    }

    /* In handler callback */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        print_routing_id(source_rid->data, source_rid->size);
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    void print_routing_id(const zlink::routing_id_t &rid) {
        auto bytes = rid.to_bytes();
        std::print("routing_id[{}]: ", bytes.size());
        for (auto b : bytes)
            std::print("{:02x}", b);
        std::println();
    }

    /* In handler callback */
    auto sender = router.send_handle();
    router.on_receive([](const zlink_routing_id_t *source_rid,
                         zlink_msg_t *parts, size_t part_count,
                         void *) {
        print_routing_id(zlink::routing_id_t(*source_rid));
        zlink::detail::close_message_array(parts, part_count);
    }, nullptr);
    ```

=== "Java"

    ```java
    static void printRoutingId(RoutingId rid) {
        byte[] bytes = rid.toByteArray();
        System.out.printf("routing_id[%d]: ", bytes.length);
        for (byte b : bytes)
            System.out.printf("%02x", b);
        System.out.println();
    }

    /* In handler callback */
    router.onReceive((received) -> {
        printRoutingId(received.routingId());
    });
    ```

=== "Python"

    ```python
    def print_routing_id(rid: bytes):
        print(f"routing_id[{len(rid)}]: {rid.hex()}")

    # In handler callback
    def on_message(received):
        print_routing_id(received.routing_id)

    router.on_receive(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    function printRoutingId(rid: Buffer) {
        console.log(`routing_id[${rid.length}]: ${rid.toString("hex")}`);
    }

    // In handler callback
    router.onReceive((routingId, parts) => {
        printRoutingId(routingId!);
    });
    ```

=== "C#/.NET"

    ```csharp
    static void PrintRoutingId(string rid)
    {
        Console.WriteLine($"routing_id: {rid}");
    }

    // In handler callback
    router.OnReceive((received) => {
        PrintRoutingId(received.RoutingId);
    });
    ```

=== "Rust"

    ```rust
    fn print_routing_id(rid: &RoutingId) {
        print!("routing_id[{}]: ", rid.len());
        for b in rid.data() {
            print!("{:02x}", b);
        }
        println!();
    }

    // In handler callback
    router.on_receive(|received| {
        print_routing_id(received.routing_id());
    })?;
    ```

=== "Go"

    ```go
    func printRoutingID(rid zlink.RoutingID) {
        data := rid.Bytes()
        fmt.Printf("routing_id[%d]: %x\n", len(data), data)
    }

    // In handler callback
    router.OnReceive(func(received *zlink.Received) {
        printRoutingID(received.RoutingID())
    })
    ```

### String routing_id

If the user-defined routing_id is an ASCII string, it can be printed directly.

=== "C"

    ```c
    zlink_set_routing_id(dealer, "D1", 2);

    /* In ROUTER handler callback */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        char rid[256];
        memcpy(rid, source_rid->data, source_rid->size);
        rid[source_rid->size] = '\0';
        printf("routing_id: %s\n", rid);  /* "D1" */
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    dealer.set_routing_id(zlink::routing_id_t("D1"));

    /* In ROUTER handler callback */
    router.on_receive([](const zlink_routing_id_t *source_rid,
                         zlink_msg_t *parts, size_t part_count,
                         void *) {
        auto rid_str = zlink::routing_id_t(*source_rid).to_string();
        std::println("routing_id: {}", rid_str);  /* "D1" */
        zlink::detail::close_message_array(parts, part_count);
    }, nullptr);
    ```

=== "Java"

    ```java
    dealer.setRoutingId(RoutingId.copyOf("D1".getBytes()));

    /* In ROUTER handler callback */
    router.onReceive((received) -> {
        String rid = new String(received.routingId().toByteArray());
        System.out.printf("routing_id: %s%n", rid);  /* "D1" */
    });
    ```

=== "Python"

    ```python
    dealer.set_routing_id(b"D1")

    # In ROUTER handler callback
    def on_message(received):
        rid = received.routing_id.decode()
        print(f"routing_id: {rid}")  # "D1"

    router.on_receive(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    dealer.setRoutingId(Buffer.from("D1"));

    // In ROUTER handler callback
    router.onReceive((routingId, parts) => {
        const rid = routingId!.toString("utf-8");
        console.log(`routing_id: ${rid}`);  // "D1"
    });
    ```

=== "C#/.NET"

    ```csharp
    dealer.DealerOptions.RoutingId = new RoutingId("D1");

    // In ROUTER handler callback
    router.OnReceive((received) => {
        Console.WriteLine($"routing_id: {received.RoutingId}");  // "D1"
    });
    ```

=== "Rust"

    ```rust
    dealer.set_routing_id(&RoutingId::new(b"D1")?)?;

    // In ROUTER handler callback
    router.on_receive(|received| {
        let rid = std::str::from_utf8(received.routing_id().data())
            .unwrap_or("<binary>");
        println!("routing_id: {}", rid);  // "D1"
    })?;
    ```

=== "Go"

    ```go
    rid, _ := zlink.NewRoutingID([]byte("D1"))
    dealer.SetRoutingID(rid)

    // In ROUTER handler callback
    router.OnReceive(func(received *zlink.Received) {
        ridStr := string(received.RoutingID().Bytes())
        fmt.Printf("routing_id: %s\n", ridStr)  // "D1"
    })
    ```

### Checking Auto-Generated routing_id

=== "C"

    ```c
    /* Query the auto-assigned routing_id after socket creation */
    zlink_routing_id_t rid;
    zlink_get_routing_id(socket, &rid);
    printf("Auto-generated routing_id: %u bytes\n", rid.size);  /* 16 bytes (UUID) */
    ```

=== "C++"

    ```cpp
    /* Query the auto-assigned routing_id after socket creation */
    zlink::routing_id_t rid;
    dealer.get_routing_id(rid);
    std::println("Auto-generated routing_id: {} bytes", rid.size());  /* 16 bytes (UUID) */
    ```

=== "Java"

    ```java
    /* Query the auto-assigned routing_id after socket creation */
    byte[] rid = dealer.getOption(SocketOptions.ROUTING_ID_BYTES);
    System.out.printf("Auto-generated routing_id: %d bytes%n", rid.length);  /* 16 bytes (UUID) */
    ```

=== "Python"

    ```python
    # Query the auto-assigned routing_id after socket creation
    rid = socket.get_routing_id()
    print(f"Auto-generated routing_id: {len(rid)} bytes")  # 16 bytes (UUID)
    ```

=== "Node/TypeScript"

    ```typescript
    // Query the auto-assigned routing_id after socket creation
    const rid = socket.getRoutingId();
    console.log(`Auto-generated routing_id: ${rid.length} bytes`);  // 16 bytes (UUID)
    ```

=== "C#/.NET"

    ```csharp
    // Query the auto-assigned routing_id after socket creation
    var rid = dealer.DealerOptions.RoutingId;
    Console.WriteLine($"Auto-generated routing_id: {rid.Value.Length} bytes");  // 16 bytes (UUID)
    ```

=== "Rust"

    ```rust
    // Query the auto-assigned routing_id after socket creation
    let rid = dealer.routing_id()?;
    println!("Auto-generated routing_id: {} bytes", rid.len());  // 16 bytes (UUID)
    ```

=== "Go"

    ```go
    // Query the auto-assigned routing_id after socket creation
    rid, _ := dealer.RoutingID()
    fmt.Printf("Auto-generated routing_id: %d bytes\n", len(rid.Bytes()))  // 16 bytes (UUID)
    ```

## 9. Binary Handling Principles

- Treat routing_id as **binary data**
- String conversion is the application's responsibility
- Auto-generated routing_ids use an internal format; no numeric conversion API is provided
- Use `memcmp()` for comparison (string comparison functions must not be used)
- Hex format is recommended for log output

=== "C"

    ```c
    /* routing_id comparison */
    if (rid_size == 2 && memcmp(rid, "D1", 2) == 0) {
        /* Message from client D1 */
    }
    ```

=== "C++"

    ```cpp
    /* routing_id comparison */
    auto rid = zlink::routing_id_t(*source_rid);
    if (rid.to_string() == "D1") {
        /* Message from client D1 */
    }
    ```

=== "Java"

    ```java
    /* routing_id comparison */
    if (received.routingId().equals(
            RoutingId.copyOf("D1".getBytes()))) {
        /* Message from client D1 */
    }
    ```

=== "Python"

    ```python
    # routing_id comparison
    if received.routing_id == b"D1":
        # Message from client D1
        pass
    ```

=== "Node/TypeScript"

    ```typescript
    // routing_id comparison
    if (routingId && routingId.equals(Buffer.from("D1"))) {
        // Message from client D1
    }
    ```

=== "C#/.NET"

    ```csharp
    // routing_id comparison
    if (received.RoutingId == "D1")
    {
        // Message from client D1
    }
    ```

=== "Rust"

    ```rust
    // routing_id comparison
    if received.routing_id().data() == b"D1" {
        // Message from client D1
    }
    ```

=== "Go"

    ```go
    // routing_id comparison
    expected, _ := zlink.NewRoutingID([]byte("D1"))
    if received.RoutingID().Equal(expected) {
        // Message from client D1
    }
    ```

---
[← SPOT](07-3-spot.md) | [Message API →](09-message-api.md)
