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
┌─────────────────────────────────────┐
│           SPOT Node                  │
│  ┌──────────┐  ┌──────────┐         │
│  │ SPOT A   │  │ SPOT B   │         │
│  │ pub:chat │  │ sub:chat │         │
│  └──────────┘  └──────────┘         │
└─────────────────────────────────────┘
```

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

## 3. SPOT Node Setup

### 3.1 Discovery-Based Automatic Mesh

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
void *discovery = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_SPOT);

/* Connect to Registry */
zlink_discovery_connect_registry(discovery, "tcp://registry1:5550");
zlink_discovery_subscribe(discovery, "spot-node");

/* PUB bind */
zlink_spot_node_bind(node, "tcp://*:9000");

/* Register with Registry */
zlink_spot_node_connect_registry(node, "tcp://registry1:5551");
zlink_spot_node_register(node, "spot-node", NULL);

/* Discovery-based automatic peer connection */
zlink_spot_node_set_discovery(node, discovery, "spot-node");
```

### 3.2 Manual Mesh

```c
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://*:9000");

/* Directly connect to other nodes' PUB */
zlink_spot_node_connect_peer_pub(node, "tcp://node2:9000");
zlink_spot_node_connect_peer_pub(node, "tcp://node3:9000");
```

## 4. SPOT Pub/Sub Usage

### 4.1 Publishing (SPOT Pub)

```c
void *pub = zlink_spot_pub_new(node);

/* Publish */
zlink_msg_t msg;
zlink_msg_init_data(&msg, "hello world", 11, NULL, NULL);
zlink_spot_pub_publish(pub, "chat:room1:message", &msg, 1, 0);
```

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
zlink_msgv_close(parts, part_count);
```

### 4.3 Unsubscribing

```c
zlink_spot_sub_unsubscribe(sub, "chat:room1:message");
zlink_spot_sub_unsubscribe(sub, "chat:room1:*");
```

### 4.4 Raw Socket Exposure Policy

- `spot_pub` does not expose the raw socket.
- `spot_sub` provides an API to expose the raw SUB socket.

```c
void *raw_sub = zlink_spot_sub_socket(sub);
```

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
- Callbacks are invoked on the spot_node worker thread

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

## 7. Cleanup

```c
zlink_spot_pub_destroy(&pub);
zlink_spot_sub_destroy(&sub);
zlink_spot_node_destroy(&node);
zlink_discovery_destroy(&discovery);
```

---
[← Gateway](07-2-gateway.md) | [Routing ID →](08-routing-id.md)
