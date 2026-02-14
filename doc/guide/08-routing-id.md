[English](08-routing-id.md) | [한국어](08-routing-id.ko.md)

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

- If the user does not set `ZLINK_ROUTING_ID`, it is auto-generated
- Uniqueness is guaranteed based on a process-wide global counter

### own vs peer — Differences Users Should Know

| | own routing_id | peer routing_id |
|---|---|---|
| **Creation time** | At socket creation | At peer connection |
| **Size** | 16B (UUID) | Variable (ROUTER), 4B (STREAM) |
| **Usage** | Sent during handshake | Automatically prepended to received messages |
| **Configuration** | `ZLINK_ROUTING_ID` | Uses value set by the peer |

The own routing_id is automatically assigned a UUID when the socket is created and is sent to the peer during the handshake. The peer routing_id is the own routing_id sent by the peer and is automatically prepended as the first frame of received messages in ROUTER/STREAM sockets.

## 4. User-Defined routing_id

### Setting Socket Identity

```c
/* Set before bind/connect */
const char *id = "router-A";
zlink_setsockopt(socket, ZLINK_ROUTING_ID, id, strlen(id));
```

Notes:
- Must be set **before** `zlink_bind()` or `zlink_connect()`
- Cannot be changed after connection
- Empty string ("") is not allowed
- A conflict occurs if two peers with the same routing_id connect to the same ROUTER

### Considerations for User-Defined routing_id

```c
/* Good example: meaningful identifiers */
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "worker-01", 9);
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);

/* Caution: potential collision with auto-generated routing_ids */
/* Avoid UUID format (16B binary) */
```

> Reference: `core/tests/test_router_multiple_dealers.cpp` — `zlink_setsockopt(dealer1, ZLINK_ROUTING_ID, "D1", 2)`

### Querying

```c
uint8_t buf[255];
size_t size = sizeof(buf);
zlink_getsockopt(socket, ZLINK_ROUTING_ID, buf, &size);

printf("routing_id (%zu bytes): ", size);
for (size_t i = 0; i < size; ++i)
    printf("%02x", buf[i]);
printf("\n");
```

## 5. Connection Alias Setting

`ZLINK_CONNECT_ROUTING_ID` is a per-connection alias applied to the next `zlink_connect()` call. It is used when a ROUTER needs to refer to a specific connection by a meaningful name.

```c
/* Apply alias to the next connect */
const char *alias = "edge-1";
zlink_setsockopt(socket, ZLINK_CONNECT_ROUTING_ID, alias, strlen(alias));
zlink_connect(socket, "tcp://server:5555");

/* Different alias for another connection */
const char *alias2 = "edge-2";
zlink_setsockopt(socket, ZLINK_CONNECT_ROUTING_ID, alias2, strlen(alias2));
zlink_connect(socket, "tcp://server2:5556");
```

- `ZLINK_ROUTING_ID` applies to the entire socket
- `ZLINK_CONNECT_ROUTING_ID` applies to individual connections
- A single socket can have different aliases for each connection
- `ZLINK_CONNECT_ROUTING_ID` is for ROUTER-side connection paths.
- Setting it on `ZLINK_STREAM` returns `EOPNOTSUPP`.

## 6. Using routing_id with ROUTER Sockets

ROUTER sockets automatically prepend a routing_id frame to received messages. When replying, the same routing_id is used to send the message to the correct peer.

### Basic Request-Reply

```c
/* ROUTER server */
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(router, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(router, ZLINK_LAST_ENDPOINT, endpoint, &len);

/* DEALER client (explicit routing_id) */
void *dealer = zlink_socket(ctx, ZLINK_DEALER);
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer, endpoint);

/* DEALER send */
zlink_send(dealer, "Hello", 5, 0);

/* ROUTER receive: [routing_id="D1"] + [data="Hello"] */
char identity[32], data[256];
int id_size = zlink_recv(router, identity, sizeof(identity), 0);
int data_size = zlink_recv(router, data, sizeof(data), 0);
/* identity = "D1" (2 bytes), data = "Hello" (5 bytes) */

/* ROUTER reply: send routing_id as the first frame */
zlink_send(router, identity, id_size, ZLINK_SNDMORE);
zlink_send(router, "World", 5, 0);

/* DEALER receive: "World" (routing_id frame is automatically stripped) */
zlink_recv(dealer, data, sizeof(data), 0);
```

### Distinguishing Multiple Clients

```c
/* DEALER 1: routing_id = "D1" */
zlink_setsockopt(dealer1, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer1, endpoint);

/* DEALER 2: routing_id = "D2" */
zlink_setsockopt(dealer2, ZLINK_ROUTING_ID, "D2", 2);
zlink_connect(dealer2, endpoint);

/* ROUTER replies to specific clients only */
zlink_send(router, "D1", 2, ZLINK_SNDMORE);
zlink_send(router, "reply_to_d1", 11, 0);

zlink_send(router, "D2", 2, ZLINK_SNDMORE);
zlink_send(router, "reply_to_d2", 11, 0);
```

> Reference: `core/tests/test_router_multiple_dealers.cpp` — Multiple DEALER example

### Handling routing_id with zlink_msg_t

```c
/* Receive */
zlink_msg_t rid, data;
zlink_msg_init(&rid);
zlink_msg_init(&data);
zlink_msg_recv(&rid, router, 0);   /* routing_id frame */
zlink_msg_recv(&data, router, 0);  /* data frame */

/* Check routing_id size and content */
printf("routing_id: %zu bytes\n", zlink_msg_size(&rid));

/* Reply: reuse the received routing_id */
zlink_msg_send(&rid, router, ZLINK_SNDMORE);
zlink_send(router, "reply", 5, 0);

zlink_msg_close(&rid);
zlink_msg_close(&data);
```

## 7. Using routing_id with STREAM Sockets

STREAM sockets identify external clients using a 4B uint32 peer routing_id.

### Basic Usage

```c
/* Receive: [routing_id (4B)] + [payload] */
unsigned char rid[4];
char payload[4096];

zlink_recv(stream, rid, 4, 0);         /* always 4 bytes */
int size = zlink_recv(stream, payload, sizeof(payload), 0);

/* Reply: use the same routing_id */
zlink_send(stream, rid, 4, ZLINK_SNDMORE);
zlink_send(stream, response, resp_len, 0);
```

### routing_id in Connect/Disconnect Events

```c
unsigned char rid[4];
unsigned char code;

zlink_recv(stream, rid, 4, 0);
zlink_recv(stream, &code, 1, 0);

if (code == 0x01) {
    /* New client connected: save rid for subsequent communication */
    printf("Connected: %02x%02x%02x%02x\n", rid[0], rid[1], rid[2], rid[3]);
} else if (code == 0x00) {
    /* Client disconnected: identify by rid and clean up */
    printf("Disconnected: %02x%02x%02x%02x\n", rid[0], rid[1], rid[2], rid[3]);
}
```

> Reference: `core/tests/test_stream_socket.cpp` — `recv_stream_event()`, `send_stream_msg()`

### ROUTER vs STREAM routing_id Comparison

| | ROUTER | STREAM |
|---|---|---|
| **Size** | Variable (user-defined or 16B UUID) | Fixed 4B (uint32) |
| **Generation** | Peer's own routing_id | Auto-assigned by the server |
| **Configurable** | Peer sets via ZLINK_ROUTING_ID | Auto-assigned only (not configurable) |
| **Frame position** | Automatically prepended on receive | Automatically prepended on receive |

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

/* Usage */
char rid[255];
int rid_size = zlink_recv(router, rid, sizeof(rid), 0);
print_routing_id(rid, rid_size);
```

### String routing_id

If the user-defined routing_id is an ASCII string, it can be printed directly.

```c
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);

/* When received at the ROUTER */
char rid[32];
int rid_size = zlink_recv(router, rid, sizeof(rid), 0);
rid[rid_size] = '\0';
printf("routing_id: %s\n", rid);  /* "D1" */
```

### Checking Auto-Generated routing_id

```c
/* Query the auto-assigned routing_id after socket creation */
uint8_t own_id[255];
size_t own_size = sizeof(own_id);
zlink_getsockopt(socket, ZLINK_ROUTING_ID, own_id, &own_size);
printf("Auto-generated routing_id: %zu bytes\n", own_size);  /* 16 bytes (UUID) */
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
[← SPOT](07-3-spot.md) | [Message API →](09-message-api.md)
