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
zlink_set_option(stream, ZLINK_OPT_LINGER, &linger, sizeof(linger));
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

## 4. Callback Dispatch (Receive/Reply)

STREAM starts in recv mode and also supports callback receive.
- In recv mode, pull multipart frames with `zlink_recv()`.
- Call `zlink_recv_handler()` when you want callback receive; after attach,
  direct recv and data-plane `ZLINK_POLLIN` fail with `EBUSY`.
- `zlink_send_ready_handler()` is independent from receive callback mode.
  After attach, data-plane `ZLINK_POLLOUT` fails with `EBUSY`.

### Callback Dispatch

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; i++) {
        void *data = zlink_msg_data(&parts[i]);
        size_t size = zlink_msg_size(&parts[i]);

        if (size == 1 && ((uint8_t *)data)[0] == 0x01) {
            /* new client connected */
        } else if (size == 1 && ((uint8_t *)data)[0] == 0x00) {
            /* client disconnected */
        } else {
            /* echo reply */
            zlink_msg_t reply;
            zlink_msg_init_size(&reply, size);
            memcpy(zlink_msg_data(&reply), data, size);
            zlink_send_rid(stream, source_rid, &reply, 1, 0);
        }
        zlink_msg_close(&parts[i]);
    }
}

/* Attach callback dispatch (permanent, cannot be undone) */
zlink_recv_handler(stream, on_message, NULL);
```

### Key Points

| Item | Description |
|---|---|
| Attach API | `zlink_recv_handler()` |
| Callback | `zlink_socket_msg_handler_fn` |
| Lifetime | Permanent once attached (no detach) |
| Framing | Raw bytes as received from the transport |
| Send | `zlink_send_rid()` |

> When the send queue is full (HWM), `zlink_send_rid()` blocks
> (default) or returns `EAGAIN` with `ZLINK_DONTWAIT`. For advanced
> backpressure patterns, see [Performance Guide](10-performance.md).

- Only one callback can be attached at a time; calling attach while a
  callback is already attached returns `-1` with `errno=EBUSY`.
- The handler is permanent and cannot be detached for the lifetime of
  the socket.
- Close from inside the callback is not supported (fails with `EBUSY`).

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

## 6. Option and Runtime Policy

Main supported options:
- `ZLINK_OPT_MAXMSGSIZE`, `ZLINK_OPT_SNDHWM`, `ZLINK_OPT_RCVHWM`, `ZLINK_OPT_SNDBUF`, `ZLINK_OPT_RCVBUF`, `ZLINK_OPT_BACKLOG`, `ZLINK_OPT_LINGER`
- TLS/WSS server: `zlink_set_tls_server()` / TLS client: `zlink_set_tls_client()`

Unsupported/changed:
- Setting `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` on STREAM returns `EOPNOTSUPP`.

### 6.1 Default STREAM runtime profile

Defaults currently used by STREAM internals:
- `ZLINK_OPT_BACKLOG`: `65536`
- `ZLINK_OPT_SNDBUF`: `262144` when unset (`-1`)
- `ZLINK_OPT_RCVBUF`: `262144` when unset (`-1`)
- minimum in/out batch size: `12288`
- STREAM accept concurrency default: `4` (clamped to max `128`)
- STREAM session scheduling default: `rr`

### 6.2 STREAM runtime environment knobs (still supported)

- `ZLINK_ASIO_STREAM_ACCEPT_CONCURRENCY` (default `4`, STREAM listener only)
- `ZLINK_ASIO_STREAM_SESSION_SCHED` (`rr|minload`, default `rr`)
- `ZLINK_ASIO_STREAM_ENABLE_NON_TCP_SPEC_READ` (default off)
- `ZLINK_ASIO_STREAM_DISABLE_GATHER` (default off; gather enabled)
- `ZLINK_ASIO_STREAM_NOTIFY_QUEUE_DEQUE` (default on)
- `ZLINK_ASIO_STREAM_BATCH_SIZE` (default `12288`)

### 6.3 STREAM tuning envs removed (fixed constants)

The following STREAM env-based toggles were removed and are now fixed in code:
- `ZLINK_ASIO_STREAM_ENABLE_HANDLER_ALLOC` -> always enabled
- `ZLINK_ASIO_STREAM_ENABLE_READ_DRAIN` -> always enabled
- `ZLINK_ASIO_STREAM_ENABLE_SPECULATIVE_WRITE` -> fixed on for STREAM/TCP path
- `ZLINK_ASIO_STREAM_ENABLE_RX_SLAB` -> always enabled
- `ZLINK_ASIO_STREAM_GATHER_THRESHOLD` -> fixed to `8192`
- `ZLINK_ASIO_STREAM_SPEC_WRITE_BUDGET_BYTES` -> fixed to `2097152`
- `ZLINK_ASIO_STREAM_READ_DRAIN_MAX_LOOPS` -> fixed to `16`
- `ZLINK_ASIO_STREAM_READ_DRAIN_MAX_BYTES` -> fixed to `1048576`

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
