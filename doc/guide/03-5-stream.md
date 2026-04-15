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

## 3. STREAM-Specific Behavior

STREAM is the only exception type in the raw socket family. Exactly one of
three receive models may be active on a given handle.

- **raw recv**: `zlink_recv()` pulls transport fragments directly. Pair it
  with a poller watching `ZLINK_POLLIN`.
- **raw callback**: `zlink_recv_handler()` delivers raw fragments through
  a callback. Useful for event-driven servers.
- **packet callback**: `zlink_stream_packet_handler()` delivers packets
  assembled from a fixed framing convention (2B header size + 4B body
  size + header + body, all big-endian) as header/body pairs.

The three models are mutually exclusive; a second attempt to activate a
different mode on the same handle fails with `EBUSY`. Applications pick
whichever model fits best.

STREAM-specific behavior:

- `source_rid` is auto-assigned per connection by the server,
  always fixed 4 bytes (`uint32`, big-endian).
- Connect/disconnect events are delivered as messages:

| payload | meaning |
|---|---|
| `0x01` (1 byte) | connect event |
| `0x00` (1 byte) | disconnect event |
| otherwise | regular data |

??? example "Full Sample Code -- Recv"

    | Language | Source |
    |----------|--------|
    | C | [stream_recv_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/stream_recv_sample.c) |
    | C++ | [stream_recv_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/stream_recv_sample.cpp) |
    | Java | [StreamRecvSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/StreamRecvSample.java) |
    | Python | [stream_recv.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/stream_recv.py) |
    | Node | [stream_recv_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/stream_recv_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/StreamRecv/Program.cs) |
    | Rust | [stream_recv_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/stream_recv_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/stream_recv_sample/main.go) |

---

## 4. Callback Example

In STREAM callbacks, connect/disconnect events must be distinguished from data.

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
> (default) or returns `ZLINK_SUBMIT_BACKPRESSURED` with `ZLINK_DONTWAIT`. For advanced
> backpressure patterns, see [Performance Guide](10-performance.md).

- Only one callback can be attached at a time; calling attach while a
  callback is already attached returns `ZLINK_HANDLER_BUSY`.
- The handler is permanent and cannot be detached for the lifetime of
  the socket.
- Close from inside the callback is not supported (returns `ZLINK_CLOSE_BUSY`).

??? example "Full Sample Code -- Callback"

    | Language | Source |
    |----------|--------|
    | C | [stream_callback_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/stream_callback_sample.c) |
    | C++ | [stream_callback_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/stream_callback_sample.cpp) |
    | Java | [StreamCallbackSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/StreamCallbackSample.java) |
    | Python | [stream_callback.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/stream_callback.py) |
    | Node | [stream_callback_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/stream_callback_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/StreamCallback/Program.cs) |
    | Rust | [stream_callback_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/stream_callback_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/stream_callback_sample/main.go) |

---

## 4.1 Packet Callback Mode

When the upstream protocol uses the fixed framing convention (2-byte
big-endian header size + 4-byte big-endian body size + header payload +
body payload), register a packet-level callback with
`zlink_stream_packet_handler()`. The core handles fragment accumulation
and length parsing, so the application receives assembled header/body
pairs directly.

```c
void on_packet(void *stream,
               const zlink_routing_id_t *source_rid,
               zlink_msg_t *header,
               zlink_msg_t *body,
               void *userdata)
{
    /* header and body are always valid zlink_msg_t objects. Length zero
       is still delivered as a valid msg_t (never NULL). */
    /* source_rid is a borrowed view valid only for the duration of the
       callback. Copy the value if you need to keep it afterwards. */

    /* ... process header / body ... */

    zlink_msg_close(header);
    zlink_msg_close(body);
}

zlink_stream_packet_handler(stream, on_packet, NULL);
```

Rules for packet callback mode:

- `header_size` or `body_size` equal to zero is allowed; both sides are
  still delivered as valid `zlink_msg_t` objects.
- Ownership of `header` and `body` is transferred to the callback. The
  callback must close or consume each `msg_t` exactly once.
- With packet handler attached, raw recv (`zlink_recv()`), raw callback
  (`zlink_recv_handler()`), and data-plane `ZLINK_POLLIN` registration on
  the same handle all fail with `EBUSY`. A second
  `zlink_stream_packet_handler()` attach also fails with `EBUSY`.
- Malformed packets (length exceeding implementation limits, assembly
  failure, premature close, etc.) result in the connection being closed
  as the default policy. Observe such events via the socket monitor.

This mode relieves the application from re-implementing fragment
accumulation, but it does not change the fact that transport fragment
boundaries differ from packet boundaries.

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

> STREAM runtime environment variables and internal tuning constants
> are documented in [STREAM internals](../internals/stream-socket.md).

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
[← ROUTER](03-4-router.md) | [Proxy →](03-6-proxy.md) | [Transport →](04-transports.md)
