# Service Discovery 기반 인프라

## 1. 개요

zlink Service Discovery는 마이크로서비스 환경에서
서비스 인스턴스를 동적으로 발견하고 연결하는 인프라를 제공한다.
Registry 클러스터 기반의 서비스 등록/발견 시스템이다.

### 핵심 개념

| 용어 | 설명 |
|------|------|
| **Registry** | 서비스 등록/해제 관리, 목록 브로드캐스트 (PUB+ROUTER) |
| **Discovery** | Registry 구독, 서비스 목록 관리 (SUB); 연결된 서비스의 lifecycle owner |
| **소켓 패밀리** | Discovery를 통해 피어를 등록·발견하는 raw ROUTER/DEALER/PUB/SUB 소켓 |
| **서비스 역할** | 소켓 패밀리 모드에서 피어 매칭에 사용되는 소켓 수준 역할 (ROUTER/DEALER/PUB/SUB) |
| **Heartbeat** | 서비스 생존 확인 (5초 주기, 15초 타임아웃) |

### 아키텍처

```
┌──────────────────────────────────────────┐
│            Registry Cluster               │
│  Registry1(PUB+ROUTER) ◄──► Registry2    │
│       │              ◄──► Registry3      │
│       │ (서비스 목록 브로드캐스트)         │
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

## 2. Registry 설정 및 실행

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

## 3. Discovery 사용

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

## 3.1 소켓 패밀리 Discovery

raw ROUTER/DEALER/PUB/SUB 소켓은 Discovery를 사용하여 자동 피어 발견과
lifecycle 관리를 할 수 있다. SPOT 추상화 없이 소켓 수준에서 위치투명
통신을 가능하게 한다.

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

**역할 매칭:** Discovery는 서비스 역할로 관련 원격 프로바이더를 결정한다.
PUB 소켓은 SUB 피어를 발견하고 그 반대도 마찬가지다. ROUTER와 DEALER는
서로를 발견한다. 이는 자동으로 동작한다 — 역할은 attach 시 소켓 타입에서
파생된다.

**Lifecycle:** 소켓이 연결되면 `connect`, `disconnect`, `unbind`, `close`
수동 호출이 실패한다. Discovery 인스턴스를 파괴하면 모든 연결된 소켓이
종료된다.

## 4. Liveness 및 Summary 업데이트

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
   │                            │         X            │ ← entry 만료 / LOST
```

- Registry visibility는 Discovery가 소유하는 heartbeat/topology uplink로
  유지됩니다.
- SPOT과 소켓 패밀리 서비스는 로컬 registration/summary 변경을 제출하지만,
  주기적 uplink cadence는 Discovery가 담당합니다.
- Registry summary는 eventually consistent한 coarse/global view이며,
  strict final readiness gate로 사용하면 안 됩니다.

## 5. Registry 클러스터 HA

- 3노드 클러스터 권장
- flooding 방식 동기화 — 각 Registry가 수신한 서비스 목록 변경을 나머지
  모든 Registry에게 재전파하여, 최종적으로 전체 노드가 동일 정보를 갖게 되는
  브로드캐스트 전파 기법 (각 Registry가 다른 Registry의 PUB 구독)
- Eventually Consistent: 모든 Registry가 동일 상태 수렴
- `registry_id` + `list_seq`로 중복/역전 업데이트 무시

**서비스 가시성:** Registry 클러스터에서 서비스 목록은 flooding으로 전파된다.
Discovery가 하나의 Registry에만 연결해도 피어 Registry에 등록된 서비스가
브로드캐스트에 포함되어 전체 클러스터의 서비스를 볼 수 있다. 여러 Registry에
`connect_registry()`하는 것은 서비스 가시성이 아닌 **HA(장애 대응)**를 위한
것이다.

### Discovery Failover

- Discovery는 하나 이상의 Registry control endpoint에 bootstrap 연결합니다.
- bootstrap metadata로 내부 broadcast/uplink 경로를 학습합니다.
- 한 Registry 노드가 실패해도 다른 bootstrap control endpoint를 통해
  계속 동작할 수 있습니다.

## 6. 다음 단계

- [SPOT PUB/SUB](07-3-spot.ko.md) — Discovery 기반 위치투명 발행/구독
- [Registry 가이드](07-4-registry.ko.md) — 클러스터 구성, 토폴로지 조회, 운영 패턴

---
[← 서비스 개요](07-0-services.ko.md) | [SPOT →](07-3-spot.ko.md)
