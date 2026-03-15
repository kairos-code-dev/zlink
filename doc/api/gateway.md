[English](gateway.md) | [한국어](gateway.ko.md)

# Gateway

The Gateway is a service-bound load-balanced request/reply handle. It resolves
service locations automatically via Discovery (when attached) and distributes
messages across connected peers using a configurable load-balancing strategy.
All receives are dispatched through a handler callback registered at creation
time. There is no `recv()` function.

## Current API Direction

- Use `zlink_gateway_new()` with a fixed service name, routing id, and handler.
- Use `zlink_gateway_attach_discovery()` for automatic peer management.
- Use `zlink_gateway_bind()` for server-side operation.
- Use `zlink_gateway_connect()` / `zlink_gateway_disconnect()` for manual
  peer management (before discovery attachment only).
- Use `zlink_gateway_set_option()` for service-level tuning.
- Use `zlink_gateway_set_send_ready_handler()` for send-side backpressure.
- Use `zlink_gateway_monitor_open()` for edge transitions such as
  `ZLINK_GATEWAY_SEND_READY_CHANGED` and `ZLINK_GATEWAY_ROUTE_UP`.
- Use `zlink_monitor_snapshot()` on the monitor handle to read current local
  control state and queue depth.
- Use registry gateway-peer query APIs for operational peer inspection.

## Constants

### Load-Balancing Strategies

```c
typedef enum zlink_gateway_lb_strategy_t
{
    ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN = 0,
    ZLINK_GATEWAY_LB_STRATEGY_WEIGHTED    = 1
} zlink_gateway_lb_strategy_t;
```

| Constant | Description |
|----------|-------------|
| `ZLINK_GATEWAY_LB_ROUND_ROBIN` | Round-robin load balancing (default) |
| `ZLINK_GATEWAY_LB_WEIGHTED` | Weighted load balancing based on peer weight |

### Gateway Options

```c
typedef enum zlink_gateway_option_t
{
    ZLINK_GATEWAY_OPT_SNDHWM  = 0x2101,
    ZLINK_GATEWAY_OPT_RCVHWM  = 0x2102,
    ZLINK_GATEWAY_OPT_SNDTIMEO = 0x2103,
    ZLINK_GATEWAY_OPT_LINGER  = 0x2104,
    ZLINK_GATEWAY_OPT_SNDBUF  = 0x2105,
    ZLINK_GATEWAY_OPT_RCVBUF  = 0x2106
} zlink_gateway_option_t;
```

| Constant | Description |
|----------|-------------|
| `ZLINK_GATEWAY_OPT_SNDHWM` | Send high-water mark |
| `ZLINK_GATEWAY_OPT_RCVHWM` | Receive high-water mark |
| `ZLINK_GATEWAY_OPT_SNDTIMEO` | Send timeout (ms) |
| `ZLINK_GATEWAY_OPT_LINGER` | Linger period (ms) |
| `ZLINK_GATEWAY_OPT_SNDBUF` | Kernel transmit buffer size in bytes |
| `ZLINK_GATEWAY_OPT_RCVBUF` | Kernel receive buffer size in bytes |

## Functions

### zlink_gateway_new

Create a Gateway.

```c
void *zlink_gateway_new (void *ctx,
                         const char *service_name,
                         const char *routing_id,
                         zlink_socket_msg_handler_fn handler);
```

Allocates and initializes a new Gateway instance. The `service_name` is the
service identity fixed at creation time. The `routing_id` uniquely identifies
this Gateway. The `handler` callback is invoked on the I/O thread when
messages arrive.

**Returns:** A Gateway handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_gateway_send`, `zlink_gateway_destroy`

---

### zlink_gateway_attach_discovery

Attach a Discovery instance for automatic peer management.

```c
int zlink_gateway_attach_discovery (void *gateway, void *discovery);
```

Connects the Gateway to Discovery for automatic peer resolution. The
Discovery handle must have been created with `ZLINK_SERVICE_TYPE_GATEWAY`
and remains owned by the caller. After attachment, manual
connect/disconnect is no longer allowed.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_gateway_new`, `zlink_discovery_new`

---

### zlink_gateway_bind

Bind the Gateway to an endpoint.

```c
int zlink_gateway_bind (void *gateway, const char *bind_endpoint);
```

Binds the Gateway's internal socket to the specified endpoint for server-side
operation.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_gateway_last_endpoint`

---

### zlink_gateway_connect

Connect to a manually managed remote peer route.

```c
int zlink_gateway_connect (void *gateway,
                           const char *endpoint,
                           const zlink_routing_id_t *routing_id);
```

Manual connect is only allowed before discovery attachment. The remote
routing id identifies the peer for request dispatch.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_gateway_disconnect`

---

### zlink_gateway_disconnect

Disconnect a manually managed remote peer route.

```c
int zlink_gateway_disconnect (void *gateway, const char *endpoint);
```

Manual disconnect is only allowed before discovery attachment.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_gateway_connect`

---

### zlink_gateway_send

Send a message to the bound service (load-balanced).

```c
int zlink_gateway_send (void *gateway,
                        zlink_msg_t *parts,
                        size_t part_count,
                        zlink_send_flags_t flags);
```

Sends a multipart message to a peer selected by the configured load-balancing
strategy (round-robin by default). On success, ownership of the message parts
is transferred.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EHOSTUNREACH` -- no peers available.
- `EAGAIN` -- `ZLINK_DONTWAIT` was set and the operation would block.

**Thread safety:** Thread-safe. Multiple threads may call concurrently.

**See also:** `zlink_gateway_send_rid`, `zlink_gateway_set_lb_strategy`

---

### zlink_gateway_send_rid

Send a message directly to a specific peer by routing ID.

```c
int zlink_gateway_send_rid (void *gateway,
                            const zlink_routing_id_t *routing_id,
                            zlink_msg_t *parts,
                            size_t part_count,
                            zlink_send_flags_t flags);
```

Bypasses load balancing and sends to the peer identified by `routing_id`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EHOSTUNREACH` -- the specified routing ID is not connected.
- `EAGAIN` -- `ZLINK_DONTWAIT` was set and the operation would block.

**Thread safety:** Thread-safe.

**See also:** `zlink_gateway_send`

---

### zlink_gateway_set_send_ready_handler

Install or replace the send-ready callback.

```c
int zlink_gateway_set_send_ready_handler (
  void *gateway, zlink_send_ready_handler_fn handler);
```

The handler is invoked when the Gateway transitions to writable.
Use `zlink_monitor_snapshot()` on an open Gateway monitor to seed initial
state when the handler is installed after startup.

**Returns:** `0` on success, or `-1` on failure (errno is set).

---

### zlink_gateway_set_lb_strategy

Set the load-balancing strategy.

```c
int zlink_gateway_set_lb_strategy (
  void *gateway, zlink_gateway_lb_strategy_t strategy);
```

Changes the load-balancing strategy. Valid strategies are
`ZLINK_GATEWAY_LB_ROUND_ROBIN` (default) and `ZLINK_GATEWAY_LB_WEIGHTED`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_gateway_send`, `zlink_gateway_update_peer_weight`

---

### zlink_gateway_update_peer_weight

Update the authoritative weight for a specific service peer.

```c
int zlink_gateway_update_peer_weight (
  void *gateway,
  const zlink_routing_id_t *routing_id,
  uint32_t weight);
```

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_gateway_set_lb_strategy`

---

### zlink_gateway_set_option

Set a Gateway service option.

```c
int zlink_gateway_set_option (void *gateway,
                              zlink_gateway_option_t option,
                              const void *optval,
                              size_t optvallen);
```

Applies a service-level option. See Gateway Options above.

**Returns:** `0` on success, or `-1` on failure (errno is set).

---

### zlink_gateway_set_routing_id

Override the representative routing id before first bind/connect.

```c
int zlink_gateway_set_routing_id (void *gateway,
                                  const void *data,
                                  size_t size);
```

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_gateway_routing_id`

---

### zlink_gateway_routing_id

Return the representative routing id for this Gateway.

```c
int zlink_gateway_routing_id (void *gateway, zlink_routing_id_t *out);
```

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_gateway_set_routing_id`

---

### zlink_gateway_set_tls_client

Configure TLS client settings for the Gateway.

```c
int zlink_gateway_set_tls_client (void *gateway,
                                  const char *ca_cert,
                                  const char *hostname,
                                  int trust_system);
```

Enables TLS for outgoing connections.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_gateway_set_tls_server`

---

### zlink_gateway_set_tls_server

Configure TLS server settings for the Gateway.

```c
int zlink_gateway_set_tls_server (void *gateway,
                                  const char *cert,
                                  const char *key);
```

Enables TLS for incoming connections on the bound endpoint.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_gateway_set_tls_client`

---

### zlink_gateway_last_endpoint

Resolve the bound endpoint for this Gateway.

```c
int zlink_gateway_last_endpoint (void *gateway,
                                 char *endpoint,
                                 size_t *size);
```

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_gateway_bind`

---

### zlink_gateway_destroy

Destroy the Gateway and release all resources.

```c
int zlink_gateway_destroy (void **gateway_p);
```

Closes all connections, releases internal sockets, and frees the Gateway.
The pointer at `*gateway_p` is set to `NULL` after destruction. The
Discovery handle (if attached) is not affected and must be destroyed
separately.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Gateway handles are thread-safe for same-handle operational
APIs. `zlink_gateway_destroy()` is more restrictive: if another thread is
executing a Gateway callback or operational API on the same handle, destroy
fails with `errno=EBUSY`. A successful destroy clears `*gateway_p`.

**See also:** `zlink_gateway_new`
