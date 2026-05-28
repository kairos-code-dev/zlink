[English](./errno-map.md) | [한국어](./errno-map.ko.md)

[Spec Index](../README.md) · [Core Index](./README.md)

# Per-Function Result Enums

This document defines the canonical outcome mapping for public C APIs that
return result enums. Those functions use enum ranges that do not overlap
across the API, so a single `int` code is always unambiguous.

Some poller helpers use plain `int` returns with a separate
`error_out_` output. Those helpers are adjacent to this scheme, but they are
not themselves members of the result-enum families below.

Internal implementation paths still use detailed `errno` values. Exported API
boundaries normalize those values into the public result enums defined here.

## zlink_errno — Internal Error Detail

`zlink_errno()` returns the raw internal `errno` value from the calling
thread. It is most useful when a result enum is a coarse public bucket. That
includes `INTERNAL_ERROR`, where multiple internal `errno` values
intentionally share the same public result bucket.

```c
zlink_submit_result_t rc = zlink_send(s, parts, count, 0);
if (rc == ZLINK_SUBMIT_INTERNAL_ERROR) {
    int detail = zlink_errno();          /* e.g. EPROTO, ENOMEM */
    const char *msg = zlink_strerror(detail);
    log("internal error: %s", msg);
}
```

- In the common normalized cases, `zlink_errno()` is **not required**.
  Use it when you need the original internal reason behind a coarse bucket.
- `zlink_errno()` is a thread-local last-error store (GetLastError pattern).
  Only the most recent failure is retained.

---

## zlink_submit_result_t

Applies to send, request submit, and reply submit functions.

```c
typedef enum zlink_submit_result_t
{
    /* Submit succeeded. */
    ZLINK_SUBMIT_OK = 0,

    /* Normal control-flow result. */
    ZLINK_SUBMIT_BACKPRESSURED    = 1,
    ZLINK_SUBMIT_NOT_CONNECTED    = 2,
    ZLINK_SUBMIT_NOT_FOUND        = 3,
    ZLINK_SUBMIT_NOT_ADMITTED     = 13,

    /* Runtime / lifecycle failure. */
    ZLINK_SUBMIT_TERMINATED       = 4,

    /* Caller contract violation. */
    ZLINK_SUBMIT_INVALID_HANDLE   = 5,
    ZLINK_SUBMIT_INVALID_ARGUMENT = 6,
    ZLINK_SUBMIT_NOT_SUPPORTED    = 7,
    ZLINK_SUBMIT_INVALID_STATE    = 8,
    ZLINK_SUBMIT_THREAD_VIOLATION = 9,

    /* Internal failure. */
    ZLINK_SUBMIT_OUT_OF_MEMORY    = 10,
    ZLINK_SUBMIT_SEQ_EXHAUSTED    = 11,
    ZLINK_SUBMIT_INTERNAL_ERROR   = 12
} zlink_submit_result_t;
```

### Submit Result Classification

#### Normal control-flow result

These are expected outcomes that callers may handle directly.

| Result | Internal errno | Meaning |
|---|---|---|
| `OK` | -- | Submit succeeded |
| `BACKPRESSURED` | `EAGAIN` | Send queue full (HWM) or not writable yet |
| `NOT_CONNECTED` | `ENOTCONN`, `EHOSTUNREACH` | Target peer or path is not connected |
| `NOT_FOUND` | `ENOENT` | Target peer, spot, or routed destination was not found |
| `NOT_ADMITTED` | `ECONNREFUSED`-class | The local peer knows the remote weight is `0`, so new outbound is refused. The connection itself may still be alive; the failure lifts when the peer returns to a positive weight. State propagation is best-effort, so under races the same situation may surface first as `NOT_CONNECTED` or `NOT_FOUND`. |

#### Runtime / lifecycle failure

| Result | Internal errno | Meaning |
|---|---|---|
| `TERMINATED` | `ETERM` | Context was terminated |

#### Caller contract violation

These indicate a bug in the caller.

| Result | Internal errno | Meaning |
|---|---|---|
| `INVALID_HANDLE` | `EFAULT` | NULL handle or invalid pointer |
| `INVALID_ARGUMENT` | `EINVAL` | Wrong socket type, NULL handler, zero `request_seq`, invalid routing ID |
| `NOT_SUPPORTED` | `ENOTSUP` | Operation not supported for this socket type or invalid flags |
| `INVALID_STATE` | `EFSM`, and some non-request submit `EBUSY` state conflicts | Socket or handle is in the wrong state |
| `THREAD_VIOLATION` | `EMTHREAD` | Socket accessed from the wrong thread model |

#### Internal failure

| Result | Internal errno | Meaning |
|---|---|---|
| `OUT_OF_MEMORY` | `ENOMEM` | Memory allocation failed |
| `SEQ_EXHAUSTED` | `EBUSY` | Request submit path exhausted the pending request sequence space |
| `INTERNAL_ERROR` | `EPROTO` and other internal submit failures | Internal send/request/reply submit error |

### Applicable functions

| Category | Functions |
|---|---|
| Send | `zlink_send`, `zlink_send_rid`, `zlink_publish` |
| Socket request | `zlink_dealer_request`, `zlink_router_request` |
| Socket reply | `zlink_router_reply` |
| SPOT request | `zlink_spot_request_channel`, `zlink_router_request_spot` |
| SPOT send | `zlink_spot_send_channel`, `zlink_router_send_spot` |
| SPOT reply | `zlink_spot_reply_spot`, `zlink_spot_reply_router`, `zlink_router_reply_spot` |

---

## zlink_request_result_t

Applies to request completion delivered through `zlink_reply_handler_fn`.
This enum is separate from the submit result -- it represents the outcome
of the request after it was successfully submitted.

```c
typedef enum zlink_request_result_t
{
    ZLINK_REQUEST_OK              = 0,
    ZLINK_REQUEST_TIMED_OUT       = 101,
    ZLINK_REQUEST_NOT_FOUND       = 102,
    ZLINK_REQUEST_TERMINATED      = 103,
    ZLINK_REQUEST_PROTOCOL_ERROR  = 104,
    ZLINK_REQUEST_INTERNAL_ERROR  = 105,
    ZLINK_REQUEST_REJECTED        = 106,
    ZLINK_REQUEST_CONFLICT        = 107,
    ZLINK_REQUEST_BUSY            = 108,
    ZLINK_REQUEST_NOT_CONNECTED   = 109,
    ZLINK_REQUEST_INVALID_ARGUMENT = 110,
    ZLINK_REQUEST_INVALID_STATE   = 111,
    ZLINK_REQUEST_NOT_SUPPORTED   = 112
} zlink_request_result_t;
```

### Request Completion Classification

| Result | Callback errno | Meaning |
|---|---|---|
| `OK` | `0` | Reply payload was received successfully |
| `TIMED_OUT` | `ETIMEDOUT` | Reply did not arrive within `timeout_ms` |
| `NOT_FOUND` | `ENOENT` | Target was not found and the request completed with an error reply |
| `TERMINATED` | `ETERM` | Reserved until the request path emits explicit termination completion |
| `PROTOCOL_ERROR` | `EPROTO` | Reply envelope or error reply payload was malformed |
| `INTERNAL_ERROR` | other internal completion failures | Request completion failed without a finer public bucket |
| `REJECTED` | `EACCES`, `ECONNREFUSED` | Target explicitly rejected admission or a join request |
| `CONFLICT` | `ESTALE` | Checked reference generation mismatch, duplicate creation, or another conflict that requires the caller to query again |
| `BUSY` | `EBUSY` | The Actor is in a join or bind state and cannot complete the destroy or bind change |
| `NOT_CONNECTED` | `ENOTCONN`, `EHOSTUNREACH` | Target node or Actor owner route is unreachable |
| `INVALID_ARGUMENT` | `EINVAL`, `EFAULT` | NULL argument, invalid Actor id, invalid routing id, or another caller argument error |
| `INVALID_STATE` | `EFSM` | Current handle state does not match this request |
| `NOT_SUPPORTED` | `ENOTSUP`, `EOPNOTSUPP` | Current handle or mode does not support this request |

### Applicable functions

The first argument of `zlink_reply_handler_fn` is the request completion
result. `zlink_actor_join_spot_handler_fn` (Spot join),
`zlink_actor_join_entry_spot_handler_fn` (Entry Spot join), and
`zlink_actor_lookup_handler_fn` (remote Actor lookup) carry the same enum as
their `result.result` field. The async submit APIs return
`zlink_submit_result_t` for the submit step and deliver this enum through the
appropriate completion handler.

| Category | Functions |
|---|---|
| Routed/channel request completion | `zlink_dealer_request`, `zlink_router_request`, `zlink_spot_request_channel`, `zlink_spot_request_spot`, `zlink_spot_request_router`, `zlink_router_request_spot` |
| Actor join completion (dedicated typedef) | `zlink_spot_node_actor_join_spot` (`zlink_actor_join_spot_handler_fn`) |
| Actor Entry Spot join completion (dedicated typedef) | `zlink_spot_node_actor_join_entry_spot` (`zlink_actor_join_entry_spot_handler_fn`) |
| Actor lookup completion (dedicated typedef) | `zlink_remote_actor_get_ref` (`zlink_actor_lookup_handler_fn`) |
| Actor lifecycle request | `zlink_spot_node_actor_destroy`, `zlink_spot_node_actor_leave_spot`, `zlink_spot_node_actor_close_bound_session` |
| Stream Actor request | `zlink_stream_bind_actor`, `zlink_stream_unbind_actor` |

Actor create accepts multipart payload. A payload shape where `parts_` is NULL
and `part_count_` is greater than `0`, or where `parts_` is not NULL and
`part_count_` is `0`, maps to `ZLINK_REQUEST_INVALID_ARGUMENT` with `EINVAL`.
An invalid payload frame maps to the same public result with `EFAULT`.

---

## zlink_recv_result_t

Applies to recv-style functions, including router/SPOT receives, monitor
receives, and timer receives.

```c
typedef enum zlink_recv_result_t
{
    ZLINK_RECV_OK                 = 0,
    ZLINK_RECV_NO_DATA            = 201,  /* EAGAIN    */
    ZLINK_RECV_BUSY               = 202,  /* EBUSY     */
    ZLINK_RECV_TERMINATED         = 203,  /* ETERM     */
    ZLINK_RECV_INVALID_HANDLE     = 204,  /* EFAULT    */
    ZLINK_RECV_NOT_SUPPORTED      = 205,  /* ENOTSUP   */
    ZLINK_RECV_INTERNAL_ERROR     = 206   /* internal errno */
} zlink_recv_result_t;
```

### Recv Result Classification

| Result | Internal errno | Meaning |
|---|---|---|
| `OK` | -- | Data was received successfully |
| `NO_DATA` | `EAGAIN` | Non-blocking recv has no data, or the API-specific source has no more data to return (for example a stopped timer with no queued fire count) |
| `BUSY` | `EBUSY` | Handler already attached |
| `TERMINATED` | `ETERM` | Context was terminated |
| `INVALID_HANDLE` | `EFAULT` | NULL or invalid handle |
| `NOT_SUPPORTED` | `ENOTSUP` | Unsupported socket type for recv |
| `INTERNAL_ERROR` | other internal recv failures | Recv failed without a finer public bucket |

### Applicable functions

| Category | Functions |
|---|---|
| Router recv | `zlink_router_recv` |
| SPOT recv | `zlink_spot_recv` |
| Recv | `zlink_recv` |
| Subscribe | `zlink_subscribe` |
| Subscription event | `zlink_xpub_recv_part` |
| Socket monitor recv | `zlink_socket_monitor_recv` |
| Timer recv | `zlink_timer_recv` |

`zlink_spot_actor_join_recv()` returns a borrowed multipart view through
`parts_out_` and `part_count_out_`. NULL output pointers map to
`ZLINK_RECV_INVALID_HANDLE` with `EINVAL`; an invalid Spot handle maps to the
same public result with `EFAULT`.

---

## zlink_handler_result_t

Applies to all handler registration functions: recv handler, subscribe
handler, send-ready handler, router handler, spot handler,
spot dispatch-event handler, monitor handlers, and timer handler.

```c
typedef enum zlink_handler_result_t
{
    ZLINK_HANDLER_OK              = 0,
    ZLINK_HANDLER_INVALID_ARGUMENT = 301, /* EINVAL    */
    ZLINK_HANDLER_BUSY            = 302,  /* EBUSY     */
    ZLINK_HANDLER_NOT_SUPPORTED   = 303,  /* ENOTSUP   */
    ZLINK_HANDLER_DEADLOCK        = 304,  /* EDEADLK   */
    ZLINK_HANDLER_INVALID_HANDLE  = 305,  /* EFAULT    */
    ZLINK_HANDLER_INTERNAL_ERROR  = 306   /* internal errno */
} zlink_handler_result_t;
```

### Handler Result Classification

| Result | Internal errno | Meaning |
|---|---|---|
| `OK` | -- | Handler registered successfully |
| `INVALID_ARGUMENT` | `EINVAL` | NULL handler |
| `BUSY` | `EBUSY` | Handler already attached |
| `NOT_SUPPORTED` | `ENOTSUP` | Unsupported subject |
| `DEADLOCK` | `EDEADLK` | Reentrant call (send-ready handler only) |
| `INVALID_HANDLE` | `EFAULT` | NULL or invalid handle |
| `INTERNAL_ERROR` | other internal handler failures | Handler registration failed without a finer public bucket |

### Applicable functions

| Category | Functions |
|---|---|
| Recv handler (STREAM only) | `zlink_recv_handler` |
| Stream packet handler | `zlink_stream_packet_handler` |
| Send-ready handler | `zlink_send_ready_handler` |
| Spot dispatch-event handler | `zlink_spot_dispatch_event_handler` |
| Socket monitor handler | `zlink_socket_monitor_handler` |
| Timer handler | `zlink_timer_handler` |

---

## zlink_close_result_t

Applies to close and destroy functions.

```c
typedef enum zlink_close_result_t
{
    ZLINK_CLOSE_OK                = 0,
    ZLINK_CLOSE_BUSY              = 401,  /* EBUSY     */
    ZLINK_CLOSE_SHUTDOWN          = 402,  /* ESHUTDOWN */
    ZLINK_CLOSE_INVALID_HANDLE    = 403,  /* EFAULT    */
    ZLINK_CLOSE_INTERNAL_ERROR    = 404   /* internal errno */
} zlink_close_result_t;
```

### Close Result Classification

| Result | Internal errno | Meaning |
|---|---|---|
| `OK` | -- | Close/destroy succeeded |
| `BUSY` | `EBUSY` | In-flight callback or API call |
| `SHUTDOWN` | `ESHUTDOWN` | Already closed |
| `INVALID_HANDLE` | `EFAULT` | NULL or invalid handle |
| `INTERNAL_ERROR` | other internal close failures | Close failed without a finer public bucket |

### Applicable functions

| Category | Functions |
|---|---|
| Context close | `zlink_ctx_term`, `zlink_ctx_shutdown` |
| Socket close | `zlink_close` |
| Monitor close | `zlink_monitor_close` |
| Service destroy | `zlink_registry_destroy`, `zlink_discovery_destroy`, `zlink_spot_destroy`, `zlink_spot_node_destroy`, `zlink_registry_query_client_destroy` |
| Utility destroy | `zlink_poller_destroy`, `zlink_timer_destroy` |

---

## zlink_bind_result_t

Applies to the bind function.

```c
typedef enum zlink_bind_result_t
{
    ZLINK_BIND_OK                 = 0,
    ZLINK_BIND_INVALID_ARGUMENT   = 501,  /* EINVAL    */
    ZLINK_BIND_ADDR_IN_USE        = 502,  /* EADDRINUSE */
    ZLINK_BIND_NOT_SUPPORTED      = 503,  /* ENOTSUP   */
    ZLINK_BIND_INVALID_HANDLE     = 504,  /* EFAULT    */
    ZLINK_BIND_INTERNAL_ERROR     = 505   /* internal errno */
} zlink_bind_result_t;
```

### Bind Result Classification

| Result | Internal errno | Meaning |
|---|---|---|
| `OK` | -- | Bind succeeded |
| `INVALID_ARGUMENT` | `EINVAL` | Invalid endpoint |
| `ADDR_IN_USE` | `EADDRINUSE` | Address already bound |
| `NOT_SUPPORTED` | `ENOTSUP` | Unsupported transport |
| `INVALID_HANDLE` | `EFAULT` | NULL or invalid handle |
| `INTERNAL_ERROR` | other internal bind failures | Bind failed without a finer public bucket |

### Applicable functions

| Category | Functions |
|---|---|
| Bind | `zlink_bind`, `zlink_registry_bind` |

---

## zlink_connect_result_t

Applies to connect, disconnect, and unbind functions.

```c
typedef enum zlink_connect_result_t
{
    ZLINK_CONNECT_OK              = 0,
    ZLINK_CONNECT_INVALID_ARGUMENT = 601, /* EINVAL    */
    ZLINK_CONNECT_NOT_SUPPORTED   = 602,  /* ENOTSUP   */
    ZLINK_CONNECT_INVALID_HANDLE  = 603,  /* EFAULT    */
    ZLINK_CONNECT_INTERNAL_ERROR  = 604,  /* internal errno */
    ZLINK_CONNECT_NOT_FOUND       = 605,  /* ENOENT    */
    ZLINK_CONNECT_CONFLICT        = 606,  /* EADDRINUSE */
    ZLINK_CONNECT_BUSY            = 607   /* EBUSY     */
} zlink_connect_result_t;
```

### Connect Result Classification

| Result | Internal errno | Meaning |
|---|---|---|
| `OK` | -- | Connect/disconnect/unbind succeeded |
| `INVALID_ARGUMENT` | `EINVAL` | Invalid endpoint |
| `NOT_SUPPORTED` | `ENOTSUP` | Unsupported transport |
| `INVALID_HANDLE` | `EFAULT` | NULL or invalid handle |
| `NOT_FOUND` | `ENOENT` | Endpoint or peer routing id was not found |
| `CONFLICT` | `EADDRINUSE` | More than one peer has the same routing id, so the target is ambiguous |
| `BUSY` | `EBUSY` | Lifecycle owner, such as Discovery attachment, rejected the manual change |
| `INTERNAL_ERROR` | other internal connect failures | Connect/disconnect/unbind failed without a finer public bucket |

### Applicable functions

| Category | Functions |
|---|---|
| Connect | `zlink_connect`, `zlink_spot_node_connect_peer` |
| Disconnect | `zlink_disconnect`, `zlink_disconnect_rid`, `zlink_spot_node_disconnect_peer`, `zlink_spot_node_disconnect_peer_rid` |
| Unbind | `zlink_unbind` |

---

## zlink_config_result_t

Applies to set/get option, configuration, snapshot, message, poller mutation,
and proxy functions.

```c
typedef enum zlink_config_result_t
{
    ZLINK_CONFIG_OK               = 0,
    ZLINK_CONFIG_INVALID_HANDLE   = 701,  /* EFAULT    */
    ZLINK_CONFIG_INVALID_ARGUMENT = 702,  /* EINVAL    */
    ZLINK_CONFIG_NOT_SUPPORTED    = 703,  /* ENOTSUP   */
    ZLINK_CONFIG_INTERNAL_ERROR   = 704,  /* internal errno */
    ZLINK_CONFIG_INVALID_STATE    = 705,  /* EBUSY/ESHUTDOWN — lifecycle state rejects config */
    ZLINK_CONFIG_NOT_FOUND        = 706   /* ENOENT    — local lookup target not found */
} zlink_config_result_t;
```

### Config Result Classification

| Result | Internal errno | Meaning |
|---|---|---|
| `OK` | -- | Configuration succeeded |
| `INVALID_HANDLE` | `EFAULT` | NULL or invalid handle |
| `INVALID_ARGUMENT` | `EINVAL` | Invalid option, output pointer, or parameter shape |
| `NOT_SUPPORTED` | `ENOTSUP` | Unsupported option |
| `INTERNAL_ERROR` | other internal config failures | Configuration failed without a finer public bucket |
| `INVALID_STATE` | `EBUSY`, `ESHUTDOWN` | Handle lifecycle state rejects the config call (e.g. already started, already closed) |
| `NOT_FOUND` | `ENOENT` | Local lookup target (e.g. Spot rid, actor id) not found |

`zlink_stream_attach_actor_gateway()` returns `ZLINK_CONFIG_NOT_SUPPORTED` with
`ENOTSUP` when the target node is not routed-capable. It returns
`ZLINK_CONFIG_INVALID_STATE` with `EBUSY` when the stream is already attached to
a different ActorGateway owner.

### Actor/Spot Route Lookup Errors

`zlink_discovery_resolve_actor()` returns `ZLINK_CONFIG_INVALID_ARGUMENT` and
`EINVAL` when the Actor id is missing, empty, too long, or the output pointer is
missing. It returns `ZLINK_CONFIG_NOT_FOUND` and `ENOENT` when the Actor route
row is missing, the row value is not exactly `sizeof(zlink_actor_route_t)`, the
route key does not match `value.actor.actor_id`, or the current Spot rid/kind is
not valid.

`zlink_discovery_resolve_spot()` returns `ZLINK_CONFIG_INVALID_ARGUMENT` and
`EINVAL` when `discovery`, `spot_rid`, or the output pointer is missing, or when
`spot_rid` is empty. It returns `ZLINK_CONFIG_NOT_FOUND` and `ENOENT` when the
Spot owner topology row is missing, the owner node rid is empty, or `spot_kind`
is not Entry or user.

A send/request failure after a successful lookup is not a route lookup error.
It follows the existing routed send/request meanings such as not-connected,
not-found, timeout, and backpressure.

The default values for socket `ZLINK_OPT_SNDTIMEO` and `ZLINK_OPT_RCVTIMEO` are
`1000` ms. Send/receive paths that do not explicitly set `-1` no longer wait
forever by default.

### Applicable functions

| Category | Functions |
|---|---|
| Context | `zlink_ctx_set`, `zlink_ctx_auto_hwm_recalculate` |
| Message lifecycle | `zlink_msg_init`, `zlink_msg_init_size`, `zlink_msg_init_data`, `zlink_msg_close`, `zlink_msg_move`, `zlink_msg_copy`, `zlink_msg_adopt` |
| Socket option | `zlink_set_option`, `zlink_get_option`, `zlink_set_routing_id`, `zlink_get_routing_id`, `zlink_set_tls_server`, `zlink_set_tls_client`, `zlink_set_router_option`, `zlink_get_router_option`, `zlink_set_dealer_option`, `zlink_set_stream_option`, `zlink_get_stream_option`, `zlink_set_spot_option`, `zlink_get_spot_option`, `zlink_set_pub_option`, `zlink_get_pub_option`, `zlink_set_sub_option`, `zlink_get_sub_option`, `zlink_set_spot_node_option`, `zlink_get_spot_node_option`, `zlink_socket_set_channel_name`, `zlink_socket_get_channel_name` |
| Subscription | `zlink_set_subscription`, `zlink_unset_subscription`, `zlink_subscription_at` |
| Service attach | `zlink_socket_attach_discovery`, `zlink_spot_node_attach_discovery`, `zlink_spot_node_attach_channel_dealer`, `zlink_spot_node_attach_channel_dealer_manual`, `zlink_spot_node_attach_pub_ingress` |
| SpotNode lifecycle/lookup/bind | `zlink_spot_node_entry_spot`, `zlink_spot_node_spot_lookup`, `zlink_spot_node_actor_new`, `zlink_spot_node_actor_lookup`, `zlink_remote_actor_get_ref` |
| Registry config | `zlink_registry_set_id`, `zlink_registry_add_peer`, `zlink_registry_set`, `zlink_registry_get` |
| Discovery config | `zlink_discovery_connect_registry`, `zlink_discovery_resolve_spot`, `zlink_discovery_resolve_actor`, `zlink_discovery_set_value`, `zlink_discovery_get_value`, `zlink_discovery_member_peers` |
| Snapshot/query | `zlink_spot_node_status`, `zlink_spot_node_peers`, `zlink_spot_node_peers`, `zlink_spot_node_subjects`, `zlink_spot_node_internal_sockets`, `zlink_spot_node_spots`, `zlink_spot_node_actors`, `zlink_spot_actors`, `zlink_registry_status`, `zlink_registry_service_summary`, `zlink_registry_member_peers`, `zlink_registry_topology`, `zlink_registry_topology`, `zlink_registry_query_client_topology`, `zlink_monitor_status` |
| Poller config | `zlink_poller_add`, `zlink_poller_modify`, `zlink_poller_remove`, `zlink_poller_add_fd`, `zlink_poller_add_timer`, `zlink_poller_modify_fd`, `zlink_poller_remove_fd`, `zlink_poller_remove_timer` |
| Proxy | `zlink_proxy`, `zlink_proxy_steerable` |
| Timer config | `zlink_timer_start`, `zlink_timer_stop` |

`zlink_poll`, `zlink_poller_size`, and `zlink_poller_wait` remain plain
`int` APIs with `error_out_`. They do
not directly return `zlink_config_result_t`.

`zlink_registry_set()` returns `NOT_SUPPORTED` for an unknown
`zlink_registry_option_t`, `INVALID_ARGUMENT` when the scalar value is `0`,
and `INVALID_HANDLE` for a NULL or invalid Registry handle. `zlink_registry_get()`
returns the option value on success. On failure it returns `0`; when
`error_out_` is not NULL it also stores the normalized `zlink_config_result_t`.

---

## Submit Matrix

`Y` = the submit path can produce this normalized result.
`--` = the submit path never produces this normalized result.

### Send functions

| Result | `zlink_send` | `zlink_send_rid` | `zlink_publish` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | -- | Y | -- |
| `NOT_FOUND` | -- | -- | -- |
| `NOT_ADMITTED` | Y | Y | -- |
| `TERMINATED` | Y | Y | Y |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | Y | Y | Y |
| `THREAD_VIOLATION` | Y | Y | Y |
| `OUT_OF_MEMORY` | -- | -- | -- |
| `SEQ_EXHAUSTED` | -- | -- | -- |
| `INTERNAL_ERROR` | -- | -- | -- |

`zlink_send` returns `NOT_ADMITTED` when every ROUTER known to the DEALER has weight `0`. `zlink_send_rid` returns `NOT_ADMITTED` when the target RID has weight `0`.

### Socket request functions

| Result | `zlink_dealer_request` | `zlink_router_request` |
|---|---|---|
| `OK` | Y | Y |
| `BACKPRESSURED` | Y | Y |
| `NOT_CONNECTED` | -- | -- |
| `NOT_FOUND` | -- | -- |
| `NOT_ADMITTED` | Y | Y |
| `TERMINATED` | Y | Y |
| `INVALID_HANDLE` | Y | Y |
| `INVALID_ARGUMENT` | Y | Y |
| `NOT_SUPPORTED` | Y | Y |
| `INVALID_STATE` | Y | Y |
| `THREAD_VIOLATION` | Y | Y |
| `OUT_OF_MEMORY` | Y | Y |
| `SEQ_EXHAUSTED` | Y | Y |
| `INTERNAL_ERROR` | Y | Y |

`zlink_dealer_request` fails with `NOT_ADMITTED` when every known ROUTER has weight `0`. `zlink_router_request` fails with `NOT_ADMITTED` when the target
RID has weight `0`.

### Socket reply function

| Result | `zlink_router_reply` |
|---|---|
| `OK` | Y |
| `BACKPRESSURED` | Y |
| `NOT_CONNECTED` | -- |
| `NOT_FOUND` | -- |
| `NOT_ADMITTED` | -- |
| `TERMINATED` | Y |
| `INVALID_HANDLE` | Y |
| `INVALID_ARGUMENT` | Y |
| `NOT_SUPPORTED` | Y |
| `INVALID_STATE` | Y |
| `THREAD_VIOLATION` | Y |
| `OUT_OF_MEMORY` | -- |
| `SEQ_EXHAUSTED` | -- |
| `INTERNAL_ERROR` | Y |

Replies answer in-flight requests, so admission is not re-evaluated for them.
`zlink_router_reply` therefore never returns `NOT_ADMITTED`.

### SPOT request functions

| Result | `zlink_spot_request_channel` | `zlink_router_request_spot` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | Y | Y | Y |
| `NOT_FOUND` | Y | Y | Y |
| `NOT_ADMITTED` | Y | Y | Y |
| `TERMINATED` | -- | Y | -- |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | -- | -- | Y |
| `THREAD_VIOLATION` | -- | -- | Y |
| `OUT_OF_MEMORY` | Y | Y | Y |
| `SEQ_EXHAUSTED` | Y | Y | Y |
| `INTERNAL_ERROR` | Y | Y | Y |

SPOT request paths return `NOT_ADMITTED` when the destination SpotNode or
ROUTER has weight `0`. `zlink_spot_request_channel` may also fail with the same
class of error when the attached channel path is draining or no callable dealer
path remains.

### SPOT send functions

| Result | `zlink_spot_send_channel` | `zlink_spot_publish` | `zlink_router_send_spot` |
|---|---|---|---|---|
| `OK` | Y | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y | Y |
| `NOT_CONNECTED` | Y | Y | Y | Y |
| `NOT_FOUND` | Y | Y | Y | Y |
| `NOT_ADMITTED` | Y | Y | -- | Y |
| `TERMINATED` | -- | Y | Y | -- |
| `INVALID_HANDLE` | Y | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y | Y |
| `INVALID_STATE` | -- | -- | -- | Y |
| `THREAD_VIOLATION` | -- | -- | -- | Y |
| `OUT_OF_MEMORY` | -- | -- | -- | -- |
| `SEQ_EXHAUSTED` | -- | -- | -- | -- |
| `INTERNAL_ERROR` | Y | Y | Y | Y |

`zlink_spot_publish` is fan-out, so a single peer's weight never fails it.
The other routed/direct send functions return `NOT_ADMITTED` when the
destination peer's remote weight cache shows `0`.

### SPOT reply functions

| Result | `zlink_spot_reply_spot` | `zlink_spot_reply_router` | `zlink_router_reply_spot` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | Y | Y | Y |
| `NOT_FOUND` | Y | Y | Y |
| `NOT_ADMITTED` | -- | -- | -- |
| `TERMINATED` | -- | -- | -- |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | -- | -- | Y |
| `THREAD_VIOLATION` | -- | -- | Y |
| `OUT_OF_MEMORY` | -- | -- | -- |
| `SEQ_EXHAUSTED` | -- | -- | -- |
| `INTERNAL_ERROR` | Y | Y | Y |

SPOT replies answer in-flight requests, so admission is not re-evaluated
for them. None of the SPOT reply functions return `NOT_ADMITTED`.
Actor join submit and join reply use the same multipart payload shape as Actor
create. A mismatched `parts_`/`part_count_` pair maps to
`ZLINK_SUBMIT_INVALID_ARGUMENT` with `EINVAL`; an invalid payload frame maps to
the same public result with `EFAULT`.

---

## All Functions by Result Enum

| Result enum | Functions |
|---|---|
| `zlink_submit_result_t` | `zlink_send_part`, `zlink_send_part_rid`, `zlink_publish_part`, `zlink_dealer_request_part`, `zlink_router_request_part`, `zlink_router_reply_part`, `zlink_spot_request_channel_part`, `zlink_spot_request_spot_part`, `zlink_spot_request_router_part`, `zlink_router_request_spot_part`, `zlink_spot_send_channel_part`, `zlink_spot_send_spot_part`, `zlink_router_send_spot_part`, `zlink_spot_reply_spot_part`, `zlink_spot_reply_router_part`, `zlink_router_reply_spot_part`, `zlink_spot_node_actor_join_spot`, `zlink_spot_node_actor_join_entry_spot`, `zlink_spot_node_actor_leave_spot`, `zlink_spot_node_actor_destroy`, `zlink_spot_actor_join_reply`, `zlink_stream_bind_actor`, `zlink_stream_unbind_actor`, `zlink_stream_send_bound_actor_part`, `zlink_remote_actor_get_ref`, `zlink_spot_node_actor_send_bound_session_msg` |
| `zlink_request_result_t` | `zlink_reply_handler_fn` (completion callback), `zlink_actor_join_spot_handler_fn` (completion callback), `zlink_actor_join_entry_spot_handler_fn` (completion callback), `zlink_actor_lookup_handler_fn` (completion callback), `zlink_spot_node_actor_close_bound_session` |
| `zlink_recv_result_t` | `zlink_router_recv_part`, `zlink_spot_recv_part`, `zlink_recv_part`, `zlink_subscribe_part`, `zlink_xpub_recv_part`, `zlink_spot_subscribe_part`, `zlink_spot_recv_subscription_event`, `zlink_spot_recv_actor_lifecycle`, `zlink_socket_monitor_recv`, `zlink_timer_recv`, `zlink_spot_node_actor_recv_part`, `zlink_spot_actor_join_recv` |
| `zlink_handler_result_t` | `zlink_recv_handler` (raw STREAM only), `zlink_stream_packet_handler`, `zlink_send_ready_handler`, `zlink_spot_dispatch_event_handler`, `zlink_socket_monitor_handler`, `zlink_timer_handler` |
| `zlink_close_result_t` | `zlink_ctx_term`, `zlink_ctx_shutdown`, `zlink_close`, `zlink_monitor_close`, `zlink_registry_destroy`, `zlink_discovery_destroy`, `zlink_spot_destroy`, `zlink_spot_node_destroy`, `zlink_registry_query_client_destroy`, `zlink_poller_destroy`, `zlink_timer_destroy` |
| `zlink_bind_result_t` | `zlink_bind`, `zlink_registry_bind` |
| `zlink_connect_result_t` | `zlink_connect`, `zlink_disconnect`, `zlink_disconnect_rid`, `zlink_unbind`, `zlink_spot_node_connect_peer`, `zlink_spot_node_disconnect_peer`, `zlink_spot_node_disconnect_peer_rid`, `zlink_discovery_connect_registry`, `zlink_registry_query_client_connect` |
| `zlink_config_result_t` | `zlink_ctx_set`, `zlink_ctx_auto_hwm_recalculate`, message lifecycle functions (`zlink_msg_init` family + `zlink_msg_adopt`), all socket option/routing/subscription configuration functions, all attach functions, all SpotNode lifecycle/lookup/bind/bind functions, all registry/discovery configuration functions, all snapshot/query functions, all poller mutation functions, `zlink_proxy`, `zlink_proxy_steerable`, `zlink_timer_start`, `zlink_timer_stop`, `zlink_monitor_status` |
