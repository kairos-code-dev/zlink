[English](monitoring.md) | [한국어](monitoring.ko.md)

# Monitoring API Reference

The canonical event catalog now lives in [events.md](events.md). This file
focuses on monitor APIs, callbacks, and monitor snapshots.

## Current API Direction

There are two distinct monitoring layers:

- Raw socket monitoring:
  `zlink_socket_monitor_open()` and `zlink_close()`
- Service monitoring:
  `zlink_*_monitor_open()` and `zlink_service_monitor_close()`

Monitor handles support three patterns:

- callback delivery when a handler is supplied
- polling the monitor handle directly
- reading aggregate state with `zlink_monitor_snapshot()`

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

### zlink_monitor_snapshot_t

```c
typedef struct zlink_monitor_snapshot_t
{
    zlink_monitor_source_kind_t source_kind;
    zlink_monitor_state_mask_t state_flags;
    zlink_monitor_snapshot_detail_mask_t detail_flags;
    uint32_t ready_peer_count;
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
} zlink_monitor_snapshot_t;
```

| Field | Description |
|---|---|
| `source_kind` | Snapshot source (`SOCKET`, `GATEWAY`, `SPOT_PUB`, `SPOT_SUB`). |
| `state_flags` | Aggregate state bits such as `READY`, `BOUND_READY`, `SEND_READY`. |
| `detail_flags` | Indicates which numeric fields are populated. |
| `ready_peer_count` | Aggregate ready/connected peer count when supported. |
| `snd_pending_msgs` | Aggregate local outbound backlog in messages when supported. |
| `rcv_pending_msgs` | Aggregate local inbound backlog snapshot when supported. |

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
| `ZLINK_DISCONNECT_HANDSHAKE_FAILED` | `3` | Disconnect due to a handshake failure. |
| `ZLINK_DISCONNECT_TRANSPORT_ERROR` | `4` | Disconnect due to a transport-layer error. |
| `ZLINK_DISCONNECT_CTX_TERM` | `5` | Disconnect caused by context termination. |

### Protocol Errors

Values carried in `zlink_monitor_event_t.value` when the event is `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL`.

| Constant | Value | Description |
|---|---|---|
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | `0x10000013` | Malformed ZMP HELLO command. |

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

### zlink_monitor_snapshot

```c
int zlink_monitor_snapshot (void *monitor_,
                            zlink_monitor_snapshot_t *out_);
```

Reads the current aggregate snapshot for a socket or service monitor handle.
The snapshot is queried from the monitor source at call time. Queue counts are
local message counts, and `rcv_pending_msgs` remains approximate.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_socket_monitor_open`, `zlink_gateway_monitor_open`,
`zlink_spot_monitor_open`

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
| `ZLINK_GATEWAY_SERVICE_READY` | `1 << 8` | Local Gateway service bind/register became ready |
| `ZLINK_GATEWAY_SERVICE_LOST` | `1 << 9` | Local Gateway service publication was removed |
| `ZLINK_GATEWAY_SEND_READY_CHANGED` | `1 << 10` | Aggregate send-readiness changed; `value` is `0` or `1` |
| `ZLINK_GATEWAY_ROUTE_UP` | `1 << 11` | A route came up; `value` is current ready route count |
| `ZLINK_GATEWAY_ROUTE_DOWN` | `1 << 12` | A route went down; `value` is current ready route count |

#### SPOT Events

| Constant | Bit | Description |
|----------|-----|-------------|
| `ZLINK_SPOT_SUB_FILTER_APPLIED` | `1 << 13` | Subscription filter applied |
| `ZLINK_SPOT_SUB_SUBSCRIPTION_READY` | `1 << 14` | Subscription is ready to receive |
| `ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED` | `1 << 18` | Subject-specific remote delivery-ready count changed |
| `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED` | `1 << 19` | Subject-specific delivery-ready state changed |
| `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED` | `1 << 20` | Publisher-side first-delivery-safe ready count changed |

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
