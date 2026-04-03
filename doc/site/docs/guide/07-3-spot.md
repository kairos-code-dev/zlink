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

=== "Go"

    ```go
    ctx, err := zlink.NewContext()
    if err != nil { log.Fatal(err) }
    discovery, err := zlink.NewDiscovery(ctx, zlink.ServiceTypeSpot, "spot-node")
    if err != nil { log.Fatal(err) }
    discovery.ConnectRegistry("tcp://registry1:5551")

    node, err := ctx.SpotNode()
    if err != nil { log.Fatal(err) }
    node.Bind("tcp://*:9000")
    node.AttachDiscovery(discovery)
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

=== "Go"

    ```go
    node.Bind("tcp://127.0.0.1:0")
    status, _ := node.StatusSnapshot()
    // status.LocalEndpoint contains e.g. "tcp://127.0.0.1:43521"
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

=== "Go"

    ```go
    node, err := ctx.SpotNode()
    if err != nil { log.Fatal(err) }
    node.Bind("tcp://*:9000")
    node.ConnectPeer("tcp://node2:9000")
    node.ConnectPeer("tcp://node3:9000")
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

=== "Go"

    ```go
    spot, err := node.Spot()
    if err != nil { log.Fatal(err) }
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

=== "Go"

    ```go
    spot.Publish("chat:room1:message", zlink.NewMessage([]byte("hello world")))
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

=== "Go"

    ```go
    spot.SetSubscription("chat:room1:message")
    spot.SetSubscription("chat:room1:*")

    spot.UnsetSubscription("chat:room1:message")
    spot.UnsetSubscription("chat:room1:*")
    ```

### 4.4 Receiving Messages

Both `SpotNode` and unified `Spot` start in **recv model**. You can either
pull messages directly or switch the receive surface once to **callback mode**.
Send-ready remains a separate axis.

#### Recv model (default)

In recv model, pull messages with `zlink_subscribe()`.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        /* Publisher node */
        void *pub_node = zlink_spot_node_new(ctx);
        zlink_spot_node_bind(pub_node, "tcp://127.0.0.1:0");
        zlink_spot_node_status_t status;
        zlink_spot_node_status_snapshot(pub_node, &status);

        /* Subscriber node -- connect to publisher */
        void *sub_node = zlink_spot_node_new(ctx);
        zlink_spot_node_bind(sub_node, "tcp://127.0.0.1:0");
        zlink_spot_node_connect_peer(sub_node, status.local_endpoint);

        void *publisher = zlink_spot_new(pub_node);
        void *subscriber = zlink_spot_new(sub_node);

        zlink_set_subscription(subscriber, "chat:room1:message");
        zlink_msleep(100);  /* wait for subscription to propagate */

        /* Publish */
        zlink_msg_t part;
        zlink_msg_init_size(&part, 11);
        memcpy(zlink_msg_data(&part), "hello world", 11);
        zlink_publish(publisher, "chat:room1:message", &part, 1, 0);

        /* Receive */
        zlink_routing_id_t source_rid;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        char topic_buf[256];
        size_t topic_len = sizeof(topic_buf);
        int rc = zlink_subscribe(subscriber, &source_rid, &parts, &part_count,
                                      topic_buf, &topic_len, 0);
        if (rc == 0) {
            printf("Topic: %.*s Data: %.*s\n",
                   (int)topic_len, topic_buf,
                   (int)zlink_msg_size(&parts[0]),
                   (char *)zlink_msg_data(&parts[0]));
            for (size_t i = 0; i < part_count; i++)
                zlink_msg_close(&parts[i]);
        }

        zlink_spot_destroy(&subscriber);
        zlink_spot_destroy(&publisher);
        zlink_spot_node_destroy(&sub_node);
        zlink_spot_node_destroy(&pub_node);
        zlink_ctx_term(ctx);
        return 0;
    }
    ```

=== "C++"

    ```cpp
    #include <zlink/services/spot.hpp>
    #include <iostream>
    #include <thread>

    int main()
    {
        zlink::context_t ctx;

        zlink::spot_node pub_node(ctx);
        pub_node.bind("tcp://127.0.0.1:0");
        auto pub_status = pub_node.status_snapshot();

        zlink::spot_node sub_node(ctx);
        sub_node.bind("tcp://127.0.0.1:0");
        sub_node.connect_peer(pub_status.local_endpoint);

        auto publisher = zlink::spot(pub_node);
        auto subscriber = zlink::spot(sub_node);

        subscriber.set_subscription("chat:room1:message");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        publisher.publish("chat:room1:message", "hello world");

        auto [topic, parts] = subscriber.subscribe();
        std::cout << "Topic: " << topic
                  << " Data: " << parts[0].str() << std::endl;

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class SpotRecvExample {
        public static void main(String[] args) throws Exception {
            var ctx = Zlink.contextNew();

            var pubNode = ctx.spotNodeNew();
            pubNode.bind("tcp://127.0.0.1:0");
            var pubStatus = pubNode.statusSnapshot();

            var subNode = ctx.spotNodeNew();
            subNode.bind("tcp://127.0.0.1:0");
            subNode.connectPeer(pubStatus.localEndpoint());

            var publisher = pubNode.spotNew();
            var subscriber = subNode.spotNew();

            subscriber.setSubscription("chat:room1:message");
            Thread.sleep(100);

            publisher.publish("chat:room1:message", "hello world".getBytes());

            var msg = subscriber.subscribe();
            System.out.printf("Topic: %s Data: %s%n",
                msg.topic(), new String(msg.parts()[0].data()));

            subscriber.destroy();
            publisher.destroy();
            subNode.destroy();
            pubNode.destroy();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink
    import time

    ctx = zlink.Context()

    pub_node = zlink.SpotNode(ctx)
    pub_node.bind("tcp://127.0.0.1:0")
    pub_status = pub_node.status_snapshot()

    sub_node = zlink.SpotNode(ctx)
    sub_node.bind("tcp://127.0.0.1:0")
    sub_node.connect_peer(pub_status.local_endpoint)

    publisher = zlink.Spot(pub_node)
    subscriber = zlink.Spot(sub_node)

    subscriber.set_subscription("chat:room1:message")
    time.sleep(0.1)

    publisher.publish("chat:room1:message", b"hello world")

    topic, parts = subscriber.subscribe()
    print(f"Topic: {topic} Data: {parts[0].decode()}")

    subscriber.destroy()
    publisher.destroy()
    sub_node.destroy()
    pub_node.destroy()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    const pubNode = new zlink.SpotNode(ctx);
    pubNode.bind('tcp://127.0.0.1:0');
    const pubStatus = pubNode.statusSnapshot();

    const subNode = new zlink.SpotNode(ctx);
    subNode.bind('tcp://127.0.0.1:0');
    subNode.connectPeer(pubStatus.localEndpoint);

    const publisher = new zlink.Spot(pubNode);
    const subscriber = new zlink.Spot(subNode);

    subscriber.setSubscription('chat:room1:message');
    await new Promise(r => setTimeout(r, 100));

    publisher.publish('chat:room1:message', Buffer.from('hello world'));

    const { topic, parts } = subscriber.subscribe();
    console.log(`Topic: ${topic} Data: ${parts[0].toString()}`);

    subscriber.destroy();
    publisher.destroy();
    subNode.destroy();
    pubNode.destroy();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new ZlinkContext();

    using var pubNode = new SpotNode(ctx);
    pubNode.Bind("tcp://127.0.0.1:0");
    var pubStatus = pubNode.StatusSnapshot();

    using var subNode = new SpotNode(ctx);
    subNode.Bind("tcp://127.0.0.1:0");
    subNode.ConnectPeer(pubStatus.LocalEndpoint);

    using var publisher = new Spot(pubNode);
    using var subscriber = new Spot(subNode);

    subscriber.SetSubscription("chat:room1:message");
    Thread.Sleep(100);

    publisher.Publish("chat:room1:message", "hello world"u8.ToArray());

    var (topic, parts) = subscriber.Subscribe();
    Console.WriteLine($"Topic: {topic} Data: {parts[0].GetString()}");
    ```

=== "Rust"

    ```rust
    use zlink::Context;
    use std::thread;
    use std::time::Duration;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new()?;

        let pub_node = zlink::SpotNode::new(&ctx)?;
        pub_node.bind("tcp://127.0.0.1:0")?;
        let pub_status = pub_node.status_snapshot()?;

        let sub_node = zlink::SpotNode::new(&ctx)?;
        sub_node.bind("tcp://127.0.0.1:0")?;
        sub_node.connect_peer(&pub_status.local_endpoint)?;

        let publisher = zlink::Spot::new(&pub_node)?;
        let subscriber = zlink::Spot::new(&sub_node)?;

        subscriber.set_subscription("chat:room1:message")?;
        thread::sleep(Duration::from_millis(100));

        publisher.publish("chat:room1:message", b"hello world")?;

        let (topic, parts) = subscriber.subscribe()?;
        println!("Topic: {} Data: {}", topic,
                 String::from_utf8_lossy(parts[0].data()));

        Ok(())
    }
    ```

=== "Go"

    ```go
    package main

    import (
        "fmt"
        "log"
        "time"
        "github.com/kairos-code-dev/zlink-go"
    )

    func main() {
        ctx, err := zlink.NewContext()
        if err != nil { log.Fatal(err) }

        pubNode, _ := ctx.SpotNode()
        pubNode.Bind("tcp://127.0.0.1:0")
        pubStatus, _ := pubNode.StatusSnapshot()

        subNode, _ := ctx.SpotNode()
        subNode.Bind("tcp://127.0.0.1:0")
        subNode.ConnectPeer(pubStatus.LocalEndpoint)

        publisher, _ := pubNode.Spot()
        subscriber, _ := subNode.Spot()

        subscriber.SetSubscription("chat:room1:message")
        time.Sleep(100 * time.Millisecond)

        publisher.Publish("chat:room1:message",
            zlink.NewMessage([]byte("hello world")))

        topic, parts, err := subscriber.Subscribe()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Topic: %s Data: %s\n", topic, string(parts[0].Data()))

        subscriber.Destroy()
        publisher.Destroy()
        subNode.Destroy()
        pubNode.Destroy()
    }
    ```

??? example "Full Sample Code"

    | Language | Source |
    |----------|--------|
    | C | [spot_recv_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/spot_recv_sample.c) |
    | C++ | [spot_recv_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/spot_recv_sample.cpp) |
    | Java | [SpotRecvSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/SpotRecvSample.java) |
    | Python | [spot_recv.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/spot_recv.py) |
    | Node | [spot_recv_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/spot_recv_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/SpotRecv/Program.cs) |
    | Rust | [spot_recv_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/spot_recv_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/spot_recv_sample/main.go) |

#### Callback model

Install the callback with `zlink_subscribe_handler()` to make a one-way
transition from recv model to callback model. Incoming messages are then
dispatched automatically through that callback.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    void on_message(const zlink_routing_id_t *source_rid,
                    const char *topic, size_t topic_len,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        printf("Topic: %.*s Data: %.*s\n",
               (int)topic_len, topic,
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
    }

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        void *pub_node = zlink_spot_node_new(ctx);
        zlink_spot_node_bind(pub_node, "tcp://127.0.0.1:0");
        zlink_spot_node_status_t status;
        zlink_spot_node_status_snapshot(pub_node, &status);

        void *sub_node = zlink_spot_node_new(ctx);
        zlink_spot_node_bind(sub_node, "tcp://127.0.0.1:0");
        zlink_spot_node_connect_peer(sub_node, status.local_endpoint);

        void *publisher = zlink_spot_new(pub_node);
        void *subscriber = zlink_spot_new(sub_node);

        zlink_set_subscription(subscriber, "chat:room1:*");
        zlink_subscribe_handler(subscriber, on_message, NULL);
        zlink_msleep(100);

        zlink_msg_t part;
        zlink_msg_init_size(&part, 11);
        memcpy(zlink_msg_data(&part), "hello world", 11);
        zlink_publish(publisher, "chat:room1:message", &part, 1, 0);

        zlink_msleep(200);  /* let callback fire */

        zlink_spot_destroy(&subscriber);
        zlink_spot_destroy(&publisher);
        zlink_spot_node_destroy(&sub_node);
        zlink_spot_node_destroy(&pub_node);
        zlink_ctx_term(ctx);
        return 0;
    }
    ```

=== "C++"

    ```cpp
    #include <zlink/services/spot.hpp>
    #include <iostream>
    #include <thread>

    int main()
    {
        zlink::context_t ctx;

        zlink::spot_node pub_node(ctx);
        pub_node.bind("tcp://127.0.0.1:0");
        auto pub_status = pub_node.status_snapshot();

        zlink::spot_node sub_node(ctx);
        sub_node.bind("tcp://127.0.0.1:0");
        sub_node.connect_peer(pub_status.local_endpoint);

        auto publisher = zlink::spot(pub_node);
        auto subscriber = zlink::spot(sub_node);

        subscriber.set_subscription("chat:room1:*");
        subscriber.subscribe_handler([](const auto& source_rid,
                                        std::string_view topic,
                                        std::span<zlink::msg> parts) {
            std::cout << "Topic: " << topic
                      << " Data: " << parts[0].str() << std::endl;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        publisher.publish("chat:room1:message", "hello world");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class SpotCallbackExample {
        public static void main(String[] args) throws Exception {
            var ctx = Zlink.contextNew();

            var pubNode = ctx.spotNodeNew();
            pubNode.bind("tcp://127.0.0.1:0");
            var pubStatus = pubNode.statusSnapshot();

            var subNode = ctx.spotNodeNew();
            subNode.bind("tcp://127.0.0.1:0");
            subNode.connectPeer(pubStatus.localEndpoint());

            var publisher = pubNode.spotNew();
            var subscriber = subNode.spotNew();

            subscriber.setSubscription("chat:room1:*");
            subscriber.subscribeHandler((sourceRid, topic, parts) -> {
                System.out.printf("Topic: %s Data: %s%n",
                    topic, new String(parts[0].data()));
            });
            Thread.sleep(100);

            publisher.publish("chat:room1:message", "hello world".getBytes());
            Thread.sleep(200);

            subscriber.destroy();
            publisher.destroy();
            subNode.destroy();
            pubNode.destroy();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink
    import time

    ctx = zlink.Context()

    pub_node = zlink.SpotNode(ctx)
    pub_node.bind("tcp://127.0.0.1:0")
    pub_status = pub_node.status_snapshot()

    sub_node = zlink.SpotNode(ctx)
    sub_node.bind("tcp://127.0.0.1:0")
    sub_node.connect_peer(pub_status.local_endpoint)

    publisher = zlink.Spot(pub_node)
    subscriber = zlink.Spot(sub_node)

    subscriber.set_subscription("chat:room1:*")

    def on_message(source_rid, topic, parts):
        print(f"Topic: {topic} Data: {parts[0].decode()}")

    subscriber.subscribe_handler(on_message)
    time.sleep(0.1)

    publisher.publish("chat:room1:message", b"hello world")
    time.sleep(0.2)

    subscriber.destroy()
    publisher.destroy()
    sub_node.destroy()
    pub_node.destroy()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    const pubNode = new zlink.SpotNode(ctx);
    pubNode.bind('tcp://127.0.0.1:0');
    const pubStatus = pubNode.statusSnapshot();

    const subNode = new zlink.SpotNode(ctx);
    subNode.bind('tcp://127.0.0.1:0');
    subNode.connectPeer(pubStatus.localEndpoint);

    const publisher = new zlink.Spot(pubNode);
    const subscriber = new zlink.Spot(subNode);

    subscriber.setSubscription('chat:room1:*');
    subscriber.subscribeHandler((sourceRid, topic, parts) => {
        console.log(`Topic: ${topic} Data: ${parts[0].toString()}`);
    });
    await new Promise(r => setTimeout(r, 100));

    publisher.publish('chat:room1:message', Buffer.from('hello world'));
    await new Promise(r => setTimeout(r, 200));

    subscriber.destroy();
    publisher.destroy();
    subNode.destroy();
    pubNode.destroy();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new ZlinkContext();

    using var pubNode = new SpotNode(ctx);
    pubNode.Bind("tcp://127.0.0.1:0");
    var pubStatus = pubNode.StatusSnapshot();

    using var subNode = new SpotNode(ctx);
    subNode.Bind("tcp://127.0.0.1:0");
    subNode.ConnectPeer(pubStatus.LocalEndpoint);

    using var publisher = new Spot(pubNode);
    using var subscriber = new Spot(subNode);

    subscriber.SetSubscription("chat:room1:*");
    subscriber.SubscribeHandler((sourceRid, topic, parts) => {
        Console.WriteLine($"Topic: {topic} Data: {parts[0].GetString()}");
    });
    Thread.Sleep(100);

    publisher.Publish("chat:room1:message", "hello world"u8.ToArray());
    Thread.Sleep(200);
    ```

=== "Rust"

    ```rust
    use zlink::Context;
    use std::thread;
    use std::time::Duration;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new()?;

        let pub_node = zlink::SpotNode::new(&ctx)?;
        pub_node.bind("tcp://127.0.0.1:0")?;
        let pub_status = pub_node.status_snapshot()?;

        let sub_node = zlink::SpotNode::new(&ctx)?;
        sub_node.bind("tcp://127.0.0.1:0")?;
        sub_node.connect_peer(&pub_status.local_endpoint)?;

        let publisher = zlink::Spot::new(&pub_node)?;
        let subscriber = zlink::Spot::new(&sub_node)?;

        subscriber.set_subscription("chat:room1:*")?;
        subscriber.subscribe_handler(|source_rid, topic, parts| {
            println!("Topic: {} Data: {}",
                     topic, String::from_utf8_lossy(parts[0].data()));
        });
        thread::sleep(Duration::from_millis(100));

        publisher.publish("chat:room1:message", b"hello world")?;
        thread::sleep(Duration::from_millis(200));

        Ok(())
    }
    ```

=== "Go"

    ```go
    package main

    import (
        "fmt"
        "log"
        "time"
        "github.com/kairos-code-dev/zlink-go"
    )

    func main() {
        ctx, err := zlink.NewContext()
        if err != nil { log.Fatal(err) }

        pubNode, _ := ctx.SpotNode()
        pubNode.Bind("tcp://127.0.0.1:0")
        pubStatus, _ := pubNode.StatusSnapshot()

        subNode, _ := ctx.SpotNode()
        subNode.Bind("tcp://127.0.0.1:0")
        subNode.ConnectPeer(pubStatus.LocalEndpoint)

        publisher, _ := pubNode.Spot()
        subscriber, _ := subNode.Spot()

        subscriber.SetSubscription("chat:room1:*")
        subscriber.SubscribeHandler(func(sourceRid zlink.RoutingID, topic string, parts []zlink.Message) {
            fmt.Printf("Topic: %s Data: %s\n", topic, string(parts[0].Data()))
        })
        time.Sleep(100 * time.Millisecond)

        publisher.Publish("chat:room1:message",
            zlink.NewMessage([]byte("hello world")))
        time.Sleep(200 * time.Millisecond)

        subscriber.Destroy()
        publisher.Destroy()
        subNode.Destroy()
        pubNode.Destroy()
    }
    ```

??? example "Full Sample Code"

    | Language | Source |
    |----------|--------|
    | C | [spot_callback_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/spot_callback_sample.c) |
    | C++ | [spot_callback_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/spot_callback_sample.cpp) |
    | Java | [SpotCallbackSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/SpotCallbackSample.java) |
    | Python | [spot_callback.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/spot_callback.py) |
    | Node | [spot_callback_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/spot_callback_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/SpotCallback/Program.cs) |
    | Rust | [spot_callback_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/spot_callback_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/spot_callback_sample/main.go) |

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

=== "Go"

    ```go
    spot.Destroy()
    node.Destroy()
    discovery.Destroy()
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
