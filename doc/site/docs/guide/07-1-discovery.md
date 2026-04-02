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

=== "C"

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

    /* Bind and start */
    zlink_registry_bind(registry,
        "tcp://*:5550",    /* PUB (service list broadcast) */
        "tcp://*:5551"     /* ROUTER (registration/heartbeat/queries) */
    );

    /* ... application logic ... */

    zlink_registry_destroy(&registry);
    zlink_ctx_term(ctx);
    ```

=== "C++"

    ```cpp
    auto ctx = zlink::context();
    auto registry = zlink::registry(ctx);

    registry.add_peer("tcp://registry2:5550");
    registry.add_peer("tcp://registry3:5550");
    registry.set_heartbeat(5000, 15000);
    registry.set_broadcast_interval(30000);
    registry.bind("tcp://*:5550", "tcp://*:5551");

    // ... application logic ...

    registry.close();
    ctx.close();
    ```

=== "Java"

    ```java
    var ctx = Zlink.contextNew();
    var registry = ctx.registryNew();

    registry.addPeer("tcp://registry2:5550");
    registry.addPeer("tcp://registry3:5550");
    registry.setHeartbeat(5000, 15000);
    registry.setBroadcastInterval(30000);
    registry.bind("tcp://*:5550", "tcp://*:5551");

    // ... application logic ...

    registry.destroy();
    ctx.term();
    ```

=== "Python"

    ```python
    ctx = zlink.Context()
    registry = zlink.Registry(ctx)

    registry.add_peer("tcp://registry2:5550")
    registry.add_peer("tcp://registry3:5550")
    registry.set_heartbeat(5000, 15000)
    registry.set_broadcast_interval(30000)
    registry.bind("tcp://*:5550", "tcp://*:5551")

    # ... application logic ...

    registry.destroy()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    const ctx = new zlink.Context();
    const registry = new zlink.Registry(ctx);

    registry.addPeer("tcp://registry2:5550");
    registry.addPeer("tcp://registry3:5550");
    registry.setHeartbeat(5000, 15000);
    registry.setBroadcastInterval(30000);
    registry.bind("tcp://*:5550", "tcp://*:5551");

    // ... application logic ...

    registry.destroy();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using var ctx = new ZlinkContext();
    using var registry = new Registry(ctx);

    registry.AddPeer("tcp://registry2:5550");
    registry.AddPeer("tcp://registry3:5550");
    registry.SetHeartbeat(5000, 15000);
    registry.SetBroadcastInterval(30000);
    registry.Bind("tcp://*:5550", "tcp://*:5551");

    // ... application logic ...
    ```

=== "Rust"

    ```rust
    let ctx = zlink::Context::new()?;
    let registry = zlink::Registry::new(&ctx)?;

    registry.add_peer("tcp://registry2:5550")?;
    registry.add_peer("tcp://registry3:5550")?;
    registry.set_heartbeat(5000, 15000)?;
    registry.set_broadcast_interval(30000)?;
    registry.bind("tcp://*:5550", "tcp://*:5551")?;

    // ... application logic ...

    registry.destroy()?;
    ctx.term()?;
    ```

=== "Go"

    ```go
    ctx, err := zlink.NewContext()
    if err != nil { log.Fatal(err) }
    registry, err := zlink.NewRegistry(ctx)
    if err != nil { log.Fatal(err) }

    registry.AddPeer("tcp://registry2:5550")
    registry.AddPeer("tcp://registry3:5550")
    registry.SetHeartbeat(5000, 15000)
    registry.SetBroadcastInterval(30000)
    registry.Bind("tcp://*:5550", "tcp://*:5551")

    // ... application logic ...

    registry.Destroy()
    ctx.Term()
    ```

## 3. Using Discovery

=== "C"

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

    zlink_monitor_close(&mon);
    zlink_discovery_destroy(&discovery);
    ```

=== "C++"

    ```cpp
    auto discovery = zlink::discovery(ctx,
        zlink::service_type::spot, "order-service");

    discovery.connect_registry("tcp://registry1:5551");
    discovery.connect_registry("tcp://registry2:5551");

    auto mon = discovery.service_monitor_open(
        {zlink::discovery_monitor_event::service_up
         | zlink::discovery_monitor_event::providers_changed});
    mon.set_handler(on_discovery_event);

    // ... Discovery delivers events through the callback ...

    mon.close();
    discovery.close();
    ```

=== "Java"

    ```java
    var discovery = ctx.discoveryNew(ServiceType.SPOT, "order-service");

    discovery.connectRegistry("tcp://registry1:5551");
    discovery.connectRegistry("tcp://registry2:5551");

    var opts = new ServiceMonitorOptions(
        DiscoveryMonitorEvent.SERVICE_UP | DiscoveryMonitorEvent.PROVIDERS_CHANGED);
    var mon = discovery.serviceMonitorOpen(opts);
    mon.setHandler(this::onDiscoveryEvent);

    // ... Discovery delivers events through the callback ...

    mon.close();
    discovery.destroy();
    ```

=== "Python"

    ```python
    discovery = zlink.Discovery(ctx, zlink.SERVICE_TYPE_SPOT, "order-service")

    discovery.connect_registry("tcp://registry1:5551")
    discovery.connect_registry("tcp://registry2:5551")

    opts = zlink.ServiceMonitorOptions(
        events=zlink.DISCOVERY_MONITOR_EVENT_SERVICE_UP
               | zlink.DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED)
    mon = discovery.service_monitor_open(opts)
    mon.set_handler(on_discovery_event)

    # ... Discovery delivers events through the callback ...

    mon.close()
    discovery.destroy()
    ```

=== "Node/TypeScript"

    ```typescript
    const discovery = new zlink.Discovery(ctx,
        zlink.SERVICE_TYPE_SPOT, "order-service");

    discovery.connectRegistry("tcp://registry1:5551");
    discovery.connectRegistry("tcp://registry2:5551");

    const mon = discovery.serviceMonitorOpen({
        events: zlink.DISCOVERY_MONITOR_EVENT_SERVICE_UP
              | zlink.DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED
    });
    mon.setHandler(onDiscoveryEvent);

    // ... Discovery delivers events through the callback ...

    mon.close();
    discovery.destroy();
    ```

=== "C#/.NET"

    ```csharp
    using var discovery = new Discovery(ctx, ServiceType.Spot, "order-service");

    discovery.ConnectRegistry("tcp://registry1:5551");
    discovery.ConnectRegistry("tcp://registry2:5551");

    using var mon = discovery.ServiceMonitorOpen(new ServiceMonitorOptions {
        Events = DiscoveryMonitorEvent.ServiceUp
               | DiscoveryMonitorEvent.ProvidersChanged });
    mon.SetHandler(OnDiscoveryEvent);

    // ... Discovery delivers events through the callback ...
    ```

=== "Rust"

    ```rust
    let discovery = zlink::Discovery::new(&ctx,
        zlink::ServiceType::Spot, "order-service")?;

    discovery.connect_registry("tcp://registry1:5551")?;
    discovery.connect_registry("tcp://registry2:5551")?;

    let mon = discovery.service_monitor_open(&zlink::ServiceMonitorOptions::new(
        zlink::DISCOVERY_MONITOR_EVENT_SERVICE_UP
        | zlink::DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED))?;
    mon.set_handler(on_discovery_event);

    // ... Discovery delivers events through the callback ...

    mon.close();
    discovery.destroy()?;
    ```

=== "Go"

    ```go
    discovery, err := zlink.NewDiscovery(ctx,
        zlink.ServiceTypeSpot, "order-service")
    if err != nil { log.Fatal(err) }

    discovery.ConnectRegistry("tcp://registry1:5551")
    discovery.ConnectRegistry("tcp://registry2:5551")

    opts := zlink.ServiceMonitorOptions{Events:
        zlink.DiscoveryMonitorEventServiceUp |
        zlink.DiscoveryMonitorEventProvidersChanged}
    mon, err := discovery.ServiceMonitorOpen(opts)
    if err != nil { log.Fatal(err) }
    mon.SetHandler(onDiscoveryEvent)

    // ... Discovery delivers events through the callback ...

    mon.Close()
    discovery.Destroy()
    ```

## 3.1 Socket Family Discovery

Raw ROUTER/DEALER/PUB/SUB sockets can use Discovery for automatic peer
discovery and lifecycle management. This enables location-transparent
communication at the socket level without the SPOT abstraction.

=== "C"

    ```c
    /* Create Discovery with SOCKET type */
    void *discovery = zlink_discovery_new(ctx,
        ZLINK_SERVICE_TYPE_SOCKET, "price-feed");
    zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

    /* Create a PUB socket and attach it to Discovery */
    void *pub = zlink_socket_new(ctx, ZLINK_PUB);
    zlink_bind(pub, "tcp://*:9100");
    zlink_socket_attach_discovery(pub, discovery);
    /* Discovery registers the PUB endpoint and manages heartbeats. */

    /* ... publish messages ... */

    zlink_discovery_destroy(&discovery);
    ```

=== "C++"

    ```cpp
    auto discovery = zlink::discovery(ctx,
        zlink::service_type::socket, "price-feed");
    discovery.connect_registry("tcp://registry1:5551");

    auto pub = zlink::socket(ctx, zlink::socket_type::pub_);
    pub.bind("tcp://*:9100");
    pub.attach_discovery(discovery);

    // ... publish messages ...

    discovery.close();
    ```

=== "Java"

    ```java
    var discovery = ctx.discoveryNew(ServiceType.SOCKET, "price-feed");
    discovery.connectRegistry("tcp://registry1:5551");

    var pub = ctx.socket(SocketType.PUB);
    pub.bind("tcp://*:9100");
    pub.attachDiscovery(discovery);

    // ... publish messages ...

    discovery.destroy();
    ```

=== "Python"

    ```python
    discovery = zlink.Discovery(ctx, zlink.SERVICE_TYPE_SOCKET, "price-feed")
    discovery.connect_registry("tcp://registry1:5551")

    pub = ctx.socket(zlink.PUB)
    pub.bind("tcp://*:9100")
    pub.attach_discovery(discovery)

    # ... publish messages ...

    discovery.destroy()
    ```

=== "Node/TypeScript"

    ```typescript
    const discovery = new zlink.Discovery(ctx,
        zlink.SERVICE_TYPE_SOCKET, "price-feed");
    discovery.connectRegistry("tcp://registry1:5551");

    const pub = ctx.socket(zlink.PUB);
    pub.bind("tcp://*:9100");
    pub.attachDiscovery(discovery);

    // ... publish messages ...

    discovery.destroy();
    ```

=== "C#/.NET"

    ```csharp
    using var discovery = new Discovery(ctx, ServiceType.Socket, "price-feed");
    discovery.ConnectRegistry("tcp://registry1:5551");

    using var pub = ctx.CreateSocket(SocketType.Pub);
    pub.Bind("tcp://*:9100");
    pub.AttachDiscovery(discovery);

    // ... publish messages ...
    ```

=== "Rust"

    ```rust
    let discovery = zlink::Discovery::new(&ctx,
        zlink::ServiceType::Socket, "price-feed")?;
    discovery.connect_registry("tcp://registry1:5551")?;

    let pub_sock = ctx.socket(zlink::PUB)?;
    pub_sock.bind("tcp://*:9100")?;
    pub_sock.attach_discovery(&discovery)?;

    // ... publish messages ...

    discovery.destroy()?;
    ```

=== "Go"

    ```go
    discovery, err := zlink.NewDiscovery(ctx,
        zlink.ServiceTypeSocket, "price-feed")
    if err != nil { log.Fatal(err) }
    discovery.ConnectRegistry("tcp://registry1:5551")

    pubSock, err := ctx.PubSocket()
    if err != nil { log.Fatal(err) }
    pubSock.Bind("tcp://*:9100")
    pubSock.AttachDiscovery(discovery)

    // ... publish messages ...

    discovery.Destroy()
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
