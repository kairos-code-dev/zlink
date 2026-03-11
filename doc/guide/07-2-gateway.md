[English](07-2-gateway.md) | [한국어](07-2-gateway.ko.md)

# Gateway Service (Location-Transparent Request/Reply)

## 1. Overview

Gateway is a unified service handle that automatically discovers services
based on Discovery, supports load-balanced message sending, and direct
callback-based receiving. A single Gateway handle can act as both a
client (sender) and a server (receiver).

> **About the name**: Gateway serves as an entry point and client-side load
> balancer for a specific service. Unlike API Gateways (such as Kong or AWS
> API Gateway) that include authentication, rate limiting, and protocol
> translation, Gateway is a lightweight gateway focused on service access
> and load balancing.

**Gateway is thread-safe.** While regular zlink sockets (PAIR, DEALER,
ROUTER, etc.) must be used from a single thread only, Gateway can be
safely used concurrently from multiple threads through internal mutex
protection.

## 2. Creating a Gateway

A Gateway fixes its service name, routing ID, and receive handler at
creation time.

```c
/* Define receive handler */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count)
{
    /* Process incoming message */
    printf("Received: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    /* parts are cleaned up automatically after handler returns */
}

void *gateway = zlink_gateway_new(ctx, "payment-service",
                                   "gateway-1", on_message);
```

## 3. Server (Receiver) Setup

To act as a server, bind an endpoint on the Gateway and register via
Discovery.

```c
void *ctx = zlink_ctx_new();

/* Discovery setup */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

/* Create Gateway (register receive handler) */
void *server = zlink_gateway_new(ctx, "payment-service",
                                  "payment-server-1", on_request);

/* Attach Discovery */
zlink_gateway_attach_discovery(server, discovery);

/* Bind business socket */
zlink_gateway_bind(server, "tcp://*:5555");
```

## 4. Client (Sender) Setup

```c
/* Discovery setup */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");
zlink_discovery_subscribe(discovery, "payment-service");

/* Create Gateway */
void *client = zlink_gateway_new(ctx, "payment-service",
                                  "client-1", on_reply);

/* Attach Discovery */
zlink_gateway_attach_discovery(client, discovery);

/* Load balancing configuration */
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

Receives are dispatched automatically through the handler callback
registered at creation time. There is no separate `recv()` call.

```c
void on_reply(const zlink_routing_id_t *source_rid,
              zlink_msg_t *parts, size_t part_count)
{
    /* Process reply */
}
```

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

| | Regular Sockets (PAIR, DEALER, ROUTER, etc.) | Gateway |
|---|---|---|
| **Thread safety** | Single-thread use only | **Thread-safe** -- concurrent use from multiple threads |
| **External synchronization** | Application must synchronize in multi-threaded use | Not needed -- internal mutex protection |
| **Background work** | None | Background worker handles Discovery updates |

### Thread-safe API

All public Gateway APIs are protected by internal mutexes. They are safe
to call concurrently from multiple threads.

- `zlink_gateway_send()`
- `zlink_gateway_send_rid()`
- `zlink_gateway_set_lb_strategy()`
- `zlink_gateway_set_option()`
- `zlink_gateway_set_tls_client()`
- `zlink_gateway_connection_count()`

### Multi-threaded Usage Example

```c
/* Gateway is thread-safe, so it can be shared across threads */
void *gateway = zlink_gateway_new(ctx, "my-service", "gw-1", on_reply);
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

**1. Low contention through send-only design**

Gateway's send is protected by internal mutexes for thread-safety, and the
design minimizes lock overhead.

**2. Simplified application architecture**

Regular sockets require single-thread ownership, so multi-threaded
environments need separate message queues or proxy patterns. Gateway
eliminates this extra complexity by allowing multiple threads to call send
directly.

```
Regular sockets (multi-threaded):
  Thread A ──┐
  Thread B ──┼── inproc queue ── dedicated I/O thread ── ROUTER socket
  Thread C ──┘

Gateway (multi-threaded):
  Thread A ──┐
  Thread B ──┼── Gateway (internal mutex protection) ── send ──→ Server
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

## 8. Automatic Connect/Disconnect

Gateway automatically connects to and disconnects from peers based on
Discovery events.

- Server added: auto-connect to new server
- Server removed: disconnect removed server

## 9. End-to-End Example

```c
void *ctx = zlink_ctx_new();

/* === Registry === */
void *registry = zlink_registry_new(ctx);
zlink_registry_set_endpoints(registry, "tcp://*:5550", "tcp://*:5551");
zlink_registry_start(registry);

/* === Server === */
void *server_discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(server_discovery, "tcp://127.0.0.1:5551");

void *server = zlink_gateway_new(ctx, "echo-service",
                                  "echo-server-1", on_request);
zlink_gateway_attach_discovery(server, server_discovery);
zlink_gateway_bind(server, "tcp://*:5555");

/* === Client === */
void *client_discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(client_discovery, "tcp://127.0.0.1:5551");
zlink_discovery_subscribe(client_discovery, "echo-service");

void *client = zlink_gateway_new(ctx, "echo-service",
                                  "client-1", on_reply);
zlink_gateway_attach_discovery(client, client_discovery);

/* Wait for service availability */
while (!zlink_discovery_service_available(client_discovery, "echo-service"))
    msleep(100);

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

## 10. API Summary

| Function | Description |
|----------|-------------|
| `zlink_gateway_new(ctx, service_name, routing_id, handler)` | Create Gateway |
| `zlink_gateway_attach_discovery(gateway, discovery)` | Attach Discovery |
| `zlink_gateway_bind(gateway, endpoint)` | Bind receive endpoint (server role) |
| `zlink_gateway_send(gateway, parts, count, flags)` | Send multipart message (with LB) |
| `zlink_gateway_send_rid(gateway, rid, parts, count, flags)` | Send to specific peer |
| `zlink_gateway_set_lb_strategy(gateway, strategy)` | Set LB strategy |
| `zlink_gateway_set_option(gateway, option, val, len)` | Set service options |
| `zlink_gateway_set_routing_id(gateway, data, size)` | Set routing ID |
| `zlink_gateway_routing_id(gateway, out)` | Get routing ID |
| `zlink_gateway_set_tls_client(gateway, ca, host, trust)` | Set TLS client configuration |
| `zlink_gateway_set_tls_server(gateway, cert, key)` | Set TLS server configuration |
| `zlink_gateway_last_endpoint(gateway, buf, size)` | Resolve bound endpoint |
| `zlink_gateway_connection_count(gateway)` | Get connected peer count |
| `zlink_gateway_update_peer_weight(gateway, rid, weight)` | Update peer weight |
| `zlink_gateway_router_peers(gateway, peers, count)` | List peers |
| `zlink_gateway_peer_info(gateway, rid, info)` | Get specific peer info |
| `zlink_gateway_destroy(&gateway)` | Destroy |

---
[← Discovery](07-1-discovery.md) | [SPOT →](07-3-spot.md)
