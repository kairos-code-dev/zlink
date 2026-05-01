[Spec Index](../../README.md) · [Core Index](../README.md) · [Socket Common](README.md)

# Socket — STREAM

Raw TCP/WS communication with peer routing-id addressing. STREAM is
bind-only; it does NOT support `zlink_connect`.

## Receive Model

STREAM is the only socket type that exposes three receive models. Exactly
one of the three may be active on a given handle.

- raw recv: `zlink_recv()` pulls transport fragments directly.
- raw callback: `zlink_recv_handler()` delivers raw fragments through a
  callback.
- packet callback: `zlink_stream_packet_handler()` delivers packets
  assembled from the fixed framing convention as header/body pairs.

A second attempt to activate a different mode on the same handle fails
with `EBUSY`. Mode transitions are one-way only, and the three models are
mutually exclusive.

## Automatic HWM defaults

STREAM is classified as the `stream` policy class by the context automatic HWM
policy. The active auto-HWM profile selects the unit budget and message-size
cap; the default profile is `balanced`. Auto-HWM is opt-in. If the application
leaves `SNDBUF` / `RCVBUF` unset, STREAM uses the compatibility default
`262144`.

## Stream Options (`zlink_stream_option_t`)

Used with `zlink_set_stream_option()` / `zlink_get_stream_option()`.

| Constant | Description |
|---|---|
| `ZLINK_STREAM_OPT_NOTIFY` | Enable STREAM connect/disconnect notifications (`int`; 0 or 1) |

## Functions

### zlink_set_stream_option

Set a stream-specific option.

```c
zlink_config_result_t zlink_set_stream_option (void *handle_,
                              zlink_stream_option_t option_,
                              const void *optval_,
                              size_t optvallen_);
```

Configures a STREAM socket option. Use `zlink_set_option()` for common
options shared across all socket types.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_get_stream_option`, `zlink_set_option`

---

### zlink_get_stream_option

Get a stream-specific option.

```c
zlink_config_result_t zlink_get_stream_option (void *handle_,
                              zlink_stream_option_t option_,
                              void *optval_,
                              size_t *optvallen_);
```

Retrieves the current value of a STREAM socket option.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_set_stream_option`

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

**See also:** `zlink_send_rid`, `zlink_send`, `zlink_recv`

---

### Non-blocking routed send

Non-blocking routed send using the routed send API.

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
See [errno-map.md](../errno-map.md) for the full result matrix. On success,
ownership of all parts is transferred to the library. On failure,
ownership remains with the caller.

**Returns:** `ZLINK_SUBMIT_OK` on success, or a `zlink_submit_result_t` value indicating the failure reason. See [errno-map.md](../errno-map.md).

**See also:** `zlink_send_rid`

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
`zlink_multipart_close()`) and free the array. Only usable when the STREAM
handle is in raw recv mode. If raw callback mode
(`zlink_recv_handler()` attached) or packet callback mode
(`zlink_stream_packet_handler()` attached) is active, this call fails with
`errno=EBUSY`. Pass `ZLINK_DONTWAIT` to return immediately when no message
is available.

**Returns:** `ZLINK_RECV_OK` on success; otherwise a `zlink_recv_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**Errors:** `EAGAIN` if the operation would block and `ZLINK_DONTWAIT` was
set, or if `ZLINK_OPT_RCVTIMEO` expired. `EBUSY` if a raw or packet
callback is attached. `ETERM` if the context was terminated.

**See also:** `zlink_send`, `zlink_recv_handler`,
`zlink_stream_packet_handler`, `zlink_multipart_close`

---

### zlink_recv_handler

Attach a raw receive callback to a raw `STREAM` socket.

```c
zlink_handler_result_t zlink_recv_handler (
  void *s_, zlink_socket_msg_handler_fn handler_, void *userdata_);
```

Attach a raw message receive handler. Supported subjects: raw `STREAM`
only. Unsupported subjects (PAIR, DEALER, etc.) fail with `ENOTSUP`.
After attach, `zlink_recv()`, `zlink_stream_packet_handler()`, and
data-plane poller `ZLINK_POLLIN` on the same handle fail with
`errno=EBUSY`. A second attach on the same handle also fails with
`errno=EBUSY`.

**Returns:** `ZLINK_HANDLER_OK` on success. On failure, returns a
`zlink_handler_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_recv`, `zlink_stream_packet_handler`

---

### zlink_stream_packet_handler

Attach a packet-level receive callback to a raw `STREAM` socket.

```c
typedef void (*zlink_stream_packet_handler_fn) (
  void *stream_,
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *header_,
  zlink_msg_t *body_,
  void *userdata_);

zlink_handler_result_t zlink_stream_packet_handler (
  void *stream_,
  zlink_stream_packet_handler_fn handler_,
  void *userdata_);
```

This function applies only to raw `STREAM`. Other socket types fail with
`ENOTSUP`.

Once attached, the implementation accumulates incoming bytes per
connection and invokes the callback once for each complete packet. The
framing convention is fixed and parsed in this order:

1. `header_size`: 2-byte big-endian `uint16_t`
2. `body_size`: 4-byte big-endian `uint32_t`
3. header payload (`header_size` bytes)
4. body payload (`body_size` bytes)

Packets with `header_size == 0` or `body_size == 0` are allowed, as are
packets with both sizes zero. In every case `header_` and `body_` are
delivered as valid length-zero `zlink_msg_t` objects, never `NULL`.

Ownership rules:

- `source_rid_` is a borrowed view pointing to the routing id of the
  sending client connection. It is valid only for the duration of the
  callback; callers that need to retain it must copy the value.
- Ownership of `header_` and `body_` is transferred to the callback. The
  callback must close or otherwise consume each `zlink_msg_t` exactly
  once.

If raw callback mode (`zlink_recv_handler()`) is already attached to the
same handle, this call fails with `EBUSY`. Conversely, once packet
callback mode is active, `zlink_recv()`, `zlink_recv_handler()`, and
data-plane `ZLINK_POLLIN` on the same handle all fail with `EBUSY`. A
second `zlink_stream_packet_handler()` attach on the same handle also
fails with `EBUSY`.

If a malformed packet is detected during assembly (premature connection
close, declared length exceeding implementation limits, internal assembly
failure, etc.), the connection is treated as an invalid stream in packet
mode and is closed as the default policy. Partial packets are never
delivered to the callback. Malformed events are observable through the
socket monitor surface.

**Returns:** `ZLINK_HANDLER_OK` on success. On failure, returns a
`zlink_handler_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**Errors:** `INVALID_ARGUMENT` if the handler is NULL. `NOT_SUPPORTED` if
the handle is not raw `STREAM`. `BUSY` if another receive mode is already
active.

**See also:** `zlink_recv`, `zlink_recv_handler`

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
guaranteed to succeed. Unsupported subjects return `ENOTSUP`.

**Returns:** `ZLINK_HANDLER_OK` on success; otherwise a `zlink_handler_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_send`
