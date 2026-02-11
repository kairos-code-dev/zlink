[English](03-1-pair.md) | [한국어](03-1-pair.ko.md)

# PAIR Socket

## 1. Overview

The PAIR socket forms an exclusive 1:1 bidirectional connection with exactly one peer. If a second peer connects, the first connection is dropped.

**Key characteristics:**
- Only a single pipe is allowed (1:1 exclusive)
- Bidirectional free messaging (send/recv order does not matter)
- The simplest socket type

**Valid socket combinations:** PAIR ↔ PAIR

```
┌────────┐              ┌────────┐
│ PAIR A │◄────────────►│ PAIR B │
└────────┘   양방향     └────────┘
```

## 2. Basic Usage

### Creation and Connection

```c
void *ctx = zlink_ctx_new();

/* Server side */
void *server = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(server, "tcp://*:5555");

/* Client side */
void *client = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(client, "tcp://127.0.0.1:5555");
```

### Message Exchange

```c
/* Server → Client */
zlink_send(server, "Hello", 5, 0);

/* Client receives */
char buf[256];
int size = zlink_recv(client, buf, sizeof(buf), 0);

/* Client → Server (immediately possible since it is bidirectional) */
zlink_send(client, "World", 5, 0);
size = zlink_recv(server, buf, sizeof(buf), 0);
```

### Sending Constant Data

Constant (literal) data can be sent directly without copying.

```c
/* zlink_send_const: sends without internal copy */
zlink_send_const(server, "foo", 3, ZLINK_SNDMORE);
zlink_send_const(server, "foobar", 6, 0);

/* Receiver receives normally */
recv_buf(client, buf, sizeof(buf), 0);  /* "foo" */
recv_buf(client, buf, sizeof(buf), 0);  /* "foobar" */
```

> Reference: `core/tests/test_pair_inproc.cpp` -- `test_zlink_send_const()` test

## 3. Message Format

The PAIR socket exchanges **application data only** without routing_id frames or envelopes.

```
Single frame:     [data]
Multipart frame:  [frame1][frame2]...[frameN]
```

Multipart send:

```c
zlink_send(server, "header", 6, ZLINK_SNDMORE);
zlink_send(server, "body", 4, 0);  /* last frame */
```

## 4. Socket Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `ZLINK_SNDHWM` | int | 1000 | Maximum number of messages in the send queue |
| `ZLINK_RCVHWM` | int | 1000 | Maximum number of messages in the receive queue |
| `ZLINK_LINGER` | int | -1 | Wait time for unsent messages on close (ms), -1=infinite |
| `ZLINK_SNDTIMEO` | int | -1 | Send timeout (ms), -1=infinite |
| `ZLINK_RCVTIMEO` | int | -1 | Receive timeout (ms), -1=infinite |

```c
int hwm = 5000;
zlink_setsockopt(socket, ZLINK_SNDHWM, &hwm, sizeof(hwm));

int linger = 0;  /* return immediately on close */
zlink_setsockopt(socket, ZLINK_LINGER, &linger, sizeof(linger));
```

## 5. Usage Patterns

### Pattern 1: Inter-thread Signaling (inproc)

The most common PAIR use case. Zero-copy communication between threads via the inproc transport.

```c
/* Main thread */
void *signal = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(signal, "inproc://signal");

/* Worker thread */
void *worker_signal = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(worker_signal, "inproc://signal");

/* Worker → Main: task completion signal */
zlink_send(worker_signal, "DONE", 4, 0);

/* Main: wait for signal */
char buf[16];
zlink_recv(signal, buf, sizeof(buf), 0);
```

> Reference: `core/tests/test_pair_inproc.cpp` -- bind → connect → bounce pattern

### Pattern 2: TCP Communication

1:1 communication over the network. Wildcard bind enables automatic port assignment.

```c
/* Server: wildcard port */
void *server = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(server, "tcp://127.0.0.1:*");

/* Query the assigned endpoint */
char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(server, ZLINK_LAST_ENDPOINT, endpoint, &len);

/* Client: connect using the queried endpoint */
void *client = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(client, endpoint);
```

> Reference: `core/tests/test_pair_tcp.cpp` -- `bind_loopback_ipv4()` + wildcard bind

### Pattern 3: Connection by DNS Name

You can also connect using a hostname.

```c
void *client = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(client, "tcp://localhost:5555");
```

> Reference: `core/tests/test_pair_tcp.cpp` -- `test_pair_tcp_connect_by_name()`

### Pattern 4: IPC Communication

Inter-process communication on the same machine (Linux/macOS).

```c
void *server = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(server, "ipc:///tmp/myapp.ipc");

void *client = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(client, "ipc:///tmp/myapp.ipc");
```

> Reference: `core/tests/test_pair_ipc.cpp` -- includes IPC path length validation

## 6. Caveats

### Only a Single Peer Allowed

A PAIR socket maintains only one connection. If a second peer connects, the first connection is dropped.

```
 Allowed:  PAIR A ↔ PAIR B      (1:1)
 Invalid:  PAIR A ← PAIR B      (N:1 attempt drops existing connection)
               ← PAIR C
```

Use DEALER/ROUTER if N:1 communication is needed.

### inproc bind Order

With the inproc transport, **bind must be called before connect**.

```c
/* Correct order */
zlink_bind(a, "inproc://signal");     /* 1. bind first */
zlink_connect(b, "inproc://signal");  /* 2. connect */

/* Wrong order -- fails */
zlink_connect(b, "inproc://signal");  /* fails because bind has not been called yet */
zlink_bind(a, "inproc://signal");
```

### IPC Path Length

The file path of an IPC endpoint cannot exceed the system limit (typically 108 characters).

```c
/* Path too long → ENAMETOOLONG error */
zlink_bind(socket, "ipc:///very/long/path/.../endpoint.ipc");
```

> Reference: `core/tests/test_pair_ipc.cpp` -- `test_endpoint_too_long()`

### HWM Behavior

When there is no peer or the peer is slow, outgoing messages are queued up to the HWM. When the HWM is exceeded, `zlink_send()` blocks (default) or returns `EAGAIN` (`ZLINK_DONTWAIT`).

### LINGER Setting

When `zlink_close()` is called and there are unsent messages remaining, it waits for the LINGER duration. For tests or when a fast shutdown is needed:

```c
int linger = 0;
zlink_setsockopt(socket, ZLINK_LINGER, &linger, sizeof(linger));
```

---
[← Socket Patterns](03-0-socket-patterns.md) | [PUB/SUB →](03-2-pubsub.md)
