# SPOT Topic PUB/SUB (Location-Transparent Publish/Subscribe)

> This guide reflects the recv-first public surface.
> `SpotNode` and unified `Spot` start in recv model and use
> `zlink_subscribe_handler()` for the one-way transition to callback model.

## 1. Overview

SPOT is a location-transparent, topic-based publish/subscribe system. It automatically constructs a PUB/SUB Mesh based on Discovery, enabling topic message publishing and subscribing across the entire cluster.

> **About the name**: SPOT derives its name from "spot" (location). Each object (node) publishes topics from its own location and subscribes to topics from other locations, forming an object-level, location-transparent pub/sub mesh system.

### Core Terminology

| Term | Description |
|------|-------------|
| **SPOT Node** | PUB/SUB Mesh participant agent (one per node) |
| **SPOT Pub** | Topic publishing path (the hot path of `spot` / `spot_node`) |
| **SPOT Sub** | Topic subscription/receive handle |
| **Topic** | String key-based message channel |
| **Pattern** | Prefix + `*` wildcard subscription |
| **Handler** | Callback function automatically invoked on message receipt |

## 2. Architecture

### Local publish — delivery within the same node

```
  SpotPub           SPOT Node            SpotSub
    |               (worker)               |
    |  -- publish -->  |                   |
    |    (inproc)      |                   |
    |                  | -- callback -----> |
    |                  |    (inproc)        |
```

When SpotPub publishes, the SPOT Node's internal worker receives it and
delivers directly to SpotSub on the same node via callback.

### Remote propagation — delivery across cluster nodes

```
  SpotPub          Node 1              Node 2           SpotSub
  (Node 1)        (worker)            (worker)          (Node 2)
    |                |                   |                  |
    | -- publish --> |                   |                  |
    |   (inproc)     |                   |                  |
    |                | -- PUB ---------> |                  |
    |                |   (tcp mesh)      |                  |
    |                |                   | -- callback ---> |
    |                |                   |    (inproc)      |
```

The worker forks a local publish into two paths:
1. Delivers to SpotSub on the same node (local path above)
2. Sends to remote nodes via mesh PUB socket

The remote node's worker delivers mesh-received messages to its own SpotSub only;
it **never re-publishes to the mesh** (loop prevention).

### Full topology overview

```
+------------- Node 1 -------------+     +------------- Node 2 -------------+
|                                   |     |                                   |
|  SpotPub --> worker --> SpotSub   |     |  SpotPub --> worker --> SpotSub   |
|                 |                 |     |                 ^                 |
|                 | PUB             |     |            SUB  |                 |
|                 +---- tcp --------+---->+------------------                 |
|                 ^                 |     |                 |                 |
|            SUB  |                 |     |                 | PUB             |
|                 +---- tcp --------+<----+------------------                 |
|                                   |     |                                   |
+-----------------------------------+     +-----------------------------------+
```

- Each node's worker sends via **PUB socket** and receives from other nodes via **SUB socket**
- Only local publishes enter the mesh; remote receives are never re-published (loop prevention)
- When Discovery is attached, this mesh topology is configured automatically

## 3. SPOT Node Setup

### 3.1 Discovery-Based Automatic Mesh

=== "C"

    ```c
    void *ctx = zlink_ctx_new();

    /* Discovery setup (peer discovery + registry uplink / heartbeat owner) */
    void *discovery = zlink_discovery_new(ctx,
        ZLINK_SERVICE_TYPE_SPOT, "spot-node");
    zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

    /* SPOT Node setup */
    void *node = zlink_spot_node_new(ctx);
    zlink_spot_node_bind(node, "tcp://*:9000");

    /* Attach Discovery */
    zlink_spot_node_attach_discovery(node, discovery);
    ```

=== "C++"

    ```cpp
    auto ctx = zlink::context();
    auto discovery = zlink::discovery(ctx, zlink::service_type::spot, "spot-node");
    discovery.connect_registry("tcp://registry1:5551");

    auto node = zlink::spot_node(ctx);
    node.bind("tcp://*:9000");
    node.attach_discovery(discovery);
    ```

=== "Java"

    ```java
    var ctx = Zlink.contextNew();
    var discovery = ctx.discoveryNew(ServiceType.SPOT, "spot-node");
    discovery.connectRegistry("tcp://registry1:5551");

    var node = ctx.spotNodeNew();
    node.bind("tcp://*:9000");
    node.attachDiscovery(discovery);
    ```

=== "Python"

    ```python
    ctx = zlink.Context()
    discovery = zlink.Discovery(ctx, zlink.SERVICE_TYPE_SPOT, "spot-node")
    discovery.connect_registry("tcp://registry1:5551")

    node = zlink.SpotNode(ctx)
    node.bind("tcp://*:9000")
    node.attach_discovery(discovery)
    ```

=== "Node/TypeScript"

    ```typescript
    const ctx = new zlink.Context();
    const discovery = new zlink.Discovery(ctx, zlink.SERVICE_TYPE_SPOT, "spot-node");
    discovery.connectRegistry("tcp://registry1:5551");

    const node = new zlink.SpotNode(ctx);
    node.bind("tcp://*:9000");
    node.attachDiscovery(discovery);
    ```

=== "C#/.NET"

    ```csharp
    using var ctx = new ZlinkContext();
    using var discovery = new Discovery(ctx, ServiceType.Spot, "spot-node");
    discovery.ConnectRegistry("tcp://registry1:5551");

    using var node = new SpotNode(ctx);
    node.Bind("tcp://*:9000");
    node.AttachDiscovery(discovery);
    ```

=== "Rust"

    ```rust
    let ctx = zlink::Context::new()?;
    let discovery = zlink::Discovery::new(&ctx, zlink::ServiceType::Spot, "spot-node")?;
    discovery.connect_registry("tcp://registry1:5551")?;

    let node = zlink::SpotNode::new(&ctx)?;
    node.bind("tcp://*:9000")?;
    node.attach_discovery(&discovery)?;
    ```

> `SpotNode` is the topology and lifecycle owner for mesh participation.
> It does not expose the generic data-plane facade (publish/subscribe).
> For publish/subscribe, create a facade with `zlink_spot_new(node)`.

**Note:** It is recommended to call `attach_discovery()` after bind.
Once Discovery is attached, peers are automatically discovered and
connected through the Registry.

**Ephemeral port:** `zlink_spot_node_bind()` supports port 0 for dynamic
port allocation. Use `zlink_spot_node_status_snapshot()` to retrieve the
actual assigned endpoint from `local_endpoint`:

=== "C"

    ```c
    zlink_spot_node_bind(node, "tcp://127.0.0.1:0");
    zlink_spot_node_status_t status;
    zlink_spot_node_status_snapshot(node, &status);
    /* status.local_endpoint contains e.g. "tcp://127.0.0.1:43521" */
    ```

=== "C++"

    ```cpp
    node.bind("tcp://127.0.0.1:0");
    auto status = node.status_snapshot();
    // status.local_endpoint contains e.g. "tcp://127.0.0.1:43521"
    ```

=== "Java"

    ```java
    node.bind("tcp://127.0.0.1:0");
    var status = node.statusSnapshot();
    // status.localEndpoint() contains e.g. "tcp://127.0.0.1:43521"
    ```

=== "Python"

    ```python
    node.bind("tcp://127.0.0.1:0")
    status = node.status_snapshot()
    # status.local_endpoint contains e.g. "tcp://127.0.0.1:43521"
    ```

=== "Node/TypeScript"

    ```typescript
    node.bind("tcp://127.0.0.1:0");
    const status = node.statusSnapshot();
    // status.localEndpoint contains e.g. "tcp://127.0.0.1:43521"
    ```

=== "C#/.NET"

    ```csharp
    node.Bind("tcp://127.0.0.1:0");
    var status = node.StatusSnapshot();
    // status.LocalEndpoint contains e.g. "tcp://127.0.0.1:43521"
    ```

=== "Rust"

    ```rust
    node.bind("tcp://127.0.0.1:0")?;
    let status = node.status_snapshot()?;
    // status.local_endpoint contains e.g. "tcp://127.0.0.1:43521"
    ```

### 3.2 Manual Mesh

=== "C"

    ```c
    void *node = zlink_spot_node_new(ctx);
    zlink_spot_node_bind(node, "tcp://*:9000");

    /* Directly connect to other nodes' PUB */
    zlink_spot_node_connect_peer(node, "tcp://node2:9000");
    zlink_spot_node_connect_peer(node, "tcp://node3:9000");
    ```

=== "C++"

    ```cpp
    auto node = zlink::spot_node(ctx);
    node.bind("tcp://*:9000");
    node.connect_peer("tcp://node2:9000");
    node.connect_peer("tcp://node3:9000");
    ```

=== "Java"

    ```java
    var node = ctx.spotNodeNew();
    node.bind("tcp://*:9000");
    node.connectPeer("tcp://node2:9000");
    node.connectPeer("tcp://node3:9000");
    ```

=== "Python"

    ```python
    node = zlink.SpotNode(ctx)
    node.bind("tcp://*:9000")
    node.connect_peer("tcp://node2:9000")
    node.connect_peer("tcp://node3:9000")
    ```

=== "Node/TypeScript"

    ```typescript
    const node = new zlink.SpotNode(ctx);
    node.bind("tcp://*:9000");
    node.connectPeer("tcp://node2:9000");
    node.connectPeer("tcp://node3:9000");
    ```

=== "C#/.NET"

    ```csharp
    using var node = new SpotNode(ctx);
    node.Bind("tcp://*:9000");
    node.ConnectPeer("tcp://node2:9000");
    node.ConnectPeer("tcp://node3:9000");
    ```

=== "Rust"

    ```rust
    let node = zlink::SpotNode::new(&ctx)?;
    node.bind("tcp://*:9000")?;
    node.connect_peer("tcp://node2:9000")?;
    node.connect_peer("tcp://node3:9000")?;
    ```

**Note:** In a manual mesh there is no Discovery, so there is no registry
topology visibility. This is an intended limitation.

## 4. Unified SPOT Usage

### 4.1 Create a unified handle

=== "C"

    ```c
    void *spot = zlink_spot_new(node);
    ```

=== "C++"

    ```cpp
    auto spot = zlink::spot(node);
    ```

=== "Java"

    ```java
    var spot = node.spotNew();
    ```

=== "Python"

    ```python
    spot = zlink.Spot(node)
    ```

=== "Node/TypeScript"

    ```typescript
    const spot = new zlink.Spot(node);
    ```

=== "C#/.NET"

    ```csharp
    using var spot = new Spot(node);
    ```

=== "Rust"

    ```rust
    let spot = zlink::Spot::new(&node)?;
    ```

`zlink_spot_new(node)` creates a unified facade that borrows an existing
spot node. It provides both publish and subscribe behavior. There are no
public standalone `spot_pub` / `spot_sub` constructors.

Transport security is not configured through unified `spot`. If the service
must use `tls://` or `wss://`, configure TLS on the backing `SpotNode`
first. The internal `inproc` linkage inside unified `spot` is not a TLS
surface.

### 4.2 Publishing

=== "C"

    ```c
    zlink_msg_t part;
    zlink_msg_init_size(&part, 11);
    memcpy(zlink_msg_data(&part), "hello world", 11);
    zlink_publish(spot, "chat:room1:message", &part, 1, 0);
    ```

=== "C++"

    ```cpp
    zlink::msg part(11);
    std::memcpy(part.data(), "hello world", 11);
    spot.publish("chat:room1:message", &part, 1, 0);
    ```

=== "Java"

    ```java
    byte[] data = "hello world".getBytes();
    spot.publish("chat:room1:message", data);
    ```

=== "Python"

    ```python
    spot.publish("chat:room1:message", b"hello world")
    ```

=== "Node/TypeScript"

    ```typescript
    spot.publish("chat:room1:message", Buffer.from("hello world"));
    ```

=== "C#/.NET"

    ```csharp
    spot.Publish("chat:room1:message", "hello world"u8.ToArray());
    ```

=== "Rust"

    ```rust
    spot.publish("chat:room1:message", b"hello world")?;
    ```

### 4.3 Subscribing and unsubscribing

=== "C"

    ```c
    zlink_set_subscription(spot, "chat:room1:message");
    zlink_set_subscription(spot, "chat:room1:*");

    zlink_unset_subscription(spot, "chat:room1:message");
    zlink_unset_subscription(spot, "chat:room1:*");
    ```

=== "C++"

    ```cpp
    spot.set_subscription("chat:room1:message");
    spot.set_subscription("chat:room1:*");

    spot.unset_subscription("chat:room1:message");
    spot.unset_subscription("chat:room1:*");
    ```

=== "Java"

    ```java
    spot.setSubscription("chat:room1:message");
    spot.setSubscription("chat:room1:*");

    spot.unsetSubscription("chat:room1:message");
    spot.unsetSubscription("chat:room1:*");
    ```

=== "Python"

    ```python
    spot.set_subscription("chat:room1:message")
    spot.set_subscription("chat:room1:*")

    spot.unset_subscription("chat:room1:message")
    spot.unset_subscription("chat:room1:*")
    ```

=== "Node/TypeScript"

    ```typescript
    spot.setSubscription("chat:room1:message");
    spot.setSubscription("chat:room1:*");

    spot.unsetSubscription("chat:room1:message");
    spot.unsetSubscription("chat:room1:*");
    ```

=== "C#/.NET"

    ```csharp
    spot.SetSubscription("chat:room1:message");
    spot.SetSubscription("chat:room1:*");

    spot.UnsetSubscription("chat:room1:message");
    spot.UnsetSubscription("chat:room1:*");
    ```

=== "Rust"

    ```rust
    spot.set_subscription("chat:room1:message")?;
    spot.set_subscription("chat:room1:*")?;

    spot.unset_subscription("chat:room1:message")?;
    spot.unset_subscription("chat:room1:*")?;
    ```

### 4.4 Receiving Messages

Both `SpotNode` and unified `Spot` start in **recv model**. You can either
pull messages directly or switch the receive surface once to **callback mode**.
Send-ready remains a separate axis.

#### Recv model (default)

In recv model, pull messages with `zlink_subscribe()`.

=== "C"

    ```c
    void *spot = zlink_spot_new(node);
    zlink_set_subscription(spot, "chat:room1:message");

    /* Pull next message */
    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic_buf[256];
    size_t topic_len = sizeof(topic_buf);
    int rc = zlink_subscribe(spot, &source_rid, &parts, &part_count,
                                  topic_buf, &topic_len, 0);
    if (rc == 0) {
        printf("Topic: %.*s, Parts: %zu\n",
               (int)topic_len, topic_buf, part_count);
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    auto spot = zlink::spot(node);
    spot.set_subscription("chat:room1:message");

    auto [topic, parts] = spot.subscribe();
    std::println("Topic: {}, Parts: {}", topic, parts.size());
    ```

=== "Java"

    ```java
    var spot = node.spotNew();
    spot.setSubscription("chat:room1:message");

    var msg = spot.subscribe();
    System.out.printf("Topic: %s, Parts: %d%n", msg.topic(), msg.partCount());
    ```

=== "Python"

    ```python
    spot = zlink.Spot(node)
    spot.set_subscription("chat:room1:message")

    topic, parts = spot.subscribe()
    print(f"Topic: {topic}, Parts: {len(parts)}")
    ```

=== "Node/TypeScript"

    ```typescript
    const spot = new zlink.Spot(node);
    spot.setSubscription("chat:room1:message");

    const { topic, parts } = spot.subscribe();
    console.log(`Topic: ${topic}, Parts: ${parts.length}`);
    ```

=== "C#/.NET"

    ```csharp
    using var spot = new Spot(node);
    spot.SetSubscription("chat:room1:message");

    var (topic, parts) = spot.Subscribe();
    Console.WriteLine($"Topic: {topic}, Parts: {parts.Length}");
    ```

=== "Rust"

    ```rust
    let spot = zlink::Spot::new(&node)?;
    spot.set_subscription("chat:room1:message")?;

    let (topic, parts) = spot.subscribe()?;
    println!("Topic: {}, Parts: {}", topic, parts.len());
    ```

#### Callback model

Install the callback with `zlink_subscribe_handler()` to make a one-way
transition from recv model to callback model. Incoming messages are then
dispatched automatically through that callback.

=== "C"

    ```c
    /* Define callback function */
    void on_message(const zlink_routing_id_t *source_rid,
                    const char *topic, size_t topic_len,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        printf("Topic: %.*s, Parts: %zu\n", (int)topic_len, topic, part_count);
    }

    /* Register handler at unified spot creation */
    void *spot = zlink_spot_new(node);
    zlink_subscribe_handler(spot, on_message, NULL);
    ```

=== "C++"

    ```cpp
    auto spot = zlink::spot(node);
    spot.subscribe_handler([](const auto& source_rid,
                              std::string_view topic,
                              std::span<zlink::msg> parts) {
        std::println("Topic: {}, Parts: {}", topic, parts.size());
    });
    ```

=== "Java"

    ```java
    var spot = node.spotNew();
    spot.subscribeHandler((sourceRid, topic, parts) -> {
        System.out.printf("Topic: %s, Parts: %d%n", topic, parts.length);
    });
    ```

=== "Python"

    ```python
    spot = zlink.Spot(node)

    def on_message(source_rid, topic, parts):
        print(f"Topic: {topic}, Parts: {len(parts)}")

    spot.subscribe_handler(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    const spot = new zlink.Spot(node);
    spot.subscribeHandler((sourceRid, topic, parts) => {
        console.log(`Topic: ${topic}, Parts: ${parts.length}`);
    });
    ```

=== "C#/.NET"

    ```csharp
    using var spot = new Spot(node);
    spot.SubscribeHandler((sourceRid, topic, parts) => {
        Console.WriteLine($"Topic: {topic}, Parts: {parts.Length}");
    });
    ```

=== "Rust"

    ```rust
    let spot = zlink::Spot::new(&node)?;
    spot.subscribe_handler(|source_rid, topic, parts| {
        println!("Topic: {}, Parts: {}", topic, parts.len());
    });
    ```

**Important:** A single `spot` / `spot_node` handle can be used concurrently
from multiple threads (thread-safe). `publish` is the concurrent hot path, while
subscribe/unsubscribe/attach/peer-connect/monitor calls remain valid runtime
control-path operations. Callbacks still run on the I/O path, so slow work
should be offloaded to an application queue or worker thread.

**Constraints:**

- In recv model, use `zlink_subscribe()`
- Call `zlink_subscribe_handler()` to transition the receive surface once to callback mode
- In receive callback mode, `zlink_subscribe()` and data-plane `ZLINK_POLLIN` fail with `EBUSY`
- `zlink_send_ready_handler()` is independent from receive callback mode
- After send-ready attach, data-plane `ZLINK_POLLOUT` fails with `EBUSY`
- Replacing or clearing the callback after transition is not supported
- Callbacks are invoked on the socket dispatch / I/O path
- Blocking work in the callback can delay other I/O
- For slow processing, enqueue from the callback and handle it on your own thread
- `destroy` uses a fail-fast lifecycle gate, so the simplest pattern is to
  stop external use first and then tear down the handle

> See [Thread-Safety Guide](11-thread-safety.md) for the full three-tier contract and additional patterns.

## 5. Topic Rules

### Naming Convention

The recommended format is `<domain>:<entity>:<action>`.

Examples:
- `chat:room1:message`
- `metrics:zone1:cpu`
- `game:world1:player_move`

### Pattern Subscription Rules

- Only one `*` is allowed, and it must be at the end of the string
- Case-sensitive
- Example: `chat:*` matches both `chat:room1:message` and `chat:room2:join`

## Internal Module Structure

The SPOT internal implementation has a modular structure with separated
data plane and control plane. The public C API remains unchanged;
internal changes stay within narrow boundaries.

| Module | Role |
|--------|------|
| `spot_node_access` · `spot_subject_access` | API layer seam (service-local access) |
| `spot_handle` | Public handle struct (tag validation, pub/sub refs, pending defaults) |
| `spot_node` | SpotNode orchestration, discovery integration |
| `spot_pub` | Publish path |
| `spot_sub` | Subscribe path (option · recv separated) |
| `spot_data_plane` | Data plane core |
| `spot_data_plane_forwarding` | Ingress/egress message forwarding |
| `spot_data_plane_protocol` | Control messages, subscription updates, bootstrap |
| `spot_runtime` | Runtime lifecycle |

Multipart publish uses the shared `multipart_send_txn` module to provide
whole-message guarantees (all-or-nothing).

## 6. Delivery Policy

- Local publish (`spot`) distributes to local SPOT Subs + sends out via PUB (remote propagation)
- Remote receive (SUB) distributes to local SPOT Subs only (no re-publishing)
- No re-publishing prevents message loops and duplicates
- `subscribe()` / `unsubscribe()` return means the local socket filter has been applied;
  it does not guarantee cluster-wide propagation
- Message ordering is preserved within a single `spot` handle
- Global ordering across different `spot` handles is not guaranteed
- If both an exact topic and a pattern match the same message on the same subscriber,
  the message is delivered only once

SPOT is a live pub/sub system. It does not guarantee durable delivery,
ack/retry, exactly-once semantics, or past message replay for late joiners.

## 7. Cleanup

=== "C"

    ```c
    zlink_spot_destroy(&spot);
    zlink_spot_node_destroy(&node);
    zlink_discovery_destroy(&discovery);
    ```

=== "C++"

    ```cpp
    spot.close();
    node.close();
    discovery.close();
    ```

=== "Java"

    ```java
    spot.destroy();
    node.destroy();
    discovery.destroy();
    ```

=== "Python"

    ```python
    spot.destroy()
    node.destroy()
    discovery.destroy()
    ```

=== "Node/TypeScript"

    ```typescript
    spot.destroy();
    node.destroy();
    discovery.destroy();
    ```

=== "C#/.NET"

    ```csharp
    spot.Dispose();
    node.Dispose();
    discovery.Dispose();
    ```

=== "Rust"

    ```rust
    spot.destroy()?;
    node.destroy()?;
    discovery.destroy()?;
    ```

**Destroy order:** Destroy `spot` first, then `SpotNode`, and finally
`Discovery`. All external use of `spot` must stop before calling
`SpotNode` destroy.

> `zlink_spot_destroy()` only releases the borrowed facade. The backing
> `SpotNode` remains the lifecycle owner. For discovery-attached spot
> nodes, `zlink_discovery_destroy()` cascades shutdown to attached
> participants.

---
[← Discovery](07-1-discovery.md) | [Registry →](07-4-registry.md) | [Routing ID →](08-routing-id.md)
