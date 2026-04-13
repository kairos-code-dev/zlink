[English](spot.md) | [한국어](spot.ko.md)

[Spec Index](../README.md) · [Core Index](README.md)

# SPOT

The public SPOT API is organized into two layers:

- `SpotNode`: bind/connect/discovery/TLS wiring owner
- `Spot`: unified pub/sub facade attached to a `SpotNode`

There are no public standalone `zlink_spot_pub_*` or `zlink_spot_sub_*`
constructors, destroy functions, option setters, or monitor entrypoints.

## I/O Model

Both `SpotNode` and unified `Spot` handles start in **recv model** and use
`zlink_subscribe_handler()` for a **one-way transition** of the receive surface
to callback mode. Send-ready is a separate axis.

| | Recv Model (default) | Receive Callback Active |
|---|---|---|
| **SpotNode receive** | *(not exposed — use unified Spot)* | *(not exposed — use unified Spot)* |
| **Spot receive** | `zlink_subscribe()` | `zlink_subscribe_handler()` callback |
| **Readable poller** | `ZLINK_POLLIN` | `EBUSY` |
| **Send-ready** | `ZLINK_POLLOUT` poller or `zlink_send_ready_handler()` | `ZLINK_POLLOUT` poller or `zlink_send_ready_handler()` |

- `zlink_send_ready_handler()` does not require receive callback mode first.
- Once send-ready is attached, data-plane `ZLINK_POLLOUT` poller use fails with `EBUSY`.
- Once receive callback is attached, `zlink_subscribe()` and data-plane `ZLINK_POLLIN` fail with `EBUSY`.
- `publish()` works in both models.

## Current public surface

### SpotNode

```c
void *zlink_spot_node_new(void *ctx);
int zlink_spot_node_destroy(void **node_p);

int zlink_spot_node_bind(void *node, const char *endpoint);
int zlink_spot_node_connect_peer(void *node, const char *endpoint);
int zlink_spot_node_disconnect_peer(void *node,
                                        const char *endpoint);
int zlink_spot_node_attach_discovery(void *node, void *discovery);

int zlink_set_tls_server(void *node,
                         const char *cert,
                         const char *key,
                         int require_client_cert);
int zlink_set_tls_client(void *node,
                         const char *ca_cert,
                         const char *hostname,
                         int trust_system);

int zlink_set_option(void *node,
                     zlink_option_t option,
                     const void *optval,
                     size_t optvallen);
int zlink_get_option(void *node,
                     zlink_option_t option,
                     void *optval,
                     size_t *optvallen);

int zlink_set_routing_id(void *node,
                         const void *data,
                         size_t size);
int zlink_get_routing_id(void *node,
                         zlink_routing_id_t *out);
```

`SpotNode` is the topology and lifecycle owner. Its `service_name` is
determined by the attached Discovery instance. SpotNode does not expose
the generic data-plane facade directly. Create a unified `Spot` facade
with `zlink_spot_new(node)` for publish/subscribe/recv callback APIs.
TLS/WSS configuration is also owned by `SpotNode`; use
`zlink_set_tls_server()` / `zlink_set_tls_client()` with the node handle
before bind/connect.

### Unified Spot

```c
void *zlink_spot_new(void *node);
int zlink_spot_destroy(void **spot_p);

zlink_submit_result_t zlink_publish(void *spot,
                       const char *topic_id,
                       zlink_msg_t *parts,
                       size_t part_count,
                       zlink_send_flags_t flags);
int zlink_subscribe(void *subject_,
                    zlink_routing_id_t *source_rid_out_,
                    zlink_msg_t **parts_out_,
                    size_t *part_count_out_,
                    char *topic_id_out_,
                    size_t *topic_id_len_out_,
                    zlink_send_flags_t flags_);
int zlink_set_subscription (void *spot, const char *filter);
int zlink_unset_subscription (void *spot, const char *filter);
int zlink_subscription_at(void *spot, size_t index,
                          char *buf, size_t *len,
                          int *is_pattern);

int zlink_send_ready_handler(
  void *spot,
  zlink_send_ready_handler_fn handler,
  void *userdata);

int zlink_set_pub_option(void *spot,
                         zlink_pub_option_t option,
                         const void *optval,
                         size_t optvallen);
int zlink_get_pub_option(void *spot,
                         zlink_pub_option_t option,
                         void *optval,
                         size_t *optvallen);
int zlink_set_sub_option(void *spot,
                         zlink_sub_option_t option,
                         const void *optval,
                         size_t optvallen);
int zlink_get_sub_option(void *spot,
                         zlink_sub_option_t option,
                         void *optval,
                         size_t *optvallen);

int zlink_set_option(void *spot,
                     zlink_option_t option,
                     const void *optval,
                     size_t optvallen);
int zlink_get_option(void *spot,
                     zlink_option_t option,
                     void *optval,
                     size_t *optvallen);

int zlink_set_routing_id(void *spot,
                         const void *data,
                         size_t size);
int zlink_get_routing_id(void *spot,
                         zlink_routing_id_t *out);
```

`zlink_spot_new(node)` creates a unified facade that borrows an existing
spot node. It provides both publish and subscribe behavior. There is no
separate public publish-only or subscribe-only child handle.

Unified `Spot` is not a transport-security configuration surface. Calling
`zlink_set_tls_server()` or `zlink_set_tls_client()` with a unified `Spot`
handle fails with `ENOTSUP`. Configure TLS/WSS on the backing `SpotNode`
before the node participates in bind/connect/discovery.

`zlink_subscribe()` provides synchronous pull-style receive in recv
model. It returns the next available message with its source routing ID and
topic. `source_rid_out_`, `parts_out_`, and `topic_id_out_` are filled on
success. Pass `ZLINK_DONTWAIT` in `flags_` for non-blocking operation.
Returns `EBUSY` in callback model, except when it is called from the active
`zlink_spot_dispatch_event_handler()` callback for the same `spot_` to drain a
readable subscribe plane.

Use `zlink_spot_node_status_snapshot()`, `zlink_spot_node_peers_snapshot()`,
and `zlink_spot_node_subjects_snapshot()` for observability.

## SPOT routed request-reply

SPOT request-reply is separate from the publish/subscribe path. This surface
does not use topics; instead it carries destination and request-reply context
together via ZMP control parts.

Key rules:

- Does not mix with ordinary `zlink_publish()` / `zlink_subscribe()`.
- Wire order is `SPOT routed envelope -> request-reply envelope -> payload`.
- Reply uses the address and `request_seq` provided by the request handler as-is.
- `timeout_ms = 0` uses the implementation default of `5000 ms`.
- High-level completion finishes after the first reply.

### Callback types

```c
typedef void (*zlink_spot_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  const zlink_routing_id_t *spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);
```

`zlink_spot_handler_fn` receives both ordinary routed messages and
request-reply messages. `request_seq == 0` indicates an ordinary message;
`request_seq != 0` indicates a request-reply message.

| Parameter | Description |
|-----------|-------------|
| `source_rid_` | Routing identity of the originating node. |
| `spot_rid_` | Routing identity of the originating spot. |
| `request_seq_` | Request sequence number (0 for fire-and-forget). |
| `parts_` | Payload message parts. Callback consumes ownership. |
| `part_count_` | Number of payload parts. |
| `userdata_` | User-supplied context pointer. |

### Dispatch event types

```c
typedef enum zlink_spot_dispatch_event_t
{
    ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE = 1,
    ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE    = 2,
    ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE     = 3
} zlink_spot_dispatch_event_t;
```

| Value | Description |
|-------|-------------|
| `SUBSCRIBE_READABLE` | A subscribe-path message is ready to read. |
| `ROUTED_READABLE` | A routed-path message is ready to read. |
| `TIMER_READABLE` | An internal timer event is ready to process. |

```c
typedef void (*zlink_spot_dispatch_event_handler_fn) (
  void *spot_,
  zlink_spot_dispatch_event_t event_,
  void *userdata_);
```

`zlink_spot_dispatch_event_handler_fn` is the Spot dispatch callback.
Delivery must be serialized per `spot_`. The implementation must not invoke
this callback concurrently or reentrantly for the same `spot_`. A subsequent
dispatch callback for the same `spot_` may run only after the previous
callback has returned.

This is a public API contract. Even if subscribe, routed, and timer events
originate from different internal execution paths, dispatch callback delivery
must remain serialized per `spot_` so the caller can process Spot messaging
sequentially inside the callback.

While this dispatch callback is active for a given `spot_`, the caller may use
the synchronous receive surfaces for that same `spot_` to drain the readable
plane that triggered the event:
- `SUBSCRIBE_READABLE` -> `zlink_subscribe()`
- `ROUTED_READABLE` -> `zlink_spot_recv()`
- `TIMER_READABLE` -> `zlink_timer_recv()`

This exception is scoped to the active dispatch callback for the same `spot_`.
Outside that callback context, the usual recv-versus-callback exclusivity rules
still apply.

The serialization scope is per `spot_`. The API does not require global
serialization across different Spot handles. The implementation may process
dispatch callbacks for different `spot_` handles in parallel, provided the
serialized, non-reentrant contract is preserved for each individual `spot_`.

The implementation must also preserve a high-performance data path. To do so,
it may decouple internal topic, routed, and timer producer paths from user
callback execution, for example by using a per-spot queue, mailbox, or
scheduler. Any such mechanism is acceptable as long as the public contract
remains the same: sequential processing for the same `spot_`, and parallelism
across different `spot_` handles when available.

A dispatch event is a readability notification, not a promise that exactly one
logical message or timer fire is available. The implementation may coalesce
multiple readiness causes into a single callback delivery. The caller is
expected to drain the indicated plane until the corresponding recv call reports
that no more data is available.

| Parameter | Description |
|-----------|-------------|
| `spot_` | The spot handle that raised the event. |
| `event_` | Which internal channel became readable. |
| `userdata_` | User-supplied context pointer. |

### Spot-originated requests (via spot handle)

#### zlink_spot_request_spot

```c
zlink_submit_result_t zlink_spot_request_spot (void *spot_,
                             const zlink_routing_id_t *dest_node_rid_,
                             const zlink_routing_id_t *dest_spot_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                             zlink_reply_handler_fn handler_,
                             void *userdata_,
                             zlink_send_flags_t flags_,
                             uint32_t timeout_ms_);
```

Send a request from a spot to a remote spot via node routing. The reply
arrives asynchronously via `zlink_reply_handler_fn`. Requires both a
destination node routing ID and a destination spot routing ID.

| Parameter | Description |
|-----------|-------------|
| `spot_` | Local spot handle. |
| `dest_node_rid_` | Destination node routing identity. |
| `dest_spot_rid_` | Destination spot routing identity. |
| `parts_` | Payload message parts. Ownership is transferred. |
| `part_count_` | Number of payload parts. |
| `handler_` | Reply callback. |
| `userdata_` | User-supplied context pointer for the reply callback. |
| `flags_` | Submit policy flags (`0` or `ZLINK_DONTWAIT`). |
| `timeout_ms_` | Reply timeout in milliseconds (0 = implementation default 5000 ms). |

**Returns:** `ZLINK_SUBMIT_OK` when the request submit is accepted. On
failure, returns a `zlink_submit_result_t` value. Reply completion is
delivered separately through `zlink_reply_handler_fn`.

**See also:** `zlink_spot_reply_spot`, `zlink_reply_handler_fn`

#### zlink_spot_request_router

```c
zlink_submit_result_t zlink_spot_request_router (void *spot_,
                               const zlink_routing_id_t *peer_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               zlink_reply_handler_fn handler_,
                               void *userdata_,
                               zlink_send_flags_t flags_,
                               uint32_t timeout_ms_);
```

Send a request from a spot to a plain `ROUTER` peer. The reply arrives
asynchronously via `zlink_reply_handler_fn`.

| Parameter | Description |
|-----------|-------------|
| `spot_` | Local spot handle. |
| `peer_rid_` | Transport routing identity of the target ROUTER peer. |
| `parts_` | Payload message parts. Ownership is transferred. |
| `part_count_` | Number of payload parts. |
| `handler_` | Reply callback. |
| `userdata_` | User-supplied context pointer for the reply callback. |
| `flags_` | Submit policy flags (`0` or `ZLINK_DONTWAIT`). |
| `timeout_ms_` | Reply timeout in milliseconds (0 = implementation default 5000 ms). |

**Returns:** `ZLINK_SUBMIT_OK` when the request submit is accepted. On
failure, returns a `zlink_submit_result_t` value. Reply completion is
delivered separately through `zlink_reply_handler_fn`.

**See also:** `zlink_spot_reply_router`, `zlink_reply_handler_fn`

### Spot fire-and-forget send (via spot handle)

#### zlink_spot_send_spot

```c
zlink_submit_result_t zlink_spot_send_spot (void *spot_,
                          const zlink_routing_id_t *dest_node_rid_,
                          const zlink_routing_id_t *dest_spot_rid_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          zlink_send_flags_t flags_);
```

Send a fire-and-forget message from a spot to a remote spot via node routing.
No reply is expected.

| Parameter | Description |
|-----------|-------------|
| `spot_` | Local spot handle. |
| `dest_node_rid_` | Destination node routing identity. |
| `dest_spot_rid_` | Destination spot routing identity. |
| `parts_` | Payload message parts. Ownership is transferred. |
| `part_count_` | Number of payload parts. |
| `flags_` | Send flags (e.g. `ZLINK_DONTWAIT`). |

**Returns:** `ZLINK_SUBMIT_OK` on success. On failure, returns a
`zlink_submit_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_spot_request_spot`

#### zlink_spot_send_router

```c
zlink_submit_result_t zlink_spot_send_router (void *spot_,
                            const zlink_routing_id_t *peer_rid_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            zlink_send_flags_t flags_);
```

Send a fire-and-forget message from a spot to a plain `ROUTER` peer.
No reply is expected.

| Parameter | Description |
|-----------|-------------|
| `spot_` | Local spot handle. |
| `peer_rid_` | Transport routing identity of the target ROUTER peer. |
| `parts_` | Payload message parts. Ownership is transferred. |
| `part_count_` | Number of payload parts. |
| `flags_` | Send flags (e.g. `ZLINK_DONTWAIT`). |

**Returns:** `ZLINK_SUBMIT_OK` on success. On failure, returns a
`zlink_submit_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_spot_request_router`

### Spot-originated replies (via spot handle)

#### zlink_spot_reply_spot

```c
zlink_submit_result_t zlink_spot_reply_spot (void *spot_,
                           const zlink_routing_id_t *dest_node_rid_,
                           const zlink_routing_id_t *dest_spot_rid_,
                           uint64_t request_seq_,
                           zlink_msg_t *parts_,
                           size_t part_count_);
```

Reply to a spot-routed request. Use the `source_rid_`, `spot_rid_`, and
`request_seq_` received in the `zlink_spot_handler_fn` callback.

| Parameter | Description |
|-----------|-------------|
| `spot_` | Local spot handle. |
| `dest_node_rid_` | Destination node routing identity (from the request). |
| `dest_spot_rid_` | Destination spot routing identity (from the request). |
| `request_seq_` | Request sequence number (from the request). |
| `parts_` | Reply message parts. Ownership is transferred. |
| `part_count_` | Number of reply parts. |

**Returns:** `ZLINK_SUBMIT_OK` on success. On failure, returns a
`zlink_submit_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_spot_request_spot`, `zlink_spot_handler_fn`

#### zlink_spot_reply_router

```c
zlink_submit_result_t zlink_spot_reply_router (void *spot_,
                             const zlink_routing_id_t *peer_rid_,
                             uint64_t request_seq_,
                             zlink_msg_t *parts_,
                             size_t part_count_);
```

Reply to a request received from a plain `ROUTER` peer via the spot.

| Parameter | Description |
|-----------|-------------|
| `spot_` | Local spot handle. |
| `peer_rid_` | Transport routing identity of the requesting ROUTER peer. |
| `request_seq_` | Request sequence number (from the request). |
| `parts_` | Reply message parts. Ownership is transferred. |
| `part_count_` | Number of reply parts. |

**Returns:** `ZLINK_SUBMIT_OK` on success. On failure, returns a
`zlink_submit_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_spot_request_router`

### Spot typed receive callback and recv

#### zlink_spot_handler

```c
zlink_handler_result_t zlink_spot_handler (void *spot_,
                                           zlink_spot_handler_fn handler_,
                                           void *userdata_);
```

Attach a typed receive callback for routed messages on a spot. Only one
typed receive callback may be installed per spot. The callback receives both
ordinary routed messages and request-reply messages; distinguish them by
`request_seq` (0 = ordinary, non-zero = request-reply).

| Parameter | Description |
|-----------|-------------|
| `spot_` | Spot handle. |
| `handler_` | Callback function. `NULL` is invalid. |
| `userdata_` | User-supplied context pointer. |

**Returns:** `ZLINK_HANDLER_OK` on success. On failure, returns a
`zlink_handler_result_t` value.

**See also:** `zlink_spot_handler_fn`, `zlink_spot_recv`

#### zlink_spot_dispatch_event_handler

```c
zlink_handler_result_t zlink_spot_dispatch_event_handler (
  void *spot_,
  zlink_spot_dispatch_event_handler_fn handler_,
  void *userdata_);
```

Attach a dispatch event handler that is notified when a specific internal
channel becomes readable.

For the same `spot_`, dispatch callback delivery is serialized. The
implementation must not invoke `handler_` concurrently or reentrantly for the
same `spot_`. The next dispatch callback for the same `spot_` may run only
after the previous callback has returned. This remains true even when
subscribe, routed, and timer events originate from different internal
execution paths.

This requirement is scoped to the individual `spot_`. Different Spot handles
may be dispatched in parallel. The implementation may use internal queueing or
scheduling to preserve per-spot ordering without imposing global
serialization.

When `handler_` is executing for a given `spot_`, the caller may invoke
`zlink_subscribe()`, `zlink_spot_recv()`, and `zlink_timer_recv()` on that same
`spot_` to drain the readable plane indicated by `event_`. Outside the active
dispatch callback for that same `spot_`, the usual recv-versus-callback
conflict rules remain unchanged.

| Parameter | Description |
|-----------|-------------|
| `spot_` | Spot handle. |
| `handler_` | Dispatch event callback. `NULL` is invalid. |
| `userdata_` | User-supplied context pointer. |

**Returns:** `ZLINK_HANDLER_OK` on success. On failure, returns a
`zlink_handler_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_spot_dispatch_event_t`, `zlink_spot_dispatch_event_handler_fn`

#### zlink_spot_recv

```c
zlink_recv_result_t zlink_spot_recv (void *spot_,
                                     const zlink_routing_id_t **source_rid_out_,
                                     const zlink_routing_id_t **spot_rid_out_,
                                     uint64_t *request_seq_out_,
                                     zlink_msg_t **parts_out_,
                                     size_t *part_count_out_,
                                     int flags_);
```

Synchronous pull-style receive for routed messages on a spot (recv mode).
Returns the next available routed message along with its origin and request
context. Pass `ZLINK_DONTWAIT` in `flags_` for non-blocking operation.
Returns `EBUSY` if a typed receive callback is installed. When called from the
active `zlink_spot_dispatch_event_handler()` callback for the same `spot_`, it
may be used to drain a readable routed plane.

| Parameter | Description |
|-----------|-------------|
| `spot_` | Spot handle. |
| `source_rid_out_` | Receives a pointer to the source node routing identity. |
| `spot_rid_out_` | Receives a pointer to the source spot routing identity. |
| `request_seq_out_` | Receives the request sequence number (0 for fire-and-forget). |
| `parts_out_` | Receives a pointer to the message parts array. Caller takes ownership. |
| `part_count_out_` | Receives the number of message parts. |
| `flags_` | Receive flags (e.g. `ZLINK_DONTWAIT`). |

**Returns:** `ZLINK_SUBMIT_OK` on success. On failure, returns a
`zlink_submit_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_spot_handler`

### Router-to-Spot cross-pattern (via router handle)

#### zlink_router_request_spot

```c
zlink_submit_result_t zlink_router_request_spot (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);
```

Send a request from a `ROUTER` handle to a remote spot. The reply arrives
asynchronously via `zlink_reply_handler_fn`.

| Parameter | Description |
|-----------|-------------|
| `router_` | ROUTER handle. |
| `dest_node_rid_` | Destination node routing identity. |
| `dest_spot_rid_` | Destination spot routing identity. |
| `parts_` | Payload message parts. Ownership is transferred. |
| `part_count_` | Number of payload parts. |
| `handler_` | Reply callback. |
| `userdata_` | User-supplied context pointer for the reply callback. |
| `flags_` | Submit policy flags (`0` or `ZLINK_DONTWAIT`). |
| `timeout_ms_` | Reply timeout in milliseconds (0 = implementation default 5000 ms). |

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_router_reply_spot`, `zlink_reply_handler_fn`

#### zlink_router_reply_spot

```c
zlink_submit_result_t zlink_router_reply_spot (void *router_,
                             const zlink_routing_id_t *dest_node_rid_,
                             const zlink_routing_id_t *dest_spot_rid_,
                             uint64_t request_seq_,
                             zlink_msg_t *parts_,
                             size_t part_count_);
```

Reply from a `ROUTER` handle to a spot-originated request. The reply address
is `dest_node_rid + dest_spot_rid + request_seq`, not the transport `peer_rid`.

| Parameter | Description |
|-----------|-------------|
| `router_` | ROUTER handle. |
| `dest_node_rid_` | Destination node routing identity (from the request). |
| `dest_spot_rid_` | Destination spot routing identity (from the request). |
| `request_seq_` | Request sequence number (from the request). |
| `parts_` | Reply message parts. Ownership is transferred. |
| `part_count_` | Number of reply parts. |

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_router_request_spot`

#### zlink_router_send_spot

```c
zlink_submit_result_t zlink_router_send_spot (void *router_,
                            const zlink_routing_id_t *dest_node_rid_,
                            const zlink_routing_id_t *dest_spot_rid_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            zlink_send_flags_t flags_);
```

Send a fire-and-forget message from a `ROUTER` handle to a remote spot.

| Parameter | Description |
|-----------|-------------|
| `router_` | ROUTER handle. |
| `dest_node_rid_` | Destination node routing identity. |
| `dest_spot_rid_` | Destination spot routing identity. |
| `parts_` | Payload message parts. Ownership is transferred. |
| `part_count_` | Number of payload parts. |
| `flags_` | Send flags (e.g. `ZLINK_DONTWAIT`). |

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_router_request_spot`

#### ROUTER receive surface for SPOT traffic

```c
zlink_handler_result_t zlink_router_handler (
  void *router_,
  zlink_router_handler_fn handler_,
  void *userdata_);
zlink_recv_result_t zlink_router_recv (
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);
```

`ROUTER` uses one typed receive surface for all routed traffic. That single
surface handles both ordinary `ROUTER` messages and SPOT-originated routed
messages.

- For ordinary `ROUTER` traffic, `source_node_rid_` is the peer routing
  identity and `source_spot_rid_` is an empty routing identity.
- For SPOT-originated traffic, `source_node_rid_` and `source_spot_rid_`
  together identify the reply address.
- `request_seq_ == 0` means ordinary direct send.
- `request_seq_ != 0` means request-reply traffic.

There is no separate `zlink_router_spot_handler()` or
`zlink_router_spot_recv()` contract.

## Spot option

### zlink_spot_option_t

```c
typedef enum zlink_spot_option_t
{
    ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS = 0x3701
} zlink_spot_option_t;
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS` | `uint32_t` | 5000 | Default request-reply timeout in milliseconds. Applied when `timeout_ms = 0` is passed to a request function. |

### zlink_set_spot_option / zlink_get_spot_option

```c
int zlink_set_spot_option (void *handle_,
                           zlink_spot_option_t option_,
                           const void *optval_,
                           size_t optvallen_);
int zlink_get_spot_option (void *handle_,
                           zlink_spot_option_t option_,
                           void *optval_,
                           size_t *optvallen_);
```

Set or query spot-specific options on a spot handle.

| Parameter | Description |
|-----------|-------------|
| `handle_` | Spot handle. |
| `option_` | Option identifier from `zlink_spot_option_t`. |
| `optval_` | Pointer to the value to set (setter) or buffer to receive the value (getter). |
| `optvallen_` | Size of `optval_` in bytes (setter) or pointer to buffer size (getter). |

**Returns:** `0` on success, or `-1` on failure (errno is set).

### zlink_set_spot_node_option / zlink_get_spot_node_option

```c
int zlink_set_spot_node_option (void *handle_,
                                zlink_spot_node_option_t option_,
                                const void *optval_,
                                size_t optvallen_);
int zlink_get_spot_node_option (void *handle_,
                                zlink_spot_node_option_t option_,
                                void *optval_,
                                size_t *optvallen_);
```

Set or query SpotNode-specific options (peer batching, etc.) on a SpotNode
handle. See the "SpotNode Peer Publish Batching Options" section for the full
list of `zlink_spot_node_option_t` values.

| Parameter | Description |
|-----------|-------------|
| `handle_` | SpotNode handle. |
| `option_` | Option identifier from `zlink_spot_node_option_t`. |
| `optval_` | Pointer to the value to set (setter) or buffer to receive the value (getter). |
| `optvallen_` | Size of `optval_` in bytes (setter) or pointer to buffer size (getter). |

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Options should be set before bind/connect.

## SpotNode Internal Data-Plane HWM

SpotNode HWM settings use `zlink_set_spot_node_option()` rather than the
generic `zlink_set_option(..., SNDHWM/RCVHWM)` path. This matters because a
SpotNode contains both topic publish/subscribe flows and routed direct-delivery
flows, and those two planes must be able to use different queue budgets.

The public configuration axes are:

- `TOPIC_SEND_HWM`
- `TOPIC_RECV_HWM`
- `ROUTED_SEND_HWM`
- `ROUTED_RECV_HWM`

Those values are mapped to the internal topic and routed socket groups. The
public API does not expose individual internal socket names. It keeps the
configuration at the SpotNode level and only distinguishes topic vs. routed
traffic.

## Callback contract

```c
typedef void (*zlink_subscribe_handler_fn)(const zlink_routing_id_t *source_rid,
                                      const char *topic,
                                      size_t topic_len,
                                      zlink_msg_t *parts,
                                      size_t part_count,
                                      void *userdata);
```

- Install the callback with `zlink_subscribe_handler(node_or_spot, handler, userdata)`.
- Handles start in recv model and switch one-way to callback model.
- Once in callback model, `zlink_subscribe()` fails with `EBUSY`.
- The callback consumes ownership of `parts`.

## Option summary

| Target | Setter / Getter | Supported namespace |
|---|---|---|
| unified `spot` publish side | `zlink_set_pub_option()` / `zlink_get_pub_option()` | `ZLINK_PUB_OPT_*` |
| unified `spot` subscribe side | `zlink_set_sub_option()` / `zlink_get_sub_option()` | `ZLINK_SUB_OPT_*` |
| spot request-reply | `zlink_set_spot_option()` / `zlink_get_spot_option()` | `ZLINK_SPOT_OPT_*` |
| SpotNode peer batching | `zlink_set_spot_node_option()` / `zlink_get_spot_node_option()` | `ZLINK_SPOT_NODE_OPT_*` |
| common options (pub-side) | `zlink_set_option()` / `zlink_get_option()` | `ZLINK_OPT_SNDHWM`, `ZLINK_OPT_SNDTIMEO`, `ZLINK_OPT_LINGER`, `ZLINK_OPT_SNDBUF`, `ZLINK_OPT_RCVBUF` |
| common options (sub-side) | `zlink_set_option()` / `zlink_get_option()` | `ZLINK_OPT_RCVHWM`, `ZLINK_OPT_RCVTIMEO`, `ZLINK_OPT_LINGER`, `ZLINK_OPT_SNDBUF`, `ZLINK_OPT_RCVBUF` |
| routing_id (pub-side) | `zlink_set_routing_id()` / `zlink_get_routing_id()` | — |
| subscription management | `zlink_set_subscription()` / `zlink_unset_subscription()` / `zlink_subscription_at()` | — |

## Monitoring

SPOT no longer exposes a public service-monitor surface. Use SpotNode
status/query APIs instead of `zlink_service_monitor_open()`.

## Snapshot / Introspection

SpotNode provides lock-free, point-in-time snapshot APIs for operational
health monitoring and diagnostics. These complement the event-driven monitor
by offering pull-style inspection.

### Supporting enum types

#### zlink_spot_node_state_t

```c
typedef enum zlink_spot_node_state_t
{
    ZLINK_SPOT_NODE_STATE_IDLE          = 1,
    ZLINK_SPOT_NODE_STATE_CONNECTING    = 2,
    ZLINK_SPOT_NODE_STATE_PARTIAL_READY = 3,
    ZLINK_SPOT_NODE_STATE_READY         = 4,
    ZLINK_SPOT_NODE_STATE_ERROR         = 5
} zlink_spot_node_state_t;
```

| Value | Description |
|-------|-------------|
| `IDLE` | Node created but not yet connecting. |
| `CONNECTING` | Connection attempts in progress. |
| `PARTIAL_READY` | Some peers connected, not all. |
| `READY` | All configured peers connected. |
| `ERROR` | Unrecoverable error state. |

#### zlink_spot_peer_source_t

```c
typedef enum zlink_spot_peer_source_t
{
    ZLINK_SPOT_PEER_SOURCE_MANUAL    = 1,
    ZLINK_SPOT_PEER_SOURCE_DISCOVERY = 2,
    ZLINK_SPOT_PEER_SOURCE_MIXED     = 3
} zlink_spot_peer_source_t;
```

| Value | Description |
|-------|-------------|
| `MANUAL` | Peer added via `zlink_spot_node_connect_peer()`. |
| `DISCOVERY` | Peer discovered via an attached Discovery instance. |
| `MIXED` | Peer known from both manual configuration and discovery. |

#### zlink_spot_peer_state_t

```c
typedef enum zlink_spot_peer_state_t
{
    ZLINK_SPOT_PEER_STATE_CONFIGURED  = 1,
    ZLINK_SPOT_PEER_STATE_CONNECTING  = 2,
    ZLINK_SPOT_PEER_STATE_CONNECTED   = 3
} zlink_spot_peer_state_t;
```

| Value | Description |
|-------|-------------|
| `CONFIGURED` | Peer endpoint registered but no connection attempt yet. |
| `CONNECTING` | Connection attempt in progress. |
| `CONNECTED` | Peer is connected and ready. |

#### zlink_spot_role_t

```c
typedef enum zlink_spot_role_t
{
    ZLINK_SPOT_ROLE_PUB = 1,
    ZLINK_SPOT_ROLE_SUB = 2
} zlink_spot_role_t;
```

| Value | Description |
|-------|-------------|
| `PUB` | Publish role. |
| `SUB` | Subscribe role. |

---

### SpotNode Status Snapshot

```c
int zlink_spot_node_status_snapshot(void *node,
                                    zlink_spot_node_status_t *out);
```

Returns a single-row operational health summary of the SpotNode.

#### zlink_spot_node_status_t

```c
typedef struct zlink_spot_node_status_t
{
    char service_name[256];
    char local_endpoint[256];
    zlink_routing_id_t node_routing_id;
    zlink_spot_node_state_t state;
    uint32_t configured_peer_count;
    uint32_t active_peer_count;
    uint32_t connected_peer_count;
    uint32_t subject_count;
    uint32_t ready_subject_count;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_spot_node_status_t;
```

| Field | Description |
|-------|-------------|
| `service_name` | Null-terminated service name from the attached Discovery. |
| `local_endpoint` | Null-terminated local bind endpoint. |
| `node_routing_id` | Routing identity of this SpotNode. |
| `state` | `IDLE`, `CONNECTING`, `PARTIAL_READY`, `READY`, or `ERROR`. |
| `configured_peer_count` | Number of peers configured (manual + discovery). |
| `active_peer_count` | Number of peers actively connecting or connected. |
| `connected_peer_count` | Number of peers currently connected. |
| `subject_count` | Total subscribed subjects. |
| `ready_subject_count` | Subjects with at least one ready peer. |
| `last_error` | Last recorded error code, or 0. |
| `last_changed_ms` | Epoch ms of the last state change. |

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

---

### SpotNode Peers Snapshot / Query

```c
int zlink_spot_node_peers_snapshot(void *node,
                                   zlink_spot_node_peer_entry_t *entries,
                                   size_t *count);

int zlink_spot_node_peers_query(void *node,
                                const zlink_spot_node_peer_filter_t *filter,
                                zlink_spot_node_peer_entry_t *entries,
                                size_t *count);
```

`peers_snapshot` returns all peers. `peers_query` supports filtering by
endpoint, source, or state.

**Buffer convention:** Pass `entries = NULL` to query the required count.
Provide a caller-allocated buffer on the next call. If the buffer is too
small, the call returns `-1` with `errno = ENOBUFS` and `*count` set to the
needed capacity.

Results are ordered by `peer_endpoint` ascending.

#### zlink_spot_node_peer_entry_t

```c
typedef struct zlink_spot_node_peer_entry_t
{
    char service_name[256];
    char local_endpoint[256];
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_state_t state;
    uint64_t connected_since_ms;
    uint64_t last_changed_ms;
} zlink_spot_node_peer_entry_t;
```

| Field | Description |
|-------|-------------|
| `service_name` | Null-terminated service name. |
| `local_endpoint` | Null-terminated local endpoint. |
| `peer_endpoint` | Null-terminated peer endpoint. |
| `source` | `MANUAL`, `DISCOVERY`, or `MIXED`. |
| `state` | `CONFIGURED`, `CONNECTING`, or `CONNECTED`. |
| `connected_since_ms` | Epoch ms when the peer connected (0 if not connected). |
| `last_changed_ms` | Epoch ms of the last state change for this peer. |

#### zlink_spot_node_peer_filter_t

```c
typedef struct zlink_spot_node_peer_filter_t
{
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_state_t state;
} zlink_spot_node_peer_filter_t;
```

Set fields to non-zero values to filter. Zero-valued fields are wildcards.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

---

### SpotNode Subjects Snapshot

```c
int zlink_spot_node_subjects_snapshot(void *node,
                                      const zlink_spot_node_subject_filter_t *filter,
                                      zlink_spot_node_subject_entry_t *entries,
                                      size_t *count);
```

Returns SUB subject readiness information. v1 supports `ZLINK_SPOT_ROLE_SUB`
only; calling with PUB role in the filter returns `ENOTSUP`.

**Buffer convention:** Same as peers snapshot -- pass `entries = NULL` for
count query; `ENOBUFS` with needed count if the buffer is too small.

#### zlink_spot_node_subject_entry_t

```c
typedef struct zlink_spot_node_subject_entry_t
{
    zlink_spot_role_t role;
    char subject[256];
    uint32_t subject_kind;
    uint32_t ready_peer_count;
    uint32_t active_peer_count;
    uint64_t last_changed_ms;
} zlink_spot_node_subject_entry_t;
```

| Field | Description |
|-------|-------------|
| `role` | `ZLINK_SPOT_ROLE_SUB` (v1 only). |
| `subject` | Null-terminated subject string. |
| `subject_kind` | Subject kind identifier. |
| `ready_peer_count` | Peers with this subject in ready state. |
| `active_peer_count` | Peers actively serving this subject. |
| `last_changed_ms` | Epoch ms of the last readiness change. |

#### zlink_spot_node_subject_filter_t

```c
typedef struct zlink_spot_node_subject_filter_t
{
    zlink_spot_role_t role;
    char subject[256];
    uint32_t subject_kind;
} zlink_spot_node_subject_filter_t;
```

Set fields to non-zero values to filter. Zero-valued fields are wildcards.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

---

### Recommended monitoring flow

1. `zlink_spot_node_status_snapshot()` -- check overall health first.
2. `zlink_spot_node_peers_snapshot()` -- inspect peer connectivity.
3. `zlink_spot_node_subjects_snapshot()` -- verify subject readiness.

## SpotNode Peer Publish Batching Options

SpotNode provides optional internal batching for the peer publish path.
These options are set via `zlink_set_spot_node_option()` on the SpotNode handle.

### zlink_spot_node_option_t

```c
typedef enum zlink_spot_node_option_t {
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_ENABLE                  = 0x3601,
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_DELAY_MS                = 0x3602,
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_MAX_MESSAGES            = 0x3603,
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_MAX_BYTES               = 0x3604,
    ZLINK_SPOT_NODE_OPT_PEER_BATCH_BYPASS_BYTES            = 0x3605,
    ZLINK_SPOT_NODE_OPT_PEER_UNBATCH_MAX_MESSAGES_PER_TURN = 0x3606,
    ZLINK_SPOT_NODE_OPT_PEER_UNBATCH_MAX_BYTES_PER_TURN    = 0x3607,
    ZLINK_SPOT_NODE_OPT_TOPIC_SEND_HWM                   = 0x3608,
    ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM                   = 0x3609,
    ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM                  = 0x360A,
    ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM                  = 0x360B
} zlink_spot_node_option_t;
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `PEER_BATCH_ENABLE` | `int` (bool) | 0 (disabled) | Enable peer publish batching. Operator opt-in for homogeneous deployments. |
| `PEER_BATCH_DELAY_MS` | `int` | 20 | Maximum delay before flushing a topic bucket (ms). |
| `PEER_BATCH_MAX_MESSAGES` | `int` | 32 | Maximum messages per topic bucket before flush. |
| `PEER_BATCH_MAX_BYTES` | `int` | 65536 | Maximum bytes per topic bucket before flush. |
| `PEER_BATCH_BYPASS_BYTES` | `int` | 65536 | Messages at or above this encoded size bypass batching and are sent immediately. |
| `PEER_UNBATCH_MAX_MESSAGES_PER_TURN` | `int` | 32 | Maximum messages to unbatch per I/O turn on the receiver side. |
| `PEER_UNBATCH_MAX_BYTES_PER_TURN` | `int` | 65536 | Maximum bytes to unbatch per I/O turn on the receiver side. |
| `TOPIC_SEND_HWM` | `int` | 0 (unlimited) | Send high water mark for topic (pub/sub) messages. |
| `TOPIC_RECV_HWM` | `int` | 0 (unlimited) | Receive high water mark for topic (pub/sub) messages. |
| `ROUTED_SEND_HWM` | `int` | 0 (unlimited) | Send high water mark for routed (request-reply) messages. |
| `ROUTED_RECV_HWM` | `int` | 0 (unlimited) | Receive high water mark for routed (request-reply) messages. |

Usage:

```c
void *node = zlink_spot_node_new(ctx);

int enabled = 1;
zlink_set_spot_node_option(node, ZLINK_SPOT_NODE_OPT_PEER_BATCH_ENABLE,
                 &enabled, sizeof(enabled));

int delay_ms = 10;
zlink_set_spot_node_option(node, ZLINK_SPOT_NODE_OPT_PEER_BATCH_DELAY_MS,
                 &delay_ms, sizeof(delay_ms));

zlink_spot_node_bind(node, "tcp://*:9000");
```

**v1 constraint:** All SpotNodes in the mesh must run the same binary
generation (homogeneous deployment). No runtime capability negotiation.

**Returns:** `zlink_set_spot_node_option` / `zlink_get_spot_node_option`
return 0 on success, -1 on failure (errno is set).

**Thread safety:** Options should be set before bind/connect.

## Removed public APIs

The following families are not part of the current public SPOT surface:

- `zlink_spot_pub_*`
- `zlink_spot_sub_*`
- `zlink_spot_publish_bytes`
- `zlink_spot_node_publish_bytes`
- `zlink_spot_sub_set_handler`
- `zlink_spot_node_default_pub`
- `zlink_spot_node_default_sub`
- `zlink_spot_set_pub_option` / `zlink_spot_set_sub_option`
- `zlink_spot_node_set_pub_option` / `zlink_spot_node_set_sub_option`
- `zlink_spot_send_ready_handler` / `zlink_spot_node_send_ready_handler`
- `zlink_spot_node_set_tls_server` / `zlink_spot_node_set_tls_client`

## Example

### Callback model

```c
void on_spot_message(const zlink_routing_id_t *source_rid,
                     const char *topic,
                     size_t topic_len,
                     zlink_msg_t *parts,
                     size_t part_count,
                     void *userdata);

void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://127.0.0.1:5555");

void *spot = zlink_spot_new(node);
zlink_subscribe_handler(spot, on_spot_message, NULL);
zlink_set_subscription (spot, "room:lobby");

zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_publish(spot, "room:lobby", &part, 1, 0);

/* zlink_spot_destroy destroys only the borrowed spot facade */
zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
```

### Recv model

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://127.0.0.1:5555");

void *spot = zlink_spot_new(node);
zlink_set_subscription (spot, "room:lobby");

/* publish */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_publish(spot, "room:lobby", &part, 1, 0);

/* recv on unified spot */
zlink_routing_id_t source_rid;
zlink_msg_t *recv_parts = NULL;
size_t recv_count = 0;
char topic_buf[256];
size_t topic_len = sizeof(topic_buf);
int rc = zlink_subscribe(spot, &source_rid, &recv_parts, &recv_count,
                         topic_buf, &topic_len, 0);
if (rc == 0) {
    printf("Topic: %.*s\n", (int)topic_len, topic_buf);
    for (size_t i = 0; i < recv_count; i++)
        zlink_msg_close(&recv_parts[i]);
}

/* zlink_spot_destroy destroys only the borrowed spot facade */
zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
```
