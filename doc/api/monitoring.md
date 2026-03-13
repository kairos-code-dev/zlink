[English](monitoring.md) | [한국어](monitoring.ko.md)

# Monitoring & Peer Info API Reference

The canonical event catalog now lives in [events.md](events.md). This file
focuses on monitor APIs, callbacks, and peer-inspection helpers.

## Current API Direction

There are two distinct monitoring layers:

- Raw socket monitoring:
  `zlink_socket_monitor_open()` with a callback handler.
- Service monitoring:
  `zlink_*_monitor_open()` with a callback handler and
  `zlink_service_monitor_close()`.

All monitor events are dispatched through callbacks registered at monitor
creation time. There is no `recv()` or polling-based consumption.

Use raw socket monitors for transport/socket diagnostics. Use service
monitors for local service state transitions such as readiness, route
changes, and SPOT filter application.

## Types

### zlink_monitor_event_t

Describes a single monitor event received from a socket monitor.

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

### zlink_monitor_handler_fn

```c
typedef void (*zlink_monitor_handler_fn) (
  const zlink_monitor_event_t *event_);
```

Callback for socket monitor events, invoked on the I/O thread.

### zlink_peer_info_t

Contains information about a single connected peer.

```c
typedef struct {
    zlink_routing_id_t routing_id;
    char remote_addr[256];
    uint64_t connected_time;
    uint64_t msgs_sent;
    uint64_t msgs_received;
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
} zlink_peer_info_t;
```

| Field | Description |
|---|---|
| `routing_id` | The peer's routing identity. |
| `remote_addr` | Null-terminated remote address of the peer. |
| `connected_time` | Timestamp (epoch milliseconds) when the peer connected. |
| `msgs_sent` | Number of messages sent to this peer. |
| `msgs_received` | Number of messages received from this peer. |
| `snd_pending_msgs` | Local outbound queue backlog (messages not yet consumed by peer). |
| `rcv_pending_msgs` | Approximate local inbound backlog snapshot. |

## Constants

### Event Flags

Bitmask constants passed to `zlink_socket_monitor_open()` to select which
events to observe. Multiple flags can be combined with bitwise OR.

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
| `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL` | `0x2000` | Handshake failed due to a protocol error. |
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

### zlink_socket_monitor_open

Open and return a socket monitor handle with a fixed callback.

```c
void *zlink_socket_monitor_open (void *s_,
                                 zlink_socket_monitor_event_mask_t events_,
                                 zlink_monitor_handler_fn handler_);
```

Creates a monitor on socket `s_` and returns a handle. Events matching the
`events_` bitmask are dispatched through the `handler_` callback on the I/O
thread. The returned handle must be closed with `zlink_close()` when no
longer needed.

**Returns:** Monitor handle on success, or NULL on failure (errno is set).

**Thread safety:** Must be called from the socket's owning thread.

**See also:** `zlink_close`

---

### zlink_socket_peer_info

Get peer info by routing identity.

```c
int zlink_socket_peer_info (void *socket_,
                            const zlink_routing_id_t *routing_id_,
                            zlink_peer_info_t *info_);
```

Looks up the peer identified by `routing_id_` and fills the `info_` structure.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_socket_peer_routing_id`, `zlink_socket_peers`

---

### zlink_socket_peer_routing_id

Get a peer's routing identity by index.

```c
int zlink_socket_peer_routing_id (void *socket_,
                                  int index_,
                                  zlink_routing_id_t *out_);
```

Retrieves the routing identity of the peer at position `index_` (zero-based).

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_socket_peer_count`, `zlink_socket_peer_info`

---

### zlink_socket_peer_count

Return the number of connected peers.

```c
int zlink_socket_peer_count (void *socket_);
```

**Returns:** Number of connected peers (>= 0), or -1 on failure (errno is set).

**See also:** `zlink_socket_peer_routing_id`, `zlink_socket_peers`

---

### zlink_socket_peers

Get info for all connected peers as an array.

```c
int zlink_socket_peers (void *socket_,
                        zlink_peer_info_t *peers_,
                        size_t *count_);
```

Fills the `peers_` array with information about every connected peer. On
input, `*count_` must contain the capacity of the array. On output,
`*count_` is set to the actual number of peers written.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_socket_peer_count`, `zlink_socket_peer_info`

---

## Service Monitor API

Service monitors provide state transition events for service-layer
components (Discovery, Gateway, SPOT Pub, SPOT Sub). Unlike raw socket
monitors that report transport-level events, service monitors report
higher-level events such as readiness, route changes, and SPOT filter
application. All events are dispatched through callbacks fixed at monitor
creation time.

### zlink_service_event_t

Describes a single service monitor event.

```c
typedef struct zlink_service_event_t
{
    zlink_service_kind_t service_kind;
    uint32_t event_type;
    int32_t status;
    int32_t error_code;
    uint32_t value;
    zlink_service_event_detail_mask_t detail_flags;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
} zlink_service_event_t;
```

| Field | Description |
|-------|-------------|
| `service_kind` | One of the `ZLINK_SERVICE_KIND_*` constants. |
| `event_type` | Bitmask indicating the event. |
| `status` | Event-specific status code. |
| `error_code` | Error code when `event_type` indicates a failure. |
| `value` | Event-specific numeric value. |
| `detail_flags` | Bitmask of `ZLINK_EVENT_DETAIL_*` flags indicating which optional fields are populated. |
| `service_name` | Null-terminated service name, valid when `ZLINK_EVENT_DETAIL_SERVICE_NAME` is set. |
| `endpoint` | Null-terminated endpoint, valid when `ZLINK_EVENT_DETAIL_ENDPOINT` is set. |
| `routing_id` | Routing identity, valid when the corresponding detail flag is set. |

### zlink_service_monitor_handler_fn

```c
typedef void (*zlink_service_monitor_handler_fn) (
  const zlink_service_event_t *event_);
```

Callback for service monitor events, invoked on the I/O thread.

### Service Kind Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_SERVICE_KIND_DISCOVERY` | 1 | Discovery component |
| `ZLINK_SERVICE_KIND_GATEWAY` | 2 | Gateway component |
| `ZLINK_SERVICE_KIND_SPOT_SUB` | 3 | SPOT Subscriber component |
| `ZLINK_SERVICE_KIND_SPOT_PUB` | 4 | SPOT Publisher component |

### Service Event Constants

#### Common Events

| Constant | Bit | Description |
|----------|-----|-------------|
| `ZLINK_MONITOR_EVENT_READY` | `1 << 0` | Service is ready |
| `ZLINK_MONITOR_EVENT_LOST` | `1 << 1` | Service connectivity lost |
| `ZLINK_MONITOR_EVENT_PEER_UP` | `1 << 2` | A peer connected |
| `ZLINK_MONITOR_EVENT_PEER_DOWN` | `1 << 3` | A peer disconnected |
| `ZLINK_MONITOR_EVENT_ERROR` | `1 << 4` | An error occurred |
| `ZLINK_MONITOR_EVENT_CLOSED` | `1 << 17` | Monitor closed |

#### Discovery Events

| Constant | Bit | Description |
|----------|-----|-------------|
| `ZLINK_DISCOVERY_SERVICE_UP` | `1 << 5` | A discovered service came up |
| `ZLINK_DISCOVERY_SERVICE_DOWN` | `1 << 6` | A discovered service went down |
| `ZLINK_DISCOVERY_PROVIDERS_CHANGED` | `1 << 7` | The set of providers for a service changed |

#### Gateway Events

| Constant | Bit | Description |
|----------|-----|-------------|
| `ZLINK_GATEWAY_SERVICE_READY` | `1 << 8` | Gateway service is ready (at least one peer connected) |
| `ZLINK_GATEWAY_SERVICE_LOST` | `1 << 9` | Gateway service lost (all peers disconnected) |
| `ZLINK_GATEWAY_CONNECTION_COUNT_CHANGED` | `1 << 10` | Number of connected peers changed |
| `ZLINK_GATEWAY_ROUTE_UP` | `1 << 11` | A route came up |
| `ZLINK_GATEWAY_ROUTE_DOWN` | `1 << 12` | A route went down |

#### SPOT Events

| Constant | Bit | Description |
|----------|-----|-------------|
| `ZLINK_SPOT_SUB_FILTER_APPLIED` | `1 << 13` | Subscription filter applied |
| `ZLINK_SPOT_SUB_SUBSCRIPTION_READY` | `1 << 14` | Subscription is ready to receive |
| `ZLINK_SPOT_PUB_QUEUE_FULL` | `1 << 15` | Async publish queue is full |
| `ZLINK_SPOT_PUB_QUEUE_DRAINED` | `1 << 16` | Async publish queue drained |

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
void *zlink_discovery_monitor_open (
  void *discovery,
  zlink_discovery_monitor_event_mask_t events,
  zlink_service_monitor_handler_fn handler);
```

Creates a service monitor that dispatches events matching the `events`
bitmask through the `handler` callback. The callback is fixed at open time
and cannot be replaced.

**Returns:** Monitor handle on success, or `NULL` on failure (errno is set).

**Thread safety:** The monitor handle itself is a thread-safe child handle.

**See also:** `zlink_service_monitor_close`

---

### zlink_gateway_monitor_open

Open a service monitor for a Gateway instance.

```c
void *zlink_gateway_monitor_open (
  void *gateway,
  zlink_gateway_monitor_event_mask_t events,
  zlink_service_monitor_handler_fn handler);
```

Creates a service monitor that dispatches Gateway events through the
`handler` callback.

**Returns:** Monitor handle on success, or `NULL` on failure (errno is set).

**Thread safety:** The monitor handle itself is a thread-safe child handle.

**See also:** `zlink_service_monitor_close`

---

### zlink_spot_monitor_open

Open a role-specific service monitor for a unified SPOT handle.

```c
void *zlink_spot_monitor_open (void *spot,
                               zlink_spot_role_t role,
                               zlink_spot_monitor_event_mask_t events,
                               zlink_service_monitor_handler_fn handler);
```

`role` is `ZLINK_SPOT_ROLE_PUB` or `ZLINK_SPOT_ROLE_SUB`. Events are
dispatched through the `handler` callback.

**Returns:** Monitor handle on success, or `NULL` on failure (errno is set).

**Thread safety:** The monitor handle itself is a thread-safe child handle.

**See also:** `zlink_service_monitor_close`

---

### zlink_service_monitor_close

Close a service monitor handle and release its resources.

```c
int zlink_service_monitor_close (void **monitor_p);
```

Closes the monitor and sets `*monitor_p` to `NULL`. If another thread is
executing the monitor callback, the close fails with `errno=EBUSY`.
Self-close from the callback succeeds and is deferred until the callback
returns.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_discovery_monitor_open`, `zlink_gateway_monitor_open`,
`zlink_spot_monitor_open`
