[English](socket.md) | [한국어](socket.ko.md)

# Socket API Reference

The Socket API provides functions for creating, configuring, binding,
connecting, and performing I/O on zlink sockets. Message receiving supports
two modes: callback dispatch via an attached handler, and synchronous pull
via `zlink_recv()`. Sockets start in recv mode; attaching a handler
transitions to callback mode. Sockets support several messaging patterns
including publish/subscribe, request/reply, and raw stream.

## Thread-Safety Summary

Public socket handle APIs are thread-safe by default. Not every API has the
same cost model, though.

- `send` is a hot-path API and can be called concurrently from multiple threads.
- `bind/connect/disconnect`, subscribe/unsubscribe, option/query, and monitor
  operations are valid runtime control-path calls. Correctness is preserved,
  but execution order may follow internal serialization.
- `close` uses a fail-fast lifecycle gate. If another thread is running an
  admitted API or callback on the same handle, close fails with `EBUSY`. Once
  close is accepted, new API entry fails with `ESHUTDOWN`.
- Only a small set of exceptions remain outside the default allowance:
  init-only configuration, callback-context restrictions on specific
  reentrant APIs, and concurrent sharing of the same `zlink_msg_t` instance.

## Callback Types

### zlink_socket_msg_handler_fn

```c
typedef void (*zlink_socket_msg_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);
```

Callback for multipart message dispatch on PAIR, DEALER, and ROUTER
sockets. Invoked on the owning I/O thread. Ownership of all message parts is
transferred to the callback; each part must be closed or consumed exactly once.
Used with `zlink_recv_handler()`.

### zlink_subscribe_handler_fn

```c
typedef void (*zlink_subscribe_handler_fn) (const zlink_routing_id_t *source_rid_,
                                       const char *topic_,
                                       size_t topic_len_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_,
                                       void *userdata_);
```

Callback for topic-based message dispatch on SUB and XSUB sockets.
Invoked on the owning I/O thread. Ownership of parts is transferred.
Used with `zlink_subscribe_handler()`.

### zlink_subscription_event_handler_fn

```c
typedef void (*zlink_subscription_event_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  int subscribed_,
  const char *topic_,
  size_t topic_len_,
  void *userdata_);
```

Callback for subscription notifications on XPUB sockets. `source_rid_`
identifies the subscribing peer. `subscribed_` is non-zero for subscribe,
zero for unsubscribe.
Used with `zlink_subscription_event_handler()`.

Each callback type is registered through a dedicated function. The mapping
of socket types to registration functions:

| Socket Type | Registration Function | Callback |
|---|---|---|
| PAIR, DEALER, ROUTER | `zlink_recv_handler` | `zlink_socket_msg_handler_fn` |
| SUB, XSUB | `zlink_subscribe_handler` | `zlink_subscribe_handler_fn` |
| XPUB | `zlink_subscription_event_handler` | `zlink_subscription_event_handler_fn` |
| PUB | N/A | Send-only; no handler needed |

### zlink_send_ready_handler_fn

```c
typedef void (*zlink_send_ready_handler_fn) (void *subject_, void *userdata_);
```

Callback invoked when a send-capable handle transitions to writable.

## Constants

### Socket Types

```c
typedef enum zlink_socket_type_t
{
    ZLINK_SOCKET_PAIR   = 0x1001,
    ZLINK_SOCKET_PUB    = 0x1002,
    ZLINK_SOCKET_SUB    = 0x1003,
    ZLINK_SOCKET_DEALER = 0x1004,
    ZLINK_SOCKET_ROUTER = 0x1005,
    ZLINK_SOCKET_XPUB   = 0x1006,
    ZLINK_SOCKET_XSUB   = 0x1007,
    ZLINK_SOCKET_STREAM = 0x1008
} zlink_socket_type_t;
```

Short-form aliases are also available: `ZLINK_PAIR`, `ZLINK_PUB`, `ZLINK_SUB`,
`ZLINK_DEALER`, `ZLINK_ROUTER`, `ZLINK_XPUB`, `ZLINK_XSUB`, `ZLINK_STREAM`.

### Send Flags

```c
typedef uint32_t zlink_send_flags_t;

#define ZLINK_DONTWAIT  ((zlink_send_flags_t) 0x0001u)
```

| Constant | Description |
|---|---|
| `ZLINK_DONTWAIT` | Non-blocking operation; return immediately with `EAGAIN` if the operation would block |

### Security Mechanisms

| Constant | Value | Description |
|---|---|---|
| `ZLINK_NULL` | 0 | No security mechanism (default) |
| `ZLINK_PLAIN` | 1 | PLAIN username/password authentication |

### Socket Options

Socket options are configured with `zlink_setsockopt()` and queried with
`zlink_getsockopt()`. Options use the `zlink_socket_option_t` enum.
Short-form aliases (e.g. `ZLINK_LINGER`) are also available.

#### General

| Constant | Description |
|---|---|
| `ZLINK_SOCKOPT_AFFINITY` | I/O thread affinity bitmask (`uint64_t`) |
| `ZLINK_SOCKOPT_ROUTING_ID` | Socket identity for ROUTER addressing (`binary`, max 255 bytes) |
| `ZLINK_SOCKOPT_TYPE` | Socket type (read-only, `int`) |
| `ZLINK_SOCKOPT_LINGER` | Linger period for socket shutdown in milliseconds (`int`; -1 = infinite, 0 = discard immediately) |
| `ZLINK_SOCKOPT_BACKLOG` | Maximum length of the pending connections queue (`int`) |
| `ZLINK_SOCKOPT_LAST_ENDPOINT` | Last endpoint bound (read-only, `string`) |
| `ZLINK_SOCKOPT_FD` | File descriptor for integration with external event loops (read-only, `zlink_fd_t`) |
| `ZLINK_SOCKOPT_EVENTS` | Event state bitmask (read-only, `int`) |

#### High Water Mark

| Constant | Description |
|---|---|
| `ZLINK_SOCKOPT_SNDHWM` | Send high water mark; max messages queued for sending (`int`; 0 = unlimited) |
| `ZLINK_SOCKOPT_RCVHWM` | Receive high water mark; max messages queued for receiving (`int`; 0 = unlimited) |
| `ZLINK_SOCKOPT_MAXMSGSIZE` | Maximum inbound message size in bytes (`int64_t`; -1 = unlimited) |

#### Buffers

| Constant | Description |
|---|---|
| `ZLINK_SOCKOPT_SNDBUF` | Kernel transmit buffer size in bytes (`int`; 0 = OS default) |
| `ZLINK_SOCKOPT_RCVBUF` | Kernel receive buffer size in bytes (`int`; 0 = OS default) |

#### Timing

| Constant | Description |
|---|---|
| `ZLINK_SOCKOPT_SNDTIMEO` | Send timeout in milliseconds (`int`; -1 = infinite) |
| `ZLINK_SOCKOPT_RCVTIMEO` | Receive timeout in milliseconds (`int`; -1 = infinite) |
| `ZLINK_SOCKOPT_RECONNECT_IVL` | Initial reconnection interval in milliseconds (`int`) |
| `ZLINK_SOCKOPT_RECONNECT_IVL_MAX` | Maximum reconnection interval in milliseconds (`int`; 0 = use `RECONNECT_IVL` only) |
| `ZLINK_SOCKOPT_CONNECT_TIMEOUT` | Connection timeout in milliseconds (`int`) |
| `ZLINK_SOCKOPT_TCP_MAXRT` | Maximum TCP retransmit timeout in milliseconds (`int`) |
| `ZLINK_SOCKOPT_HANDSHAKE_IVL` | ZMTP handshake timeout in milliseconds (`int`) |

#### TCP

| Constant | Description |
|---|---|
| `ZLINK_SOCKOPT_TCP_KEEPALIVE` | Override SO_KEEPALIVE (`int`; -1 = OS default, 0 = off, 1 = on) |
| `ZLINK_SOCKOPT_TCP_KEEPALIVE_CNT` | Override TCP_KEEPCNT (`int`; -1 = OS default) |
| `ZLINK_SOCKOPT_TCP_KEEPALIVE_IDLE` | Override TCP_KEEPIDLE in seconds (`int`; -1 = OS default) |
| `ZLINK_SOCKOPT_TCP_KEEPALIVE_INTVL` | Override TCP_KEEPINTVL in seconds (`int`; -1 = OS default) |
| `ZLINK_SOCKOPT_TCP_NODELAY` | Enable TCP_NODELAY (`int`; 0 or 1) |

#### Pub/Sub

| Constant | Description |
|---|---|
| `ZLINK_SOCKOPT_SUBSCRIBE` | Subscribe to a topic prefix (`binary`) |
| `ZLINK_SOCKOPT_UNSUBSCRIBE` | Unsubscribe from a topic prefix (`binary`) |
| `ZLINK_SOCKOPT_XPUB_VERBOSE` | Pass all subscription messages upstream (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_XPUB_NODROP` | Do not silently drop messages on HWM; return `EAGAIN` instead (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_XPUB_MANUAL` | Enable manual subscription management on XPUB (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_XPUB_WELCOME_MSG` | Message sent to new subscribers on connect (`binary`) |
| `ZLINK_SOCKOPT_XPUB_VERBOSER` | Pass all subscribe and unsubscribe messages upstream (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_XPUB_MANUAL_LAST_VALUE` | Enable last-value caching in manual XPUB mode (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_INVERT_MATCHING` | Invert topic matching (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_CONFLATE` | Keep only the most recent message per topic (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_ONLY_FIRST_SUBSCRIBE` | Only process the first subscription per topic prefix (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_TOPICS_COUNT` | Number of subscribed topics (read-only, `int`) |

#### Router

| Constant | Description |
|---|---|
| `ZLINK_SOCKOPT_ROUTER_MANDATORY` | Return `EHOSTUNREACH` when routing to an unconnected peer (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_ROUTER_HANDOVER` | Allow new connection to take over an existing routing identity (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_PROBE_ROUTER` | Send an empty message on connect to establish identity at the ROUTER peer (`int`; 0 or 1) |

#### Heartbeat

| Constant | Description |
|---|---|
| `ZLINK_SOCKOPT_HEARTBEAT_IVL` | ZMTP heartbeat interval in milliseconds (`int`; 0 = disabled) |
| `ZLINK_SOCKOPT_HEARTBEAT_TTL` | ZMTP heartbeat time-to-live in milliseconds (`int`) |
| `ZLINK_SOCKOPT_HEARTBEAT_TIMEOUT` | ZMTP heartbeat timeout in milliseconds (`int`) |

#### TLS

| Constant | Description |
|---|---|
| `ZLINK_SOCKOPT_TLS_CERT` | Path to PEM-encoded TLS certificate (`string`) |
| `ZLINK_SOCKOPT_TLS_KEY` | Path to PEM-encoded TLS private key (`string`) |
| `ZLINK_SOCKOPT_TLS_CA` | Path to PEM-encoded CA certificate bundle (`string`) |
| `ZLINK_SOCKOPT_TLS_VERIFY` | Enable TLS peer certificate verification (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_TLS_REQUIRE_CLIENT_CERT` | Require TLS client certificate on server sockets (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_TLS_HOSTNAME` | Expected hostname for TLS SNI and certificate verification (`string`) |
| `ZLINK_SOCKOPT_TLS_TRUST_SYSTEM` | Trust the system CA certificate store (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_TLS_PASSWORD` | Password for encrypted TLS private key (`string`) |

#### Other

| Constant | Description |
|---|---|
| `ZLINK_SOCKOPT_IPV6` | Enable IPv6 on the socket (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_IMMEDIATE` | Queue messages only to completed connections (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_BLOCKY` | Legacy option: block on context termination (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_BINDTODEVICE` | Bind socket to a specific network interface (`string`) |
| `ZLINK_SOCKOPT_CONNECT_ROUTING_ID` | Set routing identity for the next outgoing connection (`binary`) |
| `ZLINK_SOCKOPT_STREAM_NOTIFY` | Enable STREAM connect/disconnect notifications (`int`; 0 or 1) |
| `ZLINK_SOCKOPT_RATE` | Multicast data rate in kbps (`int`) |
| `ZLINK_SOCKOPT_RECOVERY_IVL` | Multicast recovery interval in milliseconds (`int`) |
| `ZLINK_SOCKOPT_MULTICAST_HOPS` | Maximum multicast hops (TTL) (`int`) |
| `ZLINK_SOCKOPT_TOS` | IP Type-of-Service value (`int`) |
| `ZLINK_SOCKOPT_MULTICAST_MAXTPDU` | Maximum multicast transport data unit size in bytes (`int`) |
| `ZLINK_SOCKOPT_ZMP_METADATA` | Attach ZMP metadata properties to outgoing connections (`binary`) |

## Functions

### zlink_socket

Create a socket.

```c
void *zlink_socket (void *context_, zlink_socket_type_t type_);
```

Creates a new socket within the given context. The `type_` parameter selects
the messaging pattern. The socket starts in recv mode. Use
`zlink_recv_handler()` to transition to callback mode. The socket
must be closed with `zlink_close()` before the context is terminated.

**Returns:** Socket handle on success, `NULL` on failure (errno is set).

**Errors:** `EINVAL` if the socket type is invalid. `EMFILE` if the maximum
number of sockets has been reached. `ETERM` if the context was terminated.

**Thread safety:** Thread-safe with respect to the context.

**See also:** `zlink_close`, `zlink_ctx_new`, `zlink_recv_handler`

---

### zlink_recv_handler

Attach a message receive handler to a socket.

```c
int zlink_recv_handler (void *s_,
                        zlink_socket_msg_handler_fn handler_,
                        void *userdata_);
```

Attach a message receive handler to a PAIR, DEALER, or ROUTER
socket. Sockets start in recv mode. This call transitions the handle to
callback mode and cannot be undone for the lifetime of the socket. A second
attach on the same handle fails with `errno=EBUSY`.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EINVAL` if the handler is NULL or the socket type does not
accept a message handler. `EBUSY` if a handler is already attached.

**See also:** `zlink_subscribe_handler`, `zlink_subscription_event_handler`,
`zlink_socket`, `zlink_close`

---

### zlink_subscribe_handler

Attach a topic-based receive handler to a socket.

```c
int zlink_subscribe_handler (void *s_,
                             zlink_subscribe_handler_fn handler_,
                             void *userdata_);
```

Attach a topic-based receive handler to a SUB or XSUB socket. Sockets
start in recv mode. This call transitions the handle to callback mode and
cannot be undone for the lifetime of the socket. A second attach on the
same handle fails with `errno=EBUSY`.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EINVAL` if the handler is NULL or the socket type does not
accept a spot handler. `EBUSY` if a handler is already attached.

**See also:** `zlink_recv_handler`, `zlink_subscription_event_handler`,
`zlink_socket`, `zlink_close`

---

### zlink_subscription_event_handler

Attach a subscription notification handler to an XPUB socket.

```c
int zlink_subscription_event_handler (void *s_,
                             zlink_subscription_event_handler_fn handler_,
                             void *userdata_);
```

Attach a subscription notification handler to an XPUB socket. The callback
receives a `source_rid_` identifying the subscribing peer, a `subscribed_`
flag, and the topic bytes. Sockets start in recv mode. This call transitions
the handle to callback mode and cannot be undone for the lifetime of the
socket. A second attach on the same handle fails with `errno=EBUSY`.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EINVAL` if the handler is NULL or the socket is not XPUB.
`EBUSY` if a handler is already attached.

**See also:** `zlink_recv_handler`, `zlink_subscribe_handler`,
`zlink_socket`, `zlink_close`

---

### zlink_close

Close a socket and release its resources.

```c
int zlink_close (void *s_);
```

Closes the socket and releases all associated resources. Any outstanding
messages in the send queue are discarded or sent depending on the
`ZLINK_LINGER` setting. Public handles follow a tiered contract: hot-path send
operations can be called concurrently from multiple threads, low-frequency control paths
serialize for correctness, and close/destroy uses a stricter lifecycle gate.
If another thread has an in-flight callback or admitted API on the same
handle, close fails with `errno=EBUSY`. After close is accepted, new API entry
fails with `errno=ESHUTDOWN`. Self-close from a send-ready or monitor callback
is deferred until callback epilogue.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `ENOTSOCK` if the handle is not a valid socket. `EBUSY` if a
callback or operation is in-flight on the handle.

**See also:** `zlink_socket`

---

### zlink_setsockopt

Set a socket option.

```c
int zlink_setsockopt (void *s_,
                      zlink_socket_option_t option_,
                      const void *optval_,
                      size_t optvallen_);
```

Configures a socket option. The `option_` parameter identifies the option
(e.g. `ZLINK_SNDHWM`, `ZLINK_LINGER`, `ZLINK_SUBSCRIBE`). The `optval_`
pointer supplies the value and `optvallen_` specifies its size in bytes.

Some options must be set before binding or connecting the socket.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EINVAL` if the option is unknown or the value is out of range.
`ETERM` if the context was terminated.

**See also:** `zlink_getsockopt`

---

### zlink_getsockopt

Get a socket option.

```c
int zlink_getsockopt (void *s_,
                      zlink_socket_option_t option_,
                      void *optval_,
                      size_t *optvallen_);
```

Retrieves the current value of a socket option.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_setsockopt`

---

### zlink_bind

Bind a socket to an address.

```c
int zlink_bind (void *s_, const char *addr_);
```

Binds the socket to a local endpoint. The endpoint string uses the format
`transport://address`, where supported transports include:

- `tcp://interface:port` or `tcp://*:port`
- `inproc://name` (in-process)
- `ipc://pathname` (inter-process, POSIX only)
- `ws://interface:port` (WebSocket)
- `tls://interface:port` (TLS-encrypted TCP)

A socket can be bound to multiple endpoints. For TCP, if port 0 is specified
the system assigns an ephemeral port; use `ZLINK_LAST_ENDPOINT` to retrieve
the actual endpoint.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EADDRINUSE` if the address is already in use. `EADDRNOTAVAIL` if
the interface does not exist. `EPROTONOSUPPORT` if the transport is not
supported.

**See also:** `zlink_connect`, `zlink_unbind`

---

### zlink_connect

Connect a socket to a remote address.

```c
int zlink_connect (void *s_, const char *addr_);
```

Connects the socket to a remote endpoint. The endpoint format is the same as
for `zlink_bind()`. A socket can connect to multiple endpoints, and the
library handles reconnection automatically if the peer becomes unavailable.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_bind`, `zlink_disconnect`

---

### zlink_unbind

Unbind a socket from an address.

```c
int zlink_unbind (void *s_, const char *addr_);
```

Removes a previously established binding.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_bind`

---

### zlink_disconnect

Disconnect a socket from a remote address.

```c
int zlink_disconnect (void *s_, const char *addr_);
```

Removes a previously established connection.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_connect`

---

### zlink_send

Send a multipart message on a socket.

```c
int zlink_send (void *s_,
                zlink_msg_t *parts_,
                size_t part_count_,
                zlink_send_flags_t flags_);
```

Sends a multipart message consisting of `part_count_` parts from the
`parts_` array on socket `s_`. On success, ownership of every part in the
array is transferred to the library and the caller must not access them
afterwards. On failure, ownership remains with the caller. The `flags_`
parameter may be 0 or `ZLINK_DONTWAIT`.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EAGAIN` if the operation would block and `ZLINK_DONTWAIT` was set.
`ETERM` if the context was terminated.

**See also:** `zlink_recv`

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
set, or if `ZLINK_RCVTIMEO` expired. `EBUSY` if a receive handler is
attached. `ETERM` if the context was terminated.

**See also:** `zlink_send`, `zlink_recv_handler`, `zlink_multipart_close`

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
send), Gateway (directed request/reply).

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EFAULT` if `s_` is NULL. `EAGAIN` if the operation would block
and `ZLINK_DONTWAIT` was set. `EHOSTUNREACH` if the target peer is not
connected (ROUTER with `ROUTER_MANDATORY`). `ETERM` if the context was
terminated.

**See also:** `zlink_send`, `zlink_recv`

---

## Pub/Sub Data-Plane API

The following functions provide the canonical pub/sub data-plane for raw
PUB, SUB, XSUB, and XPUB sockets. The same functions also apply to
`spot` and `spot_node` service handles (see [spot.md](spot.md)).

### zlink_publish

Publish a multipart message.

```c
int zlink_publish (void *subject_,
                   const char *topic_id_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   zlink_send_flags_t flags_);
```

Publishes a multipart message on the given subject. On success, ownership
of all parts is transferred to the library.

- For `spot` / `spot_node`: `topic_id_` must be non-NULL (topic-bearing
  publish). `EINVAL` if NULL.
- For raw `PUB` / `XPUB`: `topic_id_` must be NULL (raw pub publish).
  Topic matching uses the wire first-frame prefix convention.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EFAULT` if `subject_` is NULL. `EINVAL` if `topic_id_` is
NULL for spot/spot_node, or non-NULL for unsupported types. `ENOTSUP` if
the subject type does not support publish.

**See also:** `zlink_subscribe`, `zlink_subscribe_recv`

---

### zlink_subscribe

Subscribe to a topic filter.

```c
int zlink_subscribe (void *subject_, const char *filter_);
```

Subscribes the subject to messages matching `filter_`. Filter
interpretation: if `filter_` ends with `*`, it is a prefix-match pattern;
otherwise it is an exact topic.

Applicable types: raw SUB, raw XSUB, `spot`, `spot_node`.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EFAULT` if `subject_` is NULL. `EINVAL` if `filter_` is NULL,
empty, or contains invalid pattern syntax (multiple `*` or mid-string `*`).
`ENOTSUP` if the subject type does not support subscribe.

**See also:** `zlink_unsubscribe`, `zlink_subscribe_recv`

---

### zlink_unsubscribe

Unsubscribe from a topic filter.

```c
int zlink_unsubscribe (void *subject_, const char *filter_);
```

Removes a previously registered subscription. The same string
interpretation rules as `zlink_subscribe()` apply: trailing `*` means
pattern unsubscribe, otherwise exact topic unsubscribe.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EFAULT` if `subject_` is NULL. `EINVAL` if `filter_` is NULL
or empty. `ENOTSUP` if the subject type does not support unsubscribe.

**See also:** `zlink_subscribe`

---

### zlink_subscribe_recv

Receive a topic-bearing multipart message.

```c
int zlink_subscribe_recv (void *subject_,
                          zlink_routing_id_t *source_rid_out_,
                          zlink_msg_t **parts_out_,
                          size_t *part_count_out_,
                          char *topic_id_out_,
                          size_t *topic_id_len_out_,
                          zlink_send_flags_t flags_);
```

Receives the next topic-bearing message in recv mode. On success,
`*source_rid_out_` is set to the sender's routing id (zeroed if the
underlying transport does not carry identity), `*topic_id_out_` /
`*topic_id_len_out_` receive the topic bytes (binary-safe), and
`*parts_out_` / `*part_count_out_` receive the payload frames. Ownership
of the parts array is transferred to the caller.

The subject must be in recv mode (no handler attached). If a subscribe
handler has been attached, this call fails with `EBUSY`.

Applicable types: raw SUB, raw XSUB, `spot`, `spot_node`.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EFAULT` if `subject_` is NULL. `EAGAIN` if `ZLINK_DONTWAIT`
was set and no message is available. `EBUSY` if a subscribe handler is
attached. `EMSGSIZE` if the topic buffer is too small. `ENOTSUP` if the
subject type does not support subscribe recv.

**See also:** `zlink_subscribe_handler`, `zlink_subscribe`

---

### zlink_subscription_event_recv

Receive a subscription event from an XPUB socket.

```c
int zlink_subscription_event_recv (void *subject_,
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
contract as `zlink_subscribe_recv()`).

The subject must be in recv mode. If a subscription event handler has been
attached, this call fails with `EBUSY`.

Applicable types: raw XPUB only.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EFAULT` if `subject_` is NULL. `EAGAIN` if `ZLINK_DONTWAIT`
was set and no event is available. `EBUSY` if a subscription event handler
is attached. `ENOTSUP` if the subject is not XPUB.

**See also:** `zlink_subscription_event_handler`, `zlink_publish`

---

### zlink_socket_send_ready_handler

Install or replace the send-ready callback.

```c
int zlink_socket_send_ready_handler (
  void *s_, zlink_send_ready_handler_fn handler_, void *userdata_);
```

The handler is replace-only. Passing NULL is invalid. A successful replace is
visible from the next writable transition. If called reentrantly from the
same handle's send-ready callback, the call fails with `errno=EDEADLK`.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_send`

---

### zlink_multipart_close

Close all parts in a multipart message array.

```c
void zlink_multipart_close (zlink_msg_t *parts, size_t part_count);
```

Convenience function that calls `zlink_msg_close()` on each element.

**See also:** `zlink_msg_close`

---

## Socket Monitor

### zlink_socket_monitor_open

Open and return a socket monitor handle with a fixed callback.

```c
void *zlink_socket_monitor_open (void *s_,
                                 zlink_socket_monitor_event_mask_t events_,
                                 zlink_monitor_handler_fn handler_,
                                 void *userdata_);
```

Creates a monitor for socket `s_` and returns a handle. Events matching the
`events_` bitmask are dispatched through the `handler_` callback on the I/O
thread. The monitor handle must be closed with `zlink_close()` when no longer
needed.

**Returns:** Monitor handle on success, `NULL` on failure (errno is set).

**See also:** `zlink_close`
