[English](07-1-discovery.md) | [한국어](07-1-discovery.ko.md)

# Service Discovery Foundation Infrastructure

## 1. Overview

zlink Service Discovery provides the infrastructure to dynamically discover and connect to service instances in a microservices environment. It is a service registration/discovery system based on a Registry cluster.

### Core Concepts

| Term | Description |
|------|-------------|
| **Registry** | Manages service registration/deregistration, broadcasts service list (PUB+ROUTER) |
| **Discovery** | Subscribes to Registry, manages service list (SUB) |
| **Receiver** | Service receiver, registers with Registry (DEALER+ROUTER) |
| **Heartbeat** | Receiver liveness check (5-second interval, 15-second timeout) |

### Architecture

```
┌──────────────────────────────────────────┐
│            Registry Cluster               │
│  Registry1(PUB+ROUTER) ◄──► Registry2    │
│       │              ◄──► Registry3      │
│       │ (service list broadcast)          │
└───────┼──────────────────────────────────┘
        │
   ┌────┴────┐      ┌──────────┐
   │Discovery│      │ Receiver │
   │ (SUB)   │      │(DEALER+  │
   │    │    │      │ ROUTER)  │
   │    ▼    │      └──────────┘
   │ Gateway │
   │(ROUTER) │
   └─────────┘
```

## 2. Registry Setup and Execution

```c
void *ctx = zlink_ctx_new();
void *registry = zlink_registry_new(ctx);

/* Endpoint configuration */
zlink_registry_set_endpoints(registry,
    "tcp://*:5550",    /* PUB (broadcast) */
    "tcp://*:5551"     /* ROUTER (registration/heartbeat) */
);

/* Add cluster peers (optional) */
zlink_registry_add_peer(registry, "tcp://registry2:5550");
zlink_registry_add_peer(registry, "tcp://registry3:5550");

/* Heartbeat configuration (optional) */
zlink_registry_set_heartbeat(registry, 5000, 15000);

/* Broadcast interval (optional, default 30 seconds) */
zlink_registry_set_broadcast_interval(registry, 30000);

/* Start */
zlink_registry_start(registry);

/* ... application logic ... */

/* Shutdown */
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

## 3. Using Discovery

```c
/* service_type: ZLINK_SERVICE_TYPE_GATEWAY or ZLINK_SERVICE_TYPE_SPOT */
void *discovery = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_GATEWAY);

/* Connect to Registry (multiple allowed) */
zlink_discovery_connect_registry(discovery, "tcp://registry1:5550");
zlink_discovery_connect_registry(discovery, "tcp://registry2:5550");

/* Subscribe to a service */
zlink_discovery_subscribe(discovery, "payment-service");

/* Check service availability */
while (!zlink_discovery_service_available(discovery, "payment-service")) {
    printf("Waiting...\n");
    sleep(1);
}

/* Query receiver list */
zlink_receiver_info_t receivers[10];
size_t count = 10;
zlink_discovery_get_receivers(discovery, "payment-service",
                              receivers, &count);
for (size_t i = 0; i < count; i++) {
    printf("Receiver: %s (weight=%u)\n",
           receivers[i].endpoint, receivers[i].weight);
}

/* Query receiver count */
int n = zlink_discovery_receiver_count(discovery, "payment-service");

zlink_discovery_destroy(&discovery);
```

## 4. Heartbeat Mechanism

```
Receiver                    Registry
   │  REGISTER                 │
   │──────────────────────────►│
   │  REGISTER_ACK             │
   │◄──────────────────────────│
   │                           │
   │  HEARTBEAT (every 5s)     │
   │──────────────────────────►│
   │  HEARTBEAT (every 5s)     │
   │──────────────────────────►│
   │                           │
   │  (15s without heartbeat)  │
   │         X                 │ ← Receiver removed + broadcast
```

- Interval: 5 seconds (default, configurable)
- Timeout: 15 seconds (removed after 3 missed heartbeats)
- On removal, SERVICE_LIST is broadcast to all Discovery instances

## 5. Registry Cluster HA

- 3-node cluster recommended
- Flooding-based synchronization (each Registry subscribes to other Registries' PUB)
- Eventually Consistent: all Registries converge to the same state
- Duplicate/out-of-order updates ignored via `registry_id` + `list_seq`

### Receiver Failover

- Receiver sends REGISTER/HEARTBEAT to **only one Registry**
- On failure detection, switches to the next Registry and re-registers
- Exponential backoff: 200ms to max 5s (with +/-20% jitter)
- Discovery subscribes to multiple Registry PUBs simultaneously, so the service list remains available even if one node fails

## 6. Next Steps

- [Gateway Service](07-2-gateway.md) -- Discovery-based location-transparent request/reply
- [SPOT PUB/SUB](07-3-spot.md) -- Discovery-based location-transparent publish/subscribe

---
[← Services Overview](07-0-services.md) | [Gateway →](07-2-gateway.md)
