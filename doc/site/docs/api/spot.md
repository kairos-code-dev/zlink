[English](./spot.md) | [한국어](./spot.ko.md)

[Spec Index](https://github.com/kairos-code-dev/zlink/blob/main/README.md) · [Core Index](https://github.com/kairos-code-dev/zlink/blob/main/doc/README.md) · [Service Common](./README.md)

# SPOT

This document describes only the current public SPOT contract in
`core/include/zlink.h`. Pre-implementation design notes belong in separate
draft documents.

## Overview

The public SPOT surface is split into two handles.

- `SpotNode`
  Owns SPOT topology, discovery-backed peer wiring, manual peer wiring,
  channel-call `DEALER` registration, and external publish ingress.
- `Spot`
  A data-plane facade created on top of an existing `SpotNode`.

Destroying a `Spot` facade does not destroy the backing `SpotNode`.

## Construction and teardown

```c
typedef enum zlink_spot_node_mode_t {
  ZLINK_SPOT_NODE_MODE_PUBSUB = 1,
  ZLINK_SPOT_NODE_MODE_ROUTED = 2,
  ZLINK_SPOT_NODE_MODE_ALL = 3
} zlink_spot_node_mode_t;

typedef struct zlink_spot_node_options_t {
  zlink_spot_node_mode_t mode;
} zlink_spot_node_options_t;

void *zlink_spot_node_new(
  void *ctx,
  const zlink_spot_node_options_t *options);
zlink_close_result_t zlink_spot_node_destroy(void **node_p);

void *zlink_spot_new(void *node);
zlink_close_result_t zlink_spot_destroy(void **spot_p);
```

- `zlink_spot_node_new()` creates a SPOT node runtime.
- `options == NULL` and `options->mode == 0` both select
  `ZLINK_SPOT_NODE_MODE_ALL`.
- Invalid mode values fail with `NULL` and `errno == EINVAL`.
- `PUBSUB` mode enables topic publish/subscribe and rejects routed APIs with
  `ENOTSUP`.
- `ROUTED` mode enables routed request/reply and direct routed send, and
  rejects topic publish/subscribe APIs with `ENOTSUP`.
- `ALL` mode enables both planes.
- `zlink_spot_new()` borrows an existing `SpotNode` and returns a unified
  facade.
- A `Spot` inherits the mode of its backing `SpotNode`; the mode cannot be
  changed after creation.
- Disabled planes fail without creating their internal sockets.
- `zlink_spot_destroy()` closes only the facade.
- `zlink_spot_destroy()` unregisters routed target lookup before closing owned
  subjects. Unread routed messages that were already queued for that Spot may
  be dropped during teardown; callers are not required to drain everything
  before destroy.
- `zlink_spot_node_destroy()` tears down the node runtime.

## Entry Spot

When a `SpotNode` is created, it internally creates an `Entry Spot` logical state.
The `Entry Spot` is owned by the `SpotNode` and cannot be removed by the application.
Every newly created Actor immediately belongs to the `Entry Spot`.

The `Entry Spot` has its own dispatch context. The application registers a dispatch
handler on this context to handle initial messages for new Actors, perform
authentication, select a target Spot, and so on.

### Entry Spot handle

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_entry_spot(
  void *node_,
  void **spot_out_);
```

- If `node_ == NULL`, fails with `ZLINK_CONFIG_INVALID_HANDLE` and `errno == EFAULT`.
- If `spot_out_ == NULL`, fails with `ZLINK_CONFIG_INVALID_ARGUMENT` and `errno == EINVAL`.
- On failure, sets `*spot_out_ = NULL`.
- On success, stores a new Entry Spot facade handle in `*spot_out_`.
- The returned facade supports standard `Spot` APIs such as `zlink_spot_dispatch_event_handler()`.
- The `Entry Spot` logical state is owned by the `SpotNode`. The returned facade is owned by
  the application and must be closed with `zlink_spot_destroy()`.
- `zlink_spot_destroy()` closes only the facade; it does not destroy the logical Entry Spot.
- Calling this API multiple times on the same node returns different facade handles that all
  point to the same logical Entry Spot.

### Entry Spot routing id

The `Entry Spot` has a routing id like any other `Spot`.
The default is a random routing id generated when the `SpotNode` is created.
To set a fixed rid, obtain a facade with `zlink_spot_node_entry_spot()` and call
`zlink_set_routing_id(entry_spot, data, size)`.

Entry Spot rid changes are only allowed during the **configuration phase**.
The configuration phase ends when the first Actor is created, a Discovery is attached,
the SpotNode is bound or connected, or a Spot owner/Actor active route is published.

- Changing the Entry Spot rid after any Actor has been created fails with
  `ZLINK_CONFIG_INVALID_STATE` and `errno == EBUSY`.
- The Entry Spot rid cannot be changed after it has been published as an Actor active route
  or Spot owner route.
- The Entry Spot rid must not duplicate any other live user Spot rid within the same `SpotNode`.

### Spot lookup

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spot_lookup(
  void *node_,
  const zlink_routing_id_t *spot_rid_,
  void **spot_out_);
```

- If `node_ == NULL`, fails with `ZLINK_CONFIG_INVALID_HANDLE` and `errno == EFAULT`.
- If `spot_rid_ == NULL` or `spot_out_ == NULL`, fails with `ZLINK_CONFIG_INVALID_ARGUMENT`
  and `errno == EINVAL`.
- If no live local Spot matches the rid, fails with `ZLINK_CONFIG_NOT_FOUND` and
  `errno == ENOENT`; `*spot_out_` is not modified.
- On success, stores a new owned Spot facade handle in `*spot_out_`. The application must
  close it with `zlink_spot_destroy()`.
- Looking up the Entry Spot rid returns an Entry Spot facade. The Entry Spot logical state
  is owned by the `SpotNode`, so closing the last facade does not remove it.
- Remote Spot lookup is handled by Discovery Spot owner resolve. This function only looks
  up Spots within the local `SpotNode`.

## SpotNode contract

SpotNode exposes HWM only as admission control from `Spot` into `SpotNode`.
The public options are `ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE`,
`ZLINK_SPOT_NODE_OPT_ROUTER_HWM`,
`ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE`, and
`ZLINK_SPOT_NODE_OPT_PUBSUB_HWM`. Both admission channels default to the
balanced profile, which maps to HWM `16`; compact maps to `4`, low latency
maps to `8`, and throughput maps to `32`. A positive numeric HWM overrides the profile for that
channel. Setting the numeric HWM to `0` clears the override and returns to the
profile value. Negative values and unknown profiles fail with `EINVAL`.

`Spot` handles do not accept common `ZLINK_OPT_SNDHWM` or
`ZLINK_OPT_RCVHWM` settings. A `Spot` captures the current SpotNode admission
HWM when it is created; later SpotNode HWM changes apply only to later
`Spot` handles. Relay and delivery sockets inside SpotNode use HWM `0`.
The removed direction-based SpotNode HWM options and queue hard-limit options
are not part of the public contract, and their old enum numbers are reserved.

SpotNode and Spot do not expose a public weight setting. Peer weight can be
configured only on raw ROUTER and DEALER sockets. Spot peer snapshots may still
show a `weight` field; it is a remote peer state value learned from discovery
or peer signaling, not a Spot/SpotNode local option.

SpotNode is a topology and configuration handle, not a topic publisher. Calling
`zlink_publish*()` or installing a send-ready handler on a SpotNode fails with
`ENOTSUP`; create a `Spot` facade and publish through that handle instead.

### Internal socket snapshot

```c
typedef struct zlink_spot_node_socket_snapshot_filter_t {
  zlink_spot_node_socket_owner_t owner;
  zlink_socket_type_t socket_type;
  char socket_name[64];
} zlink_spot_node_socket_snapshot_filter_t;

typedef struct zlink_spot_node_socket_snapshot_entry_t {
  zlink_spot_node_socket_owner_t owner;
  uint64_t owner_id;
  char owner_name[64];
  char socket_name[64];
  zlink_socket_type_t socket_type;
  uint32_t auto_hwm_visible;
  zlink_monitor_snapshot_t snapshot;
} zlink_spot_node_socket_snapshot_entry_t;

zlink_config_result_t zlink_spot_node_internal_sockets_snapshot(
  void *node,
  const zlink_spot_node_socket_snapshot_filter_t *filter,
  zlink_spot_node_socket_snapshot_entry_t *entries,
  size_t *count);
```

- The snapshot returns only internal sockets that already exist.
- The snapshot never creates disabled or lazy sockets.
- `entries == NULL` succeeds and writes the required row count to `*count`.
- If `*count` is too small, the call fails with `ENOBUFS` and writes the
  required row count.
- `owner` may be `ANY`, `NODE`, or `SPOT`.
- `socket_type` may be `ZLINK_SOCKET_ANY` or one of the public
  `ZLINK_SOCKET_*` values.
- `socket_name` filters by the exact internal socket name when non-empty.
- `auto_hwm_visible == 1` marks a row for default Auto-HWM perf output.
- `snapshot.auto_hwm_profile`, `snapshot.auto_hwm_policy_class`,
  `snapshot.auto_hwm_unit_budget_bytes`, `snapshot.auto_hwm_size_cap`, and
  `snapshot.auto_hwm_socket_message_slots` expose the active automatic HWM
  planner result for diagnostics.
- The current SPOT topology exposes these main node socket names:
  `ingress-sub`, `local-pub`, `mesh-pub`, `mesh-xsub`, `internal-router`, and
  `external-router`.
- `PUBSUB` mode does not create routed sockets, and `ROUTED` mode does not
  create topic sockets. Snapshot calls do not activate disabled planes.

### Topology and discovery

```c
zlink_config_result_t zlink_spot_node_set_pub_bind(void *node, const char *endpoint);
zlink_connect_result_t zlink_spot_node_connect_peer(void *node,
                                                    const char *peer_endpoint);
zlink_connect_result_t zlink_spot_node_disconnect_peer(void *node,
                                                       const char *peer_endpoint);
zlink_connect_result_t zlink_spot_node_disconnect_peer_rid(
  void *node,
  const zlink_routing_id_t *target_node_rid);
zlink_config_result_t zlink_spot_node_attach_discovery(void *node,
                                                       void *discovery);
```

- `zlink_spot_node_set_pub_bind()` binds the node endpoint.
- `zlink_spot_node_connect_peer()` and `disconnect_peer()` are for manual SPOT
  mesh wiring when the endpoint is known.
- `zlink_spot_node_disconnect_peer_rid()` disconnects a peer node by target
  node routing id. It does not target an individual spot routing id under that
  node.
- They fail with `EBUSY` when a discovery is already attached.
- The `Spot` facade has no separate peer-rid disconnect function because peer
  connections are owned by the `SpotNode` runtime.
- `zlink_spot_node_attach_discovery()` requires a discovery handle that exposes
  a SPOT channel view.
- A node may have only one active SPOT discovery view at a time.

### Channel-call socket registration

```c
zlink_config_result_t zlink_spot_node_attach_channel_dealer(
  void *node,
  void *discovery,
  void *dealer);

zlink_config_result_t zlink_spot_node_attach_channel_dealer_manual(
  void *node,
  const char *channel_name,
  void *dealer);

zlink_config_result_t zlink_spot_node_attach_pub_ingress(
  void *node,
  void *pub);
```

- `attach_channel_dealer()` registers a discovery-managed `DEALER`.
- `attach_channel_dealer_manual()` registers a caller-connected `DEALER` under
  the given `channel_name`.
- Automatic and manual attach share the same channel namespace. A second dealer
  for the same channel fails with `EBUSY`.
- Attach functions do not create sockets and do not call `connect()` for you.
- Attached dealers are dedicated to the `SpotNode`. The caller keeps ownership,
  but the socket must not be reused as a generic client elsewhere.
- `zlink_spot_node_attach_pub_ingress()` registers one external `PUB` as the
  node's publish ingress source.
- Only one ingress `PUB` may be attached to a node.

## Spot data-plane contract

### Channel send/request

```c
zlink_submit_result_t zlink_spot_send_channel(
  void *spot,
  const char *channel_name,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_submit_result_t zlink_spot_request_channel(
  void *spot,
  const char *channel_name,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

- Channel calls always use an attached `DEALER`.
- Lookup is keyed by `channel_name`.
- The request reply is bound to the specific dealer selected for that request.
- Channel request reply has separate owners:
  transport owner is the selected attached `DEALER`, while delivery owner is
  the originating `Spot` dispatch stream.
- A matched channel reply is delivered through
  `ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE` and the final callback
  runs from `zlink_spot_channel_reply_progress_from()`.
- Attached channel dealer metadata may be queried with
  `zlink_socket_get_channel_name()`. Manual setups may pre-set the same fixed
  metadata with `zlink_socket_set_channel_name()`.
- `Spot` does not expose ordinary one-way send targeting a `ROUTER` by direct `rid`.
  For direct routed request initiation see the dedicated section below.

### Topic publish/subscribe

```c
zlink_submit_result_t zlink_spot_publish(
  void *spot,
  const char *service_name,
  const char *topic_id,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_recv_result_t zlink_spot_subscribe(
  void *spot,
  zlink_routing_id_t *source_rid_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  char *service_name_out,
  size_t *service_name_len_out,
  char *topic_id_out,
  size_t *topic_id_len_out,
  zlink_recv_flags_t flags);

zlink_recv_result_t zlink_spot_subscription_event(
  void *spot,
  zlink_routing_id_t *source_rid_out,
  int *subscribed_out,
  char *service_name_out,
  size_t *service_name_len_out,
  char *topic_id_out,
  size_t *topic_id_len_out,
  zlink_recv_flags_t flags);
```

The topic plane still uses the public parameter name `service_name`.
That is the current contract name for the topic namespace.

- `zlink_spot_node_attach_pub_ingress()` joins this same topic ingress path.

### Routed recv/reply

```c
zlink_recv_result_t zlink_spot_recv(
  void *spot,
  const zlink_routing_id_t **source_rid_out,
  const zlink_routing_id_t **spot_rid_out,
  uint64_t *request_seq_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  zlink_recv_flags_t flags);

zlink_submit_result_t zlink_spot_reply_spot(
  void *spot,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  uint64_t request_seq,
  zlink_msg_t *parts,
  size_t part_count);

zlink_submit_result_t zlink_spot_reply_router(
  void *spot,
  const zlink_routing_id_t *peer_rid,
  uint64_t request_seq,
  zlink_msg_t *parts,
  size_t part_count);
```

- `zlink_spot_recv()` reads the routed receive plane.
- `zlink_spot_recv()` reads only the routed ingress owned by that Spot.
- The first `zlink_spot_recv()` must not bootstrap registration or open a
  hidden queue.
- `ZLINK_DONTWAIT` with `EAGAIN` means that Spot-owned routed ingress has no
  readable data at that moment.
- Reply with `zlink_spot_reply_spot()` when the origin is another SPOT.
- Reply with `zlink_spot_reply_router()` when the origin is a ROUTER.
- Routed receive metadata (`source_rid`, `spot_rid`, `request_seq`) must stay
  intact across local forward as well as remote delivery.

### Handlers

#### Dispatch event types

```c
typedef enum zlink_spot_dispatch_event_t {
  ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE     = 1,
  ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE        = 2,
  ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE         = 3,
  ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE = 4,
  ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE         = 5,
  ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE    = 6
} zlink_spot_dispatch_event_t;

typedef enum zlink_spot_dispatch_subject_kind_t {
  ZLINK_SPOT_DISPATCH_SUBJECT_SPOT           = 1,
  ZLINK_SPOT_DISPATCH_SUBJECT_TIMER          = 2,
  ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER = 3,
  ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR          = 4
} zlink_spot_dispatch_subject_kind_t;

typedef struct zlink_spot_dispatch_info_t {
  zlink_spot_dispatch_event_t        event;
  zlink_spot_dispatch_subject_kind_t subject_kind;
  void                              *subject;
} zlink_spot_dispatch_info_t;

typedef void (*zlink_spot_dispatch_event_handler_fn)(
  void *spot_,
  const zlink_spot_dispatch_info_t *info_,
  void *userdata_);
```

- `event` identifies the readable work plane.
- `subject_kind` tells the caller how to interpret `subject`.
- `subject` is the concrete drain target instance.
- `CHANNEL_REPLY_READABLE` means a channel request completion is ready for the
  originating `Spot`. It does not expose raw dealer receive.
- `SUBSCRIBE_READABLE` and `ROUTED_READABLE` are readiness notifications, not
  one-event-per-message delivery counters.
- `ACTOR_READABLE` means a specific Actor has unread parts ready. `subject` is
  a callback-lifetime `const zlink_actor_ref_t *`. The caller copies the value
  and drains it with `zlink_spot_node_actor_recv_part()`.
- `ACTOR_JOIN_READABLE` means the Spot has Actor join requests to process.
  Drain with `zlink_spot_actor_join_recv()` until `ZLINK_RECV_NO_DATA`.

Drain rules by dispatch subject:

| event | subject_kind | subject | drain |
|-------|--------------|---------|-------|
| `SUBSCRIBE_READABLE` | `SPOT` | `spot_` (or NULL) | `zlink_spot_subscribe()` |
| `ROUTED_READABLE` | `SPOT` | `spot_` (or NULL) | `zlink_spot_recv()` |
| `TIMER_READABLE` | `TIMER` | timer handle | `zlink_timer_recv()` |
| `CHANNEL_REPLY_READABLE` | `CHANNEL_DEALER` | attached dealer handle | `zlink_spot_channel_reply_progress_from()` |
| `ACTOR_READABLE` | `ACTOR` | callback-lifetime `const zlink_actor_ref_t *` | `zlink_spot_node_actor_recv_part()` |
| `ACTOR_JOIN_READABLE` | `SPOT` | `spot_` | `zlink_spot_actor_join_recv()` |

Dispatch priority is fixed as:

1. `SUBSCRIBE_READABLE`
2. `ROUTED_READABLE`
3. `CHANNEL_REPLY_READABLE`
4. `TIMER_READABLE`
5. `ACTOR_JOIN_READABLE`
6. `ACTOR_READABLE`

```c
zlink_handler_result_t zlink_spot_handler(
  void *spot,
  zlink_spot_handler_fn handler,
  void *userdata);

zlink_handler_result_t zlink_spot_dispatch_event_handler(
  void *spot,
  zlink_spot_dispatch_event_handler_fn handler,
  void *userdata);

int zlink_spot_channel_reply_progress_from(
  void *spot,
  void *dealer);

zlink_config_result_t zlink_socket_set_channel_name(
  void *socket,
  const char *channel_name);

zlink_config_result_t zlink_socket_get_channel_name(
  void *socket,
  char *channel_name_buf,
  size_t channel_name_capacity,
  size_t *channel_name_len_out);
```

`zlink_spot_handler()` is a **routed-only direct callback**. The callback receives
routed message payloads inline. Subscribe, channel reply, timer, and Actor events
are not delivered through this handler. If any of those event types are needed,
`zlink_spot_handler()` cannot be used.

`zlink_spot_dispatch_event_handler()` is a **unified readiness notification**. It
covers all event types (subscribe, routed, channel reply, timer, Actor join, Actor
readable). The callback signals that data is available; the caller reads it with the
corresponding drain API (`zlink_spot_recv()`, `zlink_spot_subscribe()`, etc.).

Subscribe, channel reply, timer, and Actor events have **no direct callback mode**.
They can only be consumed via `zlink_spot_dispatch_event_handler()` readiness.

The two handlers are mutually exclusive. Registering one while the other is active
fails with `ZLINK_HANDLER_BUSY`. Choosing `zlink_spot_handler()` means the Spot
cannot receive subscribe, channel reply, timer, or Actor events.

After `SUBSCRIBE_READABLE` or `ROUTED_READABLE`, callers must drain until the
corresponding pull API returns `ZLINK_RECV_NO_DATA` / `EAGAIN`.
`zlink_spot_channel_reply_progress_from()` drains channel reply completions
for the attached dealer identified by the dispatch `subject`.

## Spot routed request initiation

`Spot` can initiate routed requests and one-way direct sends.
The following paths are exposed.

### Core helper substrate

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_router_part (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);
```

### C API wrapper

```c
ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_spot (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_router (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_send_spot (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);
```

- `zlink_spot_request_spot()` pairs with `zlink_spot_reply_spot(_part)` on the replier side.
- `zlink_spot_request_router()` pairs with `zlink_router_reply_spot(_part)`.
- On `ZLINK_SUBMIT_OK` the handler is registered and called exactly once.
- On any other return value the handler is not registered.
- For the full result-code mapping see
  [errno-map.md](errno-map.md) `zlink_submit_result_t` and
  `zlink_request_result_t`.
- `zlink_spot_send_spot()` performs a one-way routed send to a destination `Spot`. No handler, no reply wait.
- Returns `ZLINK_SUBMIT_OK` when the message is accepted into the send path.

## Router-side direct SPOT addressing

ROUTER supports explicit destination addressing for one-way send and request.

```c
zlink_submit_result_t zlink_router_send_spot(
  void *router,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_submit_result_t zlink_router_request_spot(
  void *router,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_router_reply_spot(
  void *router,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  uint64_t request_seq,
  zlink_msg_t *parts,
  size_t part_count);
```

## Monitoring and snapshots

```c
zlink_config_result_t zlink_spot_node_status_snapshot(
  void *node,
  zlink_spot_node_status_t *out);

zlink_config_result_t zlink_spot_node_peers_snapshot(
  void *node,
  zlink_spot_node_peer_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_peers_query(
  void *node,
  const zlink_spot_node_peer_filter_t *filter,
  zlink_spot_node_peer_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_subjects_snapshot(
  void *node,
  const zlink_spot_node_subject_filter_t *filter,
  zlink_spot_node_subject_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_spots_snapshot(
  void *node,
  zlink_spot_node_spot_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_actors_snapshot(
  void *node,
  zlink_spot_node_actor_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_actors_snapshot(
  void *spot,
  zlink_actor_ref_t *entries,
  size_t *count);
```

There is no dedicated public SPOT-node monitor recv API. Use the
snapshot/query functions.

- `entries == NULL` causes a snapshot function to write only the required row
  count into `*count`.
- Insufficient `*count` fails with `ENOBUFS` and writes the required count.
- `zlink_spot_node_spots_snapshot()` returns the list of local Spots.
  Entry Spot is also included in this list.
  `joined_actor_count`, `pending_actor_join_count`, `route_synced`, and
  `last_changed_ms` are diagnostic values.
- `zlink_spot_node_actors_snapshot()` returns live local Actor rows. Each row
  contains the Actor ref, joined state, joined Spot rid, route sync state,
  unread message count, and `last_changed_ms`.
- `zlink_spot_actors_snapshot()` returns the list of Actor refs that are joined
  to a given Spot.
- Snapshot values must not be used as flow-control contracts.

`zlink_spot_node_status_t.disconnected_sub_target_count` and
`zlink_spot_node_status_t.disconnected_routed_target_count` remain in the
status structure for ABI compatibility. The current HWM policy does not
disconnect local subscribe or routed targets because a delivery queue grew.

## Relationship to Poller

The current public poller API is unchanged.

- `zlink_poller_event_t` does not carry owner Spot, dispatch event kind, or
  drain subject together.
- The canonical unified readable-notification surface for SPOT is
  `zlink_spot_dispatch_event_handler()`.

## Actor contract

An Actor is a routing target owned by `SpotNode`. Public Actor operations use
`zlink_actor_ref_t`, not an opaque pointer. Actors have no public socket
option, inproc endpoint, or transport endpoint. There is no per-Actor HWM
option. Unread parts that arrive at an Actor are read only through
`zlink_spot_node_actor_recv_part()`.

### Actor ref types

```c
#define ZLINK_ACTOR_ID_MAX 256

typedef struct zlink_actor_ref_t {
  zlink_routing_id_t node_rid;
  char actor_id[ZLINK_ACTOR_ID_MAX];
  uint64_t generation;
} zlink_actor_ref_t;

typedef struct zlink_actor_recv_info_t {
  zlink_actor_ref_t actor;
  zlink_routing_id_t source_node_rid;
  zlink_routing_id_t source_session_rid;
  uint32_t flags;
} zlink_actor_recv_info_t;

typedef struct zlink_actor_join_info_t {
  zlink_actor_ref_t source_actor;
  zlink_actor_ref_t target_actor;
  zlink_routing_id_t source_node_rid;
  zlink_routing_id_t source_spot_rid;
  zlink_routing_id_t target_node_rid;
  zlink_routing_id_t target_spot_rid;
  uint64_t join_epoch;
  void *request;
  uint32_t flags;
} zlink_actor_join_info_t;

typedef enum zlink_actor_create_status_t {
  ZLINK_ACTOR_CREATE_CREATED  = 1,
  ZLINK_ACTOR_CREATE_EXISTING = 2
} zlink_actor_create_status_t;

typedef struct zlink_actor_create_result_t {
  zlink_actor_create_status_t status;
  zlink_actor_ref_t actor;
} zlink_actor_create_result_t;

typedef enum zlink_actor_admission_result_t {
  ZLINK_ACTOR_ADMISSION_ACCEPT = 1,
  ZLINK_ACTOR_ADMISSION_REJECT = 2
} zlink_actor_admission_result_t;

typedef zlink_actor_admission_result_t (*zlink_actor_admission_handler_fn)(
  void *node,
  const char *actor_id,
  const zlink_msg_t *message,
  void *userdata);
```

An actor id is a NUL-terminated UTF-8 byte sequence. The valid maximum length
is `ZLINK_ACTOR_ID_MAX - 1` (255 bytes). Empty ids, NULL ids, and ids longer
than 255 bytes fail with `EINVAL`. Within a single `SpotNode` all live actor
ids are unique. Different `SpotNode` instances may hold Actors with the same
actor id simultaneously.

`zlink_actor_ref_t.generation == 0` is an unchecked ref. An unchecked ref is
not an error; ref-based APIs interpret it as targeting the current Actor with
the same actor id on the target node. A checked ref (`generation != 0`) must
match the target Actor's current generation. A mismatch results in a stale or
conflict-class failure.

There is no per-Actor dispatch context and no Actor-specific callback handler.
Actor events are always dispatched through the current Spot's dispatch stream,
which eliminates the dispatch context gap that would arise if Actors could
switch Spots while a separate dispatch context was still active.

### Creation and lookup

```c
zlink_config_result_t zlink_spot_node_actor_new(
  void *node,
  const char *actor_id,
  zlink_actor_ref_t *actor_out);

zlink_config_result_t zlink_spot_node_actor_lookup(
  void *node,
  const char *actor_id,
  zlink_actor_ref_t *out);

zlink_config_result_t zlink_remote_actor_get_ref(
  const zlink_routing_id_t *target_node_rid,
  const char *actor_id,
  zlink_actor_ref_t *out);
```

- `zlink_spot_node_actor_new()` creates a local Actor in the Entry Spot and
  returns a checked ref in `actor_out`. Actor creation does not go through the
  Entry Spot dispatch handler or any join request handler.
- Creating with a live actor id that already exists on the same node fails with
  an `EBUSY`-class result.
- `node == NULL` fails with `ZLINK_CONFIG_INVALID_HANDLE`; `errno` is `EFAULT`.
- `actor_id == NULL` or `actor_out == NULL` fails with
  `ZLINK_CONFIG_INVALID_ARGUMENT`; `errno` is `EINVAL`.
- `zlink_spot_node_actor_lookup()` queries a live local Actor owned by the
  caller node and returns a checked ref. If no live Actor with that id exists,
  the result is a not-found-class failure.
- `zlink_remote_actor_get_ref()` builds an unchecked ref (generation 0) from a
  target node rid and actor id only. It does not verify the peer connection,
  perform a handshake, or confirm that the target Actor exists. The result is
  used as input to ref-based APIs.

### Remote create-or-get

```c
zlink_request_result_t zlink_spot_node_create_remote_actor(
  void *node,
  const zlink_routing_id_t *target_node_rid,
  const char *actor_id,
  zlink_msg_t *message,
  zlink_actor_create_result_t *out,
  uint32_t timeout_ms);

zlink_handler_result_t zlink_spot_node_actor_admission_handler(
  void *node,
  zlink_actor_admission_handler_fn handler,
  void *userdata);
```

- The admission handler is called only when the target node has no Actor with
  that id.
- When the target Actor already exists the handler is not called and
  `ZLINK_ACTOR_CREATE_EXISTING` is returned. The existing Actor's current Spot
  is not changed.
- When the handler accepts, the Actor is created in the target node's Entry Spot
  and `ZLINK_ACTOR_CREATE_CREATED` is returned.
- When the handler rejects, the call ends with `ZLINK_REQUEST_REJECTED`.
- Remote create-or-get does not go through the target Spot join handler. Spot
  admission is decided by a subsequent `join` request, not by create-or-get.
- `timeout_ms == 0` is a nonblocking request. If the call cannot complete
  immediately, it fails with a timeout or busy-class result.
- On successful submit, `message` ownership transfers to the library. Ownership
  stays with the caller on validation or submit failure.
- Remote Actors are destroyed through the ref-based
  `zlink_spot_node_actor_destroy()` API. An unreachable target node or a checked
  ref generation mismatch does not remove the Actor slot.

### Spot join

```c
zlink_submit_result_t zlink_spot_node_actor_join_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *message,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

zlink_recv_result_t zlink_spot_actor_join_recv(
  void *spot,
  zlink_actor_join_info_t *info_out,
  zlink_msg_t *message_out,
  zlink_recv_flags_t flags);

zlink_submit_result_t zlink_spot_actor_join_reply(
  void *spot,
  const zlink_actor_join_info_t *info,
  uint32_t accepted,
  zlink_msg_t *message);
```

`join` moves an Actor from its current Spot to the target Spot. On accept, the
Actor's current Spot changes to the target.

`zlink_spot_node_actor_join_spot()` contracts:

- `node` is the request-owner `SpotNode` that submits the join request. The
  session owner, backend service node, and source Actor owner node may all act
  as request owner.
- When `dest_node_rid` matches the Actor owner node, this is a local join.
  Otherwise it is a remote handoff join.
- Joining a user Spot outside Entry requires the source Actor to have a bound
  STREAM session. Attempting a user Spot join without a bound session fails with
  an invalid-state-class result.
- If the Actor is already at the target Spot, the join completes as an
  asynchronous idempotent success without going through the join request handler.
- A new join, leave, or destroy while a join is pending fails with a busy or
  invalid-state-class result.
- `ZLINK_SUBMIT_OK` means the join operation was accepted for processing, not
  that it was accepted by the target Spot. The accept or reject outcome is
  delivered through the `zlink_reply_handler_fn` completion.
- `timeout_ms` is the operation timeout for the join reply and remote handoff to
  complete after a successful submit. `timeout_ms == 0` installs no operation
  timeout. This is not a nonblocking submit directive. Whether the submit stage
  returns immediately on failure is controlled by `ZLINK_DONTWAIT` in `flags`.
- On successful submit, `message` ownership transfers to the library. On local
  validation or pre-submit failure, ownership stays with the caller.
- A join request carries a single `zlink_msg_t` payload. The target Spot reads
  this payload to decide accept or reject.

`zlink_spot_actor_join_recv()` contracts:

- Call this API to drain the Spot after an `ACTOR_JOIN_READABLE` dispatch event.
- On success, join message ownership transfers to the caller.
- If `zlink_actor_join_info_t.flags & ZLINK_ACTOR_JOIN_INFO_REMOTE` is nonzero,
  this is a remote handoff join. If Actor state cannot be restored from the
  payload, the Spot must reject.
- A remote join prepare does not call `zlink_spot_node_actor_admission_handler()`.
  The target Spot join handler decides both Actor creation and Spot admission.
- `zlink_actor_join_info_t.request` is an opaque one-shot handle. The
  application must not dereference or store it directly; pass the `info` struct
  as-is to the reply call.

`zlink_spot_actor_join_reply()` contracts:

- `accepted` must be `0` (reject) or `1` (accept). Any other value fails with
  `ZLINK_SUBMIT_INVALID_ARGUMENT`; `errno` is `EINVAL`.
- `info == NULL` fails with `ZLINK_SUBMIT_INVALID_ARGUMENT`; `errno` is `EINVAL`.
- `message == NULL` sends a completion with no payload.
- On successful submit, reply message ownership transfers to the library. On
  validation failure or duplicate reply failure, ownership stays with the caller.
- Sending a second reply for the same `info.request` fails with
  `ZLINK_SUBMIT_INVALID_STATE`; `errno` is `EALREADY` or `EINVAL`.
- A reply that arrives late after a join timeout, target Spot destroy, or
  `SpotNode` shutdown fails with `ZLINK_SUBMIT_INVALID_STATE`.
- The lifetime of `info.request` extends until the join request ends through a
  reply, timeout, reject cleanup, Spot destroy, or node shutdown.

Join atomicity:

- The source Spot remains the current Spot until the target Spot accepts.
- On target reject or timeout, the Actor remains in the source Spot.
- In a remote join, the source Actor is removed from the source Spot only after
  the session Actor list compare-and-swap succeeds and the target Actor
  activation and active route update complete.
- Message ordering across a join is preserved by Actor queue arrival order.
- Destroying a Spot fails while joined Actors or pending join requests remain.

### Spot leave

```c
zlink_request_result_t zlink_spot_node_actor_leave_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *current_spot_rid,
  uint32_t timeout_ms);
```

`leave` moves an Actor from its current Spot back to the Entry Spot.

- If the Actor is already in the Entry Spot, the result is idempotent success.
- `current_spot_rid` must match the Actor's actual current Spot. If the caller's
  view of the current Spot differs from the actual current Spot, the call fails
  with an invalid-state-class result to prevent a stale leave.
- If a join request is pending, the call fails with `ZLINK_REQUEST_BUSY`;
  `errno` is `EBUSY`. Leave does not cancel a pending join.
- Leave does not go through the Entry Spot dispatch handler or any join request
  handler.
- After a successful leave, Actor messages surface through the Entry Spot
  dispatch event. Leave does not clear the Actor queue; message order is
  preserved across the leave.
- `node == NULL` fails with `ZLINK_REQUEST_INVALID_ARGUMENT`; `errno` is
  `EFAULT`.
- `actor == NULL` or `current_spot_rid == NULL` fails with
  `ZLINK_REQUEST_INVALID_ARGUMENT`; `errno` is `EINVAL`.
- `timeout_ms == 0` is a nonblocking request. If the call cannot complete
  immediately, it fails with a timeout or busy-class result.

### Teardown

```c
zlink_request_result_t zlink_spot_node_actor_destroy(
  void *node,
  const zlink_actor_ref_t *actor,
  uint32_t timeout_ms);
```

- Actor destroy is permitted only while the Actor is in the Entry Spot. If the
  Actor is in a user Spot, the call fails with `ZLINK_REQUEST_INVALID_STATE`;
  `errno` is `EBUSY`. The application must call `leave` to return the Actor to
  Entry before calling destroy.
- If a join request is pending, the call fails with `ZLINK_REQUEST_BUSY` or
  `ZLINK_REQUEST_INVALID_STATE`; `errno` is `EBUSY`. Destroy does not cancel a
  pending join.
- If a bound STREAM session exists, destroy first removes the session Actor list
  entry and the Actor's bound session ref. This cleanup does not close the
  client STREAM connection itself.
- If bound session cleanup cannot be confirmed within `timeout_ms`, the call
  fails with a timeout-class result and the Actor slot and Entry Spot membership
  are preserved. Call `zlink_spot_node_actor_close_bound_session()` before
  destroy if the client connection must also be closed.
- On successful destroy, the Actor ref becomes stale.
- `node == NULL` fails with `ZLINK_REQUEST_INVALID_ARGUMENT`; `errno` is
  `EFAULT`.
- `actor == NULL` fails with `ZLINK_REQUEST_INVALID_ARGUMENT`; `errno` is
  `EINVAL`.
- `timeout_ms == 0` is a nonblocking request.

### Message recv

```c
zlink_recv_result_t zlink_spot_node_actor_recv_part(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_actor_recv_info_t *info_out,
  zlink_msg_t *part_out,
  zlink_part_flag_t *has_more_out,
  zlink_recv_flags_t flags);
```

- Call after receiving an `ACTOR_READABLE` event from the current Spot dispatch
  context.
- `node` must be the Actor owner `SpotNode`. No remote routing is performed.
- `node == NULL` or `node` is not the Actor owner fails with
  `ZLINK_RECV_INVALID_HANDLE`; `errno` is `EFAULT`.
- `actor == NULL`, `info_out == NULL`, `part_out == NULL`, or
  `has_more_out == NULL` fails with `ZLINK_RECV_INVALID_HANDLE`; `errno` is
  `EFAULT`. (`zlink_recv_result_t` has no invalid-argument bucket, so NULL
  output pointer errors are unified under the recv invalid-handle failure.)
- With `ZLINK_DONTWAIT` and no parts available, returns `ZLINK_RECV_NO_DATA`.
- On success, `part_out` ownership transfers to the caller.
- `has_more_out` returns `ZLINK_PART_MORE` or `ZLINK_PART_FINAL`.
- `info_out->actor` is the draining Actor ref; `source_node_rid` and
  `source_session_rid` identify the STREAM session that sent the message.
- `info_out->flags` is currently `0`. Unknown bits must be treated as an invalid
  protocol condition.

### STREAM session binding

```c
zlink_submit_result_t zlink_spot_node_actor_send_bound_session_msg(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_msg_t *message,
  zlink_send_flags_t flags);

zlink_request_result_t zlink_spot_node_actor_close_bound_session(
  void *node,
  const zlink_actor_ref_t *actor,
  uint32_t timeout_ms);
```

The session owner node and the Actor owner node may be the same or different:

- **Local Actor**: session owner node and Actor owner node are the same. Bind,
  relay, and Actor-to-session send all complete within a single node.
- **Remote Actor**: session owner node and Actor owner node differ. The bind
  control request, session-to-Actor relay frame, and Actor-to-session frame all
  travel between nodes.

The session owner does not store the Actor's joined Spot state. The Actor owner
does not store the STREAM session's application state. The Actor's active route
is published by the Actor owner node's Discovery at bind success time, not at
Actor creation time.

`zlink_spot_node_actor_send_bound_session_msg()` contracts:

- Sends `message` to the Actor's bound STREAM session as a **fire-and-forget**
  submit. There is no completion handler; the return value only indicates whether
  the command was accepted.
- When `node` is the Actor owner and the Actor is confirmed not live, the ref is
  stale, or there is no bound session before submit, the call fails immediately
  with `ZLINK_SUBMIT_NOT_FOUND` or an invalid-state-class result.
- When `node` is not the Actor owner, staleness, missing Actor, and missing bound
  session are not guaranteed synchronously at submit time. If the Actor owner
  discovers these conditions when processing the command, it closes the message
  and increments the protocol drop counter.
- On successful submit, `message` ownership transfers to the library. On
  pre-submit validation failure, ownership stays with the caller.

`zlink_spot_node_actor_close_bound_session()` contracts:

- Closes the Actor's bound STREAM session and removes the session Actor list
  entry and the Actor's bound session ref.
- If no bound STREAM session exists, the call fails with a
  `ZLINK_REQUEST_NOT_FOUND`-class result.
- On successful close, the Actor moves to the Entry Spot. If the Actor is
  already in the Entry Spot, it remains there.
- After a successful close, if unread messages remain in the Actor queue, an
  `ACTOR_READABLE` event is raised on the Entry Spot dispatch handler.
- `timeout_ms == 0` is a nonblocking request.

## Constraint summary

- SPOT mesh auto-connect applies only to SPOT discovery peers.
- Generic socket providers do not become SPOT mesh peers.
- Channel calls always go through attached `DEALER` sockets.
- `SpotNode` routed topology is not a substitute for channel calls.
- Attach functions never create sockets or perform `connect()` for the caller.
