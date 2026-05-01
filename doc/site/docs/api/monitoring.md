[English](monitoring.md) | [한국어](monitoring.ko.md)

[Spec Index](../README.md) · [Core Index](README.md)

# Monitoring API Reference

The canonical event catalog lives in [events.md](events.md). This file
focuses on monitor APIs, callbacks, and monitor snapshots.

## API Structure

There is one public monitoring class:

- Socket monitoring:
  `zlink_socket_monitor_open()` with `zlink_socket_monitor_open_options_t`

Socket monitors follow this **recv/callback delivery model**:

1. **Open** -- the monitor starts in **recv model**. Use the corresponding
   `*_recv()` function to pull events.
2. **Attach handler** -- calling `*_handler()` transitions the monitor to
   **callback-only model** (one-way). After transition `*_recv()` returns
   `EBUSY`.
3. **Snapshot** -- `zlink_monitor_snapshot()` works in both models.

All monitors are closed with `zlink_monitor_close()`.

Use socket monitors for transport/socket diagnostics. Service-layer
observability is provided by Discovery, Registry, and SPOT snapshot/query
APIs rather than a separate public monitor handle.

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
    uint32_t auto_hwm_profile;
    uint32_t auto_hwm_role;
    uint32_t auto_hwm_policy_class;
    uint32_t auto_hwm_managed_connections;
    uint32_t auto_hwm_active_hwm_connections;
    uint32_t auto_hwm_observed_count;
    uint32_t auto_hwm_planning_count;
    uint32_t auto_hwm_context_total_planning_count;
    uint32_t auto_hwm_base_floor_per_connection;
    uint64_t auto_hwm_unit_budget_bytes;
    uint32_t auto_hwm_size_cap;
    uint32_t auto_hwm_effective_publish_fanout;
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
    uint64_t auto_hwm_socket_queue_share_bytes;
    uint64_t auto_hwm_socket_message_slots;
    uint64_t auto_hwm_effective_message_bytes;
    uint64_t auto_hwm_estimated_max_memory_bytes;
    uint64_t auto_hwm_last_recalc_ms;
    uint32_t auto_hwm_last_recalc_reason;
    uint32_t auto_hwm_send_blocked_ratio_ppm;
    uint32_t auto_hwm_scope;
    uint32_t auto_hwm_scope_count;
    uint64_t auto_hwm_auto_buffer_bytes;
    uint64_t auto_hwm_manual_buffer_bytes;
    uint32_t auto_hwm_buffer_connections;
    int32_t auto_hwm_deferred_sndhwm;
    int32_t auto_hwm_deferred_rcvhwm;
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
| `auto_hwm_profile` | Current automatic HWM profile. Values match `zlink_auto_hwm_profile_t`. |
| `auto_hwm_role` | Diagnostic role id. Current values are `1=control`, `2=routed`, `3=fanout`, `4=recv_ingress`, `5=spot_data`, `6=peer_queue`, `7=stream`; callers must tolerate future values. |
| `auto_hwm_policy_class` | Planner policy class used for unit-budget and size-cap selection. This is diagnostic and may grow. |
| `auto_hwm_managed_connections` | Diagnostic connection count when available. HWM is not divided by this value. |
| `auto_hwm_active_hwm_connections` | Diagnostic active connection count when available. HWM is not divided by this value. |
| `auto_hwm_observed_count` | Diagnostic observed connection count for this socket source. HWM is not reduced when this value grows. |
| `auto_hwm_planning_count` | Deprecated planning-count field. Current implementations fill `0`; callers must not use it as an HWM calculation input. |
| `auto_hwm_context_total_planning_count` | Deprecated context planning-count field. Current implementations fill `0`; callers must not use it as an HWM calculation input. |
| `auto_hwm_base_floor_per_connection` | Compatibility diagnostic floor value. The applied HWM is selected by profile, policy class, and message unit. |
| `auto_hwm_unit_budget_bytes` | Per-connection unit budget selected from the active profile and policy class. |
| `auto_hwm_size_cap` | Message-count cap selected from the active profile, policy class, and effective message size. |
| `auto_hwm_effective_publish_fanout` | SPOT publish fanout diagnostic when available. It does not make connection count reduce per-connection HWM. Non-SPOT rows may report `0`. |
| `auto_hwm_applied_sndhwm` | Currently applied send HWM on the socket. |
| `auto_hwm_applied_rcvhwm` | Currently applied recv HWM on the socket. |
| `auto_hwm_requested_sndbuf` | `SNDBUF` value requested by automatic policy. |
| `auto_hwm_requested_rcvbuf` | `RCVBUF` value requested by automatic policy. |
| `auto_hwm_effective_sndbuf` | Effective send buffer value reported in the snapshot. |
| `auto_hwm_effective_rcvbuf` | Effective recv buffer value reported in the snapshot. |
| `auto_hwm_total_memory_budget_bytes` | Deprecated context memory-budget field. Current implementations fill `0`. |
| `auto_hwm_queue_budget_bytes` | Deprecated queue-budget field. Current implementations fill `0`. |
| `auto_hwm_transport_budget_bytes` | Deprecated transport-budget field. Current implementations fill `0`. |
| `auto_hwm_runtime_reserve_bytes` | Deprecated runtime-reserve field. Current implementations fill `0`. |
| `auto_hwm_socket_queue_share_bytes` | Deprecated queue-share field. Current implementations fill `0`. |
| `auto_hwm_socket_message_slots` | Message slots derived from the selected unit budget and effective message unit. This is not a context-budget share. |
| `auto_hwm_effective_message_bytes` | Effective message unit in bytes used by the current policy calculation. |
| `auto_hwm_estimated_max_memory_bytes` | Estimated per-socket memory envelope when available; `0` means no estimate is reported. |
| `auto_hwm_last_recalc_ms` | Timestamp of the most recent auto-HWM recalculation in milliseconds. |
| `auto_hwm_last_recalc_reason` | Enum value that records why the latest recalculation ran. |
| `auto_hwm_send_blocked_ratio_ppm` | Parts-per-million ratio of send attempts that were blocked by backpressure. |
| `auto_hwm_scope` | Automatic HWM scope id. Current values are `0=none`, `1=shared`, `2=per_spot`; callers must tolerate future values. |
| `auto_hwm_scope_count` | Diagnostic scope target count. HWM is not divided by this value. |
| `auto_hwm_auto_buffer_bytes` | Auto-managed buffer diagnostic value when available. |
| `auto_hwm_manual_buffer_bytes` | User-managed buffer diagnostic value when available. |
| `auto_hwm_buffer_connections` | Deprecated buffer-accounting connection field. Current implementations fill `0`. |
| `auto_hwm_deferred_sndhwm` | Pending deferred send-HWM shrink, or `-1` when no shrink is deferred. |
| `auto_hwm_deferred_rcvhwm` | Pending deferred recv-HWM shrink, or `-1` when no shrink is deferred. |

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
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_AUTO_HWM_BUDGET` | `1 << 3` | Deprecated name kept for ABI compatibility. When set, auto-HWM role, profile, unit-budget, message-unit, and applied-HWM fields may be populated. Deprecated budget fields remain `0`. |
| `ZLINK_MONITOR_SNAPSHOT_DETAIL_AUTO_HWM_BUFFERS` | `1 << 4` | Auto-HWM transport-buffer fields are populated. |

### Auto-HWM Recalculation Reason

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_AUTO_HWM_RECALC_REASON_NONE` | `0` | No recalculation reason recorded. |
| `ZLINK_AUTO_HWM_RECALC_REASON_INITIAL` | `1` | Initial calculation. |
| `ZLINK_AUTO_HWM_RECALC_REASON_ROLE_CHANGE` | `2` | Socket role changed. |
| `ZLINK_AUTO_HWM_RECALC_REASON_POLICY_TOGGLE` | `3` | Automatic HWM policy was enabled or disabled. |
| `ZLINK_AUTO_HWM_RECALC_REASON_REFRESH` | `4` | Regular refresh. |
| `ZLINK_AUTO_HWM_RECALC_REASON_DEFERRED_SHRINK` | `5` | HWM shrink was deferred because current pending messages exceeded the new HWM. |

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

Reads the current aggregate snapshot for a socket monitor handle.
The snapshot is queried from the monitor source at call time. Queue counts are
local message counts, and `rcv_pending_msgs` is approximate. Works in
both recv model and callback model. When automatic HWM is enabled on the
target, the same snapshot also exposes the current role bucket, applied HWM
values, and the queue/transport budget used for the calculation.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_socket_monitor_open`

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

Close a socket monitor handle and release its resources.

```c
zlink_close_result_t zlink_monitor_close (void **monitor_p_);
```

Closes the monitor and sets `*monitor_p_` to `NULL`. If another thread is
executing the monitor callback, the close fails with `errno = EBUSY`.
Self-close from within a callback succeeds and is deferred until the
callback returns.

**Returns:** `ZLINK_CLOSE_OK` on success; otherwise a `zlink_close_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_socket_monitor_open`

---

## Service-Layer Observability

Discovery, Registry, and SPOT runtime state is observed through snapshot and
query APIs rather than a separate public event stream.

- Discovery: `zlink_discovery_member_peers()`,
  `zlink_discovery_member_peer_metadata()`
- Registry: `zlink_registry_status_snapshot()`,
  `zlink_registry_service_summary_snapshot()`,
  `zlink_registry_topology_snapshot()`,
  `zlink_registry_topology_query()`
- SpotNode: `zlink_spot_node_status_snapshot()`,
  `zlink_spot_node_peers_snapshot()`,
  `zlink_spot_node_peers_query()`,
  `zlink_spot_node_subjects_snapshot()`

Callers that need transition detection compare successive snapshots or query
results in application code. This keeps the public contract aligned with
`core/include/zlink.h`, which only exposes socket monitor handles.
