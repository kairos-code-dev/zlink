[Spec Index](../../README.md) · [Core Index](../README.md) · [Socket Common](README.md)

# Socket — SUB

Subscribe socket with topic filtering. SUB is receive-only for data;
subscription management is the control plane.

## Automatic HWM defaults

SUB is classified as the `recv_ingress` role by the context automatic HWM
policy. With the default context settings, recv HWM starts from floor `8`
and is recalculated within the same context budget as connection counts
grow. Manual `RCVHWM` or `RCVBUF` settings override the automatic values.

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

Configures a SUB/XSUB socket option. Also applies to spot-sub and
spotnode-sub handles. Use `zlink_set_option()` for common options shared
across all socket types.

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

Subscribes the handle to messages matching `filter_`. Filter
interpretation: if `filter_` ends with `*`, it is a prefix-match pattern;
otherwise it is an exact topic.

Applicable types: raw SUB, raw XSUB.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**Errors:** `EFAULT` if `handle_` is NULL. `EINVAL` if `filter_` is NULL,
empty, or contains invalid pattern syntax (multiple `*` or mid-string `*`).
`ENOTSUP` if the handle type does not support subscribe.

**See also:** `zlink_unset_subscription`, `zlink_subscribe`

---

### zlink_unset_subscription

Unsubscribe from a topic filter on a raw socket.

```c
zlink_config_result_t zlink_unset_subscription (void *handle_, const char *filter_);
```

Removes a previously registered subscription. The same string
interpretation rules as `zlink_set_subscription()` apply: trailing `*`
means pattern unsubscribe, otherwise exact topic unsubscribe.

Applicable types: raw SUB, raw XSUB.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**Errors:** `EFAULT` if `handle_` is NULL. `EINVAL` if `filter_` is NULL
or empty. `ENOTSUP` if the handle type does not support unsubscribe.

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

Applicable types: raw SUB, raw XSUB, `spot`, `spot_node`.

**Returns:** `ZLINK_RECV_OK` on success; otherwise a `zlink_recv_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**Errors:** `EFAULT` if `subject_` is NULL. `EAGAIN` if `ZLINK_DONTWAIT`
was set and no message is available. `EMSGSIZE` if the topic buffer is
too small. `ENOTSUP` if the subject type does not support subscribe recv.

**See also:** `zlink_set_subscription`

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
length. `*is_pattern_out_` is 1 if the filter is a prefix pattern (trailing
`*`), 0 if exact.

Applicable types: raw SUB, raw XSUB.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**Errors:** `EINVAL` if index is out of range. `EMSGSIZE` if the buffer is
too small. `ENOTSUP` if the handle type does not support subscription query.

**See also:** `zlink_set_subscription`, `zlink_get_sub_option`
