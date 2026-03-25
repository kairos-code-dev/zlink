[English](07-2-gateway.md) | [한국어](07-2-gateway.ko.md)

# Gateway Service (Location-Transparent Request/Reply)

## 1. Overview

Gateway is a unified service handle that automatically discovers services
based on Discovery, supports load-balanced message sending, and starts in a
recv model. A single Gateway handle can act as both a client (sender) and a
server (receiver), and can independently opt into receive callback and
send-ready callback.

> **About the name**: Gateway serves as an entry point and client-side load
> balancer for a specific service. Unlike API Gateways (such as Kong or AWS
> API Gateway) that include authentication, rate limiting, and protocol
> translation, Gateway is a lightweight gateway focused on service access
> and load balancing.

**Gateway is thread-safe.** A single Gateway handle can be used concurrently
from multiple threads. `send` / `send_rid` are concurrent hot-path
operations, attach/option/monitor/query operations are runtime control-path
operations, and `destroy` uses a fail-fast lifecycle gate.

## 2. Creating a Gateway

Create a Gateway handle and attach it to a Discovery service view.
Routing ID and I/O model setup are explicit follow-up steps.

A Gateway starts in **recv model**.
- `zlink_recv_handler(gateway, ...)` is supported and turns the receive surface into callback mode.
- After receive callback attach, direct recv and data-plane `ZLINK_POLLIN` fail with `EBUSY`.
- `zlink_send_ready_handler(gateway, ...)` is supported independently.
- After send-ready attach, data-plane `ZLINK_POLLOUT` fails with `EBUSY`.

### Recv model

```c
void *gateway = zlink_gateway_new(ctx);
zlink_set_routing_id(gateway, "gateway-1", 9);
/* No callback -- stay in recv model, pull with zlink_gateway_recv() */
```

## 3. Server Setup

To act as a server, bind an endpoint on the Gateway and register via
Discovery. Server-side receive is modeled with `zlink_gateway_recv()`.

### Recv model server

```c
void *ctx = zlink_ctx_new();

void *discovery = zlink_discovery_new(ctx,
    ZLINK_SERVICE_TYPE_GATEWAY, "payment-service");
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

void *server = zlink_gateway_new(ctx);
zlink_set_routing_id(server, "payment-server-1", 16);
/* Stay in recv model -- no zlink_recv_handler() call */

zlink_gateway_attach_discovery(server, discovery);
zlink_gateway_bind(server, "tcp://*:5555");

/* Application loop pulls messages */
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
while (zlink_gateway_recv(server, &source_rid, &parts, &part_count, 0) == 0) {
    /* Process request, then reply */
    zlink_gateway_send_rid(server, &source_rid, parts, part_count, 0);
}
```

## 4. Client (Sender) Setup

Gateway clients also stay in recv model. Attach Discovery and pull replies
with `zlink_gateway_recv()` after send.

### Recv model client

```c
void *discovery = zlink_discovery_new(ctx,
    ZLINK_SERVICE_TYPE_GATEWAY, "payment-service");
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

void *client = zlink_gateway_new(ctx);
zlink_set_routing_id(client, "client-1", 8);
/* Stay in recv model */

zlink_gateway_attach_discovery(client, discovery);
zlink_gateway_set_lb_strategy(client, ZLINK_GATEWAY_LB_ROUND_ROBIN);
```

## 5. Sending Messages

### 5.1 Load-Balanced Send

```c
/* Construct multipart message and send */
zlink_msg_t part;
zlink_msg_init_size(&part, 7);
memcpy(zlink_msg_data(&part), "request", 7);
zlink_gateway_send(client, &part, 1, 0);
```

### 5.2 Send to Specific Peer

```c
/* Send directly to a specific server by routing_id */
zlink_gateway_send_rid(client, &target_rid, &part, 1, 0);
```

### 5.3 Receiving Messages

Gateway stays in recv model. Pull replies or requests with
`zlink_gateway_recv()`.

#### Recv model (default)

```c
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
int rc = zlink_gateway_recv(client, &source_rid, &parts, &part_count, 0);
if (rc == 0) {
    printf("Reply: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

Use `zlink_recv_handler(gateway, ...)` only when you want callback-based
receive. In that mode, `zlink_gateway_recv()` / `zlink_recv()` and data-plane
`ZLINK_POLLIN` fail with `EBUSY`.

## 6. Load Balancing

| Strategy | Constant | Description |
|----------|----------|-------------|
| Round Robin | `ZLINK_GATEWAY_LB_ROUND_ROBIN` | Sequential selection (default) |
| Weighted | `ZLINK_GATEWAY_LB_WEIGHTED` | Weight-based (higher weight = higher selection probability) |

### Updating Weights

```c
/* Update weight for a specific peer */
zlink_gateway_update_peer_weight(server, &peer_rid, 5);
```

## 7. Thread-Safety

### Regular Sockets vs Gateway

| | General public socket handles | Gateway |
|---|---|---|
| **Thread safety** | Thread-safe by default | **Thread-safe** -- a single handle can be used concurrently from multiple threads |
| **Hot path** | `send` is the hot path | `send` / `send_rid` are the hot path |
| **Low-frequency path** | bind/connect/monitor/query are correctness-first serialized operations | attach/option/monitor/query are correctness-first serialized operations |
| **Shutdown** | `close` uses a fail-fast lifecycle gate | `destroy` uses a fail-fast lifecycle gate |

### Thread-safe API

Gateway is not "every API has the same cost model," but public handle APIs
are thread-safe by default.

- `zlink_gateway_send()`
- `zlink_gateway_send_rid()`
- `zlink_gateway_set_lb_strategy()`
- `zlink_set_option()`
- `zlink_gateway_attach_discovery()`
- `zlink_gateway_bind()`
- `zlink_gateway_connect()` / `zlink_gateway_disconnect()`
- `zlink_set_tls_client()` / `zlink_set_tls_server()`
- `zlink_get_option(gateway, ZLINK_OPT_LAST_ENDPOINT, ...)`
- `zlink_monitor_snapshot()` on an open gateway monitor
- `zlink_gateway_destroy()`

The rules users need to remember are short:

1. A Gateway handle may be shared across threads.
2. `send` / `send_rid` can be called concurrently from multiple threads.
3. Control-path APIs may still be called at runtime.
4. `destroy` is fail-fast: `EBUSY` when another admitted API exists,
   `ESHUTDOWN` for new entry after destroy is accepted.

### Multi-threaded Usage Example

```c
/* Gateway is thread-safe, so it can be shared across threads */
void *gateway = zlink_gateway_new(ctx);
zlink_set_routing_id(gateway, "gw-1", 4);
zlink_gateway_attach_discovery(gateway, discovery);

/* Worker thread function */
void *send_worker(void *arg) {
    void *gw = arg;
    zlink_msg_t part;
    zlink_msg_init_size(&part, 7);
    memcpy(zlink_msg_data(&part), "request", 7);
    /* Concurrent send calls from multiple threads -- safe */
    zlink_gateway_send(gw, &part, 1, 0);
    return NULL;
}

/* Concurrent sends from multiple threads */
for (int i = 0; i < 4; i++)
    zlink_thread_start(&send_worker, gateway);
```

### Advantages

**1. Low contention around the hot path**

Gateway's `send` / `send_rid` are designed as the high-frequency path and use
a different cost model from the control path.

**2. Simplified application architecture**

Gateway removes the need for an extra proxy layer when multiple application
threads need to send through the same logical handle.

```
Regular sockets (multi-threaded):
  Thread A ──┐
  Thread B ──┼── inproc queue ── dedicated I/O thread ── ROUTER socket
  Thread C ──┘

Gateway (multi-threaded):
  Thread A ──┐
  Thread B ──┼── Gateway ── send ──→ Server
  Thread C ──┘
```

**3. Discovery updates do not block sends**

Service pool updates (server add/remove, connect/reconnect) are handled
by a dedicated background worker thread. Even if a Discovery event arrives
during a send call, the user API is not blocked.

**4. Concurrent sends and weight updates are safe**

Multiple threads can send messages concurrently while a server
simultaneously updates weights via `zlink_gateway_update_peer_weight()`,
all without data races.

> Reference: `core/tests/discovery/test_gateway.cpp` --
> `test_gateway_concurrent_send_and_updates()`: verifies concurrent
> multi-thread sends + weight updates

> See [Thread-Safety Guide](11-thread-safety.md) for the full three-tier contract and additional patterns.

## 8. Automatic Connect/Disconnect

Gateway automatically connects to and disconnects from peers based on
Discovery events.

- Server added: auto-connect to new server
- Server removed: disconnect removed server

## Internal Module Structure

The Gateway internal implementation is split into responsibility-based
modules rather than a single file. The public C API remains unchanged;
internal changes stay within narrow boundaries.

| Module | Role |
|--------|------|
| `gateway_access` | API layer seam (service-local access) |
| `gateway_facade` | External API delegation |
| `gateway_lifecycle` | Create/destroy/attach sequencing |
| `gateway_pool` | Peer pool management, load balancing |
| `gateway_socket` | Internal ROUTER socket wiring |
| `gateway_monitor` | Service monitor event emission |
| `gateway_refresh` | Discovery-based peer refresh |

Multipart sends use the shared `multipart_send_txn` module to provide
whole-message guarantees (all-or-nothing).

## 9. End-to-End Example

### Recv model

```c
void *ctx = zlink_ctx_new();

/* === Registry === */
void *registry = zlink_registry_new(ctx);
zlink_registry_bind(registry, "tcp://*:5550", "tcp://*:5551");

/* === Server === */
void *server_discovery = zlink_discovery_new(ctx,
    ZLINK_SERVICE_TYPE_GATEWAY, "echo-service");
zlink_discovery_connect_registry(server_discovery, "tcp://127.0.0.1:5551");

void *server = zlink_gateway_new(ctx);
zlink_set_routing_id(server, "echo-server-1", 13);
zlink_gateway_attach_discovery(server, server_discovery);
zlink_gateway_bind(server, "tcp://*:5555");

/* === Client === */
void *client_discovery = zlink_discovery_new(ctx,
    ZLINK_SERVICE_TYPE_GATEWAY, "echo-service");
zlink_discovery_connect_registry(client_discovery, "tcp://127.0.0.1:5551");

void *client = zlink_gateway_new(ctx);
zlink_set_routing_id(client, "client-1", 8);
zlink_gateway_attach_discovery(client, client_discovery);

/* Wait for route readiness via gateway monitor */
/* (use ZLINK_GATEWAY_ROUTE_UP event or zlink_monitor_snapshot) */

/* Send request */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_gateway_send(client, &part, 1, 0);

/* ... on_request handler processes and replies ... */

/* Cleanup */
zlink_gateway_destroy(&client);
zlink_discovery_destroy(&client_discovery);
zlink_gateway_destroy(&server);
zlink_discovery_destroy(&server_discovery);
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

### Recv model

```c
void *ctx = zlink_ctx_new();

/* === Registry === */
void *registry = zlink_registry_new(ctx);
zlink_registry_bind(registry, "tcp://*:5550", "tcp://*:5551");

/* === Server (recv model) === */
void *server_discovery = zlink_discovery_new(ctx,
    ZLINK_SERVICE_TYPE_GATEWAY, "echo-service");
zlink_discovery_connect_registry(server_discovery, "tcp://127.0.0.1:5551");

void *server = zlink_gateway_new(ctx);
zlink_set_routing_id(server, "echo-server-1", 13);
zlink_gateway_attach_discovery(server, server_discovery);
zlink_gateway_bind(server, "tcp://*:5555");

/* === Client (recv model) === */
void *client_discovery = zlink_discovery_new(ctx,
    ZLINK_SERVICE_TYPE_GATEWAY, "echo-service");
zlink_discovery_connect_registry(client_discovery, "tcp://127.0.0.1:5551");

void *client = zlink_gateway_new(ctx);
zlink_set_routing_id(client, "client-1", 8);
zlink_gateway_attach_discovery(client, client_discovery);

/* Wait for route readiness via gateway monitor */

/* Send request */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_gateway_send(client, &part, 1, 0);

/* Pull reply */
zlink_routing_id_t source_rid;
zlink_msg_t *reply_parts = NULL;
size_t reply_count = 0;
zlink_gateway_recv(client, &source_rid, &reply_parts, &reply_count, 0);

/* Cleanup */
zlink_gateway_destroy(&client);
zlink_discovery_destroy(&client_discovery);
zlink_gateway_destroy(&server);
zlink_discovery_destroy(&server_discovery);
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

## 10. API Summary

| Function | Description |
|----------|-------------|
| `zlink_gateway_new(ctx)` | Create Gateway in recv model |
| `zlink_set_routing_id(gateway, data, size)` | Set routing ID before first bind/connect |
| `zlink_recv_handler(gateway, fn, userdata)` | Attach multipart receive callback; recv + `ZLINK_POLLIN` become `EBUSY` |
| `zlink_gateway_recv(gateway, &rid, &parts, &count, flags)` | Pull message in recv model |
| `zlink_gateway_attach_discovery(gateway, discovery)` | Attach Discovery |
| `zlink_gateway_bind(gateway, endpoint)` | Bind receive endpoint (server role) |
| `zlink_gateway_send(gateway, parts, count, flags)` | Send multipart message (with LB) |
| `zlink_gateway_send_rid(gateway, rid, parts, count, flags)` | Send to specific peer |
| `zlink_gateway_set_lb_strategy(gateway, strategy)` | Set LB strategy |
| `zlink_set_option(gateway, option, val, len)` | Set service options |
| `zlink_set_routing_id(gateway, data, size)` | Set routing ID |
| `zlink_get_routing_id(gateway, &out)` | Get routing ID |
| `zlink_set_tls_client(gateway, ca, host, trust)` | Set TLS client configuration |
| `zlink_set_tls_server(gateway, cert, key, require_client_cert)` | Set TLS server configuration |
| `zlink_get_option(gateway, ZLINK_OPT_LAST_ENDPOINT, buf, &size)` | Resolve bound endpoint |
| `zlink_monitor_snapshot(monitor, &snapshot)` | Read local bind/send readiness and queue depth |
| `zlink_gateway_update_peer_weight(gateway, rid, weight)` | Update peer weight |
| `zlink_registry_gateway_peers_query(registry, &filter, entries, &count)` | Query operational gateway-peer state |
| `zlink_gateway_destroy(&gateway)` | Destroy |

---
[← Discovery](07-1-discovery.md) | [SPOT →](07-3-spot.md)
