[English](03-5-stream.md) | [한국어](03-5-stream.ko.md)

# STREAM Socket

## 1. Overview

STREAM is a **server-only** socket for communicating with **external raw clients**.

Core rules:
- `ZLINK_STREAM` supports `zlink_bind()` only.
- Calling `zlink_connect()` on `ZLINK_STREAM` returns `EOPNOTSUPP`.
- Clients must use OS/Asio/WebSocket raw client stacks, not zlink STREAM sockets.
- Wire format is `4-byte length (big-endian) + body`.
- At the zlink API level, messages are exposed as 2 frames: `[routing_id(4B)][payload]`.

Valid combination:

```
external raw client  <---- RAW(4B length + body) ---->  STREAM(server)
```

> STREAM is not directly compatible with zlink internal sockets (PAIR/PUB/SUB/DEALER/ROUTER).

---

## 2. Server Create/Bind

```c
void *stream = zlink_socket(ctx, ZLINK_STREAM);
int linger = 0;
zlink_setsockopt(stream, ZLINK_LINGER, &linger, sizeof(linger));
zlink_bind(stream, "tcp://0.0.0.0:8080");
```

Supported server transports:
- `tcp://`
- `tls://`
- `ws://`
- `wss://`

---

## 3. Message Model

### 3.1 Wire format

```
+----------------------+-------------------+
| body_len (4B, BE)    | body (N bytes)    |
+----------------------+-------------------+
```

### 3.2 zlink STREAM API frames

Application-visible shape on STREAM:

```
Frame 0: routing_id (4 bytes)
Frame 1: payload (N bytes)
```

- `routing_id` is auto-assigned per connection by the server.
- It is always fixed 4 bytes (`uint32`, big-endian).

### 3.3 Event payloads

| payload | meaning |
|---|---|
| `0x01` (1 byte) | connect event |
| `0x00` (1 byte) | disconnect event |
| otherwise | regular data |

---

## 4. Server Receive/Reply Pattern

```c
unsigned char rid[4];
unsigned char payload[4096];

int rid_size = zlink_recv(stream, rid, sizeof(rid), 0);  // must be 4
int more = 0;
size_t more_size = sizeof(more);
zlink_getsockopt(stream, ZLINK_RCVMORE, &more, &more_size);

int n = zlink_recv(stream, payload, sizeof(payload), 0);

if (n == 1 && payload[0] == 0x01) {
    // new client connected
} else if (n == 1 && payload[0] == 0x00) {
    // client disconnected
} else {
    // regular data, reply using same rid
    zlink_send(stream, rid, 4, ZLINK_SNDMORE);
    zlink_send(stream, payload, n, 0);
}
```

---

## 5. Client Implementation Rule

Clients must be implemented as raw socket/websocket clients.

Conceptual POSIX TCP example:

```c
// send: [4B length][body]
uint32_t len_be = htonl(body_len);
send(fd, &len_be, 4, 0);
send(fd, body, body_len, 0);

// recv: [4B length][body]
recv(fd, &len_be, 4, MSG_WAITALL);
uint32_t body_len = ntohl(len_be);
recv(fd, body, body_len, MSG_WAITALL);
```

---

## 6. Option Policy

Main supported options:
- `ZLINK_MAXMSGSIZE`, `ZLINK_SNDHWM`, `ZLINK_RCVHWM`, `ZLINK_SNDBUF`, `ZLINK_RCVBUF`, `ZLINK_BACKLOG`, `ZLINK_LINGER`
- TLS/WSS server options: `ZLINK_TLS_CERT`, `ZLINK_TLS_KEY`, `ZLINK_TLS_CA`, `ZLINK_TLS_HOSTNAME`, `ZLINK_TLS_TRUST_SYSTEM`

Unsupported/changed:
- Setting `ZLINK_CONNECT_ROUTING_ID` on STREAM returns `EOPNOTSUPP`.

---

## 7. Errors and Constraints

- `zlink_connect(stream, ...)` -> `EOPNOTSUPP`
- On STREAM, non-4-byte `routing_id` frame is a protocol error
- Messages larger than `MAXMSGSIZE` are dropped and connection is closed (disconnect event)

---

## 8. Reference Tests

- `core/tests/test_stream_socket.cpp`
- `core/tests/test_stream_fastpath.cpp`
- `core/tests/routing-id/test_connect_rid_string_alias.cpp`
- `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp`

These tests use STREAM server + raw client paths.

---
[← ROUTER](03-4-router.md) | [Transport →](04-transports.md)
