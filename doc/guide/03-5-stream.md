[English](03-5-stream.md) | [한국어](03-5-stream.ko.md)

# STREAM Socket

## 1. Overview

The STREAM socket is a socket for **RAW communication with external clients** (web browsers, game clients, native TCP clients). It exchanges data immediately without ZMP (zlink protocol) handshake.

**Key characteristics:**
- No ZMP handshake -- communicates directly with external clients
- Identifies clients using a 4-byte uint32 routing_id
- Supports tcp, tls, ws, wss transports
- Detects connect/disconnect via 1-byte payload events (`0x01`/`0x00`)

**Valid socket combinations:** STREAM ↔ External client (incompatible with zlink internal sockets)

```
┌────────────┐     RAW (Length-Prefix)     ┌────────┐
│ 외부 Client │◄──────────────────────────►│ STREAM │
└────────────┘                             └────────┘
```

> STREAM sockets cannot connect to zlink internal sockets (PAIR, PUB, SUB, etc.) because the protocol differs.

## 2. Basic Usage

### Server Creation and Bind

```c
void *stream = zlink_socket(ctx, ZLINK_STREAM);
zlink_bind(stream, "tcp://*:8080");
```

### Client Connection (STREAM ↔ STREAM)

STREAM sockets can also connect to each other (both sides in RAW mode).

```c
void *server = zlink_socket(ctx, ZLINK_STREAM);
zlink_bind(server, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(server, ZLINK_LAST_ENDPOINT, endpoint, &len);

void *client = zlink_socket(ctx, ZLINK_STREAM);
zlink_connect(client, endpoint);
```

## 3. Message Format

STREAM socket messages always have a **2-frame structure**: `[routing_id(4B)][payload]`

### Receive Format

```
Frame 0: [routing_id]  (4-byte uint32, automatically assigned)
Frame 1: [payload]     (application data)
```

### Special Events

| Payload | Meaning |
|---------|------|
| 1-byte `0x01` | Connect event (new client connected) |
| 1-byte `0x00` | Disconnect event (client disconnected) |
| N-byte data | Regular data |

### Receive Code

```c
unsigned char routing_id[4];
unsigned char payload[4096];

/* Frame 0: routing_id */
int rc = zlink_recv(stream, routing_id, 4, 0);

/* Check MORE flag */
int more = 0;
size_t more_size = sizeof(more);
zlink_getsockopt(stream, ZLINK_RCVMORE, &more, &more_size);

/* Frame 1: payload */
int payload_size = zlink_recv(stream, payload, sizeof(payload), 0);

if (payload_size == 1 && payload[0] == 0x01) {
    /* New client connected */
} else if (payload_size == 1 && payload[0] == 0x00) {
    /* Client disconnected */
} else {
    /* Regular data */
}
```

> Reference: `core/tests/test_stream_socket.cpp` -- `recv_stream_event()` function

### Send Code

```c
/* Reply: prepend routing_id before sending */
zlink_send(stream, routing_id, 4, ZLINK_SNDMORE);
zlink_send(stream, "response", 8, 0);
```

> Reference: `core/tests/test_stream_socket.cpp` -- `send_stream_msg()` function

## 4. Socket Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `ZLINK_MAXMSGSIZE` | int64 | -1 | Maximum message size (connection dropped if exceeded) |
| `ZLINK_SNDHWM` | int | 300000 | Send HWM |
| `ZLINK_RCVHWM` | int | 300000 | Receive HWM |
| `ZLINK_LINGER` | int | -1 | Wait time on close (ms) |
| `ZLINK_TLS_CERT` | string | -- | TLS/WSS server certificate path |
| `ZLINK_TLS_KEY` | string | -- | TLS/WSS server private key path |
| `ZLINK_TLS_CA` | string | -- | TLS/WSS client CA path |
| `ZLINK_TLS_HOSTNAME` | string | -- | TLS hostname verification |
| `ZLINK_TLS_TRUST_SYSTEM` | int | 1 | Whether to trust the system CA store |

### MAXMSGSIZE

If a received message exceeds the specified size, the connection is dropped.

```c
int64_t maxmsg = 1024;  /* 1KB limit */
zlink_setsockopt(stream, ZLINK_MAXMSGSIZE, &maxmsg, sizeof(maxmsg));
```

> Reference: `core/tests/test_stream_socket.cpp` -- `test_stream_maxmsgsize()`

## 5. Usage Patterns

### Pattern 1: TCP Echo Server

```c
void *server = zlink_socket(ctx, ZLINK_STREAM);
int linger = 0;
zlink_setsockopt(server, ZLINK_LINGER, &linger, sizeof(linger));
zlink_bind(server, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(server, ZLINK_LAST_ENDPOINT, endpoint, &len);

void *client = zlink_socket(ctx, ZLINK_STREAM);
zlink_setsockopt(client, ZLINK_LINGER, &linger, sizeof(linger));
zlink_connect(client, endpoint);

/* Receive connect event (both sides) */
unsigned char server_id[4], client_id[4];
unsigned char code;

zlink_recv(server, server_id, 4, 0);  /* routing_id */
zlink_recv(server, &code, 1, 0);      /* 0x01 = connected */

zlink_recv(client, client_id, 4, 0);
zlink_recv(client, &code, 1, 0);      /* 0x01 = connected */

/* Client → Server */
zlink_send(client, client_id, 4, ZLINK_SNDMORE);
zlink_send(client, "hello", 5, 0);

/* Server receives */
unsigned char recv_id[4];
char recv_buf[64];
zlink_recv(server, recv_id, 4, 0);
int size = zlink_recv(server, recv_buf, sizeof(recv_buf), 0);

/* Server → Client reply (echo) */
zlink_send(server, recv_id, 4, ZLINK_SNDMORE);
zlink_send(server, recv_buf, size, 0);
```

> Reference: `core/tests/test_stream_socket.cpp` -- `test_stream_tcp_basic()`

### Pattern 2: WebSocket Server (ws://)

A WebSocket server that communicates with web browsers.

```c
void *server = zlink_socket(ctx, ZLINK_STREAM);
zlink_bind(server, "ws://127.0.0.1:*");

char endpoint[256];
size_t endpoint_len = sizeof(endpoint);
zlink_getsockopt(server, ZLINK_LAST_ENDPOINT, endpoint, &endpoint_len);

void *client = zlink_socket(ctx, ZLINK_STREAM);
zlink_connect(client, endpoint);

/* After automatic WebSocket handshake, exchange data */
unsigned char server_id[4], client_id[4];
unsigned char code;

zlink_recv(server, server_id, 4, 0);
zlink_recv(server, &code, 1, 0);  /* 0x01 */

zlink_recv(client, client_id, 4, 0);
zlink_recv(client, &code, 1, 0);  /* 0x01 */

/* Send data */
zlink_send(client, client_id, 4, ZLINK_SNDMORE);
zlink_send(client, "ws", 2, 0);
```

> Reference: `core/tests/test_stream_socket.cpp` -- `test_stream_ws_basic()`

### Pattern 3: TLS WebSocket Server (wss://)

Encrypted WebSocket communication.

```c
void *server = zlink_socket(ctx, ZLINK_STREAM);
void *client = zlink_socket(ctx, ZLINK_STREAM);

/* Server TLS configuration */
zlink_setsockopt(server, ZLINK_TLS_CERT, cert_path, strlen(cert_path));
zlink_setsockopt(server, ZLINK_TLS_KEY, key_path, strlen(key_path));
zlink_bind(server, "wss://127.0.0.1:*");

/* Client TLS configuration */
int trust_system = 0;
zlink_setsockopt(client, ZLINK_TLS_TRUST_SYSTEM, &trust_system, sizeof(trust_system));
zlink_setsockopt(client, ZLINK_TLS_CA, ca_path, strlen(ca_path));
zlink_setsockopt(client, ZLINK_TLS_HOSTNAME, "localhost", 9);

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(server, ZLINK_LAST_ENDPOINT, endpoint, &len);
zlink_connect(client, endpoint);

/* Data exchange is the same as ws after this point */
```

> Reference: `core/tests/test_stream_socket.cpp` -- `test_stream_wss_basic()`

### Pattern 4: Connect/Disconnect Detection

```c
unsigned char routing_id[4];
unsigned char code;

while (1) {
    zlink_recv(stream, routing_id, 4, 0);
    int size = zlink_recv(stream, &code, 1, ZLINK_DONTWAIT);

    if (size == 1 && code == 0x01) {
        printf("Client connected: id=%02x%02x%02x%02x\n",
               routing_id[0], routing_id[1],
               routing_id[2], routing_id[3]);
    } else if (size == 1 && code == 0x00) {
        printf("Client disconnected: id=%02x%02x%02x%02x\n",
               routing_id[0], routing_id[1],
               routing_id[2], routing_id[3]);
    } else {
        /* Process regular data */
    }
}
```

### Pattern 5: Blocking Malicious Clients with MAXMSGSIZE

```c
int64_t maxmsg = 4;  /* Reject messages exceeding 4 bytes */
zlink_setsockopt(server, ZLINK_MAXMSGSIZE, &maxmsg, sizeof(maxmsg));

/* When client sends "toolarge" (8 bytes), connection is dropped */
/* Server receives 0x00 (disconnect) event */
```

> Reference: `core/tests/test_stream_socket.cpp` -- `test_stream_maxmsgsize()`

## 6. Caveats

### Cannot Communicate with zlink Internal Sockets

STREAM sockets do not perform ZMP protocol handshake, so they cannot connect to zlink internal sockets such as PAIR, PUB, SUB, DEALER, or ROUTER. They are exclusively for external clients.

### routing_id Is Fixed at 4 Bytes

The routing_id of a STREAM socket is always a 4-byte uint32. This differs from ROUTER's variable-size routing_id.

```c
/* Verify STREAM routing_id size */
unsigned char rid[4];
int rc = zlink_recv(stream, rid, 4, 0);
assert(rc == 4);  /* always 4 bytes */
```

### Connect Event Handling Is Required

When a new client connects, the connect event (0x01) must be received. If the event is ignored, the routing_id is unknown and replies cannot be sent.

### Transport Restrictions

Only STREAM sockets support the ws and wss transports. Other socket types cannot use ws or wss. The tls transport is available for all socket types.

| Transport | STREAM | Other Sockets |
|-----------|:------:|:---------:|
| tcp | O | O |
| ipc | - | O |
| inproc | - | O |
| ws | O | - |
| wss | O | - |
| tls | O | O |

### LINGER Setting

In test environments, setting LINGER to 0 is recommended for fast cleanup.

```c
int linger = 0;
zlink_setsockopt(stream, ZLINK_LINGER, &linger, sizeof(linger));
```

> For internal implementation optimizations of the STREAM socket (WS/WSS copy elimination, etc.), see [STREAM Socket Optimization](../internals/stream-socket.md).

---
[← ROUTER](03-4-router.md) | [Transport →](04-transports.md)
