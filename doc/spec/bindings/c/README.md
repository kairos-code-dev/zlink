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

## Send Surface

The canonical public send surface includes these aggregate forms.

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

The same aggregate model extends to:

- dealer / router request-reply
- publish / subscribe-related payload send
- SPOT send / request / reply families

## SPOT Routed Request Surface

The C binding adds a routed request initiation surface for `Spot` handles.
These wrappers accept `parts_` / `part_count_` arrays and delegate internally
to the `*_part` substrate.

```c
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

`zlink_spot_request_spot` pairs with `zlink_spot_reply_spot(_part)` on the
replier side. `zlink_spot_request_router` pairs with
`zlink_router_reply_spot(_part)`.

The return type is `zlink_submit_result_t`. On `ZLINK_SUBMIT_OK` the caller
must wait for exactly one `handler_` invocation. On any other return value the
handler is not registered. See Section 8 of
`doc/draft/spot-routed-request-api.ko.md` for the full result-code mapping.

## Recv Surface

The canonical public recv surface keeps aggregate receive results.

```c
zlink_recv_result_t zlink_recv(
  void *s_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);

zlink_recv_result_t zlink_router_recv(
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);
```

The same principle extends to:

- subscribe receive
- SPOT subscribe receive
- SPOT routed receive

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
