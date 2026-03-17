[English](07-1-discovery.md) | [한국어](07-1-discovery.ko.md)

# Service Discovery Foundation Infrastructure

## 1. Overview

zlink Service Discovery provides the infrastructure to dynamically discover and connect to service instances in a microservices environment. It is a service registration/discovery system based on a Registry cluster.

### Core Concepts

| Term | Description |
|------|-------------|
| **Registry** | Manages service registration/deregistration, broadcasts service list (PUB+ROUTER) |
| **Discovery** | Subscribes to Registry, manages service list (SUB) |
| **Gateway (server)** | Server-side Gateway, registers with Registry via Discovery |
| **Heartbeat** | Service liveness check (5-second interval, 15-second timeout) |

### Architecture

```
┌──────────────────────────────────────────┐
│            Registry Cluster               │
│  Registry1(PUB+ROUTER) ◄──► Registry2    │
│       │              ◄──► Registry3      │
│       │ (service list broadcast)          │
└───────┼──────────────────────────────────┘
        │
   ┌────┴────┐
   │Discovery│
   │ (SUB)   │
   │    │    │
   │    ▼    │
   │ Gateway │  (client or server via bind)
   │(ROUTER) │
   └─────────┘
```

## 2. Registry Setup and Execution

```c
void *ctx = zlink_ctx_new();
void *registry = zlink_registry_new(ctx);

/* Add cluster peers (optional, must be called before bind) */
zlink_registry_add_peer(registry, "tcp://registry2:5550");
zlink_registry_add_peer(registry, "tcp://registry3:5550");

/* Heartbeat configuration (optional, must be called before bind) */
zlink_registry_set_heartbeat(registry, 5000, 15000);

/* Broadcast interval (optional, default 30 seconds) */
zlink_registry_set_broadcast_interval(registry, 30000);

/* Bind and start
   First arg:  PUB endpoint — broadcasts service list (Discovery SUB subscribes)
   Second arg: ROUTER endpoint — receives registration/heartbeat/queries (Discovery bootstraps here) */
zlink_registry_bind(registry,
    "tcp://*:5550",    /* PUB (service list broadcast) */
    "tcp://*:5551"     /* ROUTER (registration/heartbeat/queries) */
);

/* ... application logic ... */

/* Shutdown */
zlink_registry_destroy(&registry);
zlink_ctx_term(ctx);
```

## 3. Using Discovery

```c
/* service_type: ZLINK_SERVICE_TYPE_GATEWAY or ZLINK_SERVICE_TYPE_SPOT */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_GATEWAY);

/* Connect to Registry bootstrap/control endpoint (multiple allowed) */
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");
zlink_discovery_connect_registry(discovery, "tcp://registry2:5551");

/* Observe service state via monitor */
void *mon = zlink_discovery_monitor_open(
    discovery,
    ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP
      | ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED,
    on_discovery_event, NULL);

/* ... Discovery delivers events through the callback ... */

/* Cleanup */
zlink_service_monitor_close(&mon);
zlink_discovery_destroy(&discovery);
```

## 4. Liveness and Summary Updates

```
Gateway/SpotNode            Discovery               Registry
   │  REGISTER / summary        │                      │
   │──────────────────────────► │                      │
   │                            │ bootstrap + uplink   │
   │                            │─────────────────────►│
   │                            │  heartbeat / summary │
   │                            │─────────────────────►│
   │                            │                      │
   │                            │ (summary timeout)    │
   │                            │         X            │ ← entry ages out / LOST
```

- Registry visibility is maintained through Discovery-owned heartbeat/topology
  uplink.
- Gateway and Spot services still submit local registration/summary changes,
  but Discovery owns the periodic uplink cadence.
- Registry summary is eventually consistent and should be treated as a
  coarse/global view, not a strict final readiness gate.

## 5. Registry Cluster HA

- 3-node cluster recommended
- Flooding-based synchronization (each Registry subscribes to other Registries' PUB)
- Eventually Consistent: all Registries converge to the same state
- Duplicate/out-of-order updates ignored via `registry_id` + `list_seq`

**Service Visibility:** In a Registry cluster, the service list is propagated
via flooding. Even if a Discovery connects to only one Registry, services
registered on peer Registries are included in that Registry's broadcast,
so the full cluster's services are visible. Connecting to multiple Registries
via `connect_registry()` is for **HA (failover)**, not for service visibility.

### Discovery Failover

- Discovery bootstraps against one or more Registry control endpoints
- It learns the internal broadcast/uplink paths from bootstrap metadata
- If one Registry node fails, Discovery can continue using other configured
  bootstrap control endpoints

## 6. Next Steps

- [Gateway Service](07-2-gateway.md) -- Discovery-based location-transparent request/reply
- [SPOT PUB/SUB](07-3-spot.md) -- Discovery-based location-transparent publish/subscribe
- [Registry Guide](07-4-registry.md) -- Cluster setup, topology introspection, and operational patterns

---
[← Services Overview](07-0-services.md) | [Gateway →](07-2-gateway.md)
