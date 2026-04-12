[Spec Index](../../README.md) · [Core Index](../README.md) · [Socket Common](README.md)

# Socket — XPUB

Extended publisher with subscription forwarding and manual control. XPUB
receives subscription events from subscribers and supports manual
subscription management.

## Pub Options (`zlink_pub_option_t`)

Used with `zlink_set_pub_option()` / `zlink_get_pub_option()`.

| Constant | Description |
|---|---|
| `ZLINK_PUB_OPT_VERBOSE` | Pass all subscription messages upstream (`int`; 0 or 1) |
| `ZLINK_PUB_OPT_VERBOSER` | Pass all subscribe and unsubscribe messages upstream (`int`; 0 or 1) |
| `ZLINK_PUB_OPT_MANUAL` | Enable manual subscription management (`int`; 0 or 1) |
| `ZLINK_PUB_OPT_MANUAL_LAST_VALUE` | Enable last-value caching in manual mode (`int`; 0 or 1) |
| `ZLINK_PUB_OPT_NODROP` | Do not silently drop messages on HWM; return `EAGAIN` instead (`int`; 0 or 1) |
| `ZLINK_PUB_OPT_WELCOME_MSG` | Message sent to new subscribers on connect (`binary`) |
| `ZLINK_PUB_OPT_TOPICS_COUNT` | Number of subscribed topics (get-only, `int`) |
| `ZLINK_PUB_OPT_APPROVE_SUBSCRIBE` | Approve a pending subscription in manual mode (`binary`) |
| `ZLINK_PUB_OPT_REJECT_SUBSCRIBE` | Reject a pending subscription in manual mode (`binary`) |

## Functions

### zlink_set_pub_option

Set a pub-specific option.

```c
int zlink_set_pub_option (void *handle_,
                           zlink_pub_option_t option_,
                           const void *optval_,
                           size_t optvallen_);
```

Configures a PUB/XPUB socket option. Also applies to spot-pub and
spotnode-pub handles. Use `zlink_set_option()` for common options shared
across all socket types.

**Returns:** `ZLINK_SUBMIT_OK` on success. On failure, returns a
`zlink_submit_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_get_pub_option`, `zlink_set_option`

---

### zlink_get_pub_option

Get a pub-specific option.

```c
int zlink_get_pub_option (void *handle_,
                           zlink_pub_option_t option_,
                           void *optval_,
                           size_t *optvallen_);
```

Retrieves the current value of a PUB/XPUB socket option.

**Returns:** `ZLINK_SUBMIT_OK` on success. On failure, returns a
`zlink_submit_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**See also:** `zlink_set_pub_option`

---

### zlink_publish

Publish a multipart message.

```c
zlink_submit_result_t zlink_publish (void *subject_,
                   const char *topic_id_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   zlink_send_flags_t flags_);
```

Publishes a multipart message on the given subject. On success, ownership
of all parts is transferred to the library.

- For raw `PUB` / `XPUB`: `topic_id_` must be NULL (raw pub publish).
  Topic matching uses the wire first-frame prefix convention.

**Returns:** `ZLINK_SUBMIT_OK` on success. On failure, returns a
`zlink_submit_result_t` value. Detailed internal errno remains available
through `zlink_errno()` for diagnostics.

**Errors:** `EFAULT` if `subject_` is NULL. `EINVAL` if `topic_id_` is
NULL for spot/spot_node, or non-NULL for unsupported types. `ENOTSUP` if
the subject type does not support publish.

**See also:** `zlink_publish`, `zlink_set_subscription`, `zlink_subscribe`

---

### Non-blocking publish

Non-blocking publish using the existing publish API.

```c
zlink_submit_result_t zlink_publish (void *subject_,
                   const char *topic_id_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   zlink_send_flags_t flags_);
```

Use `zlink_publish(..., ZLINK_DONTWAIT)` for non-blocking publish.
Non-blocking send returns `ZLINK_SUBMIT_BACKPRESSURED` when the operation
would block, `ZLINK_SUBMIT_NOT_CONNECTED` when the peer is not reachable.
See [errno-map.md](../errno-map.md) for the full result matrix.

On success, ownership of all parts is transferred to the library. On
failure, ownership remains with the caller.

**Returns:** `ZLINK_SUBMIT_OK` on success, or a `zlink_submit_result_t` value indicating the failure reason. See [errno-map.md](../errno-map.md).

**See also:** `zlink_publish`

---

### zlink_subscription_event

Receive a subscription event from an XPUB socket.

```c
int zlink_subscription_event (void *subject_,
                               zlink_routing_id_t *source_rid_out_,
                               int *subscribed_out_,
                               char *topic_id_out_,
                               size_t *topic_id_len_out_,
                               zlink_send_flags_t flags_);
```

Receives the next subscription event in recv mode. On success,
`*source_rid_out_` identifies the subscribing peer, `*subscribed_out_` is
1 for subscribe or 0 for unsubscribe, and `*topic_id_out_` /
`*topic_id_len_out_` receive the topic bytes (binary-safe, same buffer
contract as `zlink_subscribe()`).

Applicable types: raw XPUB only.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EFAULT` if `subject_` is NULL. `EAGAIN` if `ZLINK_DONTWAIT`
was set and no event is available. `EMSGSIZE` if the topic buffer is too
small. `ENOTSUP` if the subject is not XPUB.

**See also:** `zlink_publish`

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
