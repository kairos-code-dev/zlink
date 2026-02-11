[English](gateway.md) | [한국어](gateway.ko.md)

# Gateway

The Gateway is a client-side load-balanced request/reply proxy that resolves
service locations automatically via Discovery. It connects to Receivers on
demand and distributes messages across them using a configurable load-
balancing strategy.

## Constants

```c
#define ZLINK_GATEWAY_LB_ROUND_ROBIN 0
#define ZLINK_GATEWAY_LB_WEIGHTED    1
#define ZLINK_GATEWAY_SOCKET_ROUTER  1
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_GATEWAY_LB_ROUND_ROBIN` | 0 | Round-robin load balancing (default) |
| `ZLINK_GATEWAY_LB_WEIGHTED` | 1 | Weighted load balancing based on receiver weight |
| `ZLINK_GATEWAY_SOCKET_ROUTER` | 1 | Internal ROUTER socket used for communication |

## Functions

### zlink_gateway_new

Create a Gateway.

```c
void *zlink_gateway_new(void *ctx,
                        void *discovery,
                        const char *routing_id);
```

Allocates and initializes a new Gateway instance. The `discovery` handle
must have been created with `ZLINK_SERVICE_TYPE_GATEWAY` and remains owned
by the caller. The `routing_id` uniquely identifies this Gateway to
Receivers.

**Returns:** A Gateway handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_gateway_send`, `zlink_gateway_recv`, `zlink_gateway_destroy`

---

### zlink_gateway_send

Send a message to a service (load-balanced).

```c
int zlink_gateway_send(void *gateway,
                       const char *service_name,
                       zlink_msg_t *parts,
                       size_t part_count,
                       int flags);
```

Sends a multipart message to a Receiver registered under `service_name`.
The Gateway selects a Receiver according to the configured load-balancing
strategy (round-robin by default). If no Receivers are available for the
service, the call fails with `EHOSTUNREACH`. On success, ownership of the
message parts is transferred to the Gateway.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EHOSTUNREACH` -- no receivers available for the service.
- `EAGAIN` -- `ZLINK_DONTWAIT` was set and the operation would block.

**Thread safety:** Thread-safe. Multiple threads may call `zlink_gateway_send`
concurrently on the same Gateway handle.

**See also:** `zlink_gateway_recv`, `zlink_gateway_send_rid`, `zlink_gateway_set_lb_strategy`

---

### zlink_gateway_recv

Receive a message.

```c
int zlink_gateway_recv(void *gateway,
                       zlink_msg_t **parts,
                       size_t *part_count,
                       int flags,
                       char *service_name_out);
```

Receives a multipart reply from any connected Receiver. On success, `*parts`
is set to a newly allocated array of message parts and `*part_count` is set
to the number of parts. The caller must close each part with
`zlink_msg_close` and free the array. The `service_name_out` parameter, if
not `NULL`, must point to a buffer of at least 256 bytes; the originating
service name is written into it.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EAGAIN` -- `ZLINK_DONTWAIT` was set and no message is available.

**Thread safety:** Not thread-safe. Only one thread should call
`zlink_gateway_recv` at a time.

**See also:** `zlink_gateway_send`

---

### zlink_gateway_send_rid

Send a message directly to a specific Receiver by routing ID.

```c
int zlink_gateway_send_rid(void *gateway,
                           const char *service_name,
                           const zlink_routing_id_t *routing_id,
                           zlink_msg_t *parts,
                           size_t part_count,
                           int flags);
```

Bypasses load balancing and sends the multipart message to the Receiver
identified by `routing_id` within the given `service_name`. This is useful
when a reply must be directed to a specific Receiver instance, such as in
a stateful protocol.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EHOSTUNREACH` -- the specified routing ID is not connected.
- `EAGAIN` -- `ZLINK_DONTWAIT` was set and the operation would block.

**Thread safety:** Thread-safe.

**See also:** `zlink_gateway_send`

---

### zlink_gateway_set_lb_strategy

Set the load-balancing strategy for a service.

```c
int zlink_gateway_set_lb_strategy(void *gateway,
                                  const char *service_name,
                                  int strategy);
```

Changes the load-balancing strategy used when sending messages to the
specified service. Valid strategies are `ZLINK_GATEWAY_LB_ROUND_ROBIN`
(default) and `ZLINK_GATEWAY_LB_WEIGHTED`. When using weighted balancing,
the weight values reported by Receivers during registration determine the
distribution ratio.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- unknown strategy value.

**Thread safety:** Not thread-safe.

**See also:** `zlink_gateway_send`

---

### zlink_gateway_setsockopt

Set a Gateway socket option.

```c
int zlink_gateway_setsockopt(void *gateway,
                             int option,
                             const void *optval,
                             size_t optvallen);
```

Applies a low-level socket option to the Gateway's internal ROUTER socket.
Commonly used to configure send/receive high-water marks, timeouts, or
keep-alive settings.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- unknown option.

**Thread safety:** Not thread-safe.

**See also:** `zlink_gateway_new`

---

### zlink_gateway_set_tls_client

Configure TLS client settings for the Gateway.

```c
int zlink_gateway_set_tls_client(void *gateway,
                                 const char *ca_cert,
                                 const char *hostname,
                                 int trust_system);
```

Enables TLS for outgoing connections from this Gateway. The `ca_cert`
parameter specifies the path to the CA certificate file used to verify
Receiver certificates. The `hostname` parameter sets the expected server
name for certificate verification. If `trust_system` is non-zero, the
system trust store is used in addition to `ca_cert`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must be called before sending messages.

**See also:** `zlink_receiver_set_tls_server`

---

### zlink_gateway_router

Return the internal ROUTER socket handle.

```c
void *zlink_gateway_router(void *gateway);
```

Returns the raw ROUTER socket handle used internally by the Gateway. This
is intended for diagnostics and advanced use cases such as custom polling.
The caller must not close or modify the socket.

**Returns:** The ROUTER socket handle, or `NULL` if the Gateway is invalid.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_gateway_new`

---

### zlink_gateway_connection_count

Return the number of receivers connected for a service.

```c
int zlink_gateway_connection_count(void *gateway,
                                   const char *service_name);
```

Returns the number of Receivers currently connected for the given service
name. This reflects active transport-level connections, not the count
reported by Discovery.

**Returns:** The connection count on success (zero or positive), or `-1` on
failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_discovery_receiver_count`

---

### zlink_gateway_destroy

Destroy the Gateway and release all resources.

```c
int zlink_gateway_destroy(void **gateway_p);
```

Closes all connections, releases internal sockets, and frees the Gateway.
The pointer at `*gateway_p` is set to `NULL` after destruction. The
Discovery handle passed to `zlink_gateway_new` is not affected and must be
destroyed separately.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must not be called concurrently with other
Gateway operations.

**See also:** `zlink_gateway_new`
