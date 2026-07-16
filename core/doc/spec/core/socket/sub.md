[Spec Index](../../README.md) · [Core Index](../README.md) · [Socket Common](README.md)

# Socket — SUB

Subscribe socket with topic filtering. SUB is receive-only for data;
subscription management is the control plane.

## Automatic HWM defaults

SUB is classified as the `recv_ingress` policy class by the context automatic
HWM policy. The active auto-HWM profile selects the unit budget and
message-size cap; the default profile is `balanced`. Manual `RCVHWM` or
`RCVBUF` settings override the automatic values.

## Sub Options (`zlink_sub_option_t`)

Used with `zlink_set_sub_option()` / `zlink_get_sub_option()`.

| Constant | Description |
|---|---|
| `ZLINK_SUB_OPT_TOPICS_COUNT` | Number of subscribed topics (get-only, `int`) |

## Functions

### zlink_set_sub_option

Set a sub-specific option.

```c
zlink_config_result_t zlink_set_sub_option (void *handle_,
                           zlink_sub_option_t option_,
                           const void *optval_,
                           size_t optvallen_);
```

Configures a SUB/XSUB socket option. Use `zlink_set_option()` for common
options shared across all socket types.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_get_sub_option`, `zlink_set_option`

---

### zlink_get_sub_option

Get a sub-specific option.

```c
zlink_config_result_t zlink_get_sub_option (void *handle_,
                           zlink_sub_option_t option_,
                           void *optval_,
                           size_t *optvallen_);
```

Retrieves the current value of a SUB/XSUB socket option.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**See also:** `zlink_set_sub_option`

---

### zlink_set_subscription

Subscribe to a topic filter on a raw socket.

```c
zlink_config_result_t zlink_set_subscription (void *handle_, const char *filter_);
```

Subscribes the handle to messages matching `filter_`. A subscription is a
byte-prefix filter: a message matches when its topic starts with the `filter_`
bytes. An empty `filter_` subscribes to all messages. The filter bytes are
binary-safe; there is no wildcard syntax (a trailing `*` is matched literally).

Applicable types: raw SUB, raw XSUB.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**Errors:** `EFAULT` if `handle_` is NULL. `EINVAL` if `filter_` is NULL, or the
handle type does not support subscribe.

**See also:** `zlink_unset_subscription`, `zlink_subscribe`

---

### zlink_unset_subscription

Unsubscribe from a topic filter on a raw socket.

```c
zlink_config_result_t zlink_unset_subscription (void *handle_, const char *filter_);
```

Removes a previously registered subscription. The same byte-prefix
interpretation as `zlink_set_subscription()` applies; the `filter_` bytes must
match a previously registered prefix.

Applicable types: raw SUB, raw XSUB.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**Errors:** `EFAULT` if `handle_` is NULL. `EINVAL` if `filter_` is NULL, or the
handle type does not support unsubscribe.

**See also:** `zlink_set_subscription`

---

### zlink_subscribe

Receive a topic-bearing multipart message.

```c
zlink_recv_result_t zlink_subscribe (void *subject_,
                     zlink_routing_id_t *source_rid_out_,
                     zlink_msg_t **parts_out_,
                     size_t *part_count_out_,
                     char *topic_id_out_,
                     size_t *topic_id_len_out_,
                     zlink_recv_flags_t flags_);
```

Receives the next topic-bearing message in recv mode. On success,
`*source_rid_out_` is set to the sender's routing id (zeroed if the
underlying transport does not carry identity), `*topic_id_out_` /
`*topic_id_len_out_` receive the topic bytes (binary-safe), and
`*parts_out_` / `*part_count_out_` receive the payload frames. Ownership
of the parts array is transferred to the caller.

Raw SUB/XSUB are recv-only types: the intended pattern is to observe
`ZLINK_POLLIN` from a poller and then pull topic messages with this
function.

Applicable types: raw SUB, raw XSUB.

**Returns:** `ZLINK_RECV_OK` on success; otherwise a `zlink_recv_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**Errors:** `EFAULT` if `subject_` is NULL. `EAGAIN` if `ZLINK_DONTWAIT`
was set and no message is available. `EMSGSIZE` if the topic buffer is
too small. `ENOTSUP` if the subject type does not support subscribe recv.

**See also:** `zlink_set_subscription`

---

### zlink_subscribe_part

Receive one payload part of a topic-bearing message from a raw `SUB` or
`XSUB` socket.

```c
ZLINK_EXPORT zlink_recv_result_t zlink_subscribe_part (void *sub_,
                                                       const zlink_routing_id_t **source_rid_out_,
                                                       char *topic_id_buf_,
                                                       size_t topic_id_capacity_,
                                                       size_t *topic_id_len_out_,
                                                       zlink_msg_t *part_out_,
                                                       zlink_part_flag_t *has_more_out_,
                                                       zlink_recv_flags_t flags_);
```

`topic_id_len_out_`, an initialized `part_out_`, and `has_more_out_` are
required. `source_rid_out_` is optional and always receives `NULL` for raw
`SUB` and `XSUB`. On success, the function copies binary-safe topic bytes into
the caller's buffer without appending a NUL byte and transfers ownership of
the payload part to the caller. The caller must close the received part
exactly once with `zlink_msg_close(part_out_)`.

When `topic_id_capacity_ == 0`, `topic_id_buf_` may be NULL; the function
successfully returns the required topic length and the payload part. If the
capacity is greater than zero but the buffer is NULL, `errno` is `EFAULT`. If
the buffer is too small, `errno` is `EMSGSIZE`. These two errors occur after
the payload part has been received, so `topic_id_len_out_`, `part_out_`, and
`has_more_out_` are valid and ownership of the part transfers to the caller.
A short buffer remains unchanged. Other failures that occur before receiving
a payload do not transfer ownership of a part.

Receive every payload part from the first through the last part of one
multipart message with this function on the same thread. `*has_more_out_` is
`ZLINK_PART_MORE` when another payload part follows and `ZLINK_PART_FINAL` for
the last part. Applicable types are raw `SUB` and raw `XSUB`.

---

### zlink_subscription_at

Retrieve the subscription filter at a given index.

```c
zlink_config_result_t zlink_subscription_at (void *handle_,
                           size_t index_,
                           char *filter_out_,
                           size_t *filter_len_inout_,
                           int *is_pattern_out_);
```

Returns the subscription filter string at `index_` (0-based). On entry,
`*filter_len_inout_` is the buffer size; on return it is set to the actual
length. `*is_pattern_out_` reports whether the filter is a pattern
subscription. All raw subscriptions in 10.0.0 are byte-prefix filters, so it
always reports `0`.

Applicable types: raw SUB, raw XSUB.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**Errors:** `ENOENT` if index is out of range. `EINVAL` if the buffer is
too small. `ENOTSUP` if the handle type does not support subscription query.

**See also:** `zlink_set_subscription`, `zlink_get_sub_option`
