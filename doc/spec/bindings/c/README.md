[Spec Index](../../README.md)

# C Binding API Contract

## Purpose

This document defines the public C binding contract.

For C, the public binding surface is the current public C API in
`core/include/zlink.h`. Unlike higher-level bindings, C does not introduce a
separate object-oriented facade over the native substrate. Instead, the C
binding exposes the current part-based C API directly.

This means:

- the public contract is still defined by `core/include/zlink.h`
- naming follows C `snake_case`
- nonblocking behavior is expressed by flags
- C does not add `try_send` / `try_recv` convenience functions as separate
  public surface

## Design Basis

The C binding follows the repository POSD design policy. The installed public
header remains the simple interface; private helper headers, build layout, and
runtime wiring stay hidden from callers.

The C surface is intentionally close to `core/include/zlink.h`; that is a
design constraint, not permission to add another shallow wrapper layer. A C API
addition is valid only when it belongs in the core public header and gives the
caller a stable contract. Local alias functions, alternate option bags, or
compatibility names that merely forward to another `zlink_*` function increase
the surface without reducing caller complexity and are not part of the binding
contract.

When a rule must be enforced in C, keep it in the narrowest owner:

- value-size and enum validation belong at the public C entrypoint boundary
- ownership transfer belongs to the message and part function contract
- helper sequencing for higher-level bindings stays outside the public C API
- build, packaging, and private helper layout do not change the public contract

## Public vs Internal Boundary

For C, the public binding surface is the installed public header only.

- application code, perf, samples, and tests must include the public C binding
  header only
- helper substrate headers and private native support headers are not public
  contract
- the current public `*_part` APIs in `core/include/zlink.h` are public C
  contract

## Codec Extensions

C does not define required codec extension helpers. The C codec policy is
specified separately in [C Codec Extension Specification](codec.md).

## Naming and Shape

The C binding keeps the canonical C shape.

- functions use `zlink_*` names in `snake_case`
- multipart payloads are represented by repeated `zlink_msg_t *part` calls
  plus `zlink_part_flag_t`
- routed send/recv uses explicit routing id parameters
- request/reply and publish/subscribe also follow explicit C part-based
  signatures

Object binding helpers such as `Received.send(...)` do not apply to C. C callers
must use the source routing ids returned by the receive API and call the
matching `zlink_*_send_*_part(...)` function explicitly.

The C binding does not introduce a second high-level naming layer above the
public C API. The higher-level SPOT operation builder policy in
`doc/spec/bindings/README.md` does not apply to C; C keeps explicit
`zlink_spot_*_part(...)` calls because the public C contract is the ABI
surface used by the other bindings.

## Nonblocking Policy

The C binding does not define separate `try_*` functions.

Instead:

- blocking and nonblocking are selected by `zlink_send_flags_t` and
  `zlink_recv_flags_t`
- `ZLINK_DONTWAIT` is the canonical nonblocking switch
- send returns `zlink_submit_result_t`
- recv returns `zlink_recv_result_t`
- callback-style request also keeps the canonical
  `zlink_*_request_part(..., flags_, part_flag_, timeout_ms_)` form rather than introducing a
  separate `try_request` public family

In other words, the C binding keeps the native C contract directly rather than
wrapping it in separate `try_*` convenience APIs.

## High-Performance Requirements

The C binding is part of a high-performance messaging library. Hot paths must
not add reflection-like dynamic dispatch layers, unnecessary heap allocation,
unnecessary message copies, coarse lock contention, hidden waits, sleeps, busy
waits, or thread joins. Multipart processing follows the public `*_part`
contract so callers and higher-level bindings can avoid native aggregate
materialization.

## Actor Dispatch API

The C binding exposes the Actor dispatch contract directly through
`core/include/zlink.h`.

Required public C surface includes:

- `zlink_actor_ref_t`, `zlink_actor_recv_info_t`, `zlink_actor_join_info_t`,
  `zlink_actor_create_result_t`, `zlink_actor_route_t`,
  `zlink_spot_node_spot_entry_t`, `zlink_spot_node_actor_entry_t`
- `zlink_actor_create_status_t`, `zlink_actor_admission_result_t`,
  `zlink_actor_admission_handler_fn`
- `ZLINK_ACTOR_ID_MAX`,
  `ZLINK_ACTOR_CREATE_CREATED`, `ZLINK_ACTOR_CREATE_EXISTING`,
  `ZLINK_ACTOR_ADMISSION_ACCEPT`, `ZLINK_ACTOR_ADMISSION_REJECT`,
  `ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC`,
  `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE`,
  `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE`,
  `ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR`
- request, submit, and recv result values used by Actor APIs:
  `ZLINK_REQUEST_OK`, `ZLINK_REQUEST_TIMED_OUT`,
  `ZLINK_REQUEST_NOT_FOUND`, `ZLINK_REQUEST_REJECTED`,
  `ZLINK_REQUEST_CONFLICT`, `ZLINK_REQUEST_BUSY`,
  `ZLINK_REQUEST_NOT_CONNECTED`, `ZLINK_REQUEST_INVALID_ARGUMENT`,
  `ZLINK_REQUEST_INVALID_STATE`, `ZLINK_REQUEST_NOT_SUPPORTED`,
  `ZLINK_SUBMIT_OK`, `ZLINK_SUBMIT_BACKPRESSURED`,
  `ZLINK_SUBMIT_NOT_CONNECTED`, `ZLINK_SUBMIT_NOT_FOUND`,
  `ZLINK_SUBMIT_NOT_ADMITTED`,
  `ZLINK_SUBMIT_INVALID_ARGUMENT`, `ZLINK_SUBMIT_INVALID_STATE`,
  `ZLINK_RECV_OK`, `ZLINK_RECV_BUSY`, `ZLINK_RECV_NOT_SUPPORTED`
- `zlink_spot_node_actor_new`, `zlink_spot_node_actor_destroy`,
  `zlink_spot_node_actor_lookup`,
  `zlink_remote_actor_get_ref`
- `zlink_spot_node_create_remote_actor`,
  `zlink_spot_node_actor_admission_handler`
- `zlink_spot_node_actor_join_spot`,
  `zlink_spot_actor_join_recv`, `zlink_spot_actor_join_reply`,
  `zlink_spot_node_actor_leave_spot`
- `zlink_stream_bind_actor`, `zlink_stream_unbind_actor`,
  `zlink_stream_send_bound_actor_part`
- `zlink_spot_node_actor_send_bound_session_msg`,
  `zlink_spot_node_actor_close_bound_session`,
  `zlink_spot_node_actor_recv_part`
- `zlink_discovery_resolve_actor`,
  `zlink_spot_node_spots_snapshot`,
  `zlink_spot_node_actors_snapshot`, `zlink_spot_actors_snapshot`

`generation == 0` is an unchecked remote ref. Per-Actor queue limit options
are not part of the C public contract.

The exact Actor dispatch function names are part of the C contract:

```c
zlink_spot_node_actor_new();
zlink_spot_node_actor_destroy();
zlink_spot_node_actor_lookup();
zlink_remote_actor_get_ref();
zlink_spot_node_create_remote_actor();
zlink_spot_node_actor_admission_handler();
zlink_spot_node_actor_join_spot();
zlink_spot_actor_join_recv();
zlink_spot_actor_join_reply();
zlink_spot_node_actor_leave_spot();
zlink_stream_bind_actor();
zlink_stream_unbind_actor();
zlink_stream_send_bound_actor_part();
zlink_spot_node_actor_send_bound_session_msg();
zlink_spot_node_actor_close_bound_session();
zlink_spot_node_actor_recv_part();
zlink_discovery_resolve_actor();
zlink_spot_node_spots_snapshot();
zlink_spot_node_actors_snapshot();
zlink_spot_actors_snapshot();
```

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
`auto_hwm_size_cap`, `auto_hwm_socket_message_slots`, and
`auto_hwm_effective_message_bytes`, plus the applied transport buffer fields
`auto_hwm_effective_sndbuf` and `auto_hwm_effective_rcvbuf`.

SPOT admission HWM defaults follow the current core header. SpotNode exposes
router and pubsub admission profile/numeric options; relay and delivery HWM are
fixed to `0`, and delivery queue hard-limit options are not part of the current
contract. Binding verification must use native headers from `core/include` and
the runtime library from `core/build`.

## Part-Based Data Plane Surface

The C binding follows `core/include/zlink.h` directly. The current public C
contract is the `*_part` substrate: each call transfers or receives one message
part, and `zlink_part_flag_t` tells whether more parts follow. Aggregate helper
names such as `zlink_send(...)`, `zlink_recv(...)`, or `zlink_spot_recv(...)`
are not part of the current public C contract.

### Send and Publish

```c
zlink_submit_result_t zlink_send_part(
  void *s_, zlink_msg_t *part_, zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

zlink_submit_result_t zlink_send_part_rid(
  void *s_, const zlink_routing_id_t *target_rid_,
  zlink_msg_t *part_, zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

zlink_submit_result_t zlink_publish_part(
  void *subject_, const char *topic_id_, zlink_msg_t *part_,
  zlink_send_flags_t flags_, zlink_part_flag_t part_flag_);
```

### Request and Reply

```c
zlink_submit_result_t zlink_dealer_request_part(
  void *dealer_, zlink_msg_t *part_, zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_, uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_, void *userdata_);

zlink_submit_result_t zlink_router_request_part(
  void *router_, const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *part_, zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_, uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_, void *userdata_);

zlink_submit_result_t zlink_router_reply_part(
  void *router_, const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_, zlink_msg_t *part_,
  zlink_part_flag_t part_flag_);
```

On `ZLINK_SUBMIT_OK` the caller must wait for exactly one `handler_`
invocation. On any other return value the handler is not registered.

### Router to SPOT

```c
zlink_submit_result_t zlink_router_send_spot_part(
  void *router_, const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_, zlink_msg_t *part_,
  zlink_send_flags_t flags_, zlink_part_flag_t part_flag_);

zlink_submit_result_t zlink_router_request_spot_part(
  void *router_, const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_, zlink_msg_t *part_,
  zlink_reply_handler_fn handler_, void *userdata_,
  zlink_send_flags_t flags_, zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);

zlink_submit_result_t zlink_router_reply_spot_part(
  void *router_, const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_, uint64_t request_seq_,
  zlink_msg_t *part_, zlink_part_flag_t part_flag_);
```

### SPOT Channels and Topic Publish

```c
zlink_submit_result_t zlink_spot_send_channel_part(
  void *spot_, const char *channel_name_, zlink_msg_t *part_,
  zlink_send_flags_t flags_, zlink_part_flag_t part_flag_);

zlink_submit_result_t zlink_spot_publish_part(
  void *spot_, const char *topic_id_, zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

zlink_submit_result_t zlink_spot_request_channel_part(
  void *spot_, const char *channel_name_, zlink_msg_t *part_,
  zlink_reply_handler_fn handler_, void *userdata_,
  zlink_send_flags_t flags_, zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);
```

Channel request completions are progressed from the owning Spot dispatch
stream through `ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE`.

### SPOT Routed Data Plane

```c
zlink_submit_result_t zlink_spot_send_spot_part(
  void *spot_, const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_, zlink_msg_t *part_,
  zlink_send_flags_t flags_, zlink_part_flag_t part_flag_);

zlink_submit_result_t zlink_spot_request_spot_part(
  void *spot_, const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_, zlink_msg_t *part_,
  zlink_reply_handler_fn handler_, void *userdata_,
  zlink_send_flags_t flags_, zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);

zlink_submit_result_t zlink_spot_request_router_part(
  void *spot_, const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *part_, zlink_reply_handler_fn handler_,
  void *userdata_, zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_, uint32_t timeout_ms_);

zlink_submit_result_t zlink_spot_reply_spot_part(
  void *spot_, const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_, uint64_t request_seq_,
  zlink_msg_t *part_, zlink_part_flag_t part_flag_);

zlink_submit_result_t zlink_spot_reply_router_part(
  void *spot_, const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_, zlink_msg_t *part_,
  zlink_part_flag_t part_flag_);
```

### Receive and Subscribe

```c
zlink_recv_result_t zlink_recv_part(
  void *s_, const zlink_routing_id_t **source_rid_out_,
  zlink_msg_t *part_out_, zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_);

zlink_recv_result_t zlink_router_recv_part(
  void *router_, const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_, zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_, zlink_recv_flags_t flags_);

zlink_recv_result_t zlink_subscribe_part(
  void *sub_, const zlink_routing_id_t **source_rid_out_,
  char *topic_id_buf_, size_t topic_id_capacity_,
  size_t *topic_id_len_out_, zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_, zlink_recv_flags_t flags_);

zlink_recv_result_t zlink_xpub_recv_part(
  void *xpub_, const zlink_routing_id_t **source_rid_out_,
  int *subscribed_out_, char *topic_id_buf_,
  size_t topic_id_capacity_, size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);
```

`source_spot_rid_out_` is non-NULL only for packets that originated from a
Spot path; for ordinary dealer-originated traffic it is NULL.

### SPOT Receive and Subscribe

```c
zlink_recv_result_t zlink_spot_subscribe_part(
  void *spot_, const zlink_routing_id_t **source_rid_out_,
  char *topic_id_buf_, size_t topic_id_capacity_,
  size_t *topic_id_len_out_, zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_);

zlink_recv_result_t zlink_spot_subscription_event_recv(
  void *spot_, const zlink_routing_id_t **source_rid_out_,
  int *subscribed_out_, char *topic_id_buf_,
  size_t topic_id_capacity_, size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);

zlink_recv_result_t zlink_spot_recv_part(
  void *spot_, const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_, zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_, zlink_recv_flags_t flags_);
```

When SPOT dispatch reports `SUBSCRIBE_READABLE` or `ROUTED_READABLE`, the event
is a readiness signal. Callers must keep draining with the corresponding
`*_part` receive function until it returns `ZLINK_RECV_NO_DATA` with `EAGAIN`.

## Implementation Follow-Up

This document treats `core/include/zlink.h` as the public C binding contract.
If a separate public C binding header is introduced later, this document must
be updated to point to that installed public header.

## Relationship to Other Bindings

C is intentionally different from higher-level bindings here.

- C keeps flags-based nonblocking control
- higher-level bindings expose language-specific value objects, callbacks, and
  empty-result nonblocking receive models while preserving the same core
  meaning

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

## Core API Surface 6.0.0 Alignment

Actor create and join payloads use aggregate multipart payloads. Public binding APIs accept a message collection for remote actor create, actor join, actor join receive, and actor join reply. A single-message convenience path may remain, but it must call the multipart path internally so empty payload and one empty message stay distinguishable. Admission handlers receive a borrowed payload view that is valid only during the callback.

Registry scalar configuration uses the registry option surface as the canonical API. Bindings expose typed options for registry id, heartbeat interval, heartbeat timeout, and broadcast interval. Existing named setters may remain as compatibility aliases and must delegate to the option API.
