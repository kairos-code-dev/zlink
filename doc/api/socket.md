[English](socket.md) | [한국어](socket.ko.md)

# Socket API Reference

The Socket API provides functions for creating, configuring, binding,
connecting, and performing I/O on zlink sockets. Sockets are the primary
interface for sending and receiving data and support several messaging
patterns including publish/subscribe, request/reply, and pipeline.

## Constants

### Socket Types

| Constant | Value | Description |
|---|---|---|
| `ZLINK_PAIR` | 0 | Exclusive pair (bidirectional one-to-one) |
| `ZLINK_PUB` | 1 | Publisher (one-to-many broadcast) |
| `ZLINK_SUB` | 2 | Subscriber (receive from publishers) |
| `ZLINK_DEALER` | 5 | Asynchronous request/reply client |
| `ZLINK_ROUTER` | 6 | Asynchronous request/reply server (identity-routed) |
| `ZLINK_XPUB` | 9 | Extended publisher (receives subscription messages) |
| `ZLINK_XSUB` | 10 | Extended subscriber (sends subscription messages) |
| `ZLINK_STREAM` | 11 | Raw TCP stream socket |

### Send/Recv Flags

| Constant | Value | Description |
|---|---|---|
| `ZLINK_DONTWAIT` | 1 | Non-blocking operation; return immediately with `EAGAIN` if the operation would block |
| `ZLINK_SNDMORE` | 2 | Indicates that more message parts will follow in a multipart message |

### Security Mechanisms

| Constant | Value | Description |
|---|---|---|
| `ZLINK_NULL` | 0 | No security mechanism (default) |
| `ZLINK_PLAIN` | 1 | PLAIN username/password authentication |

### Socket Options

Socket options are configured with `zlink_setsockopt()` and queried with
`zlink_getsockopt()`. The tables below list every option constant grouped by
category.

#### General

| Constant | Value | Description |
|---|---|---|
| `ZLINK_AFFINITY` | 4 | I/O thread affinity bitmask (`uint64_t`) |
| `ZLINK_ROUTING_ID` | 5 | Socket identity for ROUTER addressing (`binary`, max 255 bytes) |
| `ZLINK_TYPE` | 16 | Socket type (read-only, `int`) |
| `ZLINK_LINGER` | 17 | Linger period for socket shutdown in milliseconds (`int`; -1 = infinite, 0 = discard immediately) |
| `ZLINK_BACKLOG` | 19 | Maximum length of the pending connections queue (`int`) |
| `ZLINK_LAST_ENDPOINT` | 32 | Last endpoint bound (read-only, `string`) |
| `ZLINK_FD` | 14 | File descriptor for integration with external event loops (read-only, `zlink_fd_t`) |
| `ZLINK_EVENTS` | 15 | Event state bitmask: `ZLINK_POLLIN`, `ZLINK_POLLOUT` (read-only, `int`) |
| `ZLINK_RCVMORE` | 13 | More message parts pending (read-only, `int`; 1 = yes) |

#### High Water Mark

| Constant | Value | Description |
|---|---|---|
| `ZLINK_SNDHWM` | 23 | Send high water mark; max messages queued for sending (`int`; 0 = unlimited) |
| `ZLINK_RCVHWM` | 24 | Receive high water mark; max messages queued for receiving (`int`; 0 = unlimited) |
| `ZLINK_MAXMSGSIZE` | 22 | Maximum inbound message size in bytes (`int64_t`; -1 = unlimited) |

#### Buffers

| Constant | Value | Description |
|---|---|---|
| `ZLINK_SNDBUF` | 11 | Kernel transmit buffer size in bytes (`int`; 0 = OS default) |
| `ZLINK_RCVBUF` | 12 | Kernel receive buffer size in bytes (`int`; 0 = OS default) |

#### Timing

| Constant | Value | Description |
|---|---|---|
| `ZLINK_RCVTIMEO` | 27 | Receive timeout in milliseconds (`int`; -1 = infinite) |
| `ZLINK_SNDTIMEO` | 28 | Send timeout in milliseconds (`int`; -1 = infinite) |
| `ZLINK_RECONNECT_IVL` | 18 | Initial reconnection interval in milliseconds (`int`) |
| `ZLINK_RECONNECT_IVL_MAX` | 21 | Maximum reconnection interval in milliseconds (`int`; 0 = use `RECONNECT_IVL` only) |
| `ZLINK_CONNECT_TIMEOUT` | 79 | Connection timeout in milliseconds (`int`) |
| `ZLINK_TCP_MAXRT` | 80 | Maximum TCP retransmit timeout in milliseconds (`int`) |
| `ZLINK_HANDSHAKE_IVL` | 66 | ZMTP handshake timeout in milliseconds (`int`) |

#### TCP

| Constant | Value | Description |
|---|---|---|
| `ZLINK_TCP_KEEPALIVE` | 34 | Override SO_KEEPALIVE (`int`; -1 = OS default, 0 = off, 1 = on) |
| `ZLINK_TCP_KEEPALIVE_CNT` | 35 | Override TCP_KEEPCNT (`int`; -1 = OS default) |
| `ZLINK_TCP_KEEPALIVE_IDLE` | 36 | Override TCP_KEEPIDLE in seconds (`int`; -1 = OS default) |
| `ZLINK_TCP_KEEPALIVE_INTVL` | 37 | Override TCP_KEEPINTVL in seconds (`int`; -1 = OS default) |

#### Pub/Sub

| Constant | Value | Description |
|---|---|---|
| `ZLINK_SUBSCRIBE` | 6 | Subscribe to a topic prefix (`binary`) |
| `ZLINK_UNSUBSCRIBE` | 7 | Unsubscribe from a topic prefix (`binary`) |
| `ZLINK_XPUB_VERBOSE` | 40 | Pass all subscription messages upstream (`int`; 0 or 1) |
| `ZLINK_XPUB_NODROP` | 69 | Do not silently drop messages on HWM; return `EAGAIN` instead (`int`; 0 or 1) |
| `ZLINK_XPUB_MANUAL` | 71 | Enable manual subscription management on XPUB (`int`; 0 or 1) |
| `ZLINK_XPUB_WELCOME_MSG` | 72 | Message sent to new subscribers on connect (`binary`) |
| `ZLINK_XPUB_VERBOSER` | 78 | Pass all subscribe and unsubscribe messages upstream (`int`; 0 or 1) |
| `ZLINK_XPUB_MANUAL_LAST_VALUE` | 98 | Enable last-value caching in manual XPUB mode (`int`; 0 or 1) |
| `ZLINK_INVERT_MATCHING` | 74 | Invert topic matching: deliver messages that do NOT match subscriptions (`int`; 0 or 1) |
| `ZLINK_CONFLATE` | 54 | Keep only the most recent message per topic (`int`; 0 or 1) |
| `ZLINK_ONLY_FIRST_SUBSCRIBE` | 108 | Only process the first subscription per topic prefix (`int`; 0 or 1) |
| `ZLINK_TOPICS_COUNT` | 116 | Number of subscribed topics (read-only, `int`) |

#### Router

| Constant | Value | Description |
|---|---|---|
| `ZLINK_ROUTER_MANDATORY` | 33 | Return `EHOSTUNREACH` when routing to an unconnected peer (`int`; 0 or 1) |
| `ZLINK_ROUTER_HANDOVER` | 56 | Allow new connection to take over an existing routing identity (`int`; 0 or 1) |
| `ZLINK_PROBE_ROUTER` | 51 | Send an empty message on connect to establish identity at the ROUTER peer (`int`; 0 or 1) |

#### Heartbeat

| Constant | Value | Description |
|---|---|---|
| `ZLINK_HEARTBEAT_IVL` | 75 | ZMTP heartbeat interval in milliseconds (`int`; 0 = disabled) |
| `ZLINK_HEARTBEAT_TTL` | 76 | ZMTP heartbeat time-to-live in milliseconds (`int`) |
| `ZLINK_HEARTBEAT_TIMEOUT` | 77 | ZMTP heartbeat timeout in milliseconds (`int`) |

#### TLS

| Constant | Value | Description |
|---|---|---|
| `ZLINK_TLS_CERT` | 95 | Path to PEM-encoded TLS certificate (`string`) |
| `ZLINK_TLS_KEY` | 96 | Path to PEM-encoded TLS private key (`string`) |
| `ZLINK_TLS_CA` | 97 | Path to PEM-encoded CA certificate bundle (`string`) |
| `ZLINK_TLS_VERIFY` | 98 | Enable TLS peer certificate verification (`int`; 0 or 1) |
| `ZLINK_TLS_REQUIRE_CLIENT_CERT` | 99 | Require TLS client certificate on server sockets (`int`; 0 or 1) |
| `ZLINK_TLS_HOSTNAME` | 100 | Expected hostname for TLS SNI and certificate verification (`string`) |
| `ZLINK_TLS_TRUST_SYSTEM` | 101 | Trust the system CA certificate store (`int`; 0 or 1) |
| `ZLINK_TLS_PASSWORD` | 102 | Password for encrypted TLS private key (`string`) |

#### Other

| Constant | Value | Description |
|---|---|---|
| `ZLINK_IPV6` | 42 | Enable IPv6 on the socket (`int`; 0 or 1) |
| `ZLINK_IMMEDIATE` | 39 | Queue messages only to completed connections (`int`; 0 or 1) |
| `ZLINK_BLOCKY` | 70 | Legacy option: block on context termination (`int`; 0 or 1) |
| `ZLINK_USE_FD` | 89 | Use a pre-created file descriptor instead of creating a new one (`int`) |
| `ZLINK_BINDTODEVICE` | 92 | Bind socket to a specific network interface (`string`) |
| `ZLINK_CONNECT_ROUTING_ID` | 61 | Set the routing identity used for the next outgoing connection (`binary`) |
| `ZLINK_RATE` | 8 | Multicast data rate in kbps (`int`) |
| `ZLINK_RECOVERY_IVL` | 9 | Multicast recovery interval in milliseconds (`int`) |
| `ZLINK_MULTICAST_HOPS` | 25 | Maximum multicast hops (TTL) (`int`) |
| `ZLINK_TOS` | 57 | IP Type-of-Service value (`int`) |
| `ZLINK_MULTICAST_MAXTPDU` | 84 | Maximum multicast transport data unit size in bytes (`int`) |
| `ZLINK_ZMP_METADATA` | 117 | Attach ZMP metadata properties to outgoing connections (`binary`) |
| `ZLINK_REQUEST_TIMEOUT` | 90 | Request timeout for REQ sockets in milliseconds (`int`) |
| `ZLINK_REQUEST_CORRELATE` | 91 | Enable strict request-reply correlation on REQ sockets (`int`; 0 or 1) |

## Functions

### zlink_socket

Create a socket.

```c
void *zlink_socket (void *context_, int type_);
```

Creates a new socket within the given context. The `type_` parameter selects
the messaging pattern (`ZLINK_PAIR`, `ZLINK_PUB`, `ZLINK_SUB`, `ZLINK_DEALER`,
`ZLINK_ROUTER`, etc.). The returned handle is used for all subsequent socket
operations. The socket must be closed with `zlink_close()` before the context
is terminated.

**Returns:** Socket handle on success, `NULL` on failure (errno is set).

**Errors:** `EINVAL` if the socket type is invalid. `EMFILE` if the maximum
number of sockets has been reached. `ETERM` if the context was terminated.

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
`ZLINK_LINGER` setting. The socket handle is invalid after this call.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `ENOTSOCK` if the handle is not a valid socket.

**Thread safety:** Not thread-safe on the same socket.

**See also:** `zlink_socket`

---

### zlink_setsockopt

Set a socket option.

```c
int zlink_setsockopt (void *s_, int option_, const void *optval_, size_t optvallen_);
```

Configures a socket option. The `option_` parameter identifies the option
(e.g. `ZLINK_SNDHWM`, `ZLINK_LINGER`, `ZLINK_SUBSCRIBE`). The `optval_`
pointer supplies the value and `optvallen_` specifies its size in bytes. For
integer options, pass a pointer to an `int` with `optvallen_` set to
`sizeof(int)`. For string/binary options, pass the data pointer and its length.

Some options must be set before binding or connecting the socket. Refer to the
Socket Options tables above for the expected type and semantics of each option.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EINVAL` if the option is unknown or the value is out of range.
`ETERM` if the context was terminated.

**Thread safety:** Not thread-safe on the same socket.

**See also:** `zlink_getsockopt`

---

### zlink_getsockopt

Get a socket option.

```c
int zlink_getsockopt (void *s_, int option_, void *optval_, size_t *optvallen_);
```

Retrieves the current value of a socket option. The caller provides a buffer
`optval_` and passes its size via `optvallen_`. On success, `optvallen_` is
updated to reflect the actual size written. For integer options, `optval_`
must point to an `int` and `*optvallen_` must be at least `sizeof(int)`.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EINVAL` if the option is unknown or the buffer is too small.
`ETERM` if the context was terminated.

**Thread safety:** Not thread-safe on the same socket.

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

**Thread safety:** Not thread-safe on the same socket.

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

**Errors:** `EPROTONOSUPPORT` if the transport is not supported.
`ENOCOMPATPROTO` if the transport is not compatible with the socket type.

**Thread safety:** Not thread-safe on the same socket.

**See also:** `zlink_bind`, `zlink_disconnect`

---

### zlink_unbind

Unbind a socket from an address.

```c
int zlink_unbind (void *s_, const char *addr_);
```

Removes a previously established binding. The `addr_` string must match the
endpoint used in the original `zlink_bind()` call (or the value retrieved
from `ZLINK_LAST_ENDPOINT` for ephemeral ports).

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `ENOENT` if the endpoint was not bound.

**Thread safety:** Not thread-safe on the same socket.

**See also:** `zlink_bind`

---

### zlink_disconnect

Disconnect a socket from a remote address.

```c
int zlink_disconnect (void *s_, const char *addr_);
```

Removes a previously established connection. The `addr_` string must match the
endpoint used in the original `zlink_connect()` call.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `ENOENT` if the endpoint was not connected.

**Thread safety:** Not thread-safe on the same socket.

**See also:** `zlink_connect`

---

### zlink_send

Send buffer data on a socket.

```c
int zlink_send (void *s_, const void *buf_, size_t len_, int flags_);
```

Sends `len_` bytes from `buf_` on socket `s_`. The data is copied into an
internal message before transmission. The `flags_` parameter may be 0,
`ZLINK_DONTWAIT`, `ZLINK_SNDMORE`, or a bitwise combination of these. Use
`ZLINK_SNDMORE` to send multipart messages; only the final part should omit
this flag.

**Returns:** Number of bytes sent on success, -1 on failure (errno is set).

**Errors:** `EAGAIN` if the operation would block and `ZLINK_DONTWAIT` was set.
`ETERM` if the context was terminated.

**Thread safety:** Not thread-safe on the same socket.

**See also:** `zlink_send_const`, `zlink_recv`, `zlink_msg_send`

---

### zlink_send_const

Send constant data on a socket (zero-copy hint).

```c
int zlink_send_const (void *s_, const void *buf_, size_t len_, int flags_);
```

Behaves identically to `zlink_send()` but signals to the library that `buf_`
points to constant, immutable data (e.g. a string literal or static buffer).
The library may avoid copying the data internally when possible, improving
performance for frequently sent constant payloads. The caller must ensure
`buf_` remains valid and unchanged for the lifetime of the program.

**Returns:** Number of bytes sent on success, -1 on failure (errno is set).

**Errors:** `EAGAIN` if the operation would block and `ZLINK_DONTWAIT` was set.
`ETERM` if the context was terminated.

**Thread safety:** Not thread-safe on the same socket.

**See also:** `zlink_send`, `zlink_msg_init_data`

---

### zlink_recv

Receive data from a socket.

```c
int zlink_recv (void *s_, void *buf_, size_t len_, int flags_);
```

Receives up to `len_` bytes into `buf_` from socket `s_`. If the incoming
message is larger than `len_`, it is silently truncated and the return value
still reflects the original message size (which will exceed `len_`). To detect
truncation, compare the return value against `len_`. The `flags_` parameter
may be 0 or `ZLINK_DONTWAIT`.

**Returns:** Number of bytes in the original message on success (may exceed
`len_` if truncated), -1 on failure (errno is set).

**Errors:** `EAGAIN` if no message is available and `ZLINK_DONTWAIT` was set.
`ETERM` if the context was terminated.

**Thread safety:** Not thread-safe on the same socket.

**See also:** `zlink_send`, `zlink_msg_recv`

---

### zlink_socket_monitor

Start a socket monitor via an inproc address (legacy).

```c
int zlink_socket_monitor (void *s_, const char *addr_, int events_);
```

Starts monitoring socket events and publishes them to the specified inproc
endpoint `addr_`. Another socket (typically `ZLINK_PAIR`) can connect to
`addr_` to receive event notifications. The `events_` parameter is a bitmask
of `ZLINK_EVENT_*` constants selecting which events to monitor. Pass
`ZLINK_EVENT_ALL` to monitor all events.

This is the legacy monitoring interface. Prefer `zlink_socket_monitor_open()`
for new code, which returns a direct monitor handle and avoids the need for
a separate inproc socket.

**Returns:** 0 on success, -1 on failure (errno is set).

**Thread safety:** Not thread-safe on the same socket.

**See also:** `zlink_socket_monitor_open`

---

### zlink_socket_monitor_open

Open and return a socket monitor handle directly.

```c
void *zlink_socket_monitor_open (void *s_, int events_);
```

Creates a monitor for socket `s_` and returns a handle that can be used with
`zlink_monitor_recv()` to receive events directly, without requiring an inproc
endpoint. The `events_` parameter is a bitmask of `ZLINK_EVENT_*` constants.
The returned handle must be closed with `zlink_close()` when no longer needed.

**Returns:** Monitor handle on success, `NULL` on failure (errno is set).

**Thread safety:** Not thread-safe on the same socket.

**See also:** `zlink_socket_monitor`, `zlink_monitor_recv`
