[Spec Index](../../README.md) · [Core Index](../README.md) · [Socket Common](README.md)

# Socket — PAIR

1:1 bidirectional socket. Each side can send and receive. No type-specific
options.

## Applicable Functions

### zlink_send

Send a multipart message on a socket.

```c
zlink_submit_result_t zlink_send (void *s_,
                zlink_msg_t *parts_,
                size_t part_count_,
                zlink_send_flags_t flags_);
```

Sends a multipart message consisting of `part_count_` parts from the
`parts_` array on socket `s_`. On success, ownership of every part in the
array is transferred to the library and the caller must not access them
afterwards. On failure, ownership remains with the caller. The `flags_`
parameter may be 0 or `ZLINK_DONTWAIT`.

**Returns:** `ZLINK_SUBMIT_OK` on success, or a `zlink_submit_result_t` value indicating the failure reason. See [errno-map.md](../errno-map.md).

**Errors:** `BACKPRESSURED` if the operation would block and `ZLINK_DONTWAIT` was set.
`TERMINATED` if the context was terminated. See [errno-map.md](../errno-map.md) for the full result matrix.

**See also:** `zlink_send`, `zlink_recv`

---

### Non-blocking send

Non-blocking send using the same `zlink_send` entry point with
`ZLINK_DONTWAIT`.

```c
zlink_submit_result_t zlink_send (void *s_,
                zlink_msg_t *parts_,
                size_t part_count_,
                zlink_send_flags_t flags_);
```

Use `zlink_send(..., ZLINK_DONTWAIT)` for non-blocking send. Non-blocking
send returns `ZLINK_SUBMIT_BACKPRESSURED` when the operation would block,
`ZLINK_SUBMIT_NOT_CONNECTED` when the peer is not reachable. See
[errno-map.md](../errno-map.md) for the full result matrix.

On success, ownership of all parts is transferred to the library. On
failure, ownership remains with the caller.

**See also:** `zlink_send`

---

### zlink_recv

Receive a multipart message from a socket.

```c
bool zlink_recv (void *s_,
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

**Returns:** `true` on success, `false` on failure (errno is set).

**Errors:** `EAGAIN` if the operation would block and `ZLINK_DONTWAIT` was
set, or if `ZLINK_OPT_RCVTIMEO` expired. `EBUSY` if a receive handler is
attached. `ETERM` if the context was terminated.

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
