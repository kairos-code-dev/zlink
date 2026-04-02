# Proxy Pattern

## 1. Overview

A proxy relays messages between two sockets.
`zlink_proxy()` is a general-purpose utility that works with any socket
combination. Users can also build custom proxies using public APIs.

## 2. zlink_proxy() -- Built-in Proxy

=== "C"

    ```c
    int zlink_proxy (void *frontend, void *backend, void *capture);
    ```

=== "C++"

    ```cpp
    zlink::proxy(frontend, backend, capture);
    ```

=== "Java"

    ```java
    Proxy.start(frontend, backend, capture);
    ```

=== "Python"

    ```python
    zlink.proxy(frontend, backend, capture)
    ```

=== "Node/TypeScript"

    ```typescript
    zlink.proxy(frontend, backend, capture);
    ```

=== "C#/.NET"

    ```csharp
    Proxy.Start(frontend, backend, capture);
    ```

=== "Rust"

    ```rust
    zlink::proxy(&frontend, &backend, Some(&capture))?;
    ```

=== "Go"

    ```go
    zlink.Proxy(frontend, backend, capture)
    ```

- Forwards messages from `frontend` to `backend` and vice versa
- If `capture` is non-NULL, copies all passing messages to the capture socket
- **Blocking function** -- run in a dedicated thread
- **No socket type restriction** -- internally calls `socket_base_t` internal
  recv/send methods, independent of public API `ENOTSUP` restrictions

### Supported Socket Combinations

| frontend | backend | Use case |
|----------|---------|----------|
| XSUB | XPUB | PUB/SUB relay (most common) |
| ROUTER | DEALER | Request/reply broker |
| DEALER | DEALER | Load balancing relay |
| PAIR | PAIR | Inter-thread bridge |

## 3. PUB/SUB Proxy -- XSUB/XPUB

The most common proxy pattern.

```
Data flow:              PUB ──► XSUB ══ proxy ══► XPUB ──► SUB
Subscription (reverse): PUB ◄── XSUB ◄── proxy ◄── XPUB ◄── SUB
```

### 3.1 Built-in Proxy

=== "C"

    ```c
    void *xsub = zlink_socket(ctx, ZLINK_XSUB);
    zlink_bind(xsub, "tcp://*:5556");      /* PUBs connect here */

    void *xpub = zlink_socket(ctx, ZLINK_XPUB);
    zlink_bind(xpub, "tcp://*:5557");      /* SUBs connect here */

    void *capture = zlink_socket(ctx, ZLINK_PUB);
    zlink_bind(capture, "tcp://*:5558");   /* optional: message recording */

    zlink_proxy(xsub, xpub, capture);      /* blocking */
    ```

=== "C++"

    ```cpp
    zlink::xsub_socket_t xsub(ctx);
    xsub.bind("tcp://*:5556");      // PUBs connect here

    zlink::xpub_socket_t xpub(ctx);
    xpub.bind("tcp://*:5557");      // SUBs connect here

    zlink::pub_socket_t capture(ctx);
    capture.bind("tcp://*:5558");   // optional: message recording

    zlink::proxy(xsub, xpub, capture);  // blocking
    ```

=== "Java"

    ```java
    XSubSocket xsub = new XSubSocket(ctx);
    xsub.bind("tcp://*:5556");      // PUBs connect here

    XPubSocket xpub = new XPubSocket(ctx);
    xpub.bind("tcp://*:5557");      // SUBs connect here

    PubSocket capture = new PubSocket(ctx);
    capture.bind("tcp://*:5558");   // optional: message recording

    Proxy.start(xsub, xpub, capture);  // blocking
    ```

=== "Python"

    ```python
    xsub = zlink.XSubSocket(ctx)
    xsub.bind("tcp://*:5556")       # PUBs connect here

    xpub = zlink.XPubSocket(ctx)
    xpub.bind("tcp://*:5557")       # SUBs connect here

    capture = zlink.PubSocket(ctx)
    capture.bind("tcp://*:5558")    # optional: message recording

    zlink.proxy(xsub, xpub, capture)  # blocking
    ```

=== "Node/TypeScript"

    ```typescript
    const xsub = new zlink.XSubSocket(ctx);
    xsub.bind("tcp://*:5556");      // PUBs connect here

    const xpub = new zlink.XPubSocket(ctx);
    xpub.bind("tcp://*:5557");      // SUBs connect here

    const capture = new zlink.PubSocket(ctx);
    capture.bind("tcp://*:5558");   // optional: message recording

    zlink.proxy(xsub, xpub, capture);  // blocking
    ```

=== "C#/.NET"

    ```csharp
    using var xsub = new XSubSocket(ctx);
    xsub.Bind("tcp://*:5556");      // PUBs connect here

    using var xpub = new XPubSocket(ctx);
    xpub.Bind("tcp://*:5557");      // SUBs connect here

    using var capture = new PubSocket(ctx);
    capture.Bind("tcp://*:5558");   // optional: message recording

    Proxy.Start(xsub, xpub, capture);  // blocking
    ```

=== "Rust"

    ```rust
    let xsub = ctx.xsub_socket()?;
    xsub.bind("tcp://*:5556")?;     // PUBs connect here

    let xpub = ctx.xpub_socket()?;
    xpub.bind("tcp://*:5557")?;     // SUBs connect here

    let capture = ctx.pub_socket()?;
    capture.bind("tcp://*:5558")?;  // optional: message recording

    zlink::proxy(&xsub, &xpub, Some(&capture))?;  // blocking
    ```

=== "Go"

    ```go
    xsub, err := ctx.XSubSocket()
    if err != nil { panic(err) }
    xsub.Bind("tcp://*:5556")  // PUBs connect here

    xpub, err := ctx.XPubSocket()
    if err != nil { panic(err) }
    xpub.Bind("tcp://*:5557")  // SUBs connect here

    capture, err := ctx.PubSocket()
    if err != nil { panic(err) }
    capture.Bind("tcp://*:5558")  // optional: message recording

    zlink.Proxy(xsub, xpub, capture)  // blocking
    ```

`zlink_proxy()` handles two things internally:
- **Data relay**: Pulls messages from XSUB and pushes to XPUB
- **Subscription propagation**: Pulls subscription events from XPUB and pushes to XSUB

### 3.2 Manual Proxy

When custom logic (logging, filtering, topic transformation) is needed,
build a manual proxy using public APIs only.

#### Data Flow

| Step | Socket | API | Description |
|------|--------|-----|-------------|
| 1 | XSUB | `zlink_subscribe(xsub, ...)` | Receive data (topic + parts separated) |
| 2 | App | Custom logic | Filtering, transformation, logging |
| 3 | XPUB | `zlink_publish(xpub, topic, parts, ...)` | Publish data |

#### Subscription Propagation

| Step | Socket | API | Description |
|------|--------|-----|-------------|
| 1 | XPUB | `zlink_subscription_event(xpub, ...)` | Receive SUB subscribe/unsubscribe events |
| 2 | App | Custom logic | Authorization, topic remapping |
| 3 | XSUB | `zlink_set_subscription(xsub, topic)` | Propagate to upstream PUB |

#### Full Code

=== "C"

    ```c
    void *xsub = zlink_socket(ctx, ZLINK_XSUB);
    void *xpub = zlink_socket(ctx, ZLINK_XPUB);
    zlink_bind(xsub, "tcp://*:5556");
    zlink_bind(xpub, "tcp://*:5557");

    while (running) {
        /* Data relay: XSUB → app → XPUB */
        zlink_routing_id_t rid;
        zlink_msg_t *parts = NULL;
        size_t count = 0;
        char topic[256];
        size_t topic_len = sizeof(topic);
        int rc = zlink_subscribe(xsub, &rid, &parts, &count,
                                 topic, &topic_len, ZLINK_DONTWAIT);
        if (rc == 0) {
            /* Insert custom logic here (filtering, logging, etc.) */
            zlink_publish(xpub, topic, parts, count, 0);
        }

        /* Subscription propagation: XPUB → app → XSUB */
        int subscribed;
        char sub_topic[256];
        size_t sub_len = sizeof(sub_topic);
        rc = zlink_subscription_event(xpub, &rid, &subscribed,
                                      sub_topic, &sub_len, ZLINK_DONTWAIT);
        if (rc == 0) {
            /* Insert custom logic here (authorization, remapping, etc.) */
            if (subscribed)
                zlink_set_subscription(xsub, sub_topic);
            else
                zlink_unset_subscription(xsub, sub_topic);
        }
    }
    ```

=== "C++"

    ```cpp
    zlink::xsub_socket_t xsub(ctx);
    zlink::xpub_socket_t xpub(ctx);
    xsub.bind("tcp://*:5556");
    xpub.bind("tcp://*:5557");

    while (running) {
        // Data relay: XSUB -> app -> XPUB
        auto msg = xsub.subscribe(zlink::dontwait);
        if (msg) {
            // Insert custom logic here
            xpub.publish(msg->topic, msg->parts);
        }

        // Subscription propagation: XPUB -> app -> XSUB
        auto event = xpub.subscription_event(zlink::dontwait);
        if (event) {
            if (event->subscribed)
                xsub.set_subscription(event->topic);
            else
                xsub.unset_subscription(event->topic);
        }
    }
    ```

=== "Java"

    ```java
    XSubSocket xsub = new XSubSocket(ctx);
    XPubSocket xpub = new XPubSocket(ctx);
    xsub.bind("tcp://*:5556");
    xpub.bind("tcp://*:5557");

    while (running) {
        // Data relay: XSUB -> app -> XPUB
        SubscribeResult msg = xsub.subscribe(DONTWAIT);
        if (msg != null) {
            // Insert custom logic here
            xpub.publish(msg.topic(), msg.parts());
        }

        // Subscription propagation: XPUB -> app -> XSUB
        SubscriptionEvent event = xpub.subscriptionEvent(DONTWAIT);
        if (event != null) {
            if (event.subscribed())
                xsub.setSubscription(event.topic());
            else
                xsub.unsetSubscription(event.topic());
        }
    }
    ```

=== "Python"

    ```python
    xsub = zlink.XSubSocket(ctx)
    xpub = zlink.XPubSocket(ctx)
    xsub.bind("tcp://*:5556")
    xpub.bind("tcp://*:5557")

    while running:
        # Data relay: XSUB -> app -> XPUB
        msg = xsub.subscribe(dontwait=True)
        if msg:
            # Insert custom logic here
            xpub.publish(msg.topic, msg.parts)

        # Subscription propagation: XPUB -> app -> XSUB
        event = xpub.subscription_event(dontwait=True)
        if event:
            if event.subscribed:
                xsub.set_subscription(event.topic)
            else:
                xsub.unset_subscription(event.topic)
    ```

=== "Node/TypeScript"

    ```typescript
    const xsub = new zlink.XSubSocket(ctx);
    const xpub = new zlink.XPubSocket(ctx);
    xsub.bind("tcp://*:5556");
    xpub.bind("tcp://*:5557");

    while (running) {
        // Data relay: XSUB -> app -> XPUB
        const msg = xsub.subscribe({ dontwait: true });
        if (msg) {
            // Insert custom logic here
            xpub.publish(msg.topic, msg.parts);
        }

        // Subscription propagation: XPUB -> app -> XSUB
        const event = xpub.subscriptionEvent({ dontwait: true });
        if (event) {
            if (event.subscribed)
                xsub.setSubscription(event.topic);
            else
                xsub.unsetSubscription(event.topic);
        }
    }
    ```

=== "C#/.NET"

    ```csharp
    using var xsub = new XSubSocket(ctx);
    using var xpub = new XPubSocket(ctx);
    xsub.Bind("tcp://*:5556");
    xpub.Bind("tcp://*:5557");

    while (running) {
        // Data relay: XSUB -> app -> XPUB
        var msg = xsub.Subscribe(dontwait: true);
        if (msg != null) {
            // Insert custom logic here
            xpub.Publish(msg.Topic, msg.Parts);
        }

        // Subscription propagation: XPUB -> app -> XSUB
        var ev = xpub.SubscriptionEvent(dontwait: true);
        if (ev != null) {
            if (ev.Subscribed)
                xsub.SetSubscription(ev.Topic);
            else
                xsub.UnsetSubscription(ev.Topic);
        }
    }
    ```

=== "Rust"

    ```rust
    let xsub = ctx.xsub_socket()?;
    let xpub = ctx.xpub_socket()?;
    xsub.bind("tcp://*:5556")?;
    xpub.bind("tcp://*:5557")?;

    while running {
        // Data relay: XSUB -> app -> XPUB
        if let Some(msg) = xsub.subscribe(zlink::DONTWAIT)? {
            // Insert custom logic here
            xpub.publish(&msg.topic, &msg.parts)?;
        }

        // Subscription propagation: XPUB -> app -> XSUB
        if let Some(event) = xpub.subscription_event(zlink::DONTWAIT)? {
            if event.subscribed {
                xsub.set_subscription(&event.topic)?;
            } else {
                xsub.unset_subscription(&event.topic)?;
            }
        }
    }
    ```

=== "Go"

    ```go
    xsub, err := ctx.XSubSocket()
    if err != nil { panic(err) }
    xpub, err := ctx.XPubSocket()
    if err != nil { panic(err) }
    xsub.Bind("tcp://*:5556")
    xpub.Bind("tcp://*:5557")

    for running {
        // Data relay: XSUB -> app -> XPUB
        if msg, err := xsub.Subscribe(); err == nil {
            // Insert custom logic here
            xpub.Publish(msg.Topic, msg.Parts)
        }

        // Subscription propagation: XPUB -> app -> XSUB
        if event, err := xpub.SubscriptionEvent(); err == nil {
            if event.Subscribed {
                xsub.SetSubscription(event.Topic)
            } else {
                xsub.UnsetSubscription(event.Topic)
            }
        }
    }
    ```

### 3.3 Why XSUB/XPUB?

| Question | With SUB/PUB | With XSUB/XPUB |
|----------|-------------|-----------------|
| Data pass-through | SUB local filter on -- must subscribe | XSUB local filter off -- **passes all** |
| Subscription events | PUB doesn't expose | XPUB provides `subscription_event()` |
| Proxy suitability | Proxy must manage topics itself | **Relay only -- ideal for proxy** |

> **Key point:** `zlink_proxy()` internally calls `socket_base_t` internal
> methods, not public APIs. Calling `zlink_send()` on XSUB or
> `zlink_recv()` on XPUB returns `ENOTSUP`. Proxy operation is only
> possible via `zlink_proxy()` or the manual approach above
> (using dedicated APIs like `subscribe()`, `publish()`, etc.).

## 4. Request/Reply Proxy -- ROUTER/DEALER

```
Client (DEALER) ──► ROUTER ══ proxy ══► DEALER ──► Server (ROUTER)
```

=== "C"

    ```c
    void *frontend = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_bind(frontend, "tcp://*:5559");

    void *backend = zlink_socket(ctx, ZLINK_DEALER);
    zlink_bind(backend, "tcp://*:5560");

    zlink_proxy(frontend, backend, NULL);  /* blocking */
    ```

=== "C++"

    ```cpp
    zlink::router_socket_t frontend(ctx);
    frontend.bind("tcp://*:5559");

    zlink::dealer_socket_t backend(ctx);
    backend.bind("tcp://*:5560");

    zlink::proxy(frontend, backend);  // blocking
    ```

=== "Java"

    ```java
    RouterSocket frontend = new RouterSocket(ctx);
    frontend.bind("tcp://*:5559");

    DealerSocket backend = new DealerSocket(ctx);
    backend.bind("tcp://*:5560");

    Proxy.start(frontend, backend);  // blocking
    ```

=== "Python"

    ```python
    frontend = zlink.RouterSocket(ctx)
    frontend.bind("tcp://*:5559")

    backend = zlink.DealerSocket(ctx)
    backend.bind("tcp://*:5560")

    zlink.proxy(frontend, backend)  # blocking
    ```

=== "Node/TypeScript"

    ```typescript
    const frontend = new zlink.RouterSocket(ctx);
    frontend.bind("tcp://*:5559");

    const backend = new zlink.DealerSocket(ctx);
    backend.bind("tcp://*:5560");

    zlink.proxy(frontend, backend);  // blocking
    ```

=== "C#/.NET"

    ```csharp
    using var frontend = new RouterSocket(ctx);
    frontend.Bind("tcp://*:5559");

    using var backend = new DealerSocket(ctx);
    backend.Bind("tcp://*:5560");

    Proxy.Start(frontend, backend);  // blocking
    ```

=== "Rust"

    ```rust
    let frontend = ctx.router_socket()?;
    frontend.bind("tcp://*:5559")?;

    let backend = ctx.dealer_socket()?;
    backend.bind("tcp://*:5560")?;

    zlink::proxy(&frontend, &backend, None)?;  // blocking
    ```

=== "Go"

    ```go
    frontend, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    frontend.Bind("tcp://*:5559")

    backend, err := ctx.DealerSocket()
    if err != nil { panic(err) }
    backend.Bind("tcp://*:5560")

    zlink.Proxy(frontend, backend, nil)  // blocking
    ```

ROUTER/DEALER proxy has no subscription propagation, so `zlink_proxy()`
alone is sufficient. For manual construction, use `zlink_recv()` →
`zlink_send_rid()` combination.

## 5. Why Use a Proxy?

```
Direct (no proxy)                    With proxy
─────────────────                    ──────────

┌─────┐     ┌─────┐                 ┌─────┐     ┌───────────┐     ┌─────┐
│PUB 1│────►│SUB 1│                 │PUB 1│──►  │           │  ──►│SUB 1│
└─────┘     └─────┘                 └─────┘     │   XSUB    │     └─────┘
┌─────┐     ┌─────┐                 ┌─────┐     │     │     │     ┌─────┐
│PUB 2│────►│SUB 2│                 │PUB 2│──►  │     ▼     │  ──►│SUB 2│
└─────┘     └─────┘                 └─────┘     │   XPUB    │     └─────┘
                                                │  (Proxy)  │
 N × M connections                              └───────────┘
 PUB/SUB must know each other
                                                 N + M connections
                                                 Only need proxy address
```

| Use Case | Description |
|----------|-------------|
| **Reduce connections** | N×M → N+M |
| **Address decoupling** | PUB/SUB don't need each other's endpoints |
| **Dynamic scaling** | PUB/SUB add/remove independently |
| **Subscription transformation** | XPUB MANUAL mode for topic remapping/filtering |
| **Network bridging** | Connect different network segments (e.g., inproc ↔ tcp) |
| **Monitoring** | Capture socket records all passing messages |

---
[← STREAM](03-5-stream.md) | [Transport →](04-transports.md)
