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
`zlink_multipart_close()`) and free the array. PAIR has no receive callback
surface; receive is poll + `zlink_recv` only — the socket itself remains
bidirectional and supports `zlink_send`. The intended pattern is to observe
`ZLINK_POLLIN` from a poller and then pull data with this function. Pass
`ZLINK_DONTWAIT` to return immediately when no message is available.

**Returns:** `ZLINK_RECV_OK` on success; otherwise a `zlink_recv_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**Errors:** `EAGAIN` if the operation would block and `ZLINK_DONTWAIT` was
set, or if `ZLINK_OPT_RCVTIMEO` expired. `ETERM` if the context was
terminated.

**See also:** `zlink_send`, `zlink_multipart_close`

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

Supported subjects: raw `PAIR`, `PUB`, `XPUB`, `DEALER`, `ROUTER`, and
`STREAM`. Send-ready is independent from receive mode. This
callback and `ZLINK_POLLOUT` expose the same send-recovery readiness axis: a
readiness signal means it is worth retrying send, not that the retry is
guaranteed to succeed. Unsupported subjects return `ENOTSUP`.

**Returns:** `ZLINK_HANDLER_OK` on success; otherwise a `zlink_handler_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_send`
