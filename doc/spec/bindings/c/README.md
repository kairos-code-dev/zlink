[Spec Index](../../README.md)

# C Binding API Contract

## Purpose

This document defines the public C binding contract.

For C, the public binding surface is the current public C API in
`core/include/zlink.h`. Unlike higher-level bindings, C does not introduce a
separate object-oriented facade over the native substrate. Instead, the C
binding exposes the aggregate multipart API directly.

This means:

- the public contract is still defined by `core/include/zlink.h`
- naming follows C `snake_case`
- nonblocking behavior is expressed by flags
- C does not add `try_send` / `try_recv` convenience functions as separate
  public surface

## Public vs Internal Boundary

For C, the public binding surface is the installed public header only.

- application code, perf, samples, and tests must include the public C binding
  header only
- helper substrate headers and private native support headers are not public
  contract
- future helper `*_part` substrate APIs do not automatically become public C
  binding contract unless this document is updated explicitly

## Naming and Shape

The C binding keeps the canonical C shape.

- functions use `zlink_*` names in `snake_case`
- multipart payloads are represented as `zlink_msg_t *parts` plus
  `size_t part_count`
- routed send/recv uses explicit routing id parameters
- request/reply and publish/subscribe also follow explicit C aggregate
  signatures

The C binding does not introduce a second high-level naming layer above the
public C API.

## Nonblocking Policy

The C binding does not define separate `try_*` functions.

Instead:

- blocking and nonblocking are selected by `zlink_send_flags_t` and
  `zlink_recv_flags_t`
- `ZLINK_DONTWAIT` is the canonical nonblocking switch
- send returns `zlink_submit_result_t`
- recv returns `zlink_recv_result_t`
- callback-style request also keeps the canonical
  `zlink_*_request(..., flags_, timeout_ms_)` form rather than introducing a
  separate `try_request` public family

In other words, the C binding keeps the native C contract directly rather than
wrapping it in separate `try_*` convenience APIs.

## Auto-HWM Profile

The C binding exposes the native auto-HWM context profile contract directly.

```c
typedef enum zlink_auto_hwm_profile_t {
  ZLINK_AUTO_HWM_PROFILE_COMPACT = 0,
  ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY = 1,
  ZLINK_AUTO_HWM_PROFILE_BALANCED = 2,
  ZLINK_AUTO_HWM_PROFILE_THROUGHPUT = 3
} zlink_auto_hwm_profile_t;

#define ZLINK_CTX_AUTO_HWM_PROFILE_DFLT ZLINK_AUTO_HWM_PROFILE_BALANCED
```

`zlink_ctx_set(ctx, ZLINK_CTX_OPT_AUTO_HWM_PROFILE, value, &error)` selects
the profile for sockets created after the option is set. `zlink_ctx_get()`
returns the active profile. Invalid profile values return
`ZLINK_CONFIG_INVALID_ARGUMENT`.

`zlink_monitor_snapshot_t` includes the auto-HWM v2 fields
`auto_hwm_profile`, `auto_hwm_policy_class`, `auto_hwm_unit_budget_bytes`,
`auto_hwm_size_cap`, and `auto_hwm_effective_publish_fanout`.

SPOT admission HWM defaults follow the current core header. SpotNode exposes
router and pubsub admission profile/numeric options; relay and delivery HWM are
fixed to `0`, and delivery queue hard-limit options are not part of the current
contract. Binding verification must use native headers from `core/include` and
the runtime library from `core/build`.

## Send Surface

```c
zlink_submit_result_t zlink_send(
  void *s_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

zlink_submit_result_t zlink_send_rid(
  void *s_,
  const zlink_routing_id_t *target_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);
```

## Publish Surface

```c
zlink_submit_result_t zlink_publish(
  void *subject_,
  const char *topic_id_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);
```

`subject_` is a `PubSocket` or `XPubSocket` handle.

## Request-Reply Surface

Dealer-side and router-side request initiation and reply.

```c
zlink_submit_result_t zlink_dealer_request(
  void *dealer_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

zlink_submit_result_t zlink_router_request(
  void *router_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

zlink_submit_result_t zlink_router_reply(
  void *router_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_);
```

On `ZLINK_SUBMIT_OK` the caller must wait for exactly one `handler_`
invocation. On any other return value the handler is not registered.

## Router → Spot Surface

One-way send, request initiation, and reply from a router to a remote spot.

```c
zlink_submit_result_t zlink_router_send_spot(
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

zlink_submit_result_t zlink_router_request_spot(
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

zlink_submit_result_t zlink_router_reply_spot(
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_);
```

`dest_node_rid_` is the target SpotNode routing id; `dest_spot_rid_` is the
target Spot routing id within that node. `zlink_router_reply_spot` is the
replier-side counterpart of `zlink_spot_request_router`.

## SPOT Channel Surface

Channel send, channel request, and service publish from a Spot handle.
`channel_name_` must match the name of an attached dealer on the SpotNode.

```c
zlink_submit_result_t zlink_spot_send_channel(
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

zlink_submit_result_t zlink_spot_publish(
  void *spot_,
  const char *service_name_,
  const char *topic_id_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

zlink_submit_result_t zlink_spot_request_channel(
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);
```

`zlink_spot_request_channel` routes the request via the attached dealer whose
channel name matches `channel_name_`. On `ZLINK_SUBMIT_OK` the caller must
wait for exactly one `handler_` invocation. Reply completions are delivered
via the owning Spot dispatch stream (`ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE`),
not on the dealer's own event.

## SPOT Routed Request Surface

Direct routed one-way send and routed request initiation from a Spot handle to
another Spot or to a Router. These wrappers accept `parts_` / `part_count_`
arrays and delegate internally to the `*_part` substrate.

```c
zlink_submit_result_t zlink_spot_send_spot(
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

zlink_submit_result_t zlink_spot_request_spot(
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

zlink_submit_result_t zlink_spot_request_router(
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);
```

`zlink_spot_send_spot` is the direct one-way routed send surface.
`zlink_spot_request_spot` pairs with `zlink_spot_reply_spot` on the replier
side. `zlink_spot_request_router` pairs with `zlink_router_reply_spot`.

On `ZLINK_SUBMIT_OK` the caller must wait for exactly one `handler_`
invocation. On any other return value the handler is not registered. See
`doc/spec/core/errno-map.md` for the full result-code mapping.

## SPOT Routed Reply Surface

Replier-side reply from a Spot handle back to the requesting Spot or Router.

```c
zlink_submit_result_t zlink_spot_reply_spot(
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_);

zlink_submit_result_t zlink_spot_reply_router(
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_);
```

`request_seq_` is taken from the `zlink_spot_recv` result. `zlink_spot_reply_spot`
replies to a `zlink_spot_request_spot` initiator; `zlink_spot_reply_router`
replies to a `zlink_router_request_spot` initiator.

## Recv Surface

```c
/* Caller must free(parts_out) after use; zlink_multipart_close closes messages only. */
zlink_recv_result_t zlink_recv(
  void *s_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);

/* Caller must free(parts_out) after use; zlink_multipart_close closes messages only. */
zlink_recv_result_t zlink_router_recv(
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);
```

`source_spot_rid_out_` is non-NULL only for packets that originated from a Spot
(`zlink_router_send_spot` / `zlink_spot_request_router` path); for ordinary
dealer-originated traffic it is NULL.

## Subscribe Surface

```c
/* Caller must free(parts_out) after use; zlink_multipart_close closes messages only. */
zlink_recv_result_t zlink_subscribe(
  void *subject_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);

zlink_recv_result_t zlink_subscription_event(
  void *subject_,
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);
```

`subject_` is a `SubSocket` or `XSubSocket` handle. `topic_id_out_` is a
caller-supplied buffer; `topic_id_len_out_` is both the buffer capacity on
input and the written byte count on output. `subscribed_out_` is 1 for
subscribe events and 0 for unsubscribe.

## SPOT Subscribe Surface

SPOT-side subscribe recv and subscription event recv. Returns the originating
service name in addition to the topic.

```c
/* Caller must free(parts_out) after use; zlink_multipart_close closes messages only. */
zlink_recv_result_t zlink_spot_subscribe(
  void *spot_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);

zlink_recv_result_t zlink_spot_subscription_event(
  void *spot_,
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);
```

`service_name_out_` and `topic_id_out_` are caller-supplied buffers; the
corresponding `*_len_out_` parameters carry the buffer capacity on input and
the written byte count on output. `subscribed_out_` is 1 for subscribe events
and 0 for unsubscribe.

When SPOT dispatch is used, `ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE`
means the Spot has readable subscribe work. It is not a one-event-per-message
 contract. The caller must keep pulling with `zlink_spot_subscribe(...)` or
`zlink_spot_subscription_event(...)` until the API returns
`ZLINK_RECV_NO_DATA` with `EAGAIN`.

## SPOT Recv Surface

SPOT-side routed recv. Receives both one-way sends and request-reply packets
arriving at this Spot.

```c
/* Caller must free(parts_out) after use; zlink_multipart_close closes messages only. */
zlink_recv_result_t zlink_spot_recv(
  void *spot_,
  const zlink_routing_id_t **source_rid_out_,
  const zlink_routing_id_t **spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);
```

`source_rid_out_` is the originating node rid. `spot_rid_out_` is the
destination spot rid (this Spot's own rid as carried in the packet).
`request_seq_out_` is non-zero for request packets; pass it back to
`zlink_spot_reply_spot` or `zlink_spot_reply_router` to reply. The lifetime
of the routing id pointers is tied to the received message frame — copy before
freeing `parts_out_`.

The first `zlink_spot_recv(...)` call must not perform hidden activation or
hidden target registration. With `ZLINK_DONTWAIT`, `EAGAIN` means that Spot's
owned routed ingress currently has no readable data. Under dispatch,
`ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE` is also a readiness signal, so the
caller must drain with repeated `zlink_spot_recv(...)` calls until
`ZLINK_RECV_NO_DATA` / `EAGAIN`.

## Relationship to Helper Substrate

The public C binding contract is not the same thing as a future helper
substrate.

If helper substrate APIs such as `*_part` are added later for bindings and
performance work:

- they do not automatically replace the public C binding contract
- the C binding still documents the aggregate convenience surface unless the
  public C contract itself is intentionally changed

That means the helper substrate may exist underneath the C binding without
changing the public C binding shape.

## Implementation Follow-Up

This document currently treats `core/include/zlink.h` as the public C binding
contract. If helper substrate headers and a separate public C binding header
are introduced later, this document must be updated to point to the installed
public C binding header instead, and the helper substrate must remain internal.

## Relationship to Other Bindings

C is intentionally different from higher-level bindings here.

- C keeps flags-based nonblocking control
- higher-level bindings may expose `send/trySend`, `recv/tryRecv`, value
  objects, callbacks, and language-specific convenience models

This difference is acceptable because the policy goal is shared meaning, not
identical surface syntax across languages.

## SPOT Option and Status Alignment

The C binding follows `core/include/zlink.h` and `core/include/zlink_enum.h`
directly. SPOT node HWM options use the router/pubsub admission options:
`ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE`, `ZLINK_SPOT_NODE_OPT_ROUTER_HWM`,
`ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE`, and
`ZLINK_SPOT_NODE_OPT_PUBSUB_HWM`. Old direction-based HWM options are reserved,
not binding surface.

`zlink_spot_node_status_t` includes
`disconnected_sub_target_count` and
`disconnected_routed_target_count` as ABI compatibility fields. The current
core does not disconnect delivery targets because an internal delivery queue
grew, so these counters report `0`.

## Peer Disconnect by Routing ID

C exposes `zlink_disconnect_rid(void *s, const zlink_routing_id_t *peer_rid)`
and `zlink_spot_node_disconnect_peer_rid(void *node, const zlink_routing_id_t *target_node_rid)`.
It also exposes `ZLINK_OPT_RID_DUPLICATE_POLICY`,
`ZLINK_RID_DUPLICATE_REJECT`, `ZLINK_RID_DUPLICATE_HANDOVER`,
`ZLINK_CONNECT_NOT_FOUND`, `ZLINK_CONNECT_CONFLICT`, and
`ZLINK_CONNECT_BUSY`.
