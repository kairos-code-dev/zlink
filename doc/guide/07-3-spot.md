[English](07-3-spot.md) | [한국어](07-3-spot.ko.md)

# SPOT Topic PUB/SUB (Location-Transparent Publish/Subscribe)

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
void *discovery = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_SPOT);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");
zlink_discovery_subscribe(discovery, "spot-node");

/* SPOT Node setup */
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://*:9000");

/* Attach Discovery (must come before register) */
zlink_spot_node_set_discovery(node, discovery, "spot-node");

/* Register via Discovery's uplink runtime */
zlink_spot_node_register(node, "spot-node", "tcp://node1.example.com:9000");
```

**Note:** `register()` submits a registration request through the attached
Discovery's registry uplink runtime. Therefore `set_discovery()` must be
called before `register()`. Calling `register()` without an attached
Discovery fails with `EFSM`.

### 3.2 Manual Mesh

```c
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://*:9000");

/* Directly connect to other nodes' PUB */
zlink_spot_node_connect_peer_pub(node, "tcp://node2:9000");
zlink_spot_node_connect_peer_pub(node, "tcp://node3:9000");
```

**Note:** In a manual mesh there is no Discovery, so there is no registry
topology visibility. Calling `register()` is not possible. This is an
intended limitation.

## 4. SPOT Pub/Sub Usage

### 4.1 Publishing (SPOT Pub)

```c
void *pub = zlink_spot_pub_new(node);

/* Publish */
const char *msg = "hello world";
zlink_spot_pub_publish_bytes(pub, "chat:room1:message", msg, 11, 0);
```

For multipart payloads, use `zlink_spot_pub_publish()`.

### 4.2 Subscribing/Receiving (SPOT Sub)

```c
void *sub = zlink_spot_sub_new(node);

/* Subscribe to exact topic */
zlink_spot_sub_subscribe(sub, "chat:room1:message");

/* Pattern subscription (prefix matching) */
zlink_spot_sub_subscribe_pattern(sub, "chat:room1:*");

/* Receive */
zlink_msg_t *parts = NULL;
size_t part_count = 0;
char topic[256];
size_t topic_len = 256;
zlink_spot_sub_recv(sub, &parts, &part_count, 0, topic, &topic_len);

printf("Topic: %.*s\n", (int)topic_len, topic);
zlink_multipart_close(parts, part_count);
```

### 4.3 Unsubscribing

```c
zlink_spot_sub_unsubscribe(sub, "chat:room1:message");
zlink_spot_sub_unsubscribe(sub, "chat:room1:*");
```

### 4.4 Polling Integration

`spot_sub` and `spot_pub` can be registered with the poller directly.
Internal socket handles are resolved internally and never exposed to the
caller. After the poller signals readiness, use the regular service API
(`zlink_spot_sub_recv`, `zlink_spot_pub_publish_bytes`, etc.) to send or
receive messages.

```c
/* Register spot_sub with poller */
zlink_poller_add_spot_sub(poller, sub, NULL, ZLINK_POLLIN);

/* Wait for readiness */
zlink_poller_event_t ev;
zlink_poller_wait(poller, &ev, -1);

/* Use existing service API */
zlink_spot_sub_recv(sub, &parts, &part_count, 0, topic, &topic_len);
```

```c
/* Register spot_pub with poller (for back-pressure awareness) */
zlink_poller_add_spot_pub(poller, pub, NULL, ZLINK_POLLOUT);
```

**Important:** `spot_pub` is thread-safe — multiple threads may call
`publish()` concurrently on the same instance. `spot_sub` is NOT
thread-safe — `recv()`, handler, `subscribe()`, and `unsubscribe()` must
be serialized by the caller.

### 4.5 Callback Handler

Instead of `recv()`, you can register a callback function that is automatically invoked when a message arrives.

```c
/* Define callback function */
void on_message(const char *topic, size_t topic_len,
                const zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    printf("Topic: %.*s, Parts: %zu\n", (int)topic_len, topic, part_count);
}

/* Register handler */
zlink_spot_sub_set_handler(sub, on_message, NULL);

/* Unregister handler (returns after all in-flight callbacks complete) */
zlink_spot_sub_set_handler(sub, NULL, NULL);
```

**Constraints:**

- When a handler is active, calling `recv()` returns `EINVAL` (mutually exclusive)
- Passing `NULL` to unregister the handler returns only after all in-flight callbacks complete
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

- Local publish (`spot_pub`) distributes to local SPOT Subs + sends out via PUB (remote propagation)
- Remote receive (SUB) distributes to local SPOT Subs only (no re-publishing)
- No re-publishing prevents message loops and duplicates
- `subscribe()` / `unsubscribe()` return means the local socket filter has been applied;
  it does not guarantee cluster-wide propagation
- Message ordering is preserved within a single `SpotPub` instance
- Global ordering across different `SpotPub` instances is not guaranteed
- If both an exact topic and a pattern match the same message on the same subscriber,
  the message is delivered only once

SPOT is a live pub/sub system. It does not guarantee durable delivery,
ack/retry, exactly-once semantics, or past message replay for late joiners.

## 7. Cleanup

```c
zlink_spot_pub_destroy(&pub);
zlink_spot_sub_destroy(&sub);
zlink_spot_node_destroy(&node);
zlink_discovery_destroy(&discovery);
```

**Destroy order:** Destroy `SpotPub` / `SpotSub` first, then `SpotNode`,
and finally `Discovery`. All external use of `SpotPub` / `SpotSub` must
stop before calling `SpotNode` destroy.

---
[← Gateway](07-2-gateway.md) | [Routing ID →](08-routing-id.md)
