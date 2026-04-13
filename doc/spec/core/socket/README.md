[Spec Index](../../README.md) · [Core Index](../README.md)

# Socket — Common Specification

This document covers the shared foundations that apply to all socket types.
Per-type specifications (type-specific options, data-plane APIs, and
behavioral details) live in separate files.

| Socket Type | Spec |
|-------------|------|
| PAIR | [pair.md](pair.md) |
| DEALER | [dealer.md](dealer.md) |
| ROUTER | [router.md](router.md) |
| PUB | [pub.md](pub.md) |
| SUB | [sub.md](sub.md) |
| XPUB | [xpub.md](xpub.md) |
| XSUB | [xsub.md](xsub.md) |
| STREAM | [stream.md](stream.md) |

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

Callback for multipart message dispatch on multipart receive subjects (raw
`PAIR`, `DEALER`, and `STREAM`). Invoked on the owning
I/O thread. Ownership of all message parts is transferred to the callback;
each part must be closed or consumed exactly once. Used with
`zlink_recv_handler()`.

### zlink_subscribe_handler_fn

```c
typedef void (*zlink_subscribe_handler_fn) (const zlink_routing_id_t *source_rid_,
                                       const char *topic_,
                                       size_t topic_len_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_,
                                       void *userdata_);
```

Callback for topic-based message dispatch on topic-aware receive subjects
(raw `SUB`, `XSUB`, `spot`, and `spot_node`). Invoked on the owning I/O
thread. Ownership of parts is transferred. Used with
`zlink_subscribe_handler()`.

Each callback type is registered through a dedicated function. The mapping
of socket types to registration functions:

| Socket Type | Registration Function | Callback |
|---|---|---|
| PAIR, DEALER, STREAM | `zlink_recv_handler` | `zlink_socket_msg_handler_fn` |
| SUB, XSUB, spot, spot_node | `zlink_subscribe_handler` | `zlink_subscribe_handler_fn` |
| PUB | N/A | Send-only; no handler needed |

### zlink_send_ready_handler_fn

```c
typedef void (*zlink_send_ready_handler_fn) (void *subject_, void *userdata_);
```

Callback invoked when a send-capable handle transitions to writable.

### zlink_reply_handler_fn

```c
typedef void (*zlink_reply_handler_fn) (
  zlink_request_result_t result_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);
```

Callback for asynchronous request-reply completion. Invoked when a reply
arrives or the request times out. On timeout, `result_` is set to
`ZLINK_REQUEST_TIMED_OUT` and `parts_` is NULL. On success, `result_` is
`ZLINK_REQUEST_OK` and ownership of all message parts is transferred to
the callback. `result_` represents request completion as a
`zlink_request_result_t` value, not submit failure.

### zlink_router_handler_fn

```c
typedef void (*zlink_router_handler_fn) (
  const zlink_routing_id_t *source_node_rid_,
  const zlink_routing_id_t *source_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);
```

Callback for incoming routed traffic on a ROUTER socket.
`source_node_rid_` identifies the source node for every routed delivery.
For plain ROUTER peers, `source_spot_rid_` is `NULL`. For spot-originated
traffic, `source_spot_rid_` identifies the source spot.
`request_seq_ == 0` means a fire-and-forget routed message.
`request_seq_ != 0` means a request that must be replied to with the
matching ROUTER reply surface. Ownership of all message parts is
transferred to the callback.

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

Always use the fully qualified `ZLINK_SOCKET_*` constants shown above.

### Send Flags

```c
typedef uint32_t zlink_send_flags_t;

#define ZLINK_DONTWAIT  ((zlink_send_flags_t) 0x0001u)
#define ZLINK_SEND_FLAG_DONTWAIT ZLINK_DONTWAIT
```

| Constant | Description |
|---|---|
| `ZLINK_DONTWAIT` | Non-blocking operation; return immediately with `ZLINK_SUBMIT_BACKPRESSURED` if the operation would block |
| `ZLINK_SEND_FLAG_DONTWAIT` | Alias of `ZLINK_DONTWAIT` |

### Send Result

```c
typedef enum zlink_submit_result_t
{
    /* Submit succeeded. */
    ZLINK_SUBMIT_OK = 0,

    /* Normal control-flow result. */
    ZLINK_SUBMIT_BACKPRESSURED = 1,
    ZLINK_SUBMIT_NOT_CONNECTED = 2,
    ZLINK_SUBMIT_NOT_FOUND = 3,

    /* Runtime / lifecycle failure. */
    ZLINK_SUBMIT_TERMINATED = 4,

    /* Caller contract violation. */
    ZLINK_SUBMIT_INVALID_HANDLE = 5,
    ZLINK_SUBMIT_INVALID_ARGUMENT = 6,
    ZLINK_SUBMIT_NOT_SUPPORTED = 7,
    ZLINK_SUBMIT_INVALID_STATE = 8,
    ZLINK_SUBMIT_THREAD_VIOLATION = 9,

    /* Internal failure. */
    ZLINK_SUBMIT_OUT_OF_MEMORY = 10,
    ZLINK_SUBMIT_SEQ_EXHAUSTED = 11,
    ZLINK_SUBMIT_INTERNAL_ERROR = 12
} zlink_submit_result_t;
```

Used as the canonical normalized submit outcome for send, request submit,
and reply submit APIs. Exported C APIs return this enum directly. Internal
implementation paths still use detailed `errno`, and exported API
boundaries normalize those values into this public contract.

| Constant | Value | Description |
|---|---|---|
| `ZLINK_SUBMIT_OK` | 0 | Message was sent successfully |
| `ZLINK_SUBMIT_BACKPRESSURED` | 1 | Send queue is full (HWM reached) |
| `ZLINK_SUBMIT_NOT_CONNECTED` | 2 | Target path or peer is not connected |
| `ZLINK_SUBMIT_NOT_FOUND` | 3 | Target peer, spot, or routed destination was not found |
| `ZLINK_SUBMIT_TERMINATED` | 4 | Context was terminated |
| `ZLINK_SUBMIT_INVALID_HANDLE` | 5 | Handle is NULL or invalid |
| `ZLINK_SUBMIT_INVALID_ARGUMENT` | 6 | Argument is invalid for the API contract |
| `ZLINK_SUBMIT_NOT_SUPPORTED` | 7 | Operation or flags are not supported |
| `ZLINK_SUBMIT_INVALID_STATE` | 8 | Handle is in the wrong state |
| `ZLINK_SUBMIT_THREAD_VIOLATION` | 9 | Handle was accessed from the wrong thread model |
| `ZLINK_SUBMIT_OUT_OF_MEMORY` | 10 | Allocation failed while preparing the submit |
| `ZLINK_SUBMIT_SEQ_EXHAUSTED` | 11 | Request sequence space was exhausted |
| `ZLINK_SUBMIT_INTERNAL_ERROR` | 12 | Internal send/request/reply submit failure |

### Request Completion

```c
typedef enum zlink_request_result_t
{
    /* Reply completed successfully. */
    ZLINK_REQUEST_OK = 0,

    /* Completion failure visible to the requester. */
    ZLINK_REQUEST_TIMED_OUT = 1,
    ZLINK_REQUEST_NOT_FOUND = 2,
    ZLINK_REQUEST_TERMINATED = 3,
    ZLINK_REQUEST_PROTOCOL_ERROR = 4
} zlink_request_result_t;
```

Used as the canonical normalized completion outcome for
`zlink_reply_handler_fn`. The callback receives `result_` directly as a
`zlink_request_result_t` value.

| Constant | Value | Description |
|---|---|---|
| `ZLINK_REQUEST_OK` | 0 | Reply payload was received successfully |
| `ZLINK_REQUEST_TIMED_OUT` | 1 | Reply did not arrive within the configured timeout |
| `ZLINK_REQUEST_NOT_FOUND` | 2 | The target could not be found and an error reply completed the request |
| `ZLINK_REQUEST_TERMINATED` | 3 | Reserved until the request path emits explicit termination completion |
| `ZLINK_REQUEST_PROTOCOL_ERROR` | 4 | Reply envelope or error reply payload was malformed |

### Security Mechanisms

| Constant | Value | Description |
|---|---|---|
| `ZLINK_NULL` | 0 | No security mechanism (default) |
| `ZLINK_PLAIN` | 1 | PLAIN username/password authentication |

### Socket Options

Socket options use typed enums, each with a dedicated setter/getter
function pair. Common options shared across all socket types use
`zlink_set_option()` / `zlink_get_option()` with the `zlink_option_t`
enum. Socket-type-specific options use dedicated typed functions (e.g.
`zlink_set_router_option()`, `zlink_set_pub_option()`). Routing
identity, TLS configuration, and subscribe/unsubscribe have their own
dedicated functions rather than option enums.

#### Common Options (`zlink_option_t`)

Used with `zlink_set_option()` / `zlink_get_option()`.

Internally, options are classified into three ownership categories, each
with its own domain owner responsible for validation/apply. The public
API surface remains the same, but new options are assigned to an owner
based on the following classification:

| Category | Representative Options | Internal Owner |
|----------|----------------------|----------------|
| Core Socket | `SNDHWM`, `RCVHWM`, `LINGER`, `SNDTIMEO`, `RCVTIMEO` | `options_core_socket` |
| Transport/Network | `RATE`, `RECOVERY_IVL`, `SNDBUF`, `RCVBUF`, `TOS`, `PRIORITY` | `options_transport_network` |
| Protocol/Metadata | ZMP metadata | `options_protocol_metadata` |

##### Transport/Buffer

| Constant | Description |
|---|---|
| `ZLINK_OPT_AFFINITY` | I/O thread affinity bitmask (`uint64_t`) |
| `ZLINK_OPT_RATE` | Multicast data rate in kbps (`int`) |
| `ZLINK_OPT_RECOVERY_IVL` | Multicast recovery interval in milliseconds (`int`) |
| `ZLINK_OPT_SNDBUF` | Kernel transmit buffer size in bytes (`int`; 0 = OS default) |
| `ZLINK_OPT_RCVBUF` | Kernel receive buffer size in bytes (`int`; 0 = OS default) |
| `ZLINK_OPT_SNDHWM` | Send high water mark (`int`; 0 = unlimited) |
| `ZLINK_OPT_RCVHWM` | Receive high water mark (`int`; 0 = unlimited) |
| `ZLINK_OPT_MAXMSGSIZE` | Maximum inbound message size in bytes (`int64_t`; -1 = unlimited) |

##### Timing

| Constant | Description |
|---|---|
| `ZLINK_OPT_LINGER` | Linger period for socket shutdown in milliseconds (`int`; -1 = infinite, 0 = discard immediately) |
| `ZLINK_OPT_RCVTIMEO` | Receive timeout in milliseconds (`int`; -1 = infinite) |
| `ZLINK_OPT_SNDTIMEO` | Send timeout in milliseconds (`int`; -1 = infinite) |
| `ZLINK_OPT_CONNECT_TIMEOUT` | Connection timeout in milliseconds (`int`) |
| `ZLINK_OPT_RECONNECT_IVL` | Initial reconnection interval in milliseconds (`int`) |
| `ZLINK_OPT_RECONNECT_IVL_MAX` | Maximum reconnection interval in milliseconds (`int`; 0 = use RECONNECT_IVL only) |
| `ZLINK_OPT_HANDSHAKE_IVL` | ZMTP handshake timeout in milliseconds (`int`) |

##### TCP

| Constant | Description |
|---|---|
| `ZLINK_OPT_TCP_KEEPALIVE` | Override SO_KEEPALIVE (`int`; -1 = OS default, 0 = off, 1 = on) |
| `ZLINK_OPT_TCP_KEEPALIVE_CNT` | Override TCP_KEEPCNT (`int`; -1 = OS default) |
| `ZLINK_OPT_TCP_KEEPALIVE_IDLE` | Override TCP_KEEPIDLE in seconds (`int`; -1 = OS default) |
| `ZLINK_OPT_TCP_KEEPALIVE_INTVL` | Override TCP_KEEPINTVL in seconds (`int`; -1 = OS default) |
| `ZLINK_OPT_TCP_MAXRT` | Maximum TCP retransmit timeout in milliseconds (`int`) |
| `ZLINK_OPT_TCP_NODELAY` | Enable TCP_NODELAY (`int`; 0 or 1) |

##### Heartbeat

| Constant | Description |
|---|---|
| `ZLINK_OPT_HEARTBEAT_IVL` | ZMTP heartbeat interval in milliseconds (`int`; 0 = disabled) |
| `ZLINK_OPT_HEARTBEAT_TTL` | ZMTP heartbeat TTL in milliseconds (`int`) |
| `ZLINK_OPT_HEARTBEAT_TIMEOUT` | ZMTP heartbeat timeout in milliseconds (`int`) |

##### Network

| Constant | Description |
|---|---|
| `ZLINK_OPT_IPV6` | Enable IPv6 (`int`; 0 or 1) |
| `ZLINK_OPT_TOS` | IP Type-of-Service value (`int`) |
| `ZLINK_OPT_MULTICAST_HOPS` | Maximum multicast hops (TTL) (`int`) |
| `ZLINK_OPT_MULTICAST_MAXTPDU` | Maximum multicast transport data unit size in bytes (`int`) |
| `ZLINK_OPT_BINDTODEVICE` | Bind socket to a specific network interface (`string`) |
| `ZLINK_OPT_BACKLOG` | Maximum length of the pending connections queue (`int`) |

##### TLS

| Constant | Description |
|---|---|
| `ZLINK_OPT_TLS_CERT` | Path to PEM-encoded TLS certificate (`string`) |
| `ZLINK_OPT_TLS_KEY` | Path to PEM-encoded TLS private key (`string`) |
| `ZLINK_OPT_TLS_CA` | Path to PEM-encoded CA certificate bundle (`string`) |
| `ZLINK_OPT_TLS_VERIFY` | Enable TLS peer verification (`int`; 0 or 1) |
| `ZLINK_OPT_TLS_REQUIRE_CLIENT_CERT` | Require client certificate (`int`; 0 or 1) |
| `ZLINK_OPT_TLS_HOSTNAME` | Expected hostname for SNI and certificate verification (`string`) |
| `ZLINK_OPT_TLS_TRUST_SYSTEM` | Trust the system CA certificate store (`int`; 0 or 1) |
| `ZLINK_OPT_TLS_PASSWORD` | Private key passphrase (`string`) |

##### Behavior

| Constant | Description |
|---|---|
| `ZLINK_OPT_IMMEDIATE` | Queue messages only to completed connections (`int`; 0 or 1) |
| `ZLINK_OPT_CONFLATE` | Keep only the most recent message per topic (`int`; 0 or 1) |
| `ZLINK_OPT_BLOCKY` | Legacy option: block on context termination (`int`; 0 or 1) |
| `ZLINK_OPT_INVERT_MATCHING` | Invert topic matching (`int`; 0 or 1) |
| `ZLINK_OPT_ZMP_METADATA` | Attach ZMP metadata properties to outgoing connections (`binary`) |

##### Read-only (get only)

| Constant | Description |
|---|---|
| `ZLINK_OPT_FD` | File descriptor (read-only, `zlink_fd_t`) |
| `ZLINK_OPT_EVENTS` | Event state bitmask (read-only, `int`) |
| `ZLINK_OPT_TYPE` | Socket type (read-only, `int`) |
| `ZLINK_OPT_LAST_ENDPOINT` | Last endpoint bound (read-only, `string`) |
| `ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE` | Maximum discovery metadata size in bytes (read-only, `int`) |

#### Dedicated Functions (not option enums)

- **Routing ID**: `zlink_set_routing_id()` / `zlink_get_routing_id()`
- **TLS**: `zlink_set_tls_server()` / `zlink_set_tls_client()`
- **Subscribe/Unsubscribe**: `zlink_set_subscription()` / `zlink_unset_subscription()`

## Functions

### zlink_socket

Create a socket.

```c
void *zlink_socket (void *context_, zlink_socket_type_t type_);
```

Creates a new socket within the given context. The `type_` parameter selects
the messaging pattern. Raw sockets start in recv model. Multipart receive
subjects (`PAIR`, `DEALER`, `STREAM`) support
`zlink_recv_handler()` callback attach; topic-aware subjects (`SUB`, `XSUB`)
support `zlink_subscribe_handler()`. The socket must be closed with
`zlink_close()` before the context is terminated.

**Returns:** Socket handle on success, `NULL` on failure (errno is set).

**Errors:** `EINVAL` if the socket type is invalid. `EMFILE` if the maximum
number of sockets has been reached. `ETERM` if the context was terminated.

**Thread safety:** Thread-safe with respect to the context.

**See also:** `zlink_close`, `zlink_ctx_new`, `zlink_recv_handler`

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

### zlink_subscribe_handler

Attach a topic-based receive handler to a socket.

```c
bool zlink_subscribe_handler (void *s_,
                              zlink_subscribe_handler_fn handler_,
                              void *userdata_);
```

Attach a topic-based receive handler to raw `SUB`, raw `XSUB`, `spot`, or
`spot_node`. After attach, `zlink_subscribe()` and data-plane poller
`ZLINK_POLLIN` on the same subject fail with `errno=EBUSY`. A second attach
on the same subject also fails with `errno=EBUSY`. Unsupported subjects
return `ENOTSUP`.

**Returns:** `true` on success, `false` on failure (errno is set).

**Errors:** `EINVAL` if the handler is NULL. `ENOTSUP` if the handle type does
not accept a subscribe handler. `EBUSY` if a handler is already attached.

**See also:** `zlink_recv_handler`, `zlink_socket`, `zlink_close`

---

### zlink_close

Close a socket and release its resources.

```c
int zlink_close (void *s_);
```

Closes the socket and releases all associated resources. Any outstanding
messages in the send queue are discarded or sent depending on the
`ZLINK_OPT_LINGER` setting. Public handles follow a tiered contract: hot-path send
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

### zlink_set_option

Set a common socket option.

```c
int zlink_set_option (void *handle_,
                      zlink_option_t option_,
                      const void *optval_,
                      size_t optvallen_);
```

Configures a common socket option. The `option_` parameter identifies the
option (e.g. `ZLINK_OPT_SNDHWM`, `ZLINK_OPT_LINGER`). The `optval_`
pointer supplies the value and `optvallen_` specifies its size in bytes.

Some options must be set before binding or connecting the socket.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EINVAL` if the option is unknown or the value is out of range.
`ETERM` if the context was terminated.

**See also:** `zlink_get_option`

---

### zlink_get_option

Get a common socket option.

```c
int zlink_get_option (void *handle_,
                      zlink_option_t option_,
                      void *optval_,
                      size_t *optvallen_);
```

Retrieves the current value of a common socket option.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_set_option`

---

### zlink_set_routing_id

Set the routing identity on a socket.

```c
int zlink_set_routing_id (void *handle_,
                           const void *data_,
                           size_t size_);
```

Assigns a routing identity to the socket. The identity is used for ROUTER
addressing and must be at most 255 bytes. Must be set before the first
bind or connect.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_get_routing_id`

---

### zlink_get_routing_id

Get the routing identity of a socket.

```c
int zlink_get_routing_id (void *handle_,
                           zlink_routing_id_t *out_);
```

Retrieves the current routing identity into `*out_`.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_set_routing_id`

---

### zlink_set_tls_server

Configure TLS for a server socket.

```c
int zlink_set_tls_server (void *handle_,
                           const char *cert_,
                           const char *key_,
                           int require_client_cert_);
```

Configures TLS server mode on the socket. `cert_` and `key_` are paths to
PEM-encoded certificate and private key files. Set `require_client_cert_`
to 1 to require client certificate authentication.

For service handles, TLS support is surface-specific. Discovery accepts
client TLS, Registry accepts client/server TLS, and SPOT accepts TLS only
for `SpotNode` handles. Unified `Spot` and SPOT child pub/sub handles fail
with `ENOTSUP`.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_set_tls_client`

---

### zlink_set_tls_client

Configure TLS for a client socket.

```c
int zlink_set_tls_client (void *handle_,
                           const char *ca_cert_,
                           const char *hostname_,
                           int trust_system_);
```

Configures TLS client mode on the socket. `ca_cert_` is the path to a
PEM-encoded CA certificate bundle. `hostname_` sets the expected hostname
for SNI and certificate verification. Set `trust_system_` to 1 to also
trust the system CA certificate store.

For service handles, TLS support is surface-specific. Discovery accepts
client TLS, Registry accepts client/server TLS, and SPOT accepts TLS only
for `SpotNode` handles. Unified `Spot` and SPOT child pub/sub handles fail
with `ENOTSUP`.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_set_tls_server`

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
the system assigns an ephemeral port; use `ZLINK_OPT_LAST_ENDPOINT` to retrieve
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

### zlink_socket_attach_discovery

Attach a raw socket to a discovery service view.

```c
int zlink_socket_attach_discovery (void *socket_, void *discovery_);
```

Attaches a raw ROUTER, DEALER, PUB, or SUB socket to a discovery service
view. While attached, manual `connect`, `disconnect`, `unbind`, and `close`
operations fail. Destroy the discovery instance to terminate the attached
socket lifecycle.

**Returns:** 0 on success, -1 on failure (errno is set).

**See also:** `zlink_socket`, `zlink_close`

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

Open a socket monitor handle in recv model.

```c
void *zlink_socket_monitor_open (void *s_,
                                 const zlink_socket_monitor_open_options_t *options_);
```

Creates a monitor for socket `s_` and returns a handle. The `options_->events`
bitmask selects which events to observe. The monitor starts in **recv model**;
use `zlink_socket_monitor_recv()` to pull events or
`zlink_socket_monitor_handler()` to transition to callback-only model.
The monitor handle must be closed with `zlink_monitor_close()` when no longer
needed.

**Returns:** Monitor handle on success, `NULL` on failure (errno is set).

**See also:** `zlink_socket_monitor_handler`, `zlink_socket_monitor_recv`,
`zlink_monitor_snapshot`, `zlink_monitor_close`
