English | [한국어](./spot.ko.md)

[Spec Index](../../README.md) · [Core Index](../README.md) · [Service Common](./README.md)

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
- `options == NULL` is safe: the struct is not accessed, and
  `ZLINK_SPOT_NODE_MODE_ALL` is selected implicitly.
- When `options` is non-NULL and `options->mode == 0`, the zero value is
  treated as unset and also selects `ZLINK_SPOT_NODE_MODE_ALL`.
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

### Explicit routing-id Spot acquisition

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spot_get_or_new(
  void *node_,
  const zlink_routing_id_t *spot_rid_,
  void **spot_out_,
  uint32_t *created_out_);
```

- This function obtains the logical Spot identified by `spot_rid_` within the
  local `SpotNode`.
- Successful calls with the same `node_` and `spot_rid_` receive new owned
  facade handles that point to the same logical Spot.
- If the logical Spot did not exist and this call created it, `created_out_ != NULL`
  yields `*created_out_ = 1`.
- If the logical Spot already existed, the call only creates a new facade and
  `created_out_ != NULL` yields `*created_out_ = 0`.
- `created_out_ == NULL` is allowed when the caller does not need the creation flag.
- If `node_ == NULL`, fails with `ZLINK_CONFIG_INVALID_HANDLE` and `errno == EFAULT`.
- If `spot_rid_ == NULL` or `spot_out_ == NULL`, fails with
  `ZLINK_CONFIG_INVALID_ARGUMENT` and `errno == EINVAL`.
- If `spot_rid_` is empty or too large, fails with `ZLINK_CONFIG_INVALID_ARGUMENT`
  and `errno == EINVAL`.
- On failure, implementations initialize `*spot_out_ = NULL` and `*created_out_ = 0`
  when those output pointers are available.
- The returned facade handle is owned by the caller and must be closed with
  `zlink_spot_destroy()`.
- This function does not perform actor join. Joining a room or stage remains a
  separate join API step.
- Remote Spot creation or acquisition is outside this function's scope.

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
The configuration phase ends when **any one** of the following first occurs:
the first Actor is created, a Discovery is attached, the SpotNode is bound or
connected, or a Spot owner route or Actor active route is published.

> **Spot owner route**: a Discovery-published record that maps a Spot's routing
> id to the SpotNode that owns it, enabling peer nodes to route messages to
> that Spot by rid.
>
> **Actor active route**: a Discovery-published record that maps an Actor id to
> the Spot currently holding that Actor, used for remote Actor relay.

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
- Lookup is keyed on the current routing id index. When `zlink_set_routing_id()` succeeds,
  all facades pointing to the same logical Spot update together, and the lookup index moves
  atomically from old rid to new rid. After a routing id change, an old-rid lookup returns
  not-found and a new-rid lookup returns a new facade for the same logical Spot.
- Concurrent routing id changes on multiple facades for the same logical Spot are
  serialized in the `SpotNode` event loop. A duplicate rid or lifecycle lock causes failure;
  on success all facades and the lookup index reflect the last successful rid.
- A user Spot's logical state is removed when the last facade closes. If joined Actors or
  pending join requests remain, the last facade close fails with `ZLINK_CLOSE_BUSY` and
  `errno == EBUSY`. The application must move all Actors to another Spot or leave them to
  Entry Spot before removing the Spot.
- Looking up the Entry Spot rid returns an Entry Spot facade. The Entry Spot logical state
  is owned by the `SpotNode`, so closing the last facade does not remove it.
- Remote Spot lookup is handled by Discovery Spot owner resolve. This function only looks
  up Spots within the local `SpotNode`.

## SpotNode contract

SpotNode exposes HWM only as admission control from `Spot` into `SpotNode`.
The public options for `zlink_set_spot_node_option()` /
`zlink_get_spot_node_option()` are:

```c
typedef enum zlink_spot_node_option_t
{
    ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE      = 0x360E,
    ZLINK_SPOT_NODE_OPT_ROUTER_HWM              = 0x360F,
    ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE      = 0x3610,
    ZLINK_SPOT_NODE_OPT_PUBSUB_HWM              = 0x3611,
    ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN    = 0x3612,
    ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX    = 0x3613
} zlink_spot_node_option_t;
```

| Option | Type | Description |
|--------|------|-------------|
| `ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE` | `int` | HWM profile for the routed admission channel (`zlink_auto_hwm_profile_t` value) |
| `ZLINK_SPOT_NODE_OPT_ROUTER_HWM` | `int` | Numeric HWM override for the routed channel; `0` clears the override |
| `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE` | `int` | HWM profile for the pub/sub admission channel |
| `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM` | `int` | Numeric HWM override for the pub/sub channel; `0` clears the override |
| `ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN` | `int` | Minimum number of dispatch worker threads |
| `ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX` | `int` | Maximum number of dispatch worker threads |

Both admission channels default to the balanced auto-HWM profile. Without a
numeric override, the admission boundary (`publish_ingress_queue`,
`routed_send_queue`) uses a fixed per-profile message-count limit: COMPACT 64,
LOW_LATENCY 128, BALANCED 256, THROUGHPUT 512. A positive numeric HWM
overrides the automatic value for that channel. Setting the numeric HWM to
`0` clears the override and returns to the automatic value. Negative values
and unknown profiles fail with `EINVAL`.

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
typedef struct zlink_spot_node_socket_filter_t {
  zlink_spot_node_socket_owner_t owner;
  zlink_socket_type_t socket_type;
  char socket_name[64];
} zlink_spot_node_socket_filter_t;

typedef struct zlink_spot_node_socket_entry_t {
  zlink_spot_node_socket_owner_t owner;
  uint64_t owner_id;
  char owner_name[64];
  char socket_name[64];
  zlink_socket_type_t socket_type;
  uint32_t auto_hwm_visible;
  zlink_monitor_status_t snapshot;
} zlink_spot_node_socket_entry_t;

zlink_config_result_t zlink_spot_node_internal_sockets(
  void *node,
  const zlink_spot_node_socket_filter_t *filter,
  zlink_spot_node_socket_entry_t *entries,
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
  `mesh-pub`, `mesh-xsub`, and `external-router`. The
  `publish_ingress_queue`, `routed_send_queue`, and
  `external_router_ingress_queue` operate as runtime queues with no
  corresponding socket and do not appear in snapshot output.
- `PUBSUB` mode does not create routed sockets, and `ROUTED` mode does not
  create topic sockets. Snapshot calls do not activate disabled planes.

### Topology and discovery

```c
zlink_config_result_t zlink_spot_node_set_router_bind(
  void *node,
  const char *endpoint);
zlink_config_result_t zlink_spot_node_set_pub_bind(
  void *node,
  const char *endpoint);
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

- `zlink_spot_node_set_router_bind()` configures the router socket endpoint
  used for routed ingress. A ROUTED-mode node starts from this call.
- `zlink_spot_node_set_pub_bind()` configures the PUB/SUB mesh endpoint and
  starts the PUB/SUB plane. In ALL mode, call
  `zlink_spot_node_set_router_bind()` first when the node also needs routed
  ingress, then call `zlink_spot_node_set_pub_bind()`.
- `node == NULL` fails with `ZLINK_CONFIG_INVALID_HANDLE` and `EFAULT`.
  `endpoint == NULL` or an empty string fails with
  `ZLINK_CONFIG_INVALID_ARGUMENT` and `EINVAL`. Rebinding an already-bound
  plane fails with `ZLINK_CONFIG_INVALID_STATE` and `EBUSY`.
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

### Router channel peer wiring

```c
zlink_connect_result_t zlink_spot_node_connect_router_channel_peer(
  void *node,
  const char *channel_name,
  const char *endpoint);

zlink_connect_result_t zlink_spot_node_disconnect_router_channel_peer(
  void *node,
  const char *channel_name,
  const char *endpoint);

zlink_connect_result_t zlink_spot_node_disconnect_router_channel_peer_rid(
  void *node,
  const char *channel_name,
  const zlink_routing_id_t *peer_rid);

zlink_config_result_t zlink_spot_node_attach_router_channel_discovery(
  void *node,
  const char *channel_name,
  void *discovery);
```

These APIs connect the `SpotNode` routed router to a router-capable channel's
`ROUTER` peer. Once connected, that router channel can target a local `Spot`
with `zlink_router_send_spot_part()` or `zlink_router_request_spot_part()` by
using the target node routing id and target spot routing id.

- `channel_name` names the router channel peer set. `NULL` and empty strings
  fail with `EINVAL`.
- `endpoint` is the public `ROUTER` endpoint of the router channel. Callers do
  not need internal endpoint derivation rules and must not pass derived
  endpoints.
- Nodes without routed mode fail with `ENOTSUP`.
- Repeating the same manual `(channel_name, endpoint)` connect is a successful
  no-op.
- Adding a manual peer to a discovery-owned channel fails with `EBUSY`.
- Disconnecting an unknown manual endpoint fails with `ENOENT`.
- `zlink_spot_node_attach_router_channel_discovery()` accepts only discovery
  views for route mesh or client/server router channels. A channel name that
  does not match the discovery view fails with `EINVAL`.
- Manual peers and discovery peers cannot be mixed in the same channel.
- `zlink_spot_node_disconnect_router_channel_peer_rid()` disconnects by router
  channel peer routing id. SPOT mesh peers and router channel peers are exposed
  as distinct peer kinds in snapshots.

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
- `ACTOR_LIFECYCLE_READABLE` means the Spot has Actor lifecycle events to
  process. Drain with `zlink_spot_recv_actor_lifecycle()` until
  `ZLINK_RECV_NO_DATA`. Its `subject_kind` is `SPOT` and `subject` is `NULL`.

Drain rules by dispatch subject:

| event | subject_kind | subject | drain |
|-------|--------------|---------|-------|
| `SUBSCRIBE_READABLE` | `SPOT` | `spot_` (or NULL) | `zlink_spot_subscribe()` |
| `ROUTED_READABLE` | `SPOT` | `spot_` (or NULL) | `zlink_spot_recv()` |
| `TIMER_READABLE` | `TIMER` | timer handle | `zlink_timer_recv()` |
| `CHANNEL_REPLY_READABLE` | `CHANNEL_DEALER` | attached dealer handle | `zlink_spot_channel_reply_progress_from()` |
| `ACTOR_READABLE` | `ACTOR` | callback-lifetime `const zlink_actor_ref_t *` | `zlink_spot_node_actor_recv_part()` |
| `ACTOR_JOIN_READABLE` | `SPOT` | `spot_` | `zlink_spot_actor_join_recv()` |
| `ACTOR_LIFECYCLE_READABLE` | `SPOT` | `NULL` | `zlink_spot_recv_actor_lifecycle()` |

Dispatch priority is fixed as:

1. `SUBSCRIBE_READABLE`
2. `ROUTED_READABLE`
3. `CHANNEL_REPLY_READABLE`
4. `TIMER_READABLE`
5. `ACTOR_JOIN_READABLE`
6. `ACTOR_READABLE`

```c
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

`zlink_spot_dispatch_event_handler()` is a **unified readiness notification**. It
covers all event types (subscribe, routed, channel reply, timer, Actor join, Actor
readable, Actor lifecycle). The callback signals that data is available; the caller
reads it with the corresponding drain API (`zlink_spot_recv_part()`,
`zlink_spot_subscribe_part()`, `zlink_spot_recv_actor_lifecycle()`, etc.).

SPOT routed receive and Actor lifecycle events have **no direct callback mode**.
They are consumed through dispatch readiness followed by an explicit drain call.

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
zlink_config_result_t zlink_spot_node_status(
  void *node,
  zlink_spot_node_status_t *out);

zlink_config_result_t zlink_spot_node_peers(
  void *node,
  zlink_spot_node_peer_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_peers(
  void *node,
  const zlink_spot_node_peer_filter_t *filter,
  zlink_spot_node_peer_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_subjects(
  void *node,
  const zlink_spot_node_subject_filter_t *filter,
  zlink_spot_node_subject_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_spots(
  void *node,
  zlink_spot_node_spot_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_actors(
  void *node,
  zlink_spot_node_actor_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_actors(
  void *spot,
  zlink_actor_ref_t *entries,
  size_t *count);
```

There is no dedicated public SPOT-node monitor recv API. Use the
snapshot/query functions.

- `entries == NULL` causes a snapshot function to write only the required row
  count into `*count`.
- Insufficient `*count` fails with `ENOBUFS` and writes the required count.
- `zlink_spot_node_spots()` returns the list of local Spots.
  Entry Spot is also included in this list.
  `spot_kind` distinguishes Entry Spot rows from user Spot rows.
  `joined_actor_count`, `pending_actor_join_count`, `route_synced`, and
  `last_changed_ms` are diagnostic values.
- `zlink_spot_node_actors()` returns live local Actor rows. Each row
  contains the Actor ref, current Spot rid, current Spot kind, route sync
  state, unread message count, and `last_changed_ms`.
- `zlink_spot_actors()` returns the list of Actor refs that are joined
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

typedef struct zlink_actor_route_t {
  zlink_actor_ref_t actor;
  zlink_routing_id_t current_spot_rid;
  zlink_spot_kind_t current_spot_kind;
} zlink_actor_route_t;

typedef struct zlink_actor_join_result_t {
  zlink_request_result_t result;
  zlink_actor_ref_t actor;
  zlink_routing_id_t joined_spot_rid;
  uint64_t join_epoch;
  uint32_t flags;
} zlink_actor_join_result_t;

typedef struct zlink_actor_join_entry_spot_result_t {
  zlink_request_result_t result;
  zlink_actor_ref_t actor;
  zlink_routing_id_t target_node_rid;
  uint64_t join_epoch;
  uint32_t flags;
} zlink_actor_join_entry_spot_result_t;

typedef struct zlink_actor_lookup_result_t {
  zlink_request_result_t result;
  zlink_actor_ref_t actor;
  uint32_t flags;
} zlink_actor_lookup_result_t;

typedef struct zlink_spot_actor_lifecycle_info_t {
  zlink_actor_ref_t previous_actor;
  zlink_actor_ref_t current_actor;
  zlink_routing_id_t previous_spot_rid;
  zlink_routing_id_t current_spot_rid;
  uint64_t join_epoch;
  uint32_t flags;
} zlink_spot_actor_lifecycle_info_t;

typedef void (*zlink_actor_join_spot_handler_fn)(
  const zlink_actor_join_result_t *result,
  zlink_msg_t *parts,
  size_t part_count,
  void *userdata);

typedef void (*zlink_actor_join_entry_spot_handler_fn)(
  const zlink_actor_join_entry_spot_result_t *result,
  void *userdata);

typedef void (*zlink_actor_lookup_handler_fn)(
  const zlink_actor_lookup_result_t *result,
  void *userdata);

```

`zlink_actor_route_t` is returned by `zlink_discovery_resolve_actor()`.
`actor.node_rid` is the node that owns the current Actor slot,
`current_spot_rid` is the Actor's current Spot, and `current_spot_kind`
is `ZLINK_SPOT_KIND_ENTRY` or `ZLINK_SPOT_KIND_USER`.

`zlink_actor_join_result_t` is delivered to the join completion handler.
`result` is the final outcome of the join operation. On success, `actor` is the
final Actor ref (the target node's ref for a remote join) and
`joined_spot_rid` is the current Spot rid. The current node rid is read from
`actor.node_rid`. `join_epoch` is a diagnostic value identifying the committed
location change; it increases monotonically (and is non-zero) within the
SpotNode that owns the Actor slot. `flags` is reserved and is 0. On failure,
`actor` and `joined_spot_rid` are not used. The `result` pointer is valid only
for the duration of the callback; copy values inside the callback if needed
later.

`zlink_actor_join_entry_spot_result_t` is delivered to the Entry Spot join
completion handler. On success, `actor` is the final Actor ref after the move and
`target_node_rid` is the target SpotNode rid supplied by the caller. Entry Spot
join has no application join payload and no reply payload, so the callback does
not receive message parts. Idempotent success still returns a success result but
does not fire joined/left lifecycle events again because the location did not
change.

`zlink_actor_lookup_result_t` is delivered to the remote-Actor lookup
completion handler. `result` is the final outcome. On success, `actor` is a
checked Actor ref that exists on the target node. `flags` is reserved and 0.

`zlink_spot_actor_lifecycle_info_t` is delivered to a Spot lifecycle receive API.
`previous_actor` is the Actor ref before the transition (zero-value ref for
a creation event); `current_actor` is the Actor ref after the transition
(zero-value ref for a destroy event). The previous node rid is read from
`previous_actor.node_rid` and the current node rid from `current_actor.node_rid`.
`previous_spot_rid`/`current_spot_rid` are the source/target Spot rids of the
location change. A zero-value ref has `node_rid.size == 0`,
`actor_id[0] == '\0'`, and `generation == 0`; it indicates "no previous" or
"no following" Actor. `join_epoch` is the commit epoch of the location change.
`flags` is reserved and 0. The `info` pointer is valid only during the
callback.

`join_epoch` is a diagnostic value identifying the committed location change.
Epoch values are not comparable across different Actors or different
SpotNodes. Operations that do not change location (timeout, reject, validation
failure) do not increment the epoch. In a remote join the source `on_leave`,
target `on_join`, and join completion may carry epoch values from different
SpotNodes.

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

zlink_submit_result_t zlink_remote_actor_get_ref(
  void *node,
  const zlink_routing_id_t *target_node_rid,
  const char *actor_id,
  zlink_actor_lookup_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);
```

- `zlink_spot_node_actor_new()` creates a local Actor in the Entry Spot and
  returns a checked ref in `actor_out`. Actor creation does not go through the
  Entry Spot dispatch handler or any join request handler. Creation does not
  publish an active route. Creation schedules the Entry Spot `on_join`
  lifecycle event.
- Creating with a live actor id that already exists on the same node fails with
  an `EBUSY`-class result.
- `node == NULL` fails with `ZLINK_CONFIG_INVALID_HANDLE`; `errno` is `EFAULT`.
- `actor_id == NULL` or `actor_out == NULL` fails with
  `ZLINK_CONFIG_INVALID_ARGUMENT`; `errno` is `EINVAL`.
- `zlink_spot_node_actor_lookup()` queries a live local Actor owned by the
  caller node and returns a checked ref. If no live Actor with that id exists,
  the result is a not-found-class failure.
- `zlink_remote_actor_get_ref()` is an async lookup API that asks a remote node
  whether a given Actor exists and returns a checked ref. `node` is the request
  owner `SpotNode` that submits the lookup. If the target node has the Actor,
  the completion's `result->actor` is a checked ref. If it does not, the
  completion fails with a not-found-class result. The lookup target is a
  committed live Actor only; pending target Actors from in-flight remote joins
  are not exposed. With no control path to the target node, the completion
  fails with a not-connected-class result. When `target_node_rid` equals the
  request owner node, the call behaves like a local lookup.
  `handler == NULL` fails as an invalid-argument-class submit. `timeout_ms > 0`
  is the operation timeout from submit to completion; `timeout_ms == 0`
  installs no timeout. The `result` pointer is valid only inside the callback.
  This function does not create Actors, does not move them, and does not
  publish or update active routes.
- `zlink_remote_actor_get_ref()` and `zlink_discovery_resolve_actor()` differ
  in purpose: the former asks a known target node directly for a checked ref,
  while the latter queries the Registry-published active route for the
  currently public location.

### Remote Actor placement model

There is no remote-Actor create API. To place a new Actor where it must start
its life on a remote node, the application creates the Actor on that node
directly with `zlink_spot_node_actor_new()`. To move an existing Actor to
another SpotNode's Entry Spot, use `zlink_spot_node_actor_join_entry_spot()`.
The remote placement flow is:

1. Create a local Actor on the SpotNode that should own it.
2. If needed, move the Actor to a user Spot (possibly on another SpotNode) via
   `zlink_spot_node_actor_join_spot()`.
3. Leave back to the same node's Entry Spot via
   `zlink_spot_node_actor_leave_spot()`, or move to another SpotNode's Entry
   Spot via `zlink_spot_node_actor_join_entry_spot()`.
4. Use the final Actor ref returned by the join completion for follow-up Actor
   calls. Existing logical session bindings follow a successful join without a
   reattach step.

Remote Actor destroy uses the same ref-based
`zlink_spot_node_actor_destroy()` call. If the target node is unreachable or
the checked ref generation does not match, the Actor slot is not removed.

### Spot join

```c
zlink_submit_result_t zlink_spot_node_actor_join_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_actor_join_spot_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_spot_node_actor_join_entry_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *dest_node_rid,
  zlink_actor_join_entry_spot_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);

zlink_recv_result_t zlink_spot_actor_join_recv(
  void *spot,
  zlink_actor_join_info_t *info_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  zlink_recv_flags_t flags);

zlink_submit_result_t zlink_spot_actor_join_reply(
  void *spot,
  const zlink_actor_join_info_t *info,
  uint32_t accepted,
  zlink_msg_t *parts,
  size_t part_count);
```

`join` moves an Actor from its current Spot to a target user Spot. On accept,
the Actor's current Spot changes to the target.

`zlink_spot_node_actor_join_spot()` contracts:

- `node` is the request-owner `SpotNode` that submits the join request. The
  session owner, backend service node, and source Actor owner node may all act
  as request owner.
- When `dest_node_rid` matches the Actor owner node, this is a local join.
  Otherwise it is a remote handoff join.
- `dest_spot_rid` must be a user Spot on the target node. Entry Spot is not a
  valid target for this API; passing the Entry Spot rid fails as
  invalid-argument-class.
- An Actor can join a user Spot without a bound STREAM session. Actor
  location transitions and session attach are independent state transitions;
  session attach is not a validity precondition for location moves.
- If the Actor is already at the target Spot, the join completes as an
  asynchronous idempotent success without going through the join request
  handler. Idempotent join still repairs a missing or stale route by
  republishing the current location.
- A new join, leave, or destroy while a join is pending fails with a busy or
  invalid-state-class result.
- If the target user Spot has no dispatch handler installed, a user Spot join
  request is not auto-accepted. With `timeout_ms > 0` the join stays pending
  until timeout; with `timeout_ms == 0` it stays pending until a handler is
  installed and decides it, or until the Spot/SpotNode terminates.
- `ZLINK_SUBMIT_OK` means the join operation was accepted for processing, not
  that it was accepted by the target Spot. The accept or reject outcome is
  delivered through the `zlink_actor_join_spot_handler_fn` completion. On success
  the completion carries the final Actor ref (the target node's ref for a
  remote join) and the joined Spot rid.
- `handler == NULL` fails as an invalid-argument-class submit because the
  caller must receive the final Actor ref to perform follow-up Actor calls or
  location moves.
- `timeout_ms` is the operation timeout for the join reply and remote handoff
  to complete after a successful submit. `timeout_ms == 0` installs no
  operation timeout. This is not a nonblocking submit directive. Whether the
  submit stage returns immediately on failure is controlled by
  `ZLINK_DONTWAIT` in `flags`.
- On successful submit, `parts` ownership transfers to the library. On local
  validation or pre-submit failure, ownership stays with the caller.
- A join request carries a multipart payload as `zlink_msg_t` parts. The
  target Spot reads this payload to decide accept or reject.
- After an accept commit, the active route updates to the target user Spot
  location.

`zlink_spot_node_actor_join_entry_spot()` contracts:

- `node` is the request-owner `SpotNode` that submits the Entry Spot move.
- `dest_node_rid` is the target SpotNode rid, not an Entry Spot rid. There is one
  Entry Spot per SpotNode, so this API does not take a target Spot rid.
- The API moves the Actor to the target SpotNode's Entry Spot. For a local
  target it uses the target Entry Spot state. For a remote target it follows the
  existing remote Actor move rules and updates the target actor placeholder,
  active route, and bound session relay location.
- It does not enqueue an application join request. There is no message for
  `zlink_spot_actor_join_recv()` to read, and `zlink_spot_actor_join_reply()` is
  not used.
- There are no payload or reply parts. The completion callback is
  `zlink_actor_join_entry_spot_handler_fn` and receives only the result.
- On success, `result->actor` is the final Actor ref after the move and
  `result->target_node_rid` is the target SpotNode rid supplied by the caller.
- If the Actor is already in the same target SpotNode's Entry Spot, the operation
  completes as an idempotent success and does not fire joined/left lifecycle
  callbacks again.
- When the location actually changes, the previous user Spot receives a left
  lifecycle event and the target Entry Spot receives a joined lifecycle
  callback.
- If the target node is unreachable, completion fails with a not-connected-class
  result.
- Invalid Actor refs, checked-ref generation mismatches, and duplicate pending
  joins follow the existing Actor API invalid-argument or invalid-state failure
  policy.
- `handler == NULL` fails as an invalid-argument-class submit because callers
  must receive the final Actor ref for follow-up Actor APIs and session binding.

`zlink_spot_actor_join_recv()` contracts:

- Call this API to drain the Spot after an `ACTOR_JOIN_READABLE` dispatch event.
- On success, join message ownership transfers to the caller.
- If `zlink_actor_join_info_t.flags & ZLINK_ACTOR_JOIN_INFO_REMOTE` is nonzero,
  this is a remote handoff join. If Actor state cannot be restored from the
  payload, the Spot must reject.
- The only currently defined public bit in `flags` is `ZLINK_ACTOR_JOIN_INFO_REMOTE`.
  Unknown bits must be treated as an invalid protocol condition, not silently
  ignored. New public bits must not be added without a new recv/reply contract or
  a versioned info struct.
- The target Spot join handler decides Actor admission to that Spot. Entry
  Spot is the lobby for creation and leave and is not subject to admission.
- In a remote join, if a live Actor with the same actor id already exists on
  the target node, moving the source Actor would not produce a new current
  Actor; the request fails as conflict-class.
- In a remote join, if the checked ref generation conflicts with the target
  node's existing Actor, the request fails as stale- or conflict-class.
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
zlink_submit_result_t zlink_spot_node_actor_leave_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *current_spot_rid,
  zlink_reply_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);
```

`leave` moves an Actor from its current Spot back to the same node's Entry
Spot. It is an async submit API; local and remote Actors share the same
submit + completion path.

- `node` is the leave request owner. The actual leave is performed on the
  Actor owner node identified by `actor.node_rid`. When the request owner and
  the Actor owner differ, the leave is forwarded via the internal control path.
- `current_spot_rid` must match the Actor's current Spot rid on the Actor owner
  node. A mismatch is treated as a stale leave and fails with an
  invalid-state-class result.
- If the Actor is already in the Entry Spot and `current_spot_rid` matches that
  Entry Spot rid, the call is idempotent success. In that case `on_leave` and
  `on_join` are not invoked. If a stale route exists, it is repaired to the
  Entry Spot location; if no route exists, no new route is created.
- If the Actor is already in the Entry Spot but the caller passes a user Spot
  rid as `current_spot_rid`, the call fails as invalid-state-class.
- If a join request is pending, the call fails with a busy- or
  invalid-state-class result. Leave does not cancel a pending join.
- A leave that actually moves the Actor from a user Spot to the Entry Spot
  schedules the source Spot `on_leave` and Entry Spot `on_join` lifecycle
  callbacks and updates the active route to the Entry Spot location.
- After a successful leave, Actor messages surface through the Entry Spot
  dispatch event. Leave does not clear the Actor queue; message order is
  preserved across the leave.
- The final result is delivered through the `zlink_reply_handler_fn`
  completion. The completion has no payload; `parts == NULL`,
  `part_count == 0`.
- `handler == NULL` fails as an invalid-argument-class submit. A submit-stage
  failure does not invoke the completion.
- `node == NULL` fails as an invalid-handle-class submit.
- `actor == NULL` or `current_spot_rid == NULL` fails as an
  invalid-argument-class submit.
- `timeout_ms > 0` is the operation timeout from submit to completion;
  `timeout_ms == 0` installs no timeout.

### Teardown

```c
zlink_submit_result_t zlink_spot_node_actor_destroy(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_reply_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);
```

- Actor destroy succeeds only while the Actor is in the Entry Spot.
- If the Actor is in a user Spot, the call fails as invalid-state-class. The
  application must call `leave` to return the Actor to Entry before calling
  destroy.
- If a join request is pending, the call fails as busy- or
  invalid-state-class. Destroy does not cancel a pending join.
- On successful destroy, the Entry Spot Actor slot is removed. If the active
  route currently points at this Actor ref, the route is removed. If the
  active route points at a different generation, destroy does not remove the
  route.
- A successful destroy schedules the current Spot `on_leave` lifecycle
  callback.
- After a successful destroy, the Actor ref becomes stale. Local and remote
  Actors share the same submit + completion path.
- The final result is delivered through the `zlink_reply_handler_fn`
  completion. The completion has no payload; `parts == NULL`,
  `part_count == 0`.
- `handler == NULL` fails as an invalid-argument-class submit.
- `node == NULL` fails as an invalid-handle-class submit.
- `actor == NULL` fails as an invalid-argument-class submit.
- `timeout_ms > 0` is the operation timeout from submit to completion.

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
  protocol condition. New public bits must not be added without a versioning
  agreement.

### STREAM session binding

```c
zlink_config_result_t zlink_stream_attach_actor_gateway(
  void *stream,
  void *node);

zlink_submit_result_t zlink_stream_bind_actor(
  void *stream,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  zlink_reply_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_stream_unbind_actor(
  void *stream,
  const zlink_routing_id_t *session_rid,
  const char *actor_id,
  zlink_reply_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_stream_send_bound_actor_part(
  void *stream,
  const zlink_routing_id_t *session_rid,
  const char *actor_id,
  zlink_msg_t *part,
  zlink_send_flags_t flags,
  zlink_part_flag_t part_flag);

zlink_submit_result_t zlink_spot_node_actor_send_bound_session_msg(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_msg_t *message,
  zlink_send_flags_t flags);

zlink_config_result_t zlink_stream_bound_actors(
  void *stream,
  const zlink_routing_id_t *session_rid,
  zlink_actor_ref_t *entries,
  size_t *count);

zlink_request_result_t zlink_spot_node_actor_close_bound_session(
  void *node,
  const zlink_actor_ref_t *actor,
  uint32_t timeout_ms);
```

`zlink_stream_attach_actor_gateway()` chooses the session owner `SpotNode` for
STREAM Actor relay. `node` must be a routed-capable `SpotNode`. Reattaching the
same stream to the same node succeeds. Attaching the same stream to a different
node fails with an invalid-state-class config result.

Session attach is independent of Actor location. A session attach or detach
does not change the Actor's current Spot, and an Actor location move
(join/leave) does not change the session mapping.

The session owner node and the Actor owner node may be the same or different:

- **Local Actor**: session owner node and Actor owner node are the same. Bind,
  relay, and Actor-to-session send all complete within a single node.
- **Remote Actor**: session owner node and Actor owner node differ. The bind
  control request, session-to-Actor relay frame, and Actor-to-session frame all
  travel between nodes.

The session owner does not store the Actor's joined Spot state. The Actor
owner does not store the STREAM session's application state. A successful
session attach does not create or update an active route; a successful detach
does not remove an active route. The active route update timing is described
in the [Discovery active route](#discovery-active-route) section.

`zlink_stream_bind_actor()` / `zlink_stream_unbind_actor()` contracts:

- Both are nonblocking submit APIs. Even if a remote Actor owner's response
  is required, the caller thread does not block.
- The final result is delivered through the `zlink_reply_handler_fn`
  completion. The completion has no payload; `parts == NULL`,
  `part_count == 0`.
- `handler == NULL` fails as an invalid-argument-class submit. A submit-stage
  failure does not invoke the completion.
- Local Actor bind and unbind use the same completion path.
- `timeout_ms > 0` is the operation timeout from submit to completion;
  `timeout_ms == 0` installs no timeout.
- `stream` owns the STREAM session Actor mapping. `stream` must be associated
  with a session owner `SpotNode` through
  `zlink_stream_attach_actor_gateway()` or by being an internal stream owned by
  a SpotNode. The control path and relay to a remote Actor owner are performed
  by that owner `SpotNode`.
- Calling bind on a raw or connector STREAM that has no attached ActorGateway
  fails with an invalid-state-class completion. The bind target Actor's
  `node_rid` is not used as a fallback session owner.
- The bind target Actor's owner node is identified by `actor->node_rid`. When
  the Actor is on a remote node, the stream owner `SpotNode` forwards the bind
  information to the `SpotNode` named by `actor->node_rid`.
- `stream` alone does not identify a client session. A single raw STREAM
  socket can multiplex several client sessions, so `session_rid` is required
  to pick a specific client session. The `(stream, session_rid)` pair is the
  STREAM session binding key.
- Unbind uses `(stream, session_rid, actor_id)` to find the session mapping
  and forwards detach to the Actor owner based on the stored Actor ref.
- One session can attach multiple Actor refs. Reattaching the same Actor ref
  is an idempotent success.
- Attaching a different generation under the same actor id replaces the entry
  with the new Actor ref. The previous Actor's bound session ref is also
  cleared.
- Attaching an Actor that is already attached to a different session fails as
  busy-class. The single-session-per-Actor constraint is preserved; the
  existing attach must be released before binding to a new session.

`zlink_stream_send_bound_actor_part()` contracts:

- The session-bound relay does not take a separate `SpotNode` argument. It
  uses `(stream, session_rid)` to find the session mapping and relays the
  message part to the owner of the Actor ref stored under `actor_id`.
- It is a fire-and-forget submit with no completion handler.
- If an internal queue or runtime lock cannot be acquired immediately, the
  call returns a backpressured-class submit failure instead of blocking the
  caller thread.
- On `ZLINK_SUBMIT_OK` the message ownership transfers to the library. On
  submit failure ownership stays with the caller; the implementation does not
  close or reinit `part`.

`zlink_spot_node_actor_send_bound_session_msg()` contracts:

- The reverse-direction fire-and-forget relay used to push a message from the
  Actor side back to its bound session. There is no completion handler; the
  return value only indicates whether the command was accepted.
- The request-owner `node` is the SpotNode submitting the call. The session
  mapping itself is consulted on the Actor owner identified by
  `actor->node_rid`. When the Actor is on a remote node, the request owner
  forwards the relay request to the Actor owner, which then forwards the
  message to the stream owner using the stored bound session ref.
- If an internal queue or runtime lock cannot be acquired immediately, the
  call returns a backpressured-class submit failure instead of blocking.
- On successful submit, `message` ownership transfers to the library. On
  submit failure ownership stays with the caller.

After a successful remote join, the existing session mapping is updated to the
target Actor location under the same Actor id/generation. The application does
not reattach the session with the final Actor ref just to keep session relay
working. Rejects and timeouts leave the previous session mapping unchanged.

`zlink_stream_bound_actors()` contracts:

- A snapshot API that returns the list of Actor refs attached to a given
  STREAM session.
- Follows the standard 2-pass snapshot convention. With `entries == NULL` it
  reports the required count via `*count`. With `entries != NULL` it fills up
  to `*count` entries and writes the actual count back to `*count`.
- When no session mapping exists, it succeeds with `*count == 0`.
- This call only reads the local session mapping owned by `stream`; it does
  not contact a remote Actor owner to confirm existence. The returned refs may
  already be stale. To verify current existence, use a ref-based API or a
  remote lookup.

`zlink_spot_node_actor_close_bound_session()` contracts:

- Closes the Actor's bound STREAM session and removes the session Actor list
  entry and the Actor's bound session ref.
- If no bound STREAM session exists, the call fails with a
  `ZLINK_REQUEST_NOT_FOUND`-class result.
- A successful close does not move the Actor's current Spot and does not
  remove the active route.
- After a successful close, if unread messages remain in the Actor queue, an
  `ACTOR_READABLE` event is raised on the current Spot dispatch handler.
- `timeout_ms == 0` is a nonblocking request.

### Spot lifecycle receive

```c
typedef enum zlink_spot_actor_lifecycle_event_kind_t {
  ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED = 1,
  ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT = 2
} zlink_spot_actor_lifecycle_event_kind_t;

typedef struct zlink_spot_actor_lifecycle_event_t {
  zlink_spot_actor_lifecycle_event_kind_t kind;
  zlink_spot_actor_lifecycle_info_t info;
} zlink_spot_actor_lifecycle_event_t;

zlink_recv_result_t zlink_spot_recv_actor_lifecycle(
  void *spot,
  zlink_spot_actor_lifecycle_event_t *event_out,
  zlink_recv_flags_t flags);
```

`zlink_spot_recv_actor_lifecycle()` drains Actor lifecycle events for one Spot.
Lifecycle readiness is reported by `ACTOR_LIFECYCLE_READABLE`; the event payload
is not delivered inline through a direct callback.

- `kind` is `JOINED` after an Actor enters the Spot and `LEFT` after an Actor
  leaves the Spot.
- `kind` is a convenience classification of the state transition already present
  in `info`; both values must describe the same transition.
- `spot == NULL` or `event_out == NULL` fails as an invalid-handle-class result.
- With `ZLINK_DONTWAIT`, an empty lifecycle queue returns `ZLINK_RECV_NO_DATA`
  and sets errno to `EAGAIN`.
- Lifecycle events are queued only for Spots with a dispatch handler already
  registered. Registration does not replay earlier Actor transitions.

`on_join` is invoked for:

- An Actor that has just been created (initial placement in the Entry Spot;
  this `on_join` is not subject to admission).
- An Actor that has entered this Spot through a join operation (fires on the
  target Spot).
- An explicit leave API that moves an Actor from a user Spot to the Entry
  Spot (fires on the Entry Spot).

`on_leave` is invoked for:

- An Actor that has left this Spot through a join operation (fires on the
  source Spot).
- An explicit leave API that moves an Actor from a user Spot to the Entry
  Spot (fires on the source Spot).
- A destroy that removes the Actor from its current Spot.

`info` field semantics:

- For creation-driven `on_join`, `previous_actor` is a zero-value ref and
  `previous_spot_rid.size == 0`.
- For destroy-driven `on_leave`, `current_actor` is a zero-value ref and
  `current_spot_rid.size == 0`.
- In a local join `previous_actor` and `current_actor` are the same ref; in a
  remote join they may differ.
- For an explicit leave success, `previous_actor` and `current_actor` are the
  same ref; `previous_spot_rid` is the user Spot and `current_spot_rid` is
  the same node's Entry Spot.
- `join_epoch` is the commit epoch of the Actor slot named by `current_actor`
  for `on_join`, and the commit epoch of the slot named by `previous_actor`
  for `on_leave`.

Delivery rules:

- Lifecycle events are optional. They fire only when a lifecycle receive API is
  registered on the Spot.
- Creation, join, leave, and destroy callbacks that occur while no handler is
  registered are not retroactively delivered.
- Lifecycle events are not Actor queue payload, so they are not read via
  `zlink_spot_node_actor_recv_part()`.
- A lifecycle event executes in the dispatch worker context of its Spot.
  The same Spot's dispatch callback and lifecycle event never run
  concurrently.
- There is no execution-order guarantee between lifecycle events belonging
  to different Spots. In a single join operation the source `on_leave` and
  target `on_join` are both scheduled after commit, but the actual execution
  order is not part of the public contract.
- The join completion handler runs after state commit and active route update.
  Whether the lifecycle event has already run is not guaranteed.
- For deciding join completion order, the application must use the join
  completion handler and the returned final Actor ref. Lifecycle events are
  observation-only.
- The `info` pointer is valid only inside the callback; copy values inside
  the callback if needed later.
- Re-entering join, leave, or destroy on the same Actor from inside a
  lifecycle event is not supported.

### Discovery active route

`zlink_actor_route_t` represents an Actor's current dispatch location. The
route is observable via Registry-backed queries when the owner `SpotNode`'s
Discovery has `ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC` enabled and is connected
to the Registry. Otherwise `zlink_discovery_resolve_actor()` may return a
not-found-class failure even after the local Actor location changes.

- `actor` is the final Actor ref the route points to.
- `current_spot_rid` is the Actor's current Spot.
- `current_spot_kind` identifies Entry Spot or user Spot.
- The route is published after a join commit.
- The route does not indicate whether a session is bound.

Active route timing:

| Event | Route behavior |
|-------|----------------|
| Local Actor creation | Not published |
| Local user Spot join success | Published or updated to the user Spot |
| Remote user Spot join success | Published or updated to the target user Spot |
| Explicit leave from user Spot | Published or updated to the Entry Spot |
| Join reject | No change |
| Join timeout | No change |
| Session bind success | No change |
| Session unbind success | No change |
| Matching Actor destroy | Route removed |
| Stale Actor destroy | No change |

The route updates above become Registry-visible after the join or leave
commit when the current `SpotNode` owning the Actor has
`ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC` enabled and is connected to a
Registry.

## Constraint summary

- SPOT mesh auto-connect applies only to SPOT discovery peers.
- Generic socket providers do not become SPOT mesh peers.
- Channel calls always go through attached `DEALER` sockets.
- `SpotNode` routed topology is not a substitute for channel calls.
- Attach functions never create sockets or perform `connect()` for the caller.
