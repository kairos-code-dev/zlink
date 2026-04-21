[English](spot.md) | [한국어](spot.ko.md)

[Spec Index](../../README.md) · [Core Index](../README.md) · [Service Common](README.md)

# SPOT

This document describes only the current public SPOT contract in
`core/include/zlink.h`. Pre-implementation design notes belong under
`doc/spec/draft/`.

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
void *zlink_spot_node_new(void *ctx);
zlink_close_result_t zlink_spot_node_destroy(void **node_p);

void *zlink_spot_new(void *node);
zlink_close_result_t zlink_spot_destroy(void **spot_p);
```

- `zlink_spot_node_new()` creates a SPOT node runtime.
- `zlink_spot_new()` borrows an existing `SpotNode` and returns a unified
  facade.
- `zlink_spot_destroy()` closes only the facade.
- `zlink_spot_node_destroy()` tears down the node runtime.

## SpotNode contract

### Topology and discovery

```c
zlink_bind_result_t zlink_spot_node_bind(void *node, const char *endpoint);
zlink_connect_result_t zlink_spot_node_connect_peer(void *node,
                                                    const char *peer_endpoint);
zlink_connect_result_t zlink_spot_node_disconnect_peer(void *node,
                                                       const char *peer_endpoint);
zlink_config_result_t zlink_spot_node_attach_discovery(void *node,
                                                       void *discovery);
```

- `zlink_spot_node_bind()` binds the node endpoint.
- `zlink_spot_node_connect_peer()` and `disconnect_peer()` are for manual SPOT
  mesh wiring only.
- They fail with `EBUSY` when a discovery is already attached.
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
- Reply with `zlink_spot_reply_spot()` when the origin is another SPOT.
- Reply with `zlink_spot_reply_router()` when the origin is a ROUTER.

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
- `zlink_spot_channel_reply_progress_from()` drains channel reply completions
  for the attached dealer identified by the dispatch `subject`.
- `zlink_socket_set_channel_name()` stores fixed logical channel metadata on a
  socket. It does not change transport routing or discovery by itself.
- `zlink_socket_get_channel_name()` returns that fixed metadata and fails with
  `ENOENT` when the socket has no channel binding metadata.

## Spot routed request initiation

`Spot` can initiate routed requests directly. One-way direct send is not on
the public surface, but the following two paths are exposed to keep the
request/reply surface symmetric.

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
```

- `zlink_spot_request_spot()` pairs with `zlink_spot_reply_spot(_part)` on the replier side.
- `zlink_spot_request_router()` pairs with `zlink_router_reply_spot(_part)`.
- On `ZLINK_SUBMIT_OK` the handler is registered and called exactly once.
- On any other return value the handler is not registered.
- For the full result-code mapping see `doc/draft/spot-routed-request-api.ko.md` §8.

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
void *zlink_service_monitor_open(
  void *target,
  const zlink_service_monitor_open_options_t *options);

zlink_handler_result_t zlink_service_monitor_handler(
  void *monitor,
  zlink_service_monitor_handler_fn handler,
  void *userdata);

zlink_recv_result_t zlink_service_monitor_recv(
  void *monitor,
  zlink_service_monitor_event_t *out,
  zlink_recv_flags_t flags);

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

There is no dedicated public SPOT-node monitor recv API. Use the generic
service monitor plus snapshot/query functions.

## Constraint summary

- SPOT mesh auto-connect applies only to SPOT discovery peers.
- Generic socket providers do not become SPOT mesh peers.
- Channel calls always go through attached `DEALER` sockets.
- `SpotNode` routed topology is not a substitute for channel calls.
- Attach functions never create sockets or perform `connect()` for the caller.
