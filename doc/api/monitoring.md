[English](monitoring.md) | [한국어](monitoring.ko.md)

# Monitoring & Peer Info API Reference

The monitoring API lets you observe socket lifecycle events such as connections, disconnections, and handshake failures. The peer info API provides introspection into the set of peers currently connected to a ROUTER socket, including per-peer message counters and connection timestamps.

## Types

### zlink_monitor_event_t

Describes a single monitor event received from a socket monitor handle.

```c
typedef struct {
    uint64_t event;
    uint64_t value;
    zlink_routing_id_t routing_id;
    char local_addr[256];
    char remote_addr[256];
} zlink_monitor_event_t;
```

| Field | Description |
|---|---|
| `event` | Bitmask indicating the event type (one of the `ZLINK_EVENT_*` constants). |
| `value` | Event-specific value. For connection events this is the file descriptor; for error events it is the errno or protocol error code; for disconnect events it is a `ZLINK_DISCONNECT_*` reason. |
| `routing_id` | The routing identity of the peer involved in the event, if applicable. |
| `local_addr` | Null-terminated local endpoint address string. |
| `remote_addr` | Null-terminated remote endpoint address string. |

### zlink_peer_info_t

Contains information about a single connected peer.

```c
typedef struct {
    zlink_routing_id_t routing_id;
    char remote_addr[256];
    uint64_t connected_time;
    uint64_t msgs_sent;
    uint64_t msgs_received;
} zlink_peer_info_t;
```

| Field | Description |
|---|---|
| `routing_id` | The peer's routing identity. |
| `remote_addr` | Null-terminated remote address of the peer. |
| `connected_time` | Timestamp (epoch milliseconds) when the peer connected. |
| `msgs_sent` | Number of messages sent to this peer. |
| `msgs_received` | Number of messages received from this peer. |

## Constants

### Event Flags

Bitmask constants passed to `zlink_socket_monitor()` or `zlink_socket_monitor_open()` to select which events to observe. Multiple flags can be combined with bitwise OR.

| Constant | Value | Description |
|---|---|---|
| `ZLINK_EVENT_CONNECTED` | `0x0001` | Connection established to a remote peer. |
| `ZLINK_EVENT_CONNECT_DELAYED` | `0x0002` | Synchronous connect attempt failed; async retry scheduled. |
| `ZLINK_EVENT_CONNECT_RETRIED` | `0x0004` | Asynchronous connect retry in progress. |
| `ZLINK_EVENT_LISTENING` | `0x0008` | Socket successfully bound and listening. |
| `ZLINK_EVENT_BIND_FAILED` | `0x0010` | Bind attempt failed. |
| `ZLINK_EVENT_ACCEPTED` | `0x0020` | Incoming connection accepted. |
| `ZLINK_EVENT_ACCEPT_FAILED` | `0x0040` | Incoming connection accept failed. |
| `ZLINK_EVENT_CLOSED` | `0x0080` | Connection closed normally. |
| `ZLINK_EVENT_CLOSE_FAILED` | `0x0100` | Connection close failed. |
| `ZLINK_EVENT_DISCONNECTED` | `0x0200` | Session disconnected. The event value carries a `ZLINK_DISCONNECT_*` reason. |
| `ZLINK_EVENT_MONITOR_STOPPED` | `0x0400` | Monitor has been stopped and will produce no more events. |
| `ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL` | `0x0800` | Handshake failed with no further detail available. |
| `ZLINK_EVENT_CONNECTION_READY` | `0x1000` | Connection is ready for data transfer (handshake complete). |
| `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL` | `0x2000` | Handshake failed due to a protocol error. The event value carries a `ZLINK_PROTOCOL_ERROR_*` code. |
| `ZLINK_EVENT_HANDSHAKE_FAILED_AUTH` | `0x4000` | Handshake failed due to authentication failure. |
| `ZLINK_EVENT_ALL` | `0xFFFF` | Subscribe to all events. |

### Disconnect Reasons

Values carried in `zlink_monitor_event_t.value` when the event is `ZLINK_EVENT_DISCONNECTED`.

| Constant | Value | Description |
|---|---|---|
| `ZLINK_DISCONNECT_UNKNOWN` | `0` | Reason could not be determined. |
| `ZLINK_DISCONNECT_LOCAL` | `1` | Disconnect initiated by the local side. |
| `ZLINK_DISCONNECT_REMOTE` | `2` | Disconnect initiated by the remote peer. |
| `ZLINK_DISCONNECT_HANDSHAKE_FAILED` | `3` | Disconnect due to a handshake failure. |
| `ZLINK_DISCONNECT_TRANSPORT_ERROR` | `4` | Disconnect due to a transport-layer error. |
| `ZLINK_DISCONNECT_CTX_TERM` | `5` | Disconnect caused by context termination. |

### Protocol Errors

Values carried in `zlink_monitor_event_t.value` when the event is `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL`.

| Constant | Value | Description |
|---|---|---|
| `ZLINK_PROTOCOL_ERROR_ZMP_UNSPECIFIED` | `0x10000000` | Unspecified ZMP protocol error. |
| `ZLINK_PROTOCOL_ERROR_ZMP_UNEXPECTED_COMMAND` | `0x10000001` | Unexpected ZMP command received. |
| `ZLINK_PROTOCOL_ERROR_ZMP_INVALID_SEQUENCE` | `0x10000002` | Invalid ZMP command sequence. |
| `ZLINK_PROTOCOL_ERROR_ZMP_KEY_EXCHANGE` | `0x10000003` | ZMP key exchange failure. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_UNSPECIFIED` | `0x10000011` | Malformed ZMP command (unspecified). |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_MESSAGE` | `0x10000012` | Malformed ZMP MESSAGE command. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | `0x10000013` | Malformed ZMP HELLO command. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_INITIATE` | `0x10000014` | Malformed ZMP INITIATE command. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_ERROR` | `0x10000015` | Malformed ZMP ERROR command. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_READY` | `0x10000016` | Malformed ZMP READY command. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_WELCOME` | `0x10000017` | Malformed ZMP WELCOME command. |
| `ZLINK_PROTOCOL_ERROR_ZMP_INVALID_METADATA` | `0x10000018` | Invalid ZMP metadata. |
| `ZLINK_PROTOCOL_ERROR_ZMP_CRYPTOGRAPHIC` | `0x11000001` | ZMP cryptographic error. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MECHANISM_MISMATCH` | `0x11000002` | ZMP security mechanism mismatch. |
| `ZLINK_PROTOCOL_ERROR_WS_UNSPECIFIED` | `0x30000000` | Unspecified WebSocket protocol error. |

## Functions

### zlink_socket_monitor

Start a socket monitor via an inproc address. This is the legacy approach that requires you to create a separate SUB socket to receive events.

```c
int zlink_socket_monitor(void *s_, const char *addr_, int events_);
```

Registers a monitor on socket `s_` that will publish events to the inproc endpoint `addr_`. Only events matching the `events_` bitmask are published. You must create a `ZLINK_PAIR` socket, connect it to `addr_`, and receive monitor event frames manually.

For new code, prefer `zlink_socket_monitor_open()` which returns a ready-to-use monitor handle.

**Returns:** 0 on success, -1 on failure (errno is set).

**Thread safety:** Must be called from the socket's owning thread.

**See also:** `zlink_socket_monitor_open`, `zlink_monitor_recv`

---

### zlink_socket_monitor_open

Open and return a socket monitor handle directly. This is the preferred approach for monitoring socket events.

```c
void *zlink_socket_monitor_open(void *s_, int events_);
```

Creates a monitor on socket `s_` and returns an opaque monitor handle. The handle can be passed directly to `zlink_monitor_recv()` to receive structured event data. Only events matching the `events_` bitmask are delivered. Close the monitor handle with `zlink_close()` when finished.

**Returns:** Monitor handle on success, or NULL on failure (errno is set).

**Thread safety:** Must be called from the socket's owning thread.

**See also:** `zlink_monitor_recv`, `zlink_close`

---

### zlink_monitor_recv

Receive an event from a monitor handle.

```c
int zlink_monitor_recv(void *monitor_socket_, zlink_monitor_event_t *event_, int flags_);
```

Blocks until a monitor event is available on `monitor_socket_` (obtained from `zlink_socket_monitor_open()`), then fills the `event_` structure. Pass `ZLINK_DONTWAIT` in `flags_` to return immediately with `EAGAIN` if no event is pending.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:**

- `EAGAIN` -- No event available and `ZLINK_DONTWAIT` was specified.
- `ETERM` -- The context was terminated.

**Thread safety:** Must be called from the thread that owns the monitor handle.

**See also:** `zlink_socket_monitor_open`, `zlink_monitor_event_t`

---

### zlink_socket_peer_info

Get peer info by routing identity.

```c
int zlink_socket_peer_info(void *socket_, const zlink_routing_id_t *routing_id_, zlink_peer_info_t *info_);
```

Looks up the peer identified by `routing_id_` on the given ROUTER socket and fills the `info_` structure with its address, connection time, and message counters.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:**

- `EINVAL` -- The routing identity was not found or the socket is not a ROUTER.

**Thread safety:** Must be called from the socket's owning thread.

**See also:** `zlink_socket_peer_routing_id`, `zlink_socket_peers`

---

### zlink_socket_peer_routing_id

Get a peer's routing identity by index.

```c
int zlink_socket_peer_routing_id(void *socket_, int index_, zlink_routing_id_t *out_);
```

Retrieves the routing identity of the peer at position `index_` (zero-based) from the socket's internal peer table. Use together with `zlink_socket_peer_count()` to iterate over all peers.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:**

- `EINVAL` -- Index is out of range or the socket is not a ROUTER.

**Thread safety:** Must be called from the socket's owning thread.

**See also:** `zlink_socket_peer_count`, `zlink_socket_peer_info`

---

### zlink_socket_peer_count

Return the number of connected peers.

```c
int zlink_socket_peer_count(void *socket_);
```

Returns the current number of peers connected to the ROUTER socket `socket_`. The count may change between calls as peers connect and disconnect.

**Returns:** Number of connected peers (>= 0), or -1 on failure (errno is set).

**Thread safety:** Must be called from the socket's owning thread.

**See also:** `zlink_socket_peer_routing_id`, `zlink_socket_peers`

---

### zlink_socket_peers

Get info for all connected peers as an array.

```c
int zlink_socket_peers(void *socket_, zlink_peer_info_t *peers_, size_t *count_);
```

Fills the `peers_` array with information about every peer connected to the ROUTER socket. On input, `*count_` must contain the capacity of the array. On output, `*count_` is set to the actual number of peers written. If the array is too small, the call succeeds but only the first `*count_` (input) entries are written and `*count_` (output) reflects the total number of connected peers.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:**

- `EINVAL` -- The socket is not a ROUTER, or `peers_` or `count_` is NULL.

**Thread safety:** Must be called from the socket's owning thread.

**See also:** `zlink_socket_peer_count`, `zlink_socket_peer_info`
