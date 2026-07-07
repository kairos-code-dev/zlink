[English](08-routing-id.md) | [한국어](08-routing-id.ko.md)

<!-- zlink-nav:start -->
[← SPOT Actor](07-4-actor.md) | [Message API →](09-message-api.md)
<!-- zlink-nav:end -->

# Routing ID Concepts and Usage

## 1. Overview

A Routing ID is binary data that identifies sockets and connections in zlink. It is used for message routing in ROUTER sockets, for identifying external clients in STREAM sockets, and for identifying peers in monitoring.

## 2. zlink_routing_id_t

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
- A socket's own routing_id defaults to 16 random UUID-like bytes; STREAM peer
  routing IDs are 4-byte integral values allocated per connection by the
  STREAM socket (not a process-wide counter)

### own vs peer — Differences Users Should Know

| | own routing_id | peer routing_id |
|---|---|---|
| **Creation time** | At socket creation | At peer connection |
| **Size** | 16B (UUID) | Variable (ROUTER), 4B (STREAM) |
| **Usage** | Sent during handshake | Returned as a separate `source_rid` output on receive |
| **Configuration** | `zlink_set_routing_id()` | Uses value set by the peer |

The own routing_id is automatically assigned a UUID when the socket is created and is sent to the peer during the handshake. The peer routing_id is the own routing_id sent by the peer; the public receive surface exposes it as a separate `source_rid` output (callback parameter or `zlink_router_recv()` / `zlink_recv()` output), not as an in-band message frame.

## 4. User-Defined routing_id

### Setting Socket Identity

```c
/* Set before bind/connect */
const char *id = "router-A";
zlink_set_routing_id(socket, id, strlen(id));
```

Notes:
- Must be set **before** `zlink_bind()` or `zlink_connect()`
- Cannot be changed after connection
- Empty string ("") is not allowed
- A conflict occurs if two peers with the same routing_id connect to the same ROUTER

### Considerations for User-Defined routing_id

```c
/* Good example: meaningful identifiers */
zlink_set_routing_id(dealer, "worker-01", 9);
zlink_set_routing_id(dealer, "D1", 2);

/* Caution: potential collision with auto-generated routing_ids */
/* Avoid UUID format (16B binary) */
```

> Reference: `core/tests/integration/test_router_multiple_dealers.cpp` — `zlink_set_routing_id(dealer1, "D1", 2)`

### Querying

```c
zlink_routing_id_t rid;
zlink_get_routing_id(socket, &rid);

printf("routing_id (%u bytes): ", rid.size);
for (size_t i = 0; i < rid.size; ++i)
    printf("%02x", rid.data[i]);
printf("\n");
```

## 5. Connection Alias Setting

`ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` is a per-connection alias applied to the next `zlink_connect()` call. It is set via `zlink_set_router_option()` and is used when a ROUTER needs to refer to a specific connection by a meaningful name.

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

- `zlink_set_routing_id()` applies to the entire socket
- `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` (set via `zlink_set_router_option()`) applies to individual connections
- A single socket can have different aliases for each connection
- `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` is for ROUTER-side connection paths.
- `zlink_set_router_option()` accepts only ROUTER/DEALER handles; calling it on
  `ZLINK_SOCKET_STREAM` (or any other type) returns
  `ZLINK_CONFIG_INVALID_ARGUMENT` (`EINVAL`).

## 6. Using routing_id with ROUTER Sockets

In ROUTER sockets, `zlink_recv()` and recv callbacks return the sender's
routing_id as a **separate parameter** (`source_rid`), not as a message frame.
When replying, pass the same routing_id to `zlink_send_rid()`.

> **Difference from libzmq:** libzmq ROUTER returned routing_id as the
> first frame of `zmq_recv()`. In zlink, routing_id is a separate parameter
> on all socket types.

### Basic Request-Reply

```c
/* ROUTER server (recv loop) */
void *router = zlink_socket(ctx, ZLINK_SOCKET_ROUTER);
zlink_bind(router, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_get_option(router, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

/* DEALER client (explicit routing_id) */
void *dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_set_routing_id(dealer, "D1", 2);
zlink_connect(dealer, endpoint);

/* DEALER send */
zlink_msg_t req;
zlink_msg_init_size(&req, 5);
memcpy(zlink_msg_data(&req), "Hello", 5);
zlink_send(dealer, &req, 1, 0);

/* ROUTER: drain messages with zlink_router_recv() in a poller loop.
   source_node_rid = "D1" (2 bytes), parts[0] = "Hello" (5 bytes).
   Reply with zlink_send_rid(router, source_node_rid, reply, 1, 0). */
```

### Distinguishing Multiple Clients

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

> Reference: `core/tests/integration/test_router_multiple_dealers.cpp` — Multiple DEALER example

### Handling routing_id with zlink_msg_t

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

## 7. Using routing_id with STREAM Sockets

STREAM sockets identify external clients using a 4B uint32 peer routing_id.

### Basic Usage

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

### routing_id in Connect/Disconnect Events

STREAM connect/disconnect are **not** delivered as in-band data bytes. They are
reported through the socket monitor as `ZLINK_EVENT_CONNECTION_READY` /
`ZLINK_EVENT_DISCONNECTED`, each carrying the 4-byte `routing_id` of the
affected client. (Internally the STREAM notify path emits zero-byte control
events, which dispatch skips — they never reach the data callback.)

```c
void on_monitor_event(const zlink_monitor_event_t *ev, void *userdata)
{
    if (ev->event == ZLINK_EVENT_CONNECTION_READY) {
        printf("Connected: ");
        for (size_t j = 0; j < ev->routing_id.size; j++)
            printf("%02x", ev->routing_id.data[j]);
        printf("\n");
    } else if (ev->event == ZLINK_EVENT_DISCONNECTED) {
        printf("Disconnected\n");
    }
}
```

> Reference: `core/tests/integration/test_stream_socket.cpp` — STREAM monitor events

### ROUTER vs STREAM routing_id Comparison

| | ROUTER | STREAM |
|---|---|---|
| **Size** | Variable (user-defined or 16B UUID) | Fixed 4B (uint32) |
| **Generation** | Peer's own routing_id | Auto-assigned by the server |
| **Configurable** | Peer sets via `zlink_set_routing_id()` | Auto-assigned only (not configurable) |
| **Receive exposure** | Separate `source_rid` output | Separate `source_rid` output |

## 8. Debugging Tips for routing_id

### Hex Output

Since routing_id is binary data, printing it as a string may produce garbled output. Use hex format instead.

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

### String routing_id

If the user-defined routing_id is an ASCII string, it can be printed directly.

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

### Checking Auto-Generated routing_id

```c
/* Query the auto-assigned routing_id after socket creation */
zlink_routing_id_t rid;
zlink_get_routing_id(socket, &rid);
printf("Auto-generated routing_id: %u bytes\n", rid.size);  /* 16 bytes (UUID) */
```

## 9. Binary Handling Principles

- Treat routing_id as **binary data**
- String conversion is the application's responsibility
- Auto-generated routing_ids use an internal format; no numeric conversion API is provided
- Use `memcmp()` for comparison (string comparison functions must not be used)
- Hex format is recommended for log output

```c
/* routing_id comparison */
if (rid_size == 2 && memcmp(rid, "D1", 2) == 0) {
    /* Message from client D1 */
}
```

---
<!-- zlink-nav:bottom:start -->
[← SPOT Actor](07-4-actor.md) | [Message API →](09-message-api.md)
<!-- zlink-nav:bottom:end -->
