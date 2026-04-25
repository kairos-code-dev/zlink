[English](monitoring.md) | [한국어](monitoring.ko.md)

[Spec Index](../README.md) · [Core Index](README.md)

# Monitoring API Reference

The canonical event catalog lives in [events.md](events.md). This file
focuses on monitor APIs, callbacks, and monitor snapshots.

## API Structure

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
monitors only for services that still expose a public service-monitor
surface, such as Discovery.

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
| `value` | Event-specific value. For many failure events it carries errno or protocol codes. For `CONNECTION_READY` it is reserved and must not be interpreted as an aggregate ready count. |
| `routing_id` | Peer identity for peer-bound events. For peer-less events the field is still initialized and can be zero. |
| `local_addr` | Null-terminated local endpoint address string. Always initialized. |
| `remote_addr` | Null-terminated remote endpoint address string. Always initialized. |

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
    uint32_t auto_hwm_enabled;
    uint32_t auto_hwm_role;
    uint32_t auto_hwm_managed_connections;
    uint32_t auto_hwm_active_hwm_connections;
    uint32_t auto_hwm_planning_transport_connections;
    uint32_t auto_hwm_base_floor_per_connection;
    int32_t auto_hwm_applied_sndhwm;
    int32_t auto_hwm_applied_rcvhwm;
    int32_t auto_hwm_requested_sndbuf;
    int32_t auto_hwm_requested_rcvbuf;
    int32_t auto_hwm_effective_sndbuf;
    int32_t auto_hwm_effective_rcvbuf;
    uint64_t auto_hwm_total_memory_budget_bytes;
    uint64_t auto_hwm_queue_budget_bytes;
    uint64_t auto_hwm_transport_budget_bytes;
    uint64_t auto_hwm_runtime_reserve_bytes;
    uint64_t auto_hwm_group_budget_bytes;
    uint64_t auto_hwm_group_message_slots;
    uint64_t auto_hwm_effective_message_bytes;
    uint64_t auto_hwm_control_budget_bytes;
    uint64_t auto_hwm_routed_budget_bytes;
    uint64_t auto_hwm_fanout_budget_bytes;
    uint64_t auto_hwm_recv_ingress_budget_bytes;
    uint32_t auto_hwm_control_active_connections;
    uint32_t auto_hwm_routed_active_connections;
    uint32_t auto_hwm_fanout_active_connections;
    uint32_t auto_hwm_recv_ingress_active_connections;
    uint64_t auto_hwm_estimated_max_memory_bytes;
    uint64_t auto_hwm_last_recalc_ms;
    uint32_t auto_hwm_last_recalc_reason;
    uint32_t auto_hwm_send_blocked_ratio_ppm;
} zlink_monitor_snapshot_t;
```

| Field | Description |
|---|---|
| `source_kind` | Snapshot source (`SOCKET`, `SPOT_PUB`, `SPOT_SUB`). |
| `state_flags` | Aggregate state bits such as `READY`, `BOUND_READY`, `CLOSED`. `READY` is supported only on raw socket monitor sources and uses the same contract as `CONNECTION_READY`. |
| `detail_flags` | Indicates which numeric fields are populated. |
| `snd_pending_msgs` | Aggregate local outbound backlog in messages when supported. |
| `rcv_pending_msgs` | Aggregate local inbound backlog snapshot when supported. |
| `auto_hwm_enabled` | `1` when this source is currently using automatic HWM policy, otherwise `0`. |
| `auto_hwm_role` | Diagnostic role-bucket id. Current values are `1=control`, `2=routed`, `3=fanout`, `4=recv_ingress`; callers must tolerate future values. |
| `auto_hwm_managed_connections` | Connection count used by the current policy calculation. |
| `auto_hwm_active_hwm_connections` | Connection count actually used to divide HWM slots. |
| `auto_hwm_planning_transport_connections` | Connection count used for transport-buffer planning. |
| `auto_hwm_base_floor_per_connection` | Role-specific minimum HWM floor per connection. |
| `auto_hwm_applied_sndhwm` | Currently applied send HWM on the socket. |
| `auto_hwm_applied_rcvhwm` | Currently applied recv HWM on the socket. |
| `auto_hwm_requested_sndbuf` | `SNDBUF` value requested by automatic policy. |
| `auto_hwm_requested_rcvbuf` | `RCVBUF` value requested by automatic policy. |
| `auto_hwm_effective_sndbuf` | Effective send buffer value reported in the snapshot. |
| `auto_hwm_effective_rcvbuf` | Effective recv buffer value reported in the snapshot. |
| `auto_hwm_total_memory_budget_bytes` | Total context memory budget. |
| `auto_hwm_queue_budget_bytes` | Queue-budget portion used for HWM planning. |
| `auto_hwm_transport_budget_bytes` | Transport-buffer budget portion. |
| `auto_hwm_runtime_reserve_bytes` | Runtime reserve portion. |
| `auto_hwm_group_budget_bytes` | Queue budget assigned to the current role bucket. |
| `auto_hwm_group_message_slots` | Message slots available to the current role bucket after dividing by effective message size. |
| `auto_hwm_effective_message_bytes` | Effective message size used by the current policy calculation. |
| `auto_hwm_control_budget_bytes` | Queue budget assigned to the control role bucket. |
| `auto_hwm_routed_budget_bytes` | Queue budget assigned to the routed role bucket. |
| `auto_hwm_fanout_budget_bytes` | Queue budget assigned to the fanout role bucket. |
| `auto_hwm_recv_ingress_budget_bytes` | Queue budget assigned to the recv_ingress role bucket. |
| `auto_hwm_control_active_connections` | Active connection count recorded for the control role bucket in this snapshot. `0` when this source belongs to another role. |
| `auto_hwm_routed_active_connections` | Active connection count recorded for the routed role bucket in this snapshot. `0` when this source belongs to another role. |
| `auto_hwm_fanout_active_connections` | Active connection count recorded for the fanout role bucket in this snapshot. `0` when this source belongs to another role. |
| `auto_hwm_recv_ingress_active_connections` | Active connection count recorded for the recv_ingress role bucket in this snapshot. `0` when this source belongs to another role. |
| `auto_hwm_estimated_max_memory_bytes` | Estimated maximum memory envelope derived from the current context budget. |
| `auto_hwm_last_recalc_ms` | Timestamp of the most recent auto-HWM recalculation in milliseconds. |
| `auto_hwm_last_recalc_reason` | Enum value that records why the latest recalculation ran. |
| `auto_hwm_send_blocked_ratio_ppm` | Parts-per-million ratio of send attempts that were blocked by backpressure. |

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
| `ZLINK_MONITOR_STATE_READY` | `1 << 0` | Raw-socket ready level. For `SOCKET` it means a usable connection exists. SPOT sources do not use `READY`. |
| `ZLINK_MONITOR_STATE_BOUND_READY` | `1 << 1` | The source has a successful bind. |
| `ZLINK_MONITOR_STATE_CLOSED` | `1 << 3` | The source has been closed. |

### Monitor Snapshot Detail Mask

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS` | `1 << 1` | `snd_pending_msgs` field is populated. |
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS` | `1 << 2` | `rcv_pending_msgs` field is populated. |
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_AUTO_HWM_BUDGET` | `1 << 3` | Auto-HWM role, budget, and applied-HWM fields are populated. |
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_AUTO_HWM_BUFFERS` | `1 << 4` | Auto-HWM transport-buffer fields are populated. |

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
| `ZLINK_EVENT_CONNECTION_READY` | `0x1000` | Ready edge for raw sockets. Messaging may start immediately after this event on supported raw socket families. |
| `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL` | `0x2000` | Handshake failed due to a protocol error. |
| `ZLINK_EVENT_HANDSHAKE_FAILED_AUTH` | `0x4000` | Handshake failed due to authentication failure. |
| `ZLINK_EVENT_PEER_WEIGHT_CHANGED` | `0x8000` | A connected raw peer's weight changed. `value` carries the new `0..100` weight. Alias for `ZLINK_SOCKET_MONITOR_EVENT_PEER_WEIGHT_CHANGED`. |
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
zlink_handler_result_t zlink_socket_monitor_handler (
  void *monitor_,
  zlink_socket_monitor_handler_fn handler_,
  void *userdata_);
```

Transitions the monitor from recv model to **callback-only model**. Once
attached, `zlink_socket_monitor_recv()` will return a `zlink_recv_result_t`
indicating the monitor is in callback mode. This transition is one-way and
cannot be reversed.

**Returns:** `ZLINK_HANDLER_OK` on success; otherwise a `zlink_handler_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

---

### zlink_socket_monitor_recv

Receive the next event from a socket monitor in recv model.

```c
zlink_recv_result_t zlink_socket_monitor_recv (
  void *monitor_, zlink_socket_monitor_event_t *out_,
  zlink_recv_flags_t flags_);
```

Reads the next pending event into `out_`. The `flags_` parameter accepts
`ZLINK_DONTWAIT` for non-blocking operation. If the monitor has transitioned
to callback model via `zlink_socket_monitor_handler()`, this function returns
a `zlink_recv_result_t` indicating the monitor is in callback mode.

**Returns:** `ZLINK_RECV_OK` on success; otherwise a `zlink_recv_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

---

### zlink_monitor_snapshot

```c
zlink_config_result_t zlink_monitor_snapshot (void *monitor_,
                             zlink_monitor_snapshot_t *out_);
```

Reads the current aggregate snapshot for a socket or service monitor handle.
The snapshot is queried from the monitor source at call time. Queue counts are
local message counts, and `rcv_pending_msgs` is approximate. Works in
both recv model and callback model. When automatic HWM is enabled on the
target, the same snapshot also exposes the current role bucket, applied HWM
values, and the queue/transport budget used for the calculation.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

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
zlink_close_result_t zlink_monitor_close (void **monitor_p_);
```

Closes the monitor and sets `*monitor_p_` to `NULL`. If another thread is
executing the monitor callback, the close fails with `errno = EBUSY`.
Self-close from within a callback succeeds and is deferred until the
callback returns.

This is the unified close function for **all** monitor types -- both socket
monitors and service monitors.

**Returns:** `ZLINK_CLOSE_OK` on success; otherwise a `zlink_close_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_socket_monitor_open`, `zlink_service_monitor_open`

---

## Service Monitor API

Service monitors provide state transition events for service-layer
components that expose a public service-monitor surface. Unlike raw
socket monitors that report transport-level events, service monitors
report higher-level service events such as Discovery membership changes.

The target for `zlink_service_monitor_open()` is any service handle
that still exposes public service monitoring, such as Discovery, SPOT,
or SpotNode. The service kind is determined from the handle's runtime
tag -- there is no per-service open function and no `role` parameter.

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

### Supported Service Monitor Targets

`zlink_service_monitor_open()` is currently defined for handles that expose a
public service-monitor surface. Monitor targets are identified by
`zlink_monitor_target_kind_t`.

| Target | `zlink_monitor_target_kind_t` | Public recv surface |
|--------|-------------------------------|---------------------|
| `Discovery` handle | `ZLINK_MONITOR_TARGET_DISCOVERY = 2` | `zlink_service_monitor_recv()` |
| raw socket | `ZLINK_MONITOR_TARGET_SOCKET = 1` | `zlink_socket_monitor_recv()` |
| `Spot` facade | `ZLINK_MONITOR_TARGET_SPOT = 4` | `zlink_service_monitor_recv()` |
| `SpotNode` handle | `ZLINK_MONITOR_TARGET_SPOT_NODE = 5` | `zlink_service_monitor_recv()` |

`Spot` and `SpotNode` expose the generic service-monitor surface for
operational events. A caller opens a monitor on the `SpotNode` target with
`zlink_service_monitor_open()` and drains events with
`zlink_service_monitor_recv()`. The returned
`zlink_service_monitor_event_t` carries the usual monitor event fields.
There is no dedicated `SpotNode`-specific monitor recv API.

### Service Kind Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_SERVICE_KIND_DISCOVERY` | 1 | Discovery component |
| `ZLINK_SERVICE_KIND_SPOT_SUB` | 3 | SPOT sub-side service monitor event source |
| `ZLINK_SERVICE_KIND_SPOT_PUB` | 4 | SPOT pub-side service monitor event source |
| `ZLINK_SERVICE_KIND_SOCKET` | 5 | Socket family service |

### Service Event Constants

#### Common Events

| Constant | Bit | Description |
|----------|-----|-------------|
| `ZLINK_MONITOR_EVENT_ERROR` | `1 << 4` | An error occurred |
| `ZLINK_MONITOR_EVENT_CLOSED` | `1 << 17` | Monitor closed |

#### Discovery Events

| Constant | Bit | Description |
|----------|-----|-------------|
| `ZLINK_DISCOVERY_SERVICE_UP` | `1 << 5` | A discovered service came up |
| `ZLINK_DISCOVERY_SERVICE_DOWN` | `1 << 6` | A discovered service went down |
| `ZLINK_DISCOVERY_PROVIDERS_CHANGED` | `1 << 7` | The set of providers for a service changed |

#### Service Monitor Event Mask Constants

The following `ZLINK_SERVICE_MONITOR_EVENT_*` constants are the canonical
names for building the `zlink_service_monitor_open_options_t.events` bitmask.
They map to the same underlying bits as the per-service constants above.

| Constant | Maps to |
|----------|---------|
| `ZLINK_SERVICE_MONITOR_EVENT_ERROR` | `ZLINK_MONITOR_EVENT_ERROR` |
| `ZLINK_SERVICE_MONITOR_EVENT_CLOSED` | `ZLINK_MONITOR_EVENT_CLOSED` |
| `ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP` | `ZLINK_DISCOVERY_SERVICE_UP` |
| `ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN` | `ZLINK_DISCOVERY_SERVICE_DOWN` |
| `ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED` | `ZLINK_DISCOVERY_PROVIDERS_CHANGED` |
| `ZLINK_SERVICE_MONITOR_EVENT_PEER_WEIGHT_CHANGED` | Bit `1u << 8`. Discovery service monitors use this bit when a tracked peer's weight changes. `Spot` / `SpotNode` generic service monitors do not currently emit it. |
| `ZLINK_SERVICE_MONITOR_EVENT_ALL` | All service events |

### Spot / SpotNode Generic Events

`zlink_service_monitor_open(spot, ...)` and
`zlink_service_monitor_open(spot_node, ...)` support only the
operational events produced by the SPOT pub/sub monitor bridges.

| Event | `Spot` | `SpotNode` | Notes |
|-------|--------|------------|-------|
| `ZLINK_MONITOR_EVENT_ERROR` | Yes | Yes | Runtime or bridge error |
| `ZLINK_MONITOR_EVENT_CLOSED` | Yes | Yes | Terminal close event |
| `peer up` (`1u << 2`) | Yes | Yes | Peer became usable |
| `peer down` (`1u << 3`) | Yes | Yes | Peer became unusable |
| `connection ready` (`1u << 14`) | Yes | Yes | Data path ready |
| `sub filter applied` (`1u << 13`) | Yes | Yes | Sub filter applied |
| `pub queue full` (`1u << 15`) | Yes | Yes | Pub queue saturated |
| `pub queue drained` (`1u << 16`) | Yes | Yes | Pub queue recovered |

`SpotNode` monitor events are drained through the generic
`zlink_service_monitor_recv()` surface. The returned event carries
standard monitor fields; additional peer-level detail is available
through snapshot/query APIs (`zlink_spot_node_peers_snapshot()`,
`zlink_spot_node_peers_query()`).

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

Creates a service monitor on a service handle that exposes public service
monitoring and returns a handle. `target_` can be a Discovery handle,
SPOT facade, or SpotNode handle.

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
zlink_handler_result_t zlink_service_monitor_handler (
  void *monitor_,
  zlink_service_monitor_handler_fn handler_,
  void *userdata_);
```

Transitions the monitor from recv model to **callback-only model**. Once
attached, `zlink_service_monitor_recv()` will return a `zlink_recv_result_t`
indicating the monitor is in callback mode. This transition is one-way and
cannot be reversed.

**Returns:** `ZLINK_HANDLER_OK` on success; otherwise a `zlink_handler_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

---

### zlink_service_monitor_recv

Receive the next event from a service monitor in recv model.

```c
zlink_recv_result_t zlink_service_monitor_recv (
  void *monitor_, zlink_service_monitor_event_t *out_,
  zlink_recv_flags_t flags_);
```

Reads the next pending event into `out_`. The `flags_` parameter accepts
`ZLINK_DONTWAIT` for non-blocking operation. If the monitor has transitioned
to callback model via `zlink_service_monitor_handler()`, this function returns
a `zlink_recv_result_t` indicating the monitor is in callback mode.

**Returns:** `ZLINK_RECV_OK` on success; otherwise a `zlink_recv_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.
