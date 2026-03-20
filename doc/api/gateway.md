[English](gateway.md) | [한국어](gateway.ko.md)

# Gateway

The Gateway is a service-bound load-balanced request/reply handle. It resolves
service locations automatically via Discovery (when attached) and distributes
messages across connected peers using a configurable load-balancing strategy.
Gateway supports two exclusive I/O models for receiving messages.

## I/O Model

A Gateway handle starts in **recv model** and stays there unless
`zlink_recv_handler()` is called, which makes a **one-way transition** to
callback model. The two models are mutually exclusive for the lifetime of
the handle.

| | Recv Model (default) | Callback Model |
|---|---|---|
| **Receive** | `zlink_gateway_recv()` | `zlink_recv_handler()` callback |
| **Send-ready** | not available (`EBUSY`) | `zlink_send_ready_handler()` |
| **Transition** | call `zlink_recv_handler()` to switch | permanent, cannot revert |

- In recv model, `zlink_send_ready_handler()` fails with `EBUSY`.
- In callback model, `zlink_gateway_recv()` fails with `EBUSY`.
- `zlink_gateway_send()` / `zlink_gateway_send_rid()` work in both models.

## Thread-Safety Summary

A single Gateway handle can be used concurrently from multiple threads
(thread-safe).

- `zlink_gateway_send()` / `zlink_gateway_send_rid()` are concurrent hot-path
  operations.
- attach, bind/connect/disconnect, option, query, and monitor operations are
  runtime control-path operations. Correctness is preserved, but execution
  order may follow internal serialization.
- `zlink_gateway_destroy()` uses a fail-fast lifecycle gate. If another
  admitted API or callback is running, destroy fails with `EBUSY`. Once
  destroy is accepted, new API entry fails with `ESHUTDOWN`.
- Init-only settings and callback-context restrictions should be treated
  separately from normal operational APIs.

## Current API Direction

- Use `zlink_gateway_new()` with a fixed service name.
- Use `zlink_set_routing_id()` before the first bind/connect when a
  stable routing id is required.
- **Recv model (default):** Use `zlink_gateway_recv()` to pull messages.
- **Callback model:** Call `zlink_recv_handler()` once to transition; messages
  are then dispatched through the installed callback.
- Use `zlink_gateway_attach_discovery()` for automatic peer management.
- Use `zlink_gateway_bind()` for server-side operation.
- Use `zlink_gateway_connect()` / `zlink_gateway_disconnect()` for manual
  peer management (before discovery attachment only).
- Use `zlink_set_option()` / `zlink_get_option()` for service-level tuning.
- Use `zlink_send_ready_handler()` for send-side backpressure.
- Use `zlink_service_monitor_open(gateway, &options)` for edge transitions
  such as `ZLINK_GATEWAY_SEND_READY_CHANGED` and `ZLINK_GATEWAY_ROUTE_UP`.
  Close with `zlink_monitor_close()`.
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

### Common Options (via generic API)

Gateway uses the generic typed option API (`zlink_set_option` /
`zlink_get_option`) with the following `zlink_option_t` constants:

| Constant | Description |
|----------|-------------|
| `ZLINK_OPT_SNDHWM` | Send high-water mark |
| `ZLINK_OPT_RCVHWM` | Receive high-water mark |
| `ZLINK_OPT_SNDTIMEO` | Send timeout (ms) |
| `ZLINK_OPT_LINGER` | Linger period (ms) |
| `ZLINK_OPT_SNDBUF` | Kernel transmit buffer size in bytes |
| `ZLINK_OPT_RCVBUF` | Kernel receive buffer size in bytes |
| `ZLINK_OPT_LAST_ENDPOINT` | Resolved bound endpoint (get-only) |

See [socket.md](socket.md) for the full `zlink_option_t` reference.

### Router Options (via generic API)

Gateway also supports router-specific options through
`zlink_set_router_option` / `zlink_get_router_option`:

| Constant | Description |
|----------|-------------|
| `ZLINK_ROUTER_OPT_MANDATORY` | Fail sends to unroutable peers instead of dropping |
| `ZLINK_ROUTER_OPT_HANDOVER` | Allow new connections to take over an existing routing id |
| `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` | Set the routing id used when connecting to a peer |

See [socket.md](socket.md) for the full `zlink_router_option_t` reference.

## Functions

### zlink_gateway_new

Create a Gateway in recv model.

```c
void *zlink_gateway_new (void *ctx,
                         const char *service_name);
```

Allocates and initializes a new Gateway instance. The `service_name` is the
service identity fixed at creation time. Configure a representative routing id
later with `zlink_set_routing_id()` if needed.

**Returns:** A Gateway handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_gateway_send`, `zlink_gateway_destroy`

### zlink_gateway_recv

Receive a message in recv model.

```c
int zlink_gateway_recv (void *gateway,
                        zlink_routing_id_t *source_rid_out,
                        zlink_msg_t **parts,
                        size_t *part_count,
                        int flags);
```

Returns the same semantic message unit that callback mode would deliver.
`source_rid_out` receives the sender's routing ID. `parts` and `part_count`
are filled with the multipart message on success. The caller owns the
returned parts and must release them with `zlink_msg_close()`.

Pass `ZLINK_DONTWAIT` in `flags` for non-blocking operation.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EBUSY` -- handle is in callback model.
- `EAGAIN` -- `ZLINK_DONTWAIT` was set and no message is available.

**Thread safety:** Safe to call from any thread in recv model.

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

**See also:** `zlink_get_option` with `ZLINK_OPT_LAST_ENDPOINT`

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

### Options — zlink_set_option / zlink_get_option

Gateway uses the generic typed option API for service-level tuning.

```c
int zlink_set_option (void *gateway, zlink_option_t option, ...);
int zlink_get_option (void *gateway, zlink_option_t option, ...);
```

Supported options are listed in Common Options above. The last-endpoint
query previously done with `zlink_gateway_last_endpoint()` is now:

```c
zlink_get_option (gateway, ZLINK_OPT_LAST_ENDPOINT, buf, &size);
```

See [socket.md](socket.md) for full details on the generic typed option API.

---

### Router Options — zlink_set_router_option / zlink_get_router_option

Gateway supports router-specific options through the generic router option API.

```c
int zlink_set_router_option (void *gateway, zlink_router_option_t option, ...);
int zlink_get_router_option (void *gateway, zlink_router_option_t option, ...);
```

Supported options: `ZLINK_ROUTER_OPT_MANDATORY`, `ZLINK_ROUTER_OPT_HANDOVER`,
`ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID`.

See [socket.md](socket.md) for full details on the generic router option API.

---

### Routing ID — zlink_set_routing_id / zlink_get_routing_id

Gateway uses the generic routing id API.

```c
int zlink_set_routing_id (void *gateway, const void *data, size_t size);
int zlink_get_routing_id (void *gateway, zlink_routing_id_t *out);
```

Set the representative routing id before the first bind/connect. Get
returns the current routing id.

See [socket.md](socket.md) for full details.

---

### TLS — zlink_set_tls_client / zlink_set_tls_server

Gateway uses the generic TLS configuration API.

```c
int zlink_set_tls_client (void *gateway,
                          const char *ca_cert,
                          const char *hostname,
                          int trust_system);

int zlink_set_tls_server (void *gateway,
                          const char *cert,
                          const char *key,
                          int require_client_cert);
```

`zlink_set_tls_client` enables TLS for outgoing connections.
`zlink_set_tls_server` enables TLS for incoming connections on the bound
endpoint. Note: `zlink_set_tls_server` has an additional
`require_client_cert` parameter compared to the previous gateway-specific
API.

See [socket.md](socket.md) for full details.

---

### Send-Ready — zlink_send_ready_handler

Gateway uses the generic send-ready handler API. **Callback model only.**

```c
int zlink_send_ready_handler (
  void *gateway, zlink_send_ready_handler_fn handler, void *userdata);
```

The handler is invoked when the Gateway transitions to writable.
Use `zlink_monitor_snapshot()` on an open Gateway monitor to seed initial
state when the handler is installed after startup.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EBUSY` -- handle is in recv model (transition to callback model first).

See [socket.md](socket.md) for full details.

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

**Thread safety:** A single Gateway handle can be used concurrently from
multiple threads. `zlink_gateway_destroy()` is more restrictive: if another thread is
executing a Gateway callback or operational API on the same handle, destroy
fails with `errno=EBUSY`. A successful destroy clears `*gateway_p`.

**See also:** `zlink_gateway_new`

---

## Snapshot / Introspection

### Gateway Status Snapshot

```c
int zlink_gateway_status_snapshot(void *gateway,
                                  zlink_gateway_status_t *out);
```

Returns a single-row operational health summary of the Gateway.

#### zlink_gateway_status_t

```c
typedef struct zlink_gateway_status_t
{
    char service_name[256];
    char bind_endpoint[256];
    zlink_routing_id_t gateway_routing_id;
    zlink_gateway_state_t state;
    uint32_t observed_provider_count;
    uint32_t ready_provider_count;
    uint32_t active_route_count;
    uint32_t send_ready;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_gateway_status_t;
```

| Field | Description |
|-------|-------------|
| `service_name` | Null-terminated service name fixed at construction. |
| `bind_endpoint` | Null-terminated bound endpoint. |
| `gateway_routing_id` | Routing identity of this Gateway. |
| `state` | `IDLE`, `CONNECTING`, `PARTIAL_READY`, `READY`, or `ERROR`. |
| `observed_provider_count` | Total providers observed (connected + connecting). |
| `ready_provider_count` | Providers currently in ready state. |
| `active_route_count` | Number of active routes for load balancing. |
| `send_ready` | Non-zero if the Gateway is writable. |
| `last_error` | Last recorded error code, or 0. |
| `last_changed_ms` | Epoch ms of the last state change. |

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.
