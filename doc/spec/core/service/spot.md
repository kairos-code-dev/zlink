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

## SpotNode contract

The default HWM values used by SpotNode and Spot internal raw sockets are
not fixed constants anymore. They come from the context automatic HWM
policy. Manual `ZLINK_SPOT_NODE_OPT_PUB_HWM`,
`ZLINK_SPOT_NODE_OPT_SUB_HWM`,
`ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM`, and
`ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM` settings override those automatic
values. When context auto-HWM is enabled, SPOT uses the active automatic HWM
profile. Topic publish sockets are planned as `spot_data`, topic ingress
sockets as `recv_ingress`, routed sockets as `routed`, and control sockets as
`control`. SPOT publish planning does not lower per-connection HWM based on
the total number of Spot handles or peer connections.

`ZLINK_SPOT_NODE_OPT_SUB_QUEUE_HARD_LIMIT` and
`ZLINK_SPOT_NODE_OPT_ROUTED_QUEUE_HARD_LIMIT` configure the maximum number of
messages allowed in the internal delivery queues. Their defaults are
`ZLINK_SPOT_NODE_SUB_QUEUE_HARD_LIMIT_DFLT` and
`ZLINK_SPOT_NODE_ROUTED_QUEUE_HARD_LIMIT_DFLT`, currently `100` and `500`
respectively. When a
target exceeds its limit, only that sub or routed delivery target is
disconnected; the node and peer stay alive.

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
  ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE = 1,
  ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE = 2,
  ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE = 3,
  ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE = 4
} zlink_spot_dispatch_event_t;

typedef enum zlink_spot_dispatch_subject_kind_t {
  ZLINK_SPOT_DISPATCH_SUBJECT_SPOT = 1,
  ZLINK_SPOT_DISPATCH_SUBJECT_TIMER = 2,
  ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER = 3
} zlink_spot_dispatch_subject_kind_t;

typedef struct zlink_spot_dispatch_info_t {
  zlink_spot_dispatch_event_t event;
  zlink_spot_dispatch_subject_kind_t subject_kind;
  void *subject;
} zlink_spot_dispatch_info_t;

typedef void (*zlink_spot_dispatch_event_handler_fn)(
  void *spot,
  const zlink_spot_dispatch_info_t *info,
  void *userdata);
```

- `event` identifies the readable work plane.
- `subject_kind` tells the caller how to interpret `subject`.
- `subject` is the concrete drain target instance.
- `CHANNEL_REPLY_READABLE` means a channel request completion is ready for the
  originating `Spot`. It does not expose raw dealer receive.
- `SUBSCRIBE_READABLE` and `ROUTED_READABLE` are readiness notifications, not
  one-event-per-message delivery counters.

Drain rules by dispatch subject:

| event | subject_kind | subject | drain |
|-------|--------------|---------|-------|
| `SUBSCRIBE_READABLE` | `SPOT` | `spot` | `zlink_spot_subscribe()` / `zlink_spot_subscription_event()` |
| `ROUTED_READABLE` | `SPOT` | `spot` | `zlink_spot_recv()` |
| `TIMER_READABLE` | `TIMER` | timer handle | `zlink_timer_recv()` |
| `CHANNEL_REPLY_READABLE` | `CHANNEL_DEALER` | attached dealer handle | `zlink_spot_channel_reply_progress_from()` |

Dispatch priority is fixed as:

1. `SUBSCRIBE_READABLE`
2. `ROUTED_READABLE`
3. `CHANNEL_REPLY_READABLE`
4. `TIMER_READABLE`

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
- For the full result-code mapping see `doc/draft/spot-routed-request-api.ko.md` §8.
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
```

There is no dedicated public SPOT-node monitor recv API. Use the
snapshot/query functions.

`zlink_spot_node_status_t.disconnected_sub_target_count` reports local
subscribe delivery targets disconnected by queue hard limits or equivalent
delivery guards.
`zlink_spot_node_status_t.disconnected_routed_target_count` reports routed
delivery targets disconnected by those guards.

## Relationship to Poller

The current public poller API is unchanged by this SPOT runtime work.

- `zlink_poller_event_t` does not currently carry owner Spot, dispatch event
  kind, or drain subject together.
- A future Spot-aware poller extension may add that richer result surface, but
  it is not part of the current public contract.
- Today, the canonical unified readable-notification surface for SPOT is
  `zlink_spot_dispatch_event_handler()`.

## Constraint summary

- SPOT mesh auto-connect applies only to SPOT discovery peers.
- Generic socket providers do not become SPOT mesh peers.
- Channel calls always go through attached `DEALER` sockets.
- `SpotNode` routed topology is not a substitute for channel calls.
- Attach functions never create sockets or perform `connect()` for the caller.
