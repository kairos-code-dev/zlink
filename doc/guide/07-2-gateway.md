[English](07-2-gateway.md) | [한국어](07-2-gateway.ko.md)

# Gateway Service (Location-Transparent Request/Reply)

## 1. Overview

Gateway is a client component that sends messages to service Receivers in a location-transparent manner based on Discovery, and receives replies from Receivers. It handles load balancing and automatic connect/disconnect.

> **About the name**: Gateway serves as an entry point and client-side load balancer for a specific service. Unlike API Gateways (such as Kong or AWS API Gateway) that include authentication, rate limiting, and protocol translation, Gateway is a lightweight gateway focused on service access and load balancing.

**Gateway is thread-safe.** While regular zlink sockets (PAIR, DEALER, ROUTER, etc.) must be used from a single thread only, Gateway can be safely used concurrently from multiple threads through internal mutex protection.

## 2. Receiver Setup

```c
void *receiver = zlink_receiver_new(ctx, "payment-receiver-1");

/* Bind business socket */
zlink_receiver_bind(receiver, "tcp://*:5555");

/* Connect to Registry */
zlink_receiver_connect_registry(receiver, "tcp://registry1:5551");

/* Register service (advertise_endpoint auto-detected) */
zlink_receiver_register(receiver, "payment-service", NULL, 1);

/* Check registration result */
int status;
char resolved[256], error_msg[256];
zlink_receiver_register_result(receiver, "payment-service",
    &status, resolved, error_msg);
if (status != 0) {
    fprintf(stderr, "Registration failed: %s\n", error_msg);
    return -1;
}

/* Process business messages */
void *router = zlink_receiver_router(receiver);
/* Receive/reply [routing_id][msgId][payload...] from router */

zlink_receiver_destroy(&receiver);
```

### Endpoint Configuration

| bind_endpoint | advertise_endpoint | Result |
|---------------|-------------------|--------|
| `tcp://*:5555` | `NULL` | Local IP auto-detected |
| `tcp://*:5555` | `tcp://payment-server:5555` | Advertised with DNS name |

> In NAT/container environments, advertise_endpoint must be explicitly set.

## 3. Gateway Setup

```c
void *discovery = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5550");
zlink_discovery_subscribe(discovery, "payment-service");

void *gateway = zlink_gateway_new(ctx, discovery, "gateway-1");

/* Load balancing configuration */
zlink_gateway_set_lb_strategy(gateway, "payment-service",
    ZLINK_GATEWAY_LB_ROUND_ROBIN);
```

## 4. Sending Messages

### 4.1 Basic Send

```c
/* Send request from Gateway to Receiver */
zlink_msg_t req;
zlink_msg_init_data(&req, data, size, NULL, NULL);
zlink_gateway_send(gateway, "payment-service", &req, 1, 0);
```

### 4.2 Receiving Replies (Gateway)

```c
/* Receive Receiver reply at Gateway */
zlink_msg_t *parts = NULL;
size_t part_count = 0;
char service_name[256];
int rc = zlink_gateway_recv(gateway, &parts, &part_count, 0, service_name);
if (rc != -1) {
    /* Process parts[0..part_count-1] */
    zlink_msgv_close(parts, part_count);
}
```

### 4.3 Receiving/Replying on the Receiver Side

```c
/* Receive and reply on the Receiver's ROUTER socket */
void *router = zlink_receiver_router(receiver);
/* Receive [routing_id][msgId][payload...] and process reply */
```

## 5. Load Balancing

| Strategy | Constant | Description |
|----------|----------|-------------|
| Round Robin | `ZLINK_GATEWAY_LB_ROUND_ROBIN` | Sequential selection (default) |
| Weighted | `ZLINK_GATEWAY_LB_WEIGHTED` | Weight-based (higher weight = higher selection probability) |

### Updating Weights

```c
zlink_receiver_update_weight(receiver, "payment-service", 5);
```

## 6. Thread-Safety

### Regular Sockets vs Gateway

| | Regular Sockets (PAIR, DEALER, ROUTER, etc.) | Gateway |
|---|---|---|
| **Thread safety** | Single-thread use only | **Thread-safe** -- concurrent use from multiple threads |
| **External synchronization** | Application must synchronize in multi-threaded use | Not needed -- internal mutex protection |
| **Background work** | None | Background worker handles Discovery updates |

### Thread-safe API

All public Gateway APIs are protected by internal mutexes. They are safe to call concurrently from multiple threads.

- `zlink_gateway_send()`
- `zlink_gateway_send_rid()`
- `zlink_gateway_recv()`
- `zlink_gateway_set_lb_strategy()`
- `zlink_gateway_setsockopt()`
- `zlink_gateway_set_tls_client()`
- `zlink_gateway_connection_count()`

### Multi-threaded Usage Example

```c
/* Gateway is thread-safe, so it can be shared across threads */
void *gateway = zlink_gateway_new(ctx, discovery, "gw-1");

/* Worker thread function */
void *send_worker(void *arg) {
    void *gw = arg;
    zlink_msg_t req;
    zlink_msg_init_data(&req, "request", 7, NULL, NULL);
    /* Concurrent send calls from multiple threads -- safe */
    zlink_gateway_send(gw, "my-service", &req, 1, 0);
    return NULL;
}

/* Concurrent sends from multiple threads */
for (int i = 0; i < 4; i++)
    zlink_threadstart(&send_worker, gateway);
```

### Advantages

**1. Low contention through send-only design**

Gateway's send/recv are protected by internal mutexes for thread-safety, and the design minimizes lock overhead.

**2. Simplified application architecture**

Regular sockets require single-thread ownership, so multi-threaded environments need separate message queues or proxy patterns. Gateway eliminates this extra complexity by allowing multiple threads to call send directly.

```
Regular sockets (multi-threaded):
  Thread A ──┐
  Thread B ──┼── inproc queue ── dedicated I/O thread ── ROUTER socket
  Thread C ──┘

Gateway (multi-threaded):
  Thread A ──┐
  Thread B ──┼── Gateway (internal mutex protection) ── send ──→ Receiver
  Thread C ──┘
```

**3. Discovery updates do not block sends**

Service pool updates (Receiver add/remove, connect/reconnect) are handled by a dedicated background worker thread. Even if a Discovery event arrives during a send call, the user API is not blocked.

**4. Concurrent sends and weight updates are safe**

Multiple threads can send messages concurrently while a Receiver simultaneously updates weights via `zlink_receiver_update_weight()`, all without data races.

> Reference: `core/tests/discovery/test_gateway.cpp` -- `test_gateway_concurrent_send_and_updates()`: verifies concurrent multi-thread sends + weight updates

## 7. Automatic Connect/Disconnect

Gateway automatically connects to and disconnects from Receivers based on Discovery events.

- `RECEIVER_ADDED`: ROUTER connect to new Receiver
- `RECEIVER_REMOVED`: Disconnect removed Receiver

## 8. End-to-End Example

```c
void *ctx = zlink_ctx_new();

/* === Registry === */
void *registry = zlink_registry_new(ctx);
zlink_registry_set_endpoints(registry, "tcp://*:5550", "tcp://*:5551");
zlink_registry_start(registry);

/* === Receiver === */
void *receiver = zlink_receiver_new(ctx, "echo-receiver-1");
zlink_receiver_bind(receiver, "tcp://*:5555");
zlink_receiver_connect_registry(receiver, "tcp://127.0.0.1:5551");
zlink_receiver_register(receiver, "echo-service", NULL, 1);

/* === Client === */
void *discovery = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_GATEWAY);
zlink_discovery_connect_registry(discovery, "tcp://127.0.0.1:5550");
zlink_discovery_subscribe(discovery, "echo-service");

void *gateway = zlink_gateway_new(ctx, discovery, "client-1");

/* Wait for service availability */
while (!zlink_discovery_service_available(discovery, "echo-service"))
    sleep(1);

/* Request/Reply */
zlink_msg_t req;
zlink_msg_init_data(&req, "hello", 5, NULL, NULL);
zlink_gateway_send(gateway, "echo-service", &req, 1, 0);

/* ... Receiver processes and replies ... */

/* Cleanup */
zlink_gateway_destroy(&gateway);
zlink_discovery_destroy(&discovery);
zlink_receiver_destroy(&receiver);
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

## 9. API Summary

### Gateway API
| Function | Description |
|----------|-------------|
| `zlink_gateway_new(ctx, discovery, routing_id)` | Create Gateway |
| `zlink_gateway_send(...)` | Send message (with LB) |
| `zlink_gateway_recv(...)` | Receive message (Receiver reply) |
| `zlink_gateway_send_rid(...)` | Send to specific Receiver |
| `zlink_gateway_set_lb_strategy(...)` | Set LB strategy |
| `zlink_gateway_setsockopt(...)` | Set socket options |
| `zlink_gateway_set_tls_client(...)` | Set TLS client configuration |
| `zlink_gateway_router(...)` | Get ROUTER socket |
| `zlink_gateway_connection_count(...)` | Get connected Receiver count |
| `zlink_gateway_destroy(...)` | Destroy |

### Receiver API
| Function | Description |
|----------|-------------|
| `zlink_receiver_new(ctx, routing_id)` | Create Receiver |
| `zlink_receiver_bind(...)` | ROUTER bind |
| `zlink_receiver_connect_registry(...)` | Connect to Registry |
| `zlink_receiver_register(...)` | Register service |
| `zlink_receiver_register_result(...)` | Check registration result |
| `zlink_receiver_update_weight(...)` | Update weight |
| `zlink_receiver_unregister(...)` | Unregister service |
| `zlink_receiver_set_tls_server(...)` | Set TLS server configuration |
| `zlink_receiver_setsockopt(...)` | Set socket options |
| `zlink_receiver_router(...)` | Get ROUTER socket |
| `zlink_receiver_destroy(...)` | Destroy |

---
[← Discovery](07-1-discovery.md) | [SPOT →](07-3-spot.md)
