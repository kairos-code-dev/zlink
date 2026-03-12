[English](socket.md) | [한국어](socket.ko.md)

# Socket API Reference

The Socket API provides functions for creating, configuring, binding,
connecting, and performing I/O on zlink sockets. All message receiving is
handled through handler callbacks registered at socket creation time. There is
no `recv()` function. Sockets support several messaging patterns including
publish/subscribe, request/reply, and raw stream.

## Callback Types

### zlink_socket_msg_handler_fn

```c
typedef void (*zlink_socket_msg_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *parts_,
  size_t part_count_);
```

Callback for multipart message dispatch on PAIR, DEALER, ROUTER sockets.
Invoked on the owning I/O thread. Ownership of all message parts is
transferred to the callback; each part must be closed or consumed exactly once.

### zlink_spot_handler_fn

```c
typedef void (*zlink_spot_handler_fn) (const zlink_routing_id_t *source_rid_,
                                       const char *topic_,
                                       size_t topic_len_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_);
```

Callback for topic-based message dispatch on SUB and XSUB sockets.
Invoked on the owning I/O thread. Ownership of parts is transferred.

### zlink_xpub_handler_fn

```c
typedef void (*zlink_xpub_handler_fn) (int subscribed_,
                                       const uint8_t *topic_,
                                       size_t topic_len_);
```

Callback for subscription notifications on XPUB sockets.

### zlink_socket_handler_t

```c
typedef enum zlink_socket_handler_kind_t
{
    ZLINK_SOCKET_HANDLER_MSG  = 0x1201,
    ZLINK_SOCKET_HANDLER_SPOT = 0x1202,
    ZLINK_SOCKET_HANDLER_XPUB = 0x1203
} zlink_socket_handler_kind_t;

typedef struct zlink_socket_handler_t
{
    zlink_socket_handler_kind_t kind;
    union
    {
        zlink_socket_msg_handler_fn msg;
        zlink_spot_handler_fn spot;
        zlink_xpub_handler_fn xpub;
    } fn;
} zlink_socket_handler_t;
```

Handler descriptor passed to `zlink_socket()`. The `kind` field selects the
callback variant. The mapping of socket types to handler kinds:

| Socket Type | Handler Kind | Callback |
|---|---|---|
| PAIR, DEALER, ROUTER | `ZLINK_SOCKET_HANDLER_MSG` | `zlink_socket_msg_handler_fn` |
| SUB, XSUB | `ZLINK_SOCKET_HANDLER_SPOT` | `zlink_spot_handler_fn` |
| XPUB | `ZLINK_SOCKET_HANDLER_XPUB` | `zlink_xpub_handler_fn` |
| PUB | N/A | Send-only; pass `NULL` handler |
| STREAM | `ZLINK_SOCKET_HANDLER_MSG` | See STREAM callback API below |

### zlink_send_ready_handler_fn

```c
typedef void (*zlink_send_ready_handler_fn) (void *subject_);
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
#define ZLINK_SNDMORE   ((zlink_send_flags_t) 0x0002u)
```

| Constant | Description |
|---|---|
| `ZLINK_DONTWAIT` | Non-blocking operation; return immediately with `EAGAIN` if the operation would block |
| `ZLINK_SNDMORE` | Indicates that more message parts will follow in a multipart message |

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
| `ZLINK_SOCKOPT_USE_FD` | Use a pre-created file descriptor instead of creating a new one (`int`) |
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

Create a socket with a receive handler.

```c
void *zlink_socket (void *context_,
                    zlink_socket_type_t type_,
                    const zlink_socket_handler_t *handler_);
```

Creates a new socket within the given context. The `type_` parameter selects
the messaging pattern. The `handler_` parameter specifies the receive callback
that will be invoked on the I/O thread when messages arrive. For send-only
sockets (PUB), pass `NULL`. For all recv-capable types the handler must be
non-NULL.

The callback is fixed at creation time and cannot be replaced. The socket must
be closed with `zlink_close()` before the context is terminated.

**Returns:** Socket handle on success, `NULL` on failure (errno is set).

**Errors:** `EINVAL` if the socket type is invalid or handler is NULL for a
recv-capable type. `EMFILE` if the maximum number of sockets has been reached.
`ETERM` if the context was terminated.

**Thread safety:** Thread-safe with respect to the context.

**See also:** `zlink_close`, `zlink_ctx_new`

---

### zlink_close

Close a socket and release its resources.

```c
int zlink_close (void *s_);
```

Closes the socket and releases all associated resources. Any outstanding
messages in the send queue are discarded or sent depending on the
`ZLINK_LINGER` setting. If another thread has an in-flight callback or
operational API on the same handle, close fails with `errno=EBUSY`. Self-close
from a send-ready or monitor callback is deferred until callback epilogue.

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

Send buffer data on a socket.

```c
int zlink_send (void *s_,
                const void *buf_,
                size_t len_,
                zlink_send_flags_t flags_);
```

Sends `len_` bytes from `buf_` on socket `s_`. The data is copied into an
internal message before transmission. The `flags_` parameter may be 0,
`ZLINK_DONTWAIT`, `ZLINK_SNDMORE`, or a bitwise combination. Use
`ZLINK_SNDMORE` to send multipart messages; only the final part should omit
this flag.

**Returns:** Number of bytes sent on success, -1 on failure (errno is set).

**Errors:** `EAGAIN` if the operation would block and `ZLINK_DONTWAIT` was set.
`ETERM` if the context was terminated.

**See also:** `zlink_msg_send`

---

### zlink_socket_set_send_ready_handler

Install or replace the send-ready callback.

```c
int zlink_socket_set_send_ready_handler (
  void *s_, zlink_send_ready_handler_fn handler_);
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

## STREAM Callback Dispatch API

The following functions provide a callback-based interface for STREAM sockets.
STREAM receive is callback-only; `recv()` is not supported. The application
attaches a raw callback that is invoked directly on the I/O thread when data
arrives.

### zlink_stream_on_raw_fn

```c
typedef int (*zlink_stream_on_raw_fn) (const zlink_routing_id_t *rid_,
                                       zlink_msg_t *msg_);
```

Callback invoked on the STREAM I/O thread when data arrives. `rid_` identifies
the peer. `msg_` is a raw stream chunk; ownership is transferred to the
callback. The callback must release it exactly once (e.g. `zlink_msg_close()`
or consume via `zlink_stream_send_msg()`) before return, and must not retain
this pointer after return. Return 0 to continue dispatch, non-zero to request
shutdown.

---

### zlink_stream_attach_raw

Attach raw STREAM callback dispatch.

```c
int zlink_stream_attach_raw (void *s_, zlink_stream_on_raw_fn on_raw_);
```

Registers `on_raw_` as the dispatch callback for STREAM socket `s_`. Only one
callback can be attached at a time; calling this while a callback is already
attached returns -1 with `errno=EBUSY`. Attach/detach are safe to call from
application threads and serialized with STREAM send/close. Calling
attach/detach from the raw callback is not supported.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EINVAL` if `s_` is not a STREAM socket or `on_raw_` is NULL.
`EBUSY` if a callback is already attached.

**See also:** `zlink_stream_detach`, `zlink_stream_send`

---

### zlink_stream_detach

Detach STREAM callback dispatch from a socket.

```c
int zlink_stream_detach (void *s_);
```

Removes the previously attached dispatch callback. Safe to call from
application threads and serialized with STREAM send/close. Calling detach
from the raw callback is not supported.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EINVAL` if `s_` is not a STREAM socket.

**See also:** `zlink_stream_attach_raw`

---

### zlink_stream_send

Send payload to a specific STREAM peer by routing id.

```c
int zlink_stream_send (void *s_,
                       const zlink_routing_id_t *rid_,
                       const void *data_,
                       size_t size_,
                       zlink_send_flags_t flags_);
```

Sends `size_` bytes from `data_` to the peer identified by `rid_`. Internally
sends the routing id as the first frame and the payload as the second frame.
STREAM send APIs are safe to call from application threads and STREAM dispatch
callbacks; internally the socket serializes outgoing state.

**Returns:** Number of payload bytes accepted (`size_`), or -1 on failure.

**Errors:** `EINVAL` if `s_` is not a STREAM socket or `rid_` is invalid.
`EAGAIN` if the operation would block and `ZLINK_DONTWAIT` was set.

**See also:** `zlink_stream_send_msg`, `zlink_stream_attach_raw`

---

### zlink_stream_send_msg

Send a message to a specific STREAM peer by routing id.

```c
int zlink_stream_send_msg (void *s_,
                           const zlink_routing_id_t *rid_,
                           zlink_msg_t *msg_,
                           zlink_send_flags_t flags_);
```

Behaves like `zlink_stream_send()` but takes a `zlink_msg_t` instead of a raw
buffer. The message `msg_` is consumed (moved) by this call and reinitialized
before returning.

**Returns:** Number of payload bytes accepted, or -1 on failure.

**See also:** `zlink_stream_send`, `zlink_stream_attach_raw`

---

## Socket Monitor

### zlink_socket_monitor_open

Open and return a socket monitor handle with a fixed callback.

```c
void *zlink_socket_monitor_open (void *s_,
                                 zlink_socket_monitor_event_mask_t events_,
                                 zlink_monitor_handler_fn handler_);
```

Creates a monitor for socket `s_` and returns a handle. Events matching the
`events_` bitmask are dispatched through the `handler_` callback on the I/O
thread. The monitor handle must be closed with `zlink_close()` when no longer
needed.

**Returns:** Monitor handle on success, `NULL` on failure (errno is set).

**See also:** `zlink_close`
