[English](07-3-spot.md) | [한국어](07-3-spot.ko.md)

# SPOT Topic PUB/SUB (Location-Transparent Publish/Subscribe)

> This guide reflects the callback-only direct-dispatch surface.
> All receives are dispatched through the handler callback registered at creation time.

## 1. Overview

SPOT is a location-transparent, topic-based publish/subscribe system. It automatically constructs a PUB/SUB Mesh based on Discovery, enabling topic message publishing and subscribing across the entire cluster.

> **About the name**: SPOT derives its name from "spot" (location). Each object (node) publishes topics from its own location and subscribes to topics from other locations, forming an object-level, location-transparent pub/sub mesh system.

### Core Terminology

| Term | Description |
|------|-------------|
| **SPOT Node** | PUB/SUB Mesh participant agent (one per node) |
| **SPOT Pub** | Topic publishing handle (thread-safe by default) |
| **SPOT Sub** | Topic subscription/receive handle |
| **Topic** | String key-based message channel |
| **Pattern** | Prefix + `*` wildcard subscription |
| **Handler** | Callback function automatically invoked on message receipt |

## 2. Architecture

### Single Server

```
┌─────────────────────────────────────────────┐
│                 SPOT Node                    │
│  ┌──────────┐         ┌──────────┐          │
│  │ SpotPub  │         │ SpotSub  │          │
│  │ pub:chat │         │ sub:chat │          │
│  └────┬─────┘         └────▲─────┘          │
│       │    inproc          │    inproc       │
│       v                    │                 │
│  [ data plane worker (proxy forwarding) ]    │
└─────────────────────────────────────────────┘
```

- `SpotPub` publishes via an internal `PUB` facade socket into the data plane
- `SpotSub` receives via an internal `SUB` facade socket from the data plane
- The data plane worker performs local fanout and remote mesh forwarding in proxy style

### Cluster (PUB/SUB Mesh)

```
┌──────────┐     PUB/SUB      ┌──────────┐
│  Node 1  │◄───────────────►│  Node 2  │
│  PUB+SUB │                  │  PUB+SUB │
└──────────┘                  └──────────┘
      ▲                            ▲
      │         PUB/SUB            │
      └────────────────────────────┘

┌──────────┐
│  Node 3  │
│  PUB+SUB │
└──────────┘
```

Each Node's data plane worker performs proxy-style forwarding:
local publishes to the remote mesh, and remote mesh receives to local subscribers.

## 3. SPOT Node Setup

### 3.1 Discovery-Based Automatic Mesh

```c
void *ctx = zlink_ctx_new();

/* Discovery setup (peer discovery + registry uplink / heartbeat owner) */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_SPOT);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");
zlink_discovery_subscribe(discovery, "spot-node");

/* SPOT Node setup (service_name and handler are fixed at creation time) */
void *node = zlink_spot_node_new(ctx, "spot-node", on_message);
zlink_spot_node_bind(node, "tcp://*:9000");

/* Attach Discovery */
zlink_spot_node_attach_discovery(node, discovery);
```

**Note:** It is recommended to call `attach_discovery()` after bind.
Once Discovery is attached, peers are automatically discovered and
connected through the Registry.

### 3.2 Manual Mesh

```c
void *node = zlink_spot_node_new(ctx, "spot-node", on_message);
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
void *spot = zlink_spot_new(node, on_message);
```

`zlink_spot_new()` returns a unified facade with both publish and subscribe
behavior. There are no public standalone `spot_pub` / `spot_sub` constructors.

### 4.2 Publishing

```c
zlink_msg_t part;
zlink_msg_init_size(&part, 11);
memcpy(zlink_msg_data(&part), "hello world", 11);
zlink_spot_publish(spot, "chat:room1:message", &part, 1, 0);
```

### 4.3 Subscribing and unsubscribing

```c
zlink_spot_subscribe(spot, "chat:room1:message");
zlink_spot_subscribe_pattern(spot, "chat:room1:*");

zlink_spot_unsubscribe(spot, "chat:room1:message");
zlink_spot_unsubscribe(spot, "chat:room1:*");
```

Receives are dispatched automatically through the `on_message` callback
(see [4.4 Callback Handler](#44-callback-handler) below).

### 4.4 Callback Handler

Provide the callback when creating a `spot_node` or unified `spot` handle.
Incoming messages are then dispatched automatically through that callback.

```c
/* Define callback function */
void on_message(const zlink_routing_id_t *source_rid,
                const char *topic, size_t topic_len,
                zlink_msg_t *parts, size_t part_count)
{
    printf("Topic: %.*s, Parts: %zu\n", (int)topic_len, topic, part_count);
}

/* Register handler at spot_node creation */
void *node = zlink_spot_node_new(ctx, "spot-node", on_message);

/* Or register handler at unified spot creation */
void *spot = zlink_spot_new(node, on_message);
```

**Important:** A unified `spot` handle is thread-safe for same-handle
operational APIs. Callbacks still run on the I/O path, so slow work should be
offloaded to an application queue or worker thread.

**Constraints:**

- The callback must be provided at `zlink_spot_node_new()` or `zlink_spot_new()` time
- Replacing or clearing the callback after creation is not supported
- Callbacks are invoked on the socket dispatch / I/O path
- Blocking work in the callback can delay other I/O
- For slow processing, enqueue from the callback and handle it on your own thread

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
[← Gateway](07-2-gateway.md) | [Routing ID →](08-routing-id.md)
