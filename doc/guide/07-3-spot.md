[English](07-3-spot.md) | [한국어](07-3-spot.ko.md)

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

```c
void *ctx = zlink_ctx_new();

/* Discovery setup (peer discovery + registry uplink / heartbeat owner) */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_SPOT);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

/* SPOT Node setup */
void *node = zlink_spot_node_new(ctx, "spot-node");
zlink_spot_node_bind(node, "tcp://*:9000");

/* Attach Discovery */
zlink_spot_node_attach_discovery(node, discovery);
```

**Note:** It is recommended to call `attach_discovery()` after bind.
Once Discovery is attached, peers are automatically discovered and
connected through the Registry.

### 3.2 Manual Mesh

```c
void *node = zlink_spot_node_new(ctx, "spot-node");
zlink_spot_node_bind(node, "tcp://*:9000");

/* Directly connect to other nodes' PUB */
zlink_spot_node_connect_peer_pub(node, "tcp://node2:9000");
zlink_spot_node_connect_peer_pub(node, "tcp://node3:9000");
```

**Note:** In a manual mesh there is no Discovery, so there is no registry
topology visibility. This is an intended limitation.

## 4. Unified SPOT Usage

### 4.1 Create a unified handle

```c
void *spot = zlink_spot_new(node);
```

`zlink_spot_new()` returns a unified facade with both publish and subscribe
behavior. There are no public standalone `spot_pub` / `spot_sub` constructors.

### 4.2 Publishing

```c
zlink_msg_t part;
zlink_msg_init_size(&part, 11);
memcpy(zlink_msg_data(&part), "hello world", 11);
zlink_publish(spot, "chat:room1:message", &part, 1, 0);
```

### 4.3 Subscribing and unsubscribing

```c
zlink_subscribe(spot, "chat:room1:message");
zlink_subscribe(spot, "chat:room1:*");

zlink_unsubscribe(spot, "chat:room1:message");
zlink_unsubscribe(spot, "chat:room1:*");
```

### 4.4 Receiving Messages

Both `SpotNode` and unified `Spot` start in **recv model**. You can either
pull messages directly or switch once to **callback model**. The two models
are mutually exclusive for the lifetime of the handle.

#### Recv model (default)

In recv model, pull messages with `zlink_subscribe_recv()`.

```c
void *spot = zlink_spot_new(node);
zlink_subscribe(spot, "chat:room1:message");

/* Pull next message */
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
char topic_buf[256];
size_t topic_len = sizeof(topic_buf);
int rc = zlink_subscribe_recv(spot, &source_rid, &parts, &part_count,
                              topic_buf, &topic_len, 0);
if (rc == 0) {
    printf("Topic: %.*s, Parts: %zu\n",
           (int)topic_len, topic_buf, part_count);
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

#### Callback model

Install the callback with `zlink_subscribe_handler()` to make a one-way
transition from recv model to callback model. Incoming messages are then
dispatched automatically through that callback.

```c
/* Define callback function */
void on_message(const zlink_routing_id_t *source_rid,
                const char *topic, size_t topic_len,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    printf("Topic: %.*s, Parts: %zu\n", (int)topic_len, topic, part_count);
}

/* Register handler at spot_node creation */
void *node = zlink_spot_node_new(ctx, "spot-node");
zlink_subscribe_handler(node, on_message, NULL);

/* Or register handler at unified spot creation */
void *spot = zlink_spot_new(node);
zlink_subscribe_handler(spot, on_message, NULL);
```

**Important:** A single `spot` / `spot_node` handle can be used concurrently
from multiple threads (thread-safe). `publish` is the concurrent hot path, while
subscribe/unsubscribe/attach/peer-connect/monitor calls remain valid runtime
control-path operations. Callbacks still run on the I/O path, so slow work
should be offloaded to an application queue or worker thread.

**Constraints:**

- In recv model, use `zlink_subscribe_recv()`
- Call `zlink_subscribe_handler()` to transition once to callback model
- In callback model, `recv()` calls fail with `EBUSY`
- In recv model, `send_ready_handler()` fails with `EBUSY`
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

```c
zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
zlink_discovery_destroy(&discovery);
```

**Destroy order:** Destroy `spot` first, then `SpotNode`, and finally
`Discovery`. All external use of `spot` must stop before calling
`SpotNode` destroy.

---
[← Gateway](07-2-gateway.md) | [Registry →](07-4-registry.md) | [Routing ID →](08-routing-id.md)
