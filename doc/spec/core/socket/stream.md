[Spec Index](../../README.md) · [Core Index](../README.md) · [Socket Common](README.md)

# Socket — STREAM

Raw TCP/WS communication with peer routing-id addressing. STREAM is
bind-only; it does NOT support `zlink_connect`.

## Stream Options (`zlink_stream_option_t`)

Used with `zlink_set_stream_option()` / `zlink_get_stream_option()`.

| Constant | Description |
|---|---|
| `ZLINK_STREAM_OPT_NOTIFY` | Enable STREAM connect/disconnect notifications (`int`; 0 or 1) |

## Functions

### zlink_set_stream_option

Set a stream-specific option.

```c
int zlink_set_stream_option (void *handle_,
                              zlink_stream_option_t option_,
                              const void *optval_,
                              size_t optvallen_);
```

Configures a STREAM socket option. Use `zlink_set_option()` for common
options shared across all socket types.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_get_stream_option`, `zlink_set_option`

---

### zlink_get_stream_option

Get a stream-specific option.

```c
int zlink_get_stream_option (void *handle_,
                              zlink_stream_option_t option_,
                              void *optval_,
                              size_t *optvallen_);
```

Retrieves the current value of a STREAM socket option.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_set_stream_option`

---

### zlink_send_rid

Send a multipart message to a specific peer by routing id.

```c
int zlink_send_rid (void *s_,
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

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EFAULT` if `s_` is NULL. `EAGAIN` if the operation would block
and `ZLINK_DONTWAIT` was set. `EHOSTUNREACH` if the target peer is not
connected (ROUTER with `ROUTER_MANDATORY`). `ETERM` if the context was
terminated.

**See also:** `zlink_send_rid`, `zlink_send`, `zlink_recv`

---

### Non-blocking routed send

Non-blocking routed send using the existing routed send API.

```c
int zlink_send_rid (void *s_,
                    const zlink_routing_id_t *target_rid_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    zlink_send_flags_t flags_);
```

Use `zlink_send_rid(..., ZLINK_DONTWAIT)` for non-blocking routed send.
Bindings may convert errno into `zlink_send_result_t`. On success,
ownership of all parts is transferred to the library. On failure,
ownership remains with the caller.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_send_rid`

---

### zlink_recv

Receive a multipart message from a socket.

```c
int zlink_recv (void *s_,
                zlink_routing_id_t *source_rid_out_,
                zlink_msg_t **parts_out_,
                size_t *part_count_out_,
                zlink_send_flags_t flags_);
```

Receives a complete multipart message from socket `s_`. On success,
`*parts_out_` points to a library-allocated array of `*part_count_out_`
message parts, and `*source_rid_out_` is set to the routing id of the
sender (where applicable). Ownership of the parts array and each part is
transferred to the caller, who must close every part (or call
`zlink_multipart_close()`) and free the array. The socket must be in recv
mode (no handler attached). If a receive handler has been attached via
`zlink_recv_handler()`, this call fails with `errno=EBUSY`. Pass
`ZLINK_DONTWAIT` to return immediately when no message is available.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EAGAIN` if the operation would block and `ZLINK_DONTWAIT` was
set, or if `ZLINK_OPT_RCVTIMEO` expired. `EBUSY` if a receive handler is
attached. `ETERM` if the context was terminated.

**See also:** `zlink_send`, `zlink_recv_handler`, `zlink_multipart_close`

---

### zlink_recv_handler

Attach a message receive handler to a socket.

```c
int zlink_recv_handler (void *s_,
                        zlink_socket_msg_handler_fn handler_,
                        void *userdata_);
```

Attach a message receive handler to a multipart receive subject. Supported
subjects are raw `PAIR`, `DEALER`, and `STREAM`.
After attach, direct recv and data-plane poller `ZLINK_POLLIN` on the same
subject fail with `errno=EBUSY`. A second attach on the same subject also
fails with `errno=EBUSY`. Unsupported subjects return `ENOTSUP`.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EINVAL` if the handler is NULL. `ENOTSUP` if the socket type does
not accept a message handler. `EBUSY` if a handler is already attached.

**See also:** `zlink_subscribe_handler`, `zlink_socket`, `zlink_close`

---

### zlink_send_ready_handler

Install or replace the send-ready callback.

```c
int zlink_send_ready_handler (
  void *s_, zlink_send_ready_handler_fn handler_, void *userdata_);
```

The handler is replace-only. Passing NULL is invalid. A successful replace is
visible from the next writable transition. If called reentrantly from the
same handle's send-ready callback, the call fails with `errno=EDEADLK`.

Supported subjects: raw `PAIR`, `PUB`, `XPUB`, `DEALER`, `ROUTER`, `STREAM`,
`spot`, and `spot_node`. Send-ready is independent from receive
callback mode. After attach, data-plane poller `ZLINK_POLLOUT` on the same
subject fails with `errno=EBUSY`. Unsupported subjects return `ENOTSUP`.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_send`
