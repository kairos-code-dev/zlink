[English](spot.md) | [한국어](spot.ko.md)

[Spec Index](../../README.md) · [Core Index](../README.md) · [Service Common](README.md)

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
  `snapshot.auto_hwm_effective_publish_fanout` expose the active automatic HWM
  planner result for diagnostics.
- The current SPOT topology exposes these main node socket names:
  `ingress-sub`, `local-pub`, `mesh-pub`, `mesh-xsub`, `internal-router`, and
  `external-router`.
- `PUBSUB` mode does not create routed sockets, and `ROUTED` mode does not
  create topic sockets. Snapshot calls do not activate disabled planes.

### Topology and discovery

```c
zlink_bind_result_t zlink_spot_node_bind(void *node, const char *endpoint);
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

- `zlink_spot_node_bind()` binds the node endpoint.
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

- `zlink_spot_dispatch_event_handler()` serializes all dispatch callbacks for
  the same `Spot`.
- After `SUBSCRIBE_READABLE` or `ROUTED_READABLE`, callers must drain until the
  corresponding pull API returns `ZLINK_RECV_NO_DATA` / `EAGAIN`.
- Node-wide broad subscribe fan-out is not part of the public contract.
- `zlink_spot_channel_reply_progress_from()` drains channel reply completions
  for the attached dealer identified by the dispatch `subject`.
- `zlink_socket_set_channel_name()` stores fixed logical channel metadata on a
  socket. It does not change transport routing or discovery by itself.
- `zlink_socket_get_channel_name()` returns that fixed metadata and fails with
  `ENOENT` when the socket has no channel binding metadata.

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
  [errno-map.md](../errno-map.md) `zlink_submit_result_t` and
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

An actor id is a NUL-terminated string treated as a UTF-8 byte sequence. The
public buffer size is `ZLINK_ACTOR_ID_MAX`. Empty ids, ids longer than
`ZLINK_ACTOR_ID_MAX`, and NULL ids fail with `EINVAL`. Within a single
`SpotNode` all live actor ids are unique. Different `SpotNode` instances may
hold Actors with the same actor id simultaneously.

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

`zlink_actor_ref_t.generation == 0` is an unchecked ref. An unchecked ref is
not an error; ref-based APIs interpret it as targeting the current Actor with
the same actor id on the target node. A checked ref (`generation != 0`) must
match the target Actor's current generation. A mismatch results in a stale or
conflict-class failure.

### Creation, lookup, and teardown

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
zlink_request_result_t zlink_spot_node_actor_destroy(
  void *node,
  const zlink_actor_ref_t *actor,
  uint32_t timeout_ms);
```

- `zlink_spot_node_actor_new()` creates a local Actor in the Entry Spot and
  returns a checked ref in `actor_out`.
- Creating with a live actor id that already exists on the same node fails.
- `zlink_spot_node_actor_destroy()` succeeds only while the Actor is in the
  Entry Spot. User Spot membership or a pending join fails with
  `ZLINK_REQUEST_BUSY` or `ZLINK_REQUEST_INVALID_STATE`.
- On successful destroy, the Actor ref becomes stale.
- If the required control lock or detach does not complete within `timeout_ms`,
  the call returns `ZLINK_REQUEST_TIMED_OUT` and leaves the handle unchanged.
- `zlink_spot_node_actor_lookup()` returns checked refs.
- `zlink_remote_actor_get_ref()` builds an unchecked remote ref from a target
  node rid and actor id only. It does not verify the peer connection, perform a
  handshake, or confirm that the target Actor exists.

### Remote Actor create-or-get and destroy

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

- create-or-get calls the admission handler only when the target node has no
  Actor with that id.
- When the target Actor already exists the handler is not called and
  `ZLINK_ACTOR_CREATE_EXISTING` is returned.
- When the handler accepts, the Actor is created and `ZLINK_ACTOR_CREATE_CREATED`
  is returned.
- When the handler rejects, the call ends with `ZLINK_REQUEST_REJECTED`.
- On successful submit, `message` ownership transfers to the library. Ownership
  stays with the caller on validation or submit failure.
- Remote Actors are destroyed through the ref-based
  `zlink_spot_node_actor_destroy()` API.
- An unreachable target node or a checked ref generation mismatch does not
  remove the Actor slot.

### Actor and Spot join/leave

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

zlink_request_result_t zlink_spot_node_actor_leave_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *current_spot_rid,
  uint32_t timeout_ms);
```

- One Actor always belongs to exactly one Spot.
- Multiple Actors can be joined to the same Spot simultaneously.
- The Entry Spot is the default Spot immediately after Actor creation, and
  applications cannot remove the Entry logical Spot.
- Joining a user Spot outside Entry requires the Actor to have a bound STREAM
  session.
- Joining the same Actor and target Spot a second time succeeds.
- A new join, leave, or destroy while a join is pending fails with a busy or
  invalid-state result.
- A join request carries a single `zlink_msg_t` message.
- On successful `zlink_spot_actor_join_recv()` the join message ownership
  transfers to the caller.
- `zlink_spot_actor_join_reply()` with `accepted != 0` accepts; `accepted == 0`
  rejects. The reply message is delivered to the caller's join completion.
- `zlink_actor_join_info_t.request` is an opaque one-shot handle. Sending a
  second reply for the same request is not allowed.
- `info.flags & ZLINK_ACTOR_JOIN_INFO_REMOTE` identifies a remote handoff join.
- `zlink_spot_node_actor_leave_spot()` moves the Actor to Entry only when
  `current_spot_rid` matches the Actor's current Spot. A stale current Spot
  fails with a not-found or invalid-state class result.
- Destroying a Spot fails while joined Actors or pending join requests remain.

### Actor recv and bound session send

```c
zlink_recv_result_t zlink_spot_node_actor_recv_part(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_actor_recv_info_t *info_out,
  zlink_msg_t *part_out,
  zlink_part_flag_t *has_more_out,
  zlink_recv_flags_t flags);

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

- `zlink_spot_node_actor_recv_part()` reads one part from the Actor unread state.
- On success `part_out` ownership transfers to the caller.
- With `ZLINK_DONTWAIT` and no parts available, returns `ZLINK_RECV_NO_DATA`.
- `has_more_out` returns `ZLINK_PART_MORE` or `ZLINK_PART_FINAL`.
- `info_out->actor` is the receiving Actor ref; `source_node_rid` and
  `source_session_rid` identify the sender session.
- Sending to a bound STREAM session requires the Actor to be in the session
  Actor list. Without a binding, `ZLINK_SUBMIT_NOT_FOUND` is returned.
- On successful send, message ownership transfers to the library. Ownership
  stays with the caller on failure.
- `zlink_spot_node_actor_close_bound_session()` moves the Actor back to Entry
  after closing the binding.

## Constraint summary

- SPOT mesh auto-connect applies only to SPOT discovery peers.
- Generic socket providers do not become SPOT mesh peers.
- Channel calls always go through attached `DEALER` sockets.
- `SpotNode` routed topology is not a substitute for channel calls.
- Attach functions never create sockets or perform `connect()` for the caller.
