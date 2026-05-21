[Spec Index](../../README.md) · [Core Index](../README.md) · [Socket Common](./README.md)

# Socket — ROUTER

Routing-id-based addressing, identity-aware recv, directed send. ROUTER is
the reply side in request-reply patterns.

## Router Options (`zlink_router_option_t`)

Used with `zlink_set_router_option()` / `zlink_get_router_option()`.

| Constant | Description |
|---|---|
| `ZLINK_ROUTER_OPT_MANDATORY` | Return `EHOSTUNREACH` when routing to an unconnected peer (`int`; 0 or 1, default `1`) |
| `ZLINK_ROUTER_OPT_PROBE` | Send an empty message on connect to establish identity at the ROUTER peer (`int`; 0 or 1) |
| `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` | Set routing identity for the next outgoing connection (`binary`) |
| `ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS` | Default request timeout in milliseconds for `zlink_router_request()` (`uint32_t`) |
| `ZLINK_ROUTER_OPT_WEIGHT` | Local peer weight advertised to connected peers (`int`; `0..100`, default `100`) |

`ZLINK_ROUTER_OPT_MANDATORY` defaults to `1`.
Duplicate peer identity handling is controlled by the common socket option
`ZLINK_OPT_RID_DUPLICATE_POLICY`. Its default is
`ZLINK_RID_DUPLICATE_REJECT`.

- With `MANDATORY=1`, `zlink_send_rid()` returns
  `ZLINK_SUBMIT_NOT_CONNECTED` instead of silently dropping when the target
  peer is not connected. For the same reason, ROUTER's writable /
  `ZLINK_POLLOUT` observation surfaces readiness only while at least one
  reachable peer exists.
- With the default duplicate policy, a new connection arriving with the same
  peer identity keeps the existing pipe and does not register the duplicate
  pipe.

Callers that want the alternative behavior (silent drop when no peer is
reachable, or new pipe takeover for duplicate identity) must set these
options explicitly.

## Peer Weight

A ROUTER carries a peer weight that tells other peers how often they should
pick this ROUTER as a target for new work. The default is `100`. Operators
can switch to `0` before maintenance or a rolling restart so peers stop
dispatching new work to this ROUTER while in-flight work finishes.

```c
zlink_config_result_t zlink_set_router_option (
  void *handle_,
  zlink_router_option_t option_,
  const void *optval_,
  size_t optvallen_);

zlink_config_result_t zlink_get_router_option (
  void *handle_,
  zlink_router_option_t option_,
  void *optval_,
  size_t *optvallen_);
```

Weight contract:

| Value | Meaning |
|---|---|---|
| `100` | Default weight. Peers with equal positive weights keep round-robin behavior. |
| `1..99` | Positive but lower preference. Peers remain eligible and are selected proportionally less often. |
| `0` | Excluded from new outbound selection. The local ROUTER continues to serve work that has already arrived. |

Behavior:

- `0..100` transitions are allowed at runtime.
- The local ROUTER's own recv/send/reply/handler-dispatch behavior is
  unchanged by peer weight. `0` is a peer-side advisory ("do not
  pick me for new work"), not a local halt.
- Changes are propagated to connected peers as a best-effort runtime signal.
  Each peer updates its weight cache, and state resyncs after reconnect.
- Transitions are observable via the socket monitor event
  `ZLINK_EVENT_PEER_WEIGHT_CHANGED`. The peer is identified by
  `routing_id`, and `value` carries the new weight.

## Peer outbound from ROUTER

When a ROUTER sends a directed message or request to another ROUTER, it
checks the target RID's cached weight before submit.

- If the target RID has a positive weight, submit proceeds normally.
- If the target RID has weight `0`, both `zlink_send_rid()` and
  `zlink_router_request()` fail immediately with
  `ZLINK_SUBMIT_NOT_ADMITTED`.
- Because weight-cache propagation is best-effort, a race can surface
  the same refusal first as `ZLINK_SUBMIT_NOT_CONNECTED`.

Replies are not subject to this check. `zlink_router_reply()` answers an
already-received request and is allowed regardless of peer weight.

## Automatic HWM defaults

ROUTER is classified as the `routed` policy class by the context automatic HWM
policy. The active auto-HWM profile selects the unit budget and message-size
cap; the default profile is `balanced`. Manual `SNDHWM`, `RCVHWM`, `SNDBUF`,
and `RCVBUF` settings override the automatic values.

## Functions

### zlink_set_router_option

Set a router-specific option.

```c
zlink_config_result_t zlink_set_router_option (void *handle_,
                              zlink_router_option_t option_,
                              const void *optval_,
                              size_t optvallen_);
```

Configures a ROUTER socket option. Use `zlink_set_option()` for common
options shared across all socket types.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_get_router_option`, `zlink_set_option`

---

### zlink_get_router_option

Get a router-specific option.

```c
zlink_config_result_t zlink_get_router_option (void *handle_,
                              zlink_router_option_t option_,
                              void *optval_,
                              size_t *optvallen_);
```

Retrieves the current value of a ROUTER socket option.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_set_router_option`

---

### zlink_send_rid

Send a multipart message to a specific peer by routing id.

```c
zlink_submit_result_t zlink_send_rid (void *s_,
                    const zlink_routing_id_t *target_rid_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    zlink_send_flags_t flags_);
```

Sends a multipart message to the peer identified by `target_rid_`. On
success, ownership of every part is transferred to the library. On failure,
ownership remains with the caller.

Applicable handle types: ROUTER (directed reply), STREAM (peer-addressed
send).

**Returns:** `ZLINK_SUBMIT_OK` on success. On failure, returns a
`zlink_submit_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**Errors:** `INVALID_HANDLE` if `s_` is NULL. `BACKPRESSURED` if the operation would block
and `ZLINK_DONTWAIT` was set. `NOT_CONNECTED` if the target peer is not
connected (`ROUTER_MANDATORY` defaults to `1`, so this result is observable
by default unless the option is explicitly disabled). `NOT_ADMITTED` if the
target RID has weight `0`. `TERMINATED` if the
context was terminated. See [errno-map.md](../errno-map.md) for the full
result matrix.

**See also:** `zlink_send_rid`, `zlink_recv`

---

### Non-blocking routed send

Non-blocking directed send using the routed send API.

```c
zlink_submit_result_t zlink_send_rid (void *s_,
                    const zlink_routing_id_t *target_rid_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    zlink_send_flags_t flags_);
```

Use `zlink_send_rid(..., ZLINK_DONTWAIT)` for non-blocking routed send.
Non-blocking send returns `ZLINK_SUBMIT_BACKPRESSURED` when the operation
would block, `ZLINK_SUBMIT_NOT_CONNECTED` when the peer is not reachable,
and `ZLINK_SUBMIT_NOT_ADMITTED` when the target RID has weight `0`. See
[errno-map.md](../errno-map.md) for the full result matrix.

On success, ownership of all parts is transferred to the library. On
failure, ownership remains with the caller.

**Returns:** `ZLINK_SUBMIT_OK` on success, or a `zlink_submit_result_t` value indicating the failure reason. See [errno-map.md](../errno-map.md).

**See also:** `zlink_send_rid`

---

### zlink_router_request

Send an asynchronous request to a specific peer and register a reply handler.

```c
zlink_submit_result_t zlink_router_request (void *router_,
                          const zlink_routing_id_t *peer_rid_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          zlink_reply_handler_fn handler_,
                          void *userdata_,
                          zlink_send_flags_t flags_,
                          uint32_t timeout_ms_);
```

Sends a multipart request to the peer identified by `peer_rid_` on the
ROUTER socket and registers `handler_` to be invoked when the reply arrives
or the timeout expires. On success, ownership of all parts is transferred
to the library.

**Returns:** `ZLINK_SUBMIT_OK` when the request submit is accepted. On
failure, returns a `zlink_submit_result_t` value. Reply completion is
delivered separately through `zlink_reply_handler_fn`.

**See also:** `zlink_router_reply`, `zlink_reply_handler_fn`

---

### zlink_router_reply

Send a reply to a previously received request.

```c
zlink_submit_result_t zlink_router_reply (void *router_,
                        const zlink_routing_id_t *peer_rid_,
                        uint64_t request_seq_,
                        zlink_msg_t *parts_,
                        size_t part_count_);
```

Sends a multipart reply to the peer identified by `peer_rid_` for the
request with sequence number `request_seq_`. On success, ownership of all
parts is transferred to the library.

**Returns:** `ZLINK_SUBMIT_OK` on success. On failure, returns a
`zlink_submit_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_router_request`, `zlink_router_recv`

---

### zlink_router_recv

Receive routed traffic in recv mode on a ROUTER socket.

```c
zlink_recv_result_t zlink_router_recv (
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);
```

Receives the next routed delivery for a ROUTER socket. This is the only
direct recv surface for ROUTER. It covers both plain ROUTER traffic and
spot-originated routed traffic. ROUTER inbound receive is recv-only: the
intended pattern is to observe `ZLINK_POLLIN` from a poller and drain it
with this function. `zlink_router_request()` reply completion is not
delivered here; it flows through a separate `zlink_reply_handler_fn`
callback.

On success, `*source_node_rid_out_` points to the source node routing id.
For plain ROUTER traffic, `*source_spot_rid_out_` is `NULL`. For
spot-originated traffic, `*source_spot_rid_out_` points to the source
spot routing id.

`*request_seq_out_ == 0` means a fire-and-forget routed message.
`*request_seq_out_ != 0` means a request. Plain ROUTER requests are
replied to with `zlink_router_reply()`. Spot-originated requests are
replied to with `zlink_router_reply_spot()`.

The returned payload view follows the standard recv ownership rule: the
library owns the array view, while the caller must close each returned
part.

**Returns:** `ZLINK_RECV_OK` on success. On failure, returns a
`zlink_recv_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_router_reply`, `zlink_router_reply_spot`,
`zlink_router_request`

---

### Sending from ROUTER to SPOT

`zlink_router_send_spot_part()` and `zlink_router_request_spot_part()` send
routed messages from a router channel `ROUTER` to a target `Spot`. The target
node must be connected as a router channel peer through
`zlink_spot_node_connect_router_channel_peer()` or
`zlink_spot_node_attach_router_channel_discovery()`.

Callers must provide both the target node routing id and target spot routing
id. Sending to a target without a router channel peer, or before the route is
ready, follows the normal ROUTER not-connected behavior and may fail or not be
delivered. A higher-level framework must therefore use the resolver's channel
id to select the actual router-capable channel `ROUTER` socket, not only store
the channel id as metadata.

#### Sending through a Spot route resolved from an Actor id

Core does not expose Actor-direct ROUTER APIs such as
`zlink_router_send_actor()`. A caller that starts from an Actor id first calls
`zlink_discovery_resolve_actor()` and then passes `route.actor.node_rid` and
`route.current_spot_rid` to the existing Spot routed API.

```c
zlink_actor_route_t route;
if (zlink_discovery_resolve_actor(discovery, actor_id, &route)
    == ZLINK_CONFIG_OK) {
    zlink_router_send_spot(router,
                           &route.actor.node_rid,
                           &route.current_spot_rid,
                           parts,
                           part_count,
                           flags);
}
```

`route.current_spot_kind` tells the caller whether the target is the Entry Spot
or a user Spot. The application protocol decides how a payload delivered to
that Spot identifies the target Actor. The ROUTER contract only covers
delivery to the resolved Spot.

---

### zlink_recv

Receive a multipart message from a non-ROUTER socket.

```c
zlink_recv_result_t zlink_recv (void *s_,
                                zlink_routing_id_t *source_rid_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                zlink_recv_flags_t flags_);
```

Receives a complete multipart message from socket `s_`. On success,
`*parts_out_` points to a library-allocated array of `*part_count_out_`
message parts, and `*source_rid_out_` is set to the routing id of the
sender (where applicable). Ownership of the parts array and each part is
transferred to the caller, who must close every part (or call
`zlink_multipart_close()`) and free the array. The socket must be in recv
mode (no handler attached). ROUTER sockets are excluded from this
surface. If `s_` is a ROUTER socket, this call fails with
`ZLINK_RECV_NOT_SUPPORTED`. Use `zlink_router_recv()` instead.

**Returns:** `ZLINK_RECV_OK` on success. On failure, returns a
`zlink_recv_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_send`, `zlink_router_recv`, `zlink_multipart_close`

---

### zlink_set_routing_id

Set the routing identity on a socket.

```c
zlink_config_result_t zlink_set_routing_id (void *handle_,
                           const void *data_,
                           size_t size_);
```

Assigns a routing identity to the socket. The identity is used for ROUTER
addressing and must be at most 255 bytes. Must be set before the first
bind or connect.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_get_routing_id`

---

### zlink_send_ready_handler

Install or replace the send-ready callback.

```c
zlink_handler_result_t zlink_send_ready_handler (
  void *s_, zlink_send_ready_handler_fn handler_, void *userdata_);
```

The handler is replace-only. Passing NULL is invalid. A successful replace is
visible from the next writable transition. If called reentrantly from the
same handle's send-ready callback, the call fails with `errno=EDEADLK`.

Supported subjects: raw `PAIR`, `PUB`, `XPUB`, `DEALER`, `ROUTER`, `STREAM`,
`spot`, and `spot_node`. Send-ready is independent from receive mode. This
callback and `ZLINK_POLLOUT` expose the same send-recovery readiness axis: a
readiness signal means it is worth retrying send, not that the retry is
guaranteed to succeed. With the default `MANDATORY=1`, ROUTER readiness is
surfaced only while a reachable peer exists. Unsupported subjects return
`ENOTSUP`.

**Returns:** `ZLINK_HANDLER_OK` on success; otherwise a `zlink_handler_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_send`
