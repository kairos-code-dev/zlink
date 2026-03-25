[English](07-1-discovery.md) | [한국어](07-1-discovery.ko.md)

# Service Discovery Foundation Infrastructure

## 1. Overview

zlink Service Discovery provides the infrastructure to dynamically discover and connect to service instances in a microservices environment. It is a service registration/discovery system based on a Registry cluster.

### Core Concepts

| Term | Description |
|------|-------------|
| **Registry** | Manages service registration/deregistration, broadcasts service list (PUB+ROUTER) |
| **Discovery** | Subscribes to Registry, manages service list (SUB); lifecycle owner for attached services |
| **Socket Family** | Raw ROUTER/DEALER/PUB/SUB sockets that register and discover peers via Discovery |
| **Service Role** | Socket-level role (ROUTER/DEALER/PUB/SUB) used for peer matching in socket family mode |
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
   ┌────┴─────────────────────────────┐
   │           Discovery (SUB)         │
   │  ┌──────────┬─────────────────┐  │
   │  │ SPOT     │ Socket Family   │  │
   │  │(PUB+SUB) │ (R/D/P/S)      │  │
   │  └──────────┴─────────────────┘  │
   └───────────────────────────────────┘
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
/* service_type: ZLINK_SERVICE_TYPE_SPOT or ZLINK_SERVICE_TYPE_SOCKET */
void *discovery = zlink_discovery_new(ctx,
    ZLINK_SERVICE_TYPE_SPOT, "order-service");

/* Connect to Registry bootstrap/control endpoint (multiple allowed) */
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");
zlink_discovery_connect_registry(discovery, "tcp://registry2:5551");

/* Observe service state via monitor */
zlink_service_monitor_open_options_t opts = {
    .events = ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP
            | ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED,
};
void *mon = zlink_service_monitor_open(discovery, &opts);
zlink_service_monitor_handler(mon, on_discovery_event, NULL);

/* ... Discovery delivers events through the callback ... */

/* Cleanup */
zlink_monitor_close(&mon);
zlink_discovery_destroy(&discovery);
```

## 3.1 Socket Family Discovery

Raw ROUTER/DEALER/PUB/SUB sockets can use Discovery for automatic peer
discovery and lifecycle management. This enables location-transparent
communication at the socket level without the SPOT abstraction.

```c
/* Create Discovery with SOCKET type */
void *discovery = zlink_discovery_new(ctx,
    ZLINK_SERVICE_TYPE_SOCKET, "price-feed");
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

/* Create a PUB socket and attach it to Discovery */
void *pub = zlink_socket_new(ctx, ZLINK_PUB);
zlink_bind(pub, "tcp://*:9100");
zlink_socket_attach_discovery(pub, discovery);
/* Discovery registers the PUB endpoint and manages heartbeats.
   Remote SUB sockets in the same service ("price-feed") will
   automatically discover and connect to this endpoint. */

/* ... publish messages ... */

/* Destroy Discovery to shut down the attached socket */
zlink_discovery_destroy(&discovery);
```

**Role matching:** Discovery uses service roles to determine which remote
providers are relevant. A PUB socket discovers SUB peers and vice versa.
ROUTER and DEALER discover each other. This is automatic -- the role is
derived from the socket type at attach time.

**Lifecycle:** Once a socket is attached, manual `connect`, `disconnect`,
`unbind`, and `close` calls fail. Destroying the Discovery instance
terminates all attached sockets.

## 4. Liveness and Summary Updates

```
SpotNode/Socket             Discovery               Registry
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
- SPOT and socket family services submit local registration/summary
  changes, but Discovery owns the periodic uplink cadence.
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

- [SPOT PUB/SUB](07-3-spot.md) -- Discovery-based location-transparent publish/subscribe
- [Registry Guide](07-4-registry.md) -- Cluster setup, topology introspection, and operational patterns

---
[← Services Overview](07-0-services.md) | [SPOT →](07-3-spot.md)
