[Spec Index](../../README.md) · [Core Index](../README.md) · [Socket Common](README.md)

# Socket — ROUTER

Routing-id-based addressing, identity-aware recv, directed send. ROUTER is
the reply side in request-reply patterns.

## Router Options (`zlink_router_option_t`)

Used with `zlink_set_router_option()` / `zlink_get_router_option()`.

| Constant | Description |
|---|---|
| `ZLINK_ROUTER_OPT_MANDATORY` | Return `EHOSTUNREACH` when routing to an unconnected peer (`int`; 0 or 1) |
| `ZLINK_ROUTER_OPT_HANDOVER` | Allow new connection to take over an existing routing identity (`int`; 0 or 1) |
| `ZLINK_ROUTER_OPT_PROBE` | Send an empty message on connect to establish identity at the ROUTER peer (`int`; 0 or 1) |
| `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` | Set routing identity for the next outgoing connection (`binary`) |
| `ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS` | Default request timeout in milliseconds for `zlink_router_request()` (`uint32_t`) |

## Functions

### zlink_set_router_option

Set a router-specific option.

```c
int zlink_set_router_option (void *handle_,
                              zlink_router_option_t option_,
                              const void *optval_,
                              size_t optvallen_);
```

Configures a ROUTER socket option. Use `zlink_set_option()` for common
options shared across all socket types.

**Returns:** `ZLINK_SUBMIT_OK` on success. On failure, returns a
`zlink_submit_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_get_router_option`, `zlink_set_option`

---

### zlink_get_router_option

Get a router-specific option.

```c
int zlink_get_router_option (void *handle_,
                              zlink_router_option_t option_,
                              void *optval_,
                              size_t *optvallen_);
```

Retrieves the current value of a ROUTER socket option.

**Returns:** `ZLINK_SUBMIT_OK` on success. On failure, returns a
`zlink_submit_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

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
connected (ROUTER with `ROUTER_MANDATORY`). `TERMINATED` if the context was
terminated. See [errno-map.md](../errno-map.md) for the full result matrix.

**See also:** `zlink_send_rid`, `zlink_recv`

---

### Non-blocking routed send

Non-blocking directed send using the existing routed send API.

```c
zlink_submit_result_t zlink_send_rid (void *s_,
                    const zlink_routing_id_t *target_rid_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    zlink_send_flags_t flags_);
```

Use `zlink_send_rid(..., ZLINK_DONTWAIT)` for non-blocking routed send.
Non-blocking send returns `ZLINK_SUBMIT_BACKPRESSURED` when the operation
would block, `ZLINK_SUBMIT_NOT_CONNECTED` when the peer is not reachable.
See [errno-map.md](../errno-map.md) for the full result matrix.

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

**See also:** `zlink_router_request`, `zlink_router_handler`

---

### zlink_router_handler

Attach the routed receive handler to a ROUTER socket.

```c
zlink_handler_result_t zlink_router_handler (void *router_,
                                             zlink_router_handler_fn handler_,
                                             void *userdata_);
```

Attaches the single direct receive callback for a ROUTER socket. The
callback receives both plain ROUTER traffic and spot-originated routed
traffic. Plain ROUTER traffic sets `source_spot_rid_` to `NULL`.
Spot-originated traffic sets both `source_node_rid_` and
`source_spot_rid_`.

Only one routed receive callback may be attached to a ROUTER handle.
While the callback is attached, direct routed recv on the same ROUTER
handle fails with `ZLINK_RECV_BUSY`.

**Returns:** `ZLINK_HANDLER_OK` on success. On failure, returns a
`zlink_handler_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_router_reply`, `zlink_router_reply_spot`,
`zlink_router_handler_fn`, `zlink_router_recv`

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
spot-originated routed traffic.

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
`zlink_router_handler`

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

**See also:** `zlink_send`, `zlink_recv_handler`, `zlink_multipart_close`

---

### zlink_recv_handler

Attach a message receive handler to a socket.

```c
bool zlink_recv_handler (void *s_,
                         zlink_socket_msg_handler_fn handler_,
                         void *userdata_);
```

Attach a message receive handler to a multipart receive subject. Supported
subjects are raw `PAIR`, `DEALER`, and `STREAM`.
After attach, direct recv and data-plane poller `ZLINK_POLLIN` on the same
subject fail with `errno=EBUSY`. A second attach on the same subject also
fails with `errno=EBUSY`. Unsupported subjects return `ENOTSUP`.

**Returns:** `true` on success, `false` on failure (errno is set).

**Errors:** `EINVAL` if the handler is NULL. `ENOTSUP` if the socket type does
not accept a message handler. `EBUSY` if a handler is already attached.

**See also:** `zlink_subscribe_handler`, `zlink_socket`, `zlink_close`

---

### zlink_set_routing_id

Set the routing identity on a socket.

```c
int zlink_set_routing_id (void *handle_,
                           const void *data_,
                           size_t size_);
```

Assigns a routing identity to the socket. The identity is used for ROUTER
addressing and must be at most 255 bytes. Must be set before the first
bind or connect.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_get_routing_id`

---

### zlink_send_ready_handler

Install or replace the send-ready callback.

```c
bool zlink_send_ready_handler (
  void *s_, zlink_send_ready_handler_fn handler_, void *userdata_);
```

The handler is replace-only. Passing NULL is invalid. A successful replace is
visible from the next writable transition. If called reentrantly from the
same handle's send-ready callback, the call fails with `errno=EDEADLK`.

Supported subjects: raw `PAIR`, `PUB`, `XPUB`, `DEALER`, `ROUTER`, `STREAM`,
`spot`, and `spot_node`. Send-ready is independent from receive
callback mode. After attach, data-plane poller `ZLINK_POLLOUT` on the same
subject fails with `errno=EBUSY`. Unsupported subjects return `ENOTSUP`.

**Returns:** `true` on success, `false` on failure (errno is set).

**See also:** `zlink_send`
