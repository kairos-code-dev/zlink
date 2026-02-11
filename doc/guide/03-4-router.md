[English](03-4-router.md) | [한국어](03-4-router.ko.md)

# ROUTER Socket

## 1. Overview

The ROUTER socket is a **routing_id-based routing** socket. It automatically prepends a routing_id frame to received messages, and when sending, it uses the first frame's routing_id to specify the target peer.

**Key characteristics:**
- Automatically adds a routing_id frame on receive (identifies message origin)
- Specifies the target peer via the first frame on send (replies to a specific client)
- Can manage multiple peers (server/broker role)

**Valid socket combinations:** ROUTER ↔ DEALER, ROUTER ↔ ROUTER

```
┌────────┐              ┌────────┐
│DEALER 1│─────────────►│        │
│ (D1)   │              │ ROUTER │  ← distinguishes each DEALER by routing_id
└────────┘              │        │
┌────────┐              │        │
│DEALER 2│─────────────►│        │
│ (D2)   │              └────────┘
└────────┘
```

## 2. Basic Usage

### Creation and Bind

```c
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(router, "tcp://*:5558");
```

### Receiving Messages

ROUTER automatically prepends a routing_id frame to received messages.

```c
/* DEALER sends "Hello" → ROUTER receives [routing_id][Hello] */
char identity[32], data[256];
int id_size = zlink_recv(router, identity, sizeof(identity), 0);
int data_size = zlink_recv(router, data, sizeof(data), 0);
```

### Sending Messages

When replying, send the received routing_id as the first frame to specify the target.

```c
/* Use the received routing_id as-is to reply */
zlink_send(router, identity, id_size, ZLINK_SNDMORE);
zlink_send(router, "World", 5, 0);
```

## 3. Message Format

### Receive Format

When a DEALER sends a multipart `[A][B]`, the ROUTER receives `[routing_id][A][B]`.

```
DEALER sends:   [frame1][frame2]
                     ↓
ROUTER receives: [routing_id][frame1][frame2]
```

### Send Format

When ROUTER sends, the first frame must be the target routing_id. The routing_id frame is not transmitted; it is used only for routing.

```
ROUTER sends:   [routing_id][frame1][frame2]
                      ↓
DEALER receives: [frame1][frame2]   ← routing_id stripped
```

### Receive/Reply Using zlink_msg_t

```c
/* Receive */
zlink_msg_t rid, data;
zlink_msg_init(&rid);
zlink_msg_init(&data);
zlink_msg_recv(&rid, router, 0);   /* routing_id frame */
zlink_msg_recv(&data, router, 0);  /* data frame */

/* Reply: reuse routing_id as-is */
zlink_msg_send(&rid, router, ZLINK_SNDMORE);
zlink_send(router, "reply", 5, 0);

zlink_msg_close(&rid);
zlink_msg_close(&data);
```

## 4. Socket Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `ZLINK_ROUTER_MANDATORY` | int | 0 | Return EHOSTUNREACH error for undeliverable messages |
| `ZLINK_ROUTER_HANDOVER` | int | 0 | Replace existing connection on routing_id conflict |
| `ZLINK_ROUTING_ID` | binary | Auto (UUID) | The ROUTER's own routing_id |
| `ZLINK_SNDHWM` | int | 1000 | Send HWM |
| `ZLINK_RCVHWM` | int | 1000 | Receive HWM |
| `ZLINK_LINGER` | int | -1 | Wait time on close (ms) |

### ROUTER_MANDATORY

By default, ROUTER **silently drops** messages when the target cannot be found. Enabling `ROUTER_MANDATORY` returns an `EHOSTUNREACH` error instead.

```c
int mandatory = 1;
zlink_setsockopt(router, ZLINK_ROUTER_MANDATORY, &mandatory, sizeof(mandatory));

/* Attempt to send to a non-existent target */
int rc = zlink_send(router, "UNKNOWN", 7, ZLINK_SNDMORE);
/* rc == -1, errno == EHOSTUNREACH */
```

> Reference: `core/tests/test_router_mandatory.cpp` -- `test_basic()`

## 5. Usage Patterns

### Pattern 1: Multi-DEALER Server

The most basic ROUTER pattern. Distinguishes multiple DEALER clients by routing_id.

```c
/* Server */
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(router, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(router, ZLINK_LAST_ENDPOINT, endpoint, &len);

/* Client 1 */
void *d1 = zlink_socket(ctx, ZLINK_DEALER);
zlink_setsockopt(d1, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(d1, endpoint);

/* Client 2 */
void *d2 = zlink_socket(ctx, ZLINK_DEALER);
zlink_setsockopt(d2, ZLINK_ROUTING_ID, "D2", 2);
zlink_connect(d2, endpoint);

/* Receive messages from each client */
char id[32], msg[64];
int id_size = zlink_recv(router, id, sizeof(id), 0);
int msg_size = zlink_recv(router, msg, sizeof(msg), 0);

/* Reply to specific clients */
zlink_send(router, "D1", 2, ZLINK_SNDMORE);
zlink_send(router, "reply_to_d1", 11, 0);

zlink_send(router, "D2", 2, ZLINK_SNDMORE);
zlink_send(router, "reply_to_d2", 11, 0);

/* Each DEALER receives only its own reply */
char buf[64];
zlink_recv(d1, buf, sizeof(buf), 0);  /* "reply_to_d1" */
zlink_recv(d2, buf, sizeof(buf), 0);  /* "reply_to_d2" */
```

> Reference: `core/tests/test_router_multiple_dealers.cpp` -- TCP/IPC/inproc across 3 transports

### Pattern 2: Detecting Send Failures with ROUTER_MANDATORY

```c
void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(router, "tcp://*:5558");

/* Default behavior: silently drops undeliverable messages */
zlink_send(router, "UNKNOWN", 7, ZLINK_SNDMORE);
zlink_send(router, "DATA", 4, 0);
/* No error, message lost */

/* Enable MANDATORY mode */
int mandatory = 1;
zlink_setsockopt(router, ZLINK_ROUTER_MANDATORY, &mandatory, sizeof(mandatory));

/* Now returns error on undeliverable message */
int rc = zlink_send(router, "UNKNOWN", 7, ZLINK_SNDMORE);
if (rc == -1 && errno == EHOSTUNREACH) {
    /* Target "UNKNOWN" not found */
}
```

> Reference: `core/tests/test_router_mandatory.cpp` -- default drop vs MANDATORY error

### Pattern 3: Send After Confirming Connection

DEALER sends a message first to notify ROUTER of its connection, then ROUTER replies.

```c
/* DEALER connects and sends initial message */
void *dealer = zlink_socket(ctx, ZLINK_DEALER);
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "X", 1);
zlink_connect(dealer, endpoint);
zlink_send(dealer, "Hello", 5, 0);

/* ROUTER: confirm DEALER's connection */
char id[32];
zlink_recv(router, id, sizeof(id), 0);  /* "X" */
char buf[64];
zlink_recv(router, buf, sizeof(buf), 0);  /* "Hello" */

/* Now it is safe to send to "X" */
zlink_send(router, "X", 1, ZLINK_SNDMORE);
zlink_send(router, "Hello", 5, 0);
```

> Reference: `core/tests/test_router_mandatory.cpp` -- DEALER connect → message → ROUTER reply

### Pattern 4: Multiple Transports

Multiple transports can be used to connect DEALERs to the same ROUTER.

```c
void *router = zlink_socket(ctx, ZLINK_ROUTER);

/* TCP */
zlink_bind(router, "tcp://127.0.0.1:5558");

/* IPC (Linux/macOS) */
zlink_bind(router, "ipc:///tmp/router.ipc");

/* inproc (same process) */
zlink_bind(router, "inproc://router");

/* DEALERs connect via each transport -- ROUTER manages them uniformly by routing_id */
```

> Reference: `core/tests/test_router_multiple_dealers.cpp` -- TCP/IPC/inproc tests

## 6. Caveats

### Default Drop Behavior

Without `ROUTER_MANDATORY`, sending to a non-existent routing_id **silently drops** the message. Enabling `ROUTER_MANDATORY` is recommended in production.

### routing_id Changes on Reconnect

When a DEALER reconnects, its auto-generated routing_id may change. Setting an explicit routing_id is recommended for stable communication.

```c
/* Explicit routing_id -- remains the same across reconnections */
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "stable-id", 9);
```

### routing_id Conflicts

If two DEALERs with the same routing_id connect simultaneously, the second connection is rejected by default. Enable `ROUTER_HANDOVER` to replace the existing connection instead.

### Multipart Message Integrity

When sending from ROUTER, the routing_id frame and data frames must be linked with `ZLINK_SNDMORE`. Sending only the routing_id without data frames causes unexpected behavior.

```c
/* Correct send */
zlink_send(router, identity, id_size, ZLINK_SNDMORE);  /* SNDMORE is required */
zlink_send(router, data, data_size, 0);

/* Incorrect send -- must not send identity alone */
zlink_send(router, identity, id_size, 0);  /* no SNDMORE! */
```

> For a detailed explanation of routing_id concepts, see [08-routing-id.md](08-routing-id.md).

---
[← DEALER](03-3-dealer.md) | [STREAM →](03-5-stream.md)
