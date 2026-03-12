[English](monitoring.md) | [한국어](monitoring.ko.md)

# Monitoring & Peer Info API Reference

## Current API Direction

There are now two distinct monitoring layers:

- Raw socket monitoring:
  `zlink_socket_monitor_open()` and `zlink_monitor_recv()`
- Service monitoring:
  `zlink_*_monitor_open()`, `zlink_service_monitor_recv()`, and
  `zlink_poller_add_monitor()`

Use raw socket monitors for transport/socket diagnostics. Use service
monitors for local service state transitions such as readiness, route
changes, registration results, and SPOT filter application.

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

---

## Service Monitor API

Service monitors provide state transition events for service-layer
components (Discovery, Gateway, Receiver, SPOT Pub, SPOT Sub). Unlike raw
socket monitors that report transport-level events, service monitors report
higher-level events such as readiness, route changes, registration results,
and SPOT filter application.

### zlink_service_event_t

Describes a single service monitor event.

```c
typedef struct zlink_service_event_t
{
    uint16_t service_kind;
    uint32_t event_type;
    int32_t status;
    int32_t error_code;
    uint32_t value;
    uint32_t detail_flags;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
} zlink_service_event_t;
```

| Field | Description |
|-------|-------------|
| `service_kind` | One of the `ZLINK_SERVICE_KIND_*` constants identifying the component type. |
| `event_type` | Bitmask indicating the event (one of the service event constants below). |
| `status` | Event-specific status code. |
| `error_code` | Error code when `event_type` indicates a failure. |
| `value` | Event-specific numeric value. |
| `detail_flags` | Bitmask of `ZLINK_EVENT_DETAIL_*` flags indicating which optional fields are populated. |
| `service_name` | Null-terminated service name, valid when `ZLINK_EVENT_DETAIL_SERVICE_NAME` is set. |
| `endpoint` | Null-terminated endpoint, valid when `ZLINK_EVENT_DETAIL_ENDPOINT` is set. |
| `routing_id` | Routing identity of the subject or peer, valid when the corresponding detail flag is set. |

### Service Kind Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_SERVICE_KIND_DISCOVERY` | 1 | Discovery component |
| `ZLINK_SERVICE_KIND_GATEWAY` | 2 | Gateway component |
| `ZLINK_SERVICE_KIND_RECEIVER` | 3 | Receiver component |
| `ZLINK_SERVICE_KIND_SPOT_SUB` | 4 | SPOT Subscriber component |
| `ZLINK_SERVICE_KIND_SPOT_PUB` | 5 | SPOT Publisher component |

### Service Event Constants

#### Common Events

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_MONITOR_EVENT_READY` | `1 << 0` | Service is ready |
| `ZLINK_MONITOR_EVENT_LOST` | `1 << 1` | Service connectivity lost |
| `ZLINK_MONITOR_EVENT_PEER_UP` | `1 << 2` | A peer connected |
| `ZLINK_MONITOR_EVENT_PEER_DOWN` | `1 << 3` | A peer disconnected |
| `ZLINK_MONITOR_EVENT_ERROR` | `1 << 4` | An error occurred |
| `ZLINK_MONITOR_EVENT_CLOSED` | `1 << 21` | Monitor closed |

#### Discovery Events

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_DISCOVERY_SERVICE_UP` | `1 << 5` | A discovered service came up |
| `ZLINK_DISCOVERY_SERVICE_DOWN` | `1 << 6` | A discovered service went down |
| `ZLINK_DISCOVERY_PROVIDERS_CHANGED` | `1 << 7` | The set of providers for a service changed |

#### Gateway Events

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_GATEWAY_SERVICE_READY` | `1 << 8` | Gateway service is ready (at least one Receiver connected) |
| `ZLINK_GATEWAY_SERVICE_LOST` | `1 << 9` | Gateway service lost (all Receivers disconnected) |
| `ZLINK_GATEWAY_CONNECTION_COUNT_CHANGED` | `1 << 10` | Number of connected Receivers changed |
| `ZLINK_GATEWAY_ROUTE_UP` | `1 << 11` | A route to a Receiver came up |
| `ZLINK_GATEWAY_ROUTE_DOWN` | `1 << 12` | A route to a Receiver went down |

#### Receiver Events

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_RECEIVER_REGISTER_OK` | `1 << 13` | Registration succeeded |
| `ZLINK_RECEIVER_REGISTER_FAILED` | `1 << 14` | Registration failed |
| `ZLINK_RECEIVER_UNREGISTER_OK` | `1 << 15` | Unregistration succeeded |
| `ZLINK_RECEIVER_UNREGISTER_FAILED` | `1 << 16` | Unregistration failed |

#### SPOT Events

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_SPOT_SUB_FILTER_APPLIED` | `1 << 17` | Subscription filter applied |
| `ZLINK_SPOT_SUB_SUBSCRIPTION_READY` | `1 << 18` | Subscription is ready to receive |
| `ZLINK_SPOT_PUB_QUEUE_FULL` | `1 << 19` | Async publish queue is full |
| `ZLINK_SPOT_PUB_QUEUE_DRAINED` | `1 << 20` | Async publish queue drained |

### Detail Flag Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_EVENT_DETAIL_SERVICE_NAME` | `0x0001` | `service_name` field is populated |
| `ZLINK_EVENT_DETAIL_ENDPOINT` | `0x0002` | `endpoint` field is populated |
| `ZLINK_EVENT_DETAIL_SUBJECT_RID` | `0x0004` | `routing_id` contains the subject identity |
| `ZLINK_EVENT_DETAIL_PEER_RID` | `0x0008` | `routing_id` contains a peer identity |

---

### zlink_discovery_monitor_open

Open a service monitor for a Discovery instance.

```c
void *zlink_discovery_monitor_open(void *discovery, int events);
```

Creates a service monitor that delivers events matching the `events`
bitmask. Use `ZLINK_DISCOVERY_SERVICE_UP`, `ZLINK_DISCOVERY_SERVICE_DOWN`,
`ZLINK_DISCOVERY_PROVIDERS_CHANGED`, and the common event constants.

**Returns:** Monitor handle on success, or `NULL` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_service_monitor_recv`, `zlink_service_monitor_close`

---

### zlink_gateway_monitor_open

Open a service monitor for a Gateway instance.

```c
void *zlink_gateway_monitor_open(void *gateway, int events);
```

Creates a service monitor that delivers Gateway events matching the
`events` bitmask. Use `ZLINK_GATEWAY_SERVICE_READY`,
`ZLINK_GATEWAY_SERVICE_LOST`, `ZLINK_GATEWAY_ROUTE_UP`,
`ZLINK_GATEWAY_ROUTE_DOWN`, `ZLINK_GATEWAY_CONNECTION_COUNT_CHANGED`, and
the common event constants.

**Returns:** Monitor handle on success, or `NULL` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_service_monitor_recv`, `zlink_service_monitor_close`

---

### zlink_receiver_monitor_open

Open a service monitor for a Receiver instance.

```c
void *zlink_receiver_monitor_open(void *receiver, int events);
```

Creates a service monitor that delivers Receiver events matching the
`events` bitmask. Use `ZLINK_RECEIVER_REGISTER_OK`,
`ZLINK_RECEIVER_REGISTER_FAILED`, `ZLINK_RECEIVER_UNREGISTER_OK`,
`ZLINK_RECEIVER_UNREGISTER_FAILED`, and the common event constants.

**Returns:** Monitor handle on success, or `NULL` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_service_monitor_recv`, `zlink_service_monitor_close`

---

### zlink_spot_monitor_open

Open a role-specific service monitor for a unified SPOT handle.

```c
void *zlink_spot_monitor_open(void *spot,
                              zlink_spot_role_t role,
                              zlink_spot_monitor_event_mask_t events,
                              zlink_service_monitor_handler_fn handler);
```

`role` is `ZLINK_SPOT_ROLE_PUB` or `ZLINK_SPOT_ROLE_SUB`.

- For `ZLINK_SPOT_ROLE_SUB`, use `ZLINK_SPOT_SUB_FILTER_APPLIED`,
  `ZLINK_SPOT_SUB_SUBSCRIPTION_READY`, and the common event constants.
- For `ZLINK_SPOT_ROLE_PUB`, use `ZLINK_SPOT_PUB_QUEUE_FULL`,
  `ZLINK_SPOT_PUB_QUEUE_DRAINED`, and the common event constants.

**Returns:** Monitor handle on success, or `NULL` on failure (errno is set).

**Thread safety:** The monitor handle itself is a thread-safe child handle.

**See also:** `zlink_service_monitor_recv`, `zlink_service_monitor_close`

---

### zlink_service_monitor_recv

Receive an event from a service monitor handle.

```c
int zlink_service_monitor_recv(void *monitor,
                                zlink_service_event_t *event,
                                int flags);
```

Blocks until a service event is available on `monitor`, then fills the
`event` structure. Pass `ZLINK_DONTWAIT` in `flags` to return immediately
with `EAGAIN` if no event is pending. The monitor handle is obtained from
one of the `zlink_*_monitor_open()` functions.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EAGAIN` -- No event available and `ZLINK_DONTWAIT` was specified.
- `ETERM` -- The context was terminated.

**Thread safety:** Must be called from the thread that owns the monitor handle.

**See also:** `zlink_discovery_monitor_open`, `zlink_gateway_monitor_open`,
`zlink_receiver_monitor_open`, `zlink_spot_monitor_open`

---

### zlink_service_monitor_close

Close a service monitor handle and release its resources.

```c
int zlink_service_monitor_close(void **monitor_p);
```

Closes the monitor and sets `*monitor_p` to `NULL`. After closing, no
further events will be delivered through this monitor.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_service_monitor_recv`
