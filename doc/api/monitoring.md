[English](monitoring.md) | [한국어](monitoring.ko.md)

# Monitoring API Reference

The canonical event catalog now lives in [events.md](events.md). This file
focuses on monitor APIs, callbacks, and monitor snapshots.

## Current API Direction

There are two distinct monitoring classes:

- Socket monitoring:
  `zlink_socket_monitor_open()` with `zlink_socket_monitor_open_options_t`
- Service monitoring:
  `zlink_service_monitor_open()` with `zlink_service_monitor_open_options_t`

Both classes follow the same **recv/callback delivery model**:

1. **Open** -- the monitor starts in **recv model**. Use the corresponding
   `*_recv()` function to pull events.
2. **Attach handler** -- calling `*_handler()` transitions the monitor to
   **callback-only model** (one-way). After transition `*_recv()` returns
   `EBUSY`.
3. **Snapshot** -- `zlink_monitor_snapshot()` works in both models.

All monitors are closed with `zlink_monitor_close()`.

Use socket monitors for transport/socket diagnostics. Use service
monitors for local service state transitions such as readiness, route
changes, and SPOT filter application.

## Types

### zlink_monitor_event_t / zlink_socket_monitor_event_t

Describes a single monitor event received from a socket monitor.
`zlink_socket_monitor_event_t` is a typedef of `zlink_monitor_event_t`.

```c
typedef struct {
    uint64_t event;
    uint64_t value;
    zlink_routing_id_t routing_id;
    char local_addr[256];
    char remote_addr[256];
} zlink_monitor_event_t;

typedef zlink_monitor_event_t zlink_socket_monitor_event_t;
```

| Field | Description |
|---|---|
| `event` | Bitmask indicating the event type (one of the `ZLINK_EVENT_*` constants). |
| `value` | Event-specific value. For connection events this is the file descriptor; for error events it is the errno or protocol error code; for disconnect events it is a `ZLINK_DISCONNECT_*` reason. For `*_READY_CHANGED` events it is reserved and must not be interpreted as an aggregate ready count. |
| `routing_id` | The routing identity of the peer involved in the event, if applicable. |
| `local_addr` | Null-terminated local endpoint address string. |
| `remote_addr` | Null-terminated remote endpoint address string. |

### zlink_monitor_handler_fn / zlink_socket_monitor_handler_fn

```c
typedef void (*zlink_monitor_handler_fn) (
  const zlink_monitor_event_t *event_, void *userdata_);

typedef zlink_monitor_handler_fn zlink_socket_monitor_handler_fn;
```

Callback for socket monitor events, invoked on the I/O thread.

### zlink_socket_monitor_open_options_t

```c
typedef struct zlink_socket_monitor_open_options_t
{
    zlink_socket_monitor_event_mask_t events;
} zlink_socket_monitor_open_options_t;
```

| Field | Description |
|---|---|
| `events` | Bitmask of `ZLINK_EVENT_*` flags selecting which events to observe. |

### zlink_monitor_snapshot_t

```c
typedef struct zlink_monitor_snapshot_t
{
    zlink_monitor_source_kind_t source_kind;
    zlink_monitor_state_mask_t state_flags;
    zlink_monitor_snapshot_detail_mask_t detail_flags;
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
} zlink_monitor_snapshot_t;
```

| Field | Description |
|---|---|
| `source_kind` | Snapshot source (`SOCKET`, `SPOT_PUB`, `SPOT_SUB`). |
| `state_flags` | Aggregate state bits such as `READY`, `BOUND_READY`, `SEND_READY`. |
| `detail_flags` | Indicates which numeric fields are populated. |
| `snd_pending_msgs` | Aggregate local outbound backlog in messages when supported. |
| `rcv_pending_msgs` | Aggregate local inbound backlog snapshot when supported. |

## Constants

### Monitor Source Kind

```c
typedef enum zlink_monitor_source_kind_t
{
    ZLINK_MONITOR_SOURCE_SOCKET   = 1,
    ZLINK_MONITOR_SOURCE_SPOT_PUB = 3,
    ZLINK_MONITOR_SOURCE_SPOT_SUB = 4
} zlink_monitor_source_kind_t;
```

### Monitor State Mask

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_MONITOR_STATE_READY` | `1 << 0` | The source is ready (at least one connection). |
| `ZLINK_MONITOR_STATE_BOUND_READY` | `1 << 1` | The source has a successful bind. |
| `ZLINK_MONITOR_STATE_SEND_READY` | `1 << 2` | The source can accept send operations. |
| `ZLINK_MONITOR_STATE_CLOSED` | `1 << 3` | The source has been closed. |

### Monitor Snapshot Detail Mask

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS` | `1 << 1` | `snd_pending_msgs` field is populated. |
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS` | `1 << 2` | `rcv_pending_msgs` field is populated. |

### Event Flags

Bitmask constants passed via `zlink_socket_monitor_open_options_t.events`
to select which events to observe. Multiple flags can be combined with
bitwise OR.

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
| `ZLINK_EVENT_CONNECTION_READY_CHANGED` | `0x1000` | Connection readiness changed. |
| `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL` | `0x2000` | Handshake failed due to a protocol error. |
| `ZLINK_EVENT_HANDSHAKE_FAILED_AUTH` | `0x4000` | Handshake failed due to authentication failure. |
| `ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED` | `0x8000` | SUB delivery readiness changed. |
| `ZLINK_EVENT_PUB_DELIVERY_READY_CHANGED` | `0x10000` | PUB delivery readiness changed. |
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

## Socket Monitor Functions

### zlink_socket_monitor_open

Open a socket monitor handle in recv model.

```c
void *zlink_socket_monitor_open (
  void *s_, const zlink_socket_monitor_open_options_t *options_);
```

Creates a monitor on socket `s_` and returns a handle. The `options_->events`
bitmask selects which events to observe. The monitor starts in **recv model**;
use `zlink_socket_monitor_recv()` to pull events, or call
`zlink_socket_monitor_handler()` to transition to callback model.

**Returns:** Monitor handle on success, or NULL on failure (errno is set).

**Thread safety:** Must be called from the socket's owning thread.

**See also:** `zlink_socket_monitor_recv`, `zlink_socket_monitor_handler`,
`zlink_monitor_close`

---

### zlink_socket_monitor_handler

Attach a callback handler to a socket monitor (one-way transition).

```c
int zlink_socket_monitor_handler (
  void *monitor_,
  zlink_socket_monitor_handler_fn handler_,
  void *userdata_);
```

Transitions the monitor from recv model to **callback-only model**. Once
attached, `zlink_socket_monitor_recv()` will return `-1` with
`errno = EBUSY`. This transition is one-way and cannot be reversed.

**Returns:** 0 on success, -1 on failure (errno is set).

---

### zlink_socket_monitor_recv

Receive the next event from a socket monitor in recv model.

```c
int zlink_socket_monitor_recv (
  void *monitor_, zlink_socket_monitor_event_t *out_);
```

Reads the next pending event into `out_`. If the monitor has transitioned to
callback model via `zlink_socket_monitor_handler()`, this function returns
`-1` with `errno = EBUSY`.

**Returns:** 0 on success, -1 on failure (errno is set).

---

### zlink_monitor_snapshot

```c
int zlink_monitor_snapshot (void *monitor_,
                             zlink_monitor_snapshot_t *out_);
```

Reads the current aggregate snapshot for a socket or service monitor handle.
The snapshot is queried from the monitor source at call time. Queue counts are
local message counts, and `rcv_pending_msgs` remains approximate. Works in
both recv model and callback model.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_socket_monitor_open`, `zlink_service_monitor_open`

---

### zlink_monitor_ignore_handler

No-op handler for suppressing socket monitor event callbacks.

```c
void zlink_monitor_ignore_handler (
  const zlink_monitor_event_t *event_, void *userdata_);
```

Pass this to `zlink_socket_monitor_handler()` when you want to transition to
callback model but discard all events (for example, when you only need
snapshot access and want to silence the event stream).

---

### zlink_monitor_close

Close any monitor handle (socket or service) and release its resources.

```c
int zlink_monitor_close (void **monitor_p_);
```

Closes the monitor and sets `*monitor_p_` to `NULL`. If another thread is
executing the monitor callback, the close fails with `errno = EBUSY`.
Self-close from within a callback succeeds and is deferred until the
callback returns.

This is the unified close function for **all** monitor types -- both socket
monitors and service monitors.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_socket_monitor_open`, `zlink_service_monitor_open`

---

## Service Monitor API

Service monitors provide state transition events for service-layer
components (Discovery and SPOT). Unlike raw socket monitors that
report transport-level events, service monitors report higher-level events
such as readiness, route changes, and SPOT filter application.

The target for `zlink_service_monitor_open()` is any service handle
(Discovery, Spot, or SpotNode). The service kind is determined
from the handle's runtime tag -- there is no per-service open function and
no `role` parameter. Internal pub/sub structure is hidden.

### zlink_service_event_t / zlink_service_monitor_event_t

Describes a single service monitor event. `zlink_service_monitor_event_t`
is a typedef of `zlink_service_event_t`.

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
    char subject[256];
    uint32_t subject_kind;
} zlink_service_event_t;

typedef zlink_service_event_t zlink_service_monitor_event_t;
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
| `subject` | Null-terminated subject string, valid when `ZLINK_EVENT_DETAIL_SUBJECT` is set. |
| `subject_kind` | Subject kind, valid when `ZLINK_EVENT_DETAIL_SUBJECT_KIND` is set. |

### zlink_service_monitor_handler_fn

```c
typedef void (*zlink_service_monitor_handler_fn) (
  const zlink_service_event_t *event_, void *userdata_);
```

Callback for service monitor events, invoked on the I/O thread.

### zlink_service_monitor_open_options_t

```c
typedef struct zlink_service_monitor_open_options_t
{
    zlink_service_monitor_event_mask_t events;
} zlink_service_monitor_open_options_t;
```

| Field | Description |
|---|---|
| `events` | Bitmask of service monitor event flags selecting which events to observe. Uses `zlink_service_monitor_event_mask_t`, the unified mask type for all service monitors. |

### Service Kind Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_SERVICE_KIND_DISCOVERY` | 1 | Discovery component |
| `ZLINK_SERVICE_KIND_SPOT_SUB` | 3 | SPOT Subscriber component |
| `ZLINK_SERVICE_KIND_SPOT_PUB` | 4 | SPOT Publisher component |

### Service Event Constants

#### Common Events

| Constant | Bit | Description |
|----------|-----|-------------|
| `ZLINK_MONITOR_EVENT_PEER_UP` | `1 << 2` | A peer connected |
| `ZLINK_MONITOR_EVENT_PEER_DOWN` | `1 << 3` | A peer disconnected |
| `ZLINK_MONITOR_EVENT_ERROR` | `1 << 4` | An error occurred |
| `ZLINK_MONITOR_EVENT_CLOSED` | `1 << 17` | Monitor closed |

#### Discovery Events

| Constant | Bit | Description |
|----------|-----|-------------|
| `ZLINK_DISCOVERY_MONITOR_EVENT_READY_CHANGED` | `1 << 0` | Discovery readiness changed; `value` is the current ready count |
| `ZLINK_DISCOVERY_SERVICE_UP` | `1 << 5` | A discovered service came up |
| `ZLINK_DISCOVERY_SERVICE_DOWN` | `1 << 6` | A discovered service went down |
| `ZLINK_DISCOVERY_PROVIDERS_CHANGED` | `1 << 7` | The set of providers for a service changed |

#### SPOT Events

| Constant | Bit | Description |
|----------|-----|-------------|
| `ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED` | `1 << 0` | SPOT readiness changed; `value` is the current ready count |
| `ZLINK_SPOT_SUB_FILTER_APPLIED` | `1 << 13` | Subscription filter applied |
| `ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED` | `1 << 14` | Subscription readiness changed; `value` is the current ready count |
| `ZLINK_SPOT_PUB_QUEUE_FULL` | `1 << 15` | PUB queue is full |
| `ZLINK_SPOT_PUB_QUEUE_DRAINED` | `1 << 16` | PUB queue has been drained |
| `ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED` | `1 << 18` | Subject-specific remote delivery-ready membership/state changed |
| `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED` | `1 << 19` | Subject-specific delivery-ready state changed |
| `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED` | `1 << 20` | Publisher-side first-delivery-safe readiness changed |

#### Service Monitor Event Mask Constants

The following `ZLINK_SERVICE_MONITOR_EVENT_*` constants are the canonical
names for building the `zlink_service_monitor_open_options_t.events` bitmask.
They map to the same underlying bits as the per-service constants above.

| Constant | Maps to |
|----------|---------|
| `ZLINK_SERVICE_MONITOR_EVENT_ERROR` | `ZLINK_MONITOR_EVENT_ERROR` |
| `ZLINK_SERVICE_MONITOR_EVENT_CLOSED` | `ZLINK_MONITOR_EVENT_CLOSED` |
| `ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_READY_CHANGED` | `ZLINK_DISCOVERY_MONITOR_EVENT_READY_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP` | `ZLINK_DISCOVERY_SERVICE_UP` |
| `ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN` | `ZLINK_DISCOVERY_SERVICE_DOWN` |
| `ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED` | `ZLINK_DISCOVERY_PROVIDERS_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_SPOT_READY_CHANGED` | `ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED` | `ZLINK_SPOT_SUB_FILTER_APPLIED` |
| `ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY_CHANGED` | `ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_SPOT_PUB_DELIVERY_READY_CHANGED` | `ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_SPOT_SUB_DELIVERY_READY_CHANGED` | `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_SPOT_FIRST_DELIVERY_READY_CHANGED` | `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_ALL` | All service events |

### Detail Flag Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_EVENT_DETAIL_SERVICE_NAME` | `0x0001` | `service_name` field is populated |
| `ZLINK_EVENT_DETAIL_ENDPOINT` | `0x0002` | `endpoint` field is populated |
| `ZLINK_EVENT_DETAIL_SUBJECT_RID` | `0x0004` | `routing_id` contains the subject identity |
| `ZLINK_EVENT_DETAIL_PEER_RID` | `0x0008` | `routing_id` contains a peer identity |
| `ZLINK_EVENT_DETAIL_SUBJECT` | `0x0010` | `subject` field is populated |
| `ZLINK_EVENT_DETAIL_SUBJECT_KIND` | `0x0020` | `subject_kind` field is populated |

---

### zlink_service_monitor_open

Open a unified service monitor in recv model.

```c
void *zlink_service_monitor_open (
  void *target_,
  const zlink_service_monitor_open_options_t *options_);
```

Creates a service monitor on any service handle and returns a handle.
`target_` can be a Discovery, Spot, or SpotNode handle -- the
service kind is determined from the handle's runtime tag. For Spot and
SpotNode targets, internal pub/sub structure is hidden; there is no `role`
parameter.

The `options_->events` bitmask selects which events to observe, using the
unified `zlink_service_monitor_event_mask_t` type.

The monitor starts in **recv model**; use `zlink_service_monitor_recv()` to
pull events, or call `zlink_service_monitor_handler()` to transition to
callback model.

**Returns:** Monitor handle on success, or `NULL` on failure (errno is set).

**Thread safety:** The monitor handle itself is a thread-safe child handle.

**See also:** `zlink_service_monitor_recv`, `zlink_service_monitor_handler`,
`zlink_monitor_close`

---

### zlink_service_monitor_handler

Attach a callback handler to a service monitor (one-way transition).

```c
int zlink_service_monitor_handler (
  void *monitor_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_);
```

Transitions the monitor from recv model to **callback-only model**. Once
attached, `zlink_service_monitor_recv()` will return `-1` with
`errno = EBUSY`. This transition is one-way and cannot be reversed.

**Returns:** 0 on success, -1 on failure (errno is set).

---

### zlink_service_monitor_recv

Receive the next event from a service monitor in recv model.

```c
int zlink_service_monitor_recv (
  void *monitor_, zlink_service_monitor_event_t *out_);
```

Reads the next pending event into `out_`. If the monitor has transitioned to
callback model via `zlink_service_monitor_handler()`, this function returns
`-1` with `errno = EBUSY`.

**Returns:** 0 on success, -1 on failure (errno is set).
