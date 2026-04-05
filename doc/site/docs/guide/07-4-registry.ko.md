# Registry (중앙 서비스 디렉토리)

## 1. 개요

Registry는 zlink 서비스 계층의 중앙 서비스 디렉토리이자 토폴로지 요약 소스다.
SPOT 노드, 소켓 패밀리 서비스의 등록(Discovery를 통해)을 수락하고,
하트비트 기반 생존 확인을 관리하며,
집계된 서비스 목록을 연결된 Discovery에 주기적으로 브로드캐스트한다.

### 두 가지 사용 모드

| 모드 | 설명 |
|------|------|
| **독립 프로세스** | Registry를 전용 서비스로 실행. 여러 애플리케이션이 Discovery를 통해 연결. |
| **임베디드** | 애플리케이션 프로세스 내에 Registry를 Discovery, 서비스(SPOT/Socket)와 함께 직접 생성. |

**Registry는 thread-safe하다.**
하나의 Registry handle을 여러 스레드에서 동시에 사용할 수 있다.

- **구성 API** (`set_id`, `add_peer`, `set_heartbeat` 등): `bind` 전에 호출
- **조회 API** (`topology_snapshot`, `topology_query` 등): bind 이후 어떤 스레드에서든 호출 가능

## 2. Quick Start

Registry를 실행하고 Discovery를 통해 ROUTER 소켓을 연결하는 최소 예제.

=== "C"

    ```c
    void *ctx = zlink_ctx_new();

    /* === Registry === */
    void *registry = zlink_registry_new(ctx);
    /* PUB: service list broadcast, ROUTER: registration/heartbeat/queries */
    zlink_registry_bind(registry, "tcp://*:5550", "tcp://*:5551");

    /* === Discovery === */
    void *discovery = zlink_discovery_new(ctx,
        ZLINK_SERVICE_TYPE_SOCKET, "my-service");
    zlink_discovery_connect_registry(discovery, "tcp://127.0.0.1:5551");

    /* === ROUTER socket (server, Discovery-managed) === */
    void *server = zlink_socket_new(ctx, ZLINK_ROUTER);
    zlink_bind(server, "tcp://*:5555");
    zlink_socket_attach_discovery(server, discovery);

    /* ... application logic ... */

    /* Cleanup */
    zlink_discovery_destroy(&discovery);
    zlink_registry_destroy(&registry);
    zlink_ctx_term(ctx);
    ```

=== "C++"

    ```cpp
    auto ctx = zlink::context();

    // === Registry ===
    auto registry = zlink::registry(ctx);
    // PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    registry.bind("tcp://*:5550", "tcp://*:5551");

    // === Discovery ===
    auto discovery = zlink::discovery(ctx,
        zlink::service_type::socket, "my-service");
    discovery.connect_registry("tcp://127.0.0.1:5551");

    // === ROUTER socket (server, Discovery-managed) ===
    auto server = zlink::socket(ctx, zlink::socket_type::router);
    server.bind("tcp://*:5555");
    server.attach_discovery(discovery);

    // ... application logic ...

    // Cleanup
    discovery.close();
    registry.close();
    ctx.close();
    ```

=== "Java"

    ```java
    var ctx = Zlink.contextNew();

    // === Registry ===
    var registry = ctx.registryNew();
    // PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    registry.bind("tcp://*:5550", "tcp://*:5551");

    // === Discovery ===
    var discovery = ctx.discoveryNew(ServiceType.SOCKET, "my-service");
    discovery.connectRegistry("tcp://127.0.0.1:5551");

    // === ROUTER socket (server, Discovery-managed) ===
    var server = ctx.socket(SocketType.ROUTER);
    server.bind("tcp://*:5555");
    server.attachDiscovery(discovery);

    // ... application logic ...

    // Cleanup
    discovery.destroy();
    registry.destroy();
    ctx.term();
    ```

=== "Python"

    ```python
    ctx = zlink.Context()

    # === Registry ===
    registry = zlink.Registry(ctx)
    # PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    registry.bind("tcp://*:5550", "tcp://*:5551")

    # === Discovery ===
    discovery = zlink.Discovery(ctx, zlink.SERVICE_TYPE_SOCKET, "my-service")
    discovery.connect_registry("tcp://127.0.0.1:5551")

    # === ROUTER socket (server, Discovery-managed) ===
    server = ctx.socket(zlink.ROUTER)
    server.bind("tcp://*:5555")
    server.attach_discovery(discovery)

    # ... application logic ...

    # Cleanup
    discovery.destroy()
    registry.destroy()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    const ctx = new zlink.Context();

    // === Registry ===
    const registry = new zlink.Registry(ctx);
    // PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    registry.bind("tcp://*:5550", "tcp://*:5551");

    // === Discovery ===
    const discovery = new zlink.Discovery(ctx,
        zlink.SERVICE_TYPE_SOCKET, "my-service");
    discovery.connectRegistry("tcp://127.0.0.1:5551");

    // === ROUTER socket (server, Discovery-managed) ===
    const server = ctx.socket(zlink.ROUTER);
    server.bind("tcp://*:5555");
    server.attachDiscovery(discovery);

    // ... application logic ...

    // Cleanup
    discovery.destroy();
    registry.destroy();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using var ctx = new ZlinkContext();

    // === Registry ===
    using var registry = new Registry(ctx);
    // PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    registry.Bind("tcp://*:5550", "tcp://*:5551");

    // === Discovery ===
    using var discovery = new Discovery(ctx,
        ServiceType.Socket, "my-service");
    discovery.ConnectRegistry("tcp://127.0.0.1:5551");

    // === ROUTER socket (server, Discovery-managed) ===
    using var server = ctx.CreateSocket(SocketType.Router);
    server.Bind("tcp://*:5555");
    server.AttachDiscovery(discovery);

    // ... application logic ...
    ```

=== "Rust"

    ```rust
    let ctx = zlink::Context::new()?;

    // === Registry ===
    let registry = zlink::Registry::new(&ctx)?;
    // PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    registry.bind("tcp://*:5550", "tcp://*:5551")?;

    // === Discovery ===
    let discovery = zlink::Discovery::new(&ctx,
        zlink::ServiceType::Socket, "my-service")?;
    discovery.connect_registry("tcp://127.0.0.1:5551")?;

    // === ROUTER socket (server, Discovery-managed) ===
    let server = ctx.socket(zlink::ROUTER)?;
    server.bind("tcp://*:5555")?;
    server.attach_discovery(&discovery)?;

    // ... application logic ...

    // Cleanup
    discovery.destroy()?;
    registry.destroy()?;
    ctx.term()?;
    ```

=== "Go"

    ```go
    ctx, err := zlink.NewContext()
    if err != nil { log.Fatal(err) }

    // === Registry ===
    registry, err := zlink.NewRegistry(ctx)
    if err != nil { log.Fatal(err) }
    // PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    registry.Bind("tcp://*:5550", "tcp://*:5551")

    // === Discovery ===
    discovery, err := zlink.NewDiscovery(ctx,
        zlink.ServiceTypeSocket, "my-service")
    if err != nil { log.Fatal(err) }
    discovery.ConnectRegistry("tcp://127.0.0.1:5551")

    // === ROUTER socket (server, Discovery-managed) ===
    server, err := ctx.RouterSocket()
    if err != nil { log.Fatal(err) }
    server.Bind("tcp://*:5555")
    server.AttachDiscovery(discovery)

    // ... application logic ...

    // Cleanup
    discovery.Destroy()
    registry.Destroy()
    ctx.Term()
    ```

## 3. Registry 구성

모든 구성 API는 `zlink_registry_bind()` **전에** 호출해야 한다.

### 3.1 하트비트

=== "C"

    ```c
    /* interval_ms: how often services send heartbeats (default 5000 ms)
       timeout_ms:  when to expire silent services   (default 15000 ms) */
    zlink_registry_set_heartbeat(registry, 5000, 15000);
    ```

=== "C++"

    ```cpp
    // interval_ms: how often services send heartbeats (default 5000 ms)
    // timeout_ms:  when to expire silent services   (default 15000 ms)
    registry.set_heartbeat(5000, 15000);
    ```

=== "Java"

    ```java
    // interval_ms: how often services send heartbeats (default 5000 ms)
    // timeout_ms:  when to expire silent services   (default 15000 ms)
    registry.setHeartbeat(5000, 15000);
    ```

=== "Python"

    ```python
    # interval_ms: how often services send heartbeats (default 5000 ms)
    # timeout_ms:  when to expire silent services   (default 15000 ms)
    registry.set_heartbeat(5000, 15000)
    ```

=== "Node/TypeScript"

    ```typescript
    // interval_ms: how often services send heartbeats (default 5000 ms)
    // timeout_ms:  when to expire silent services   (default 15000 ms)
    registry.setHeartbeat(5000, 15000);
    ```

=== "C#/.NET"

    ```csharp
    // interval_ms: how often services send heartbeats (default 5000 ms)
    // timeout_ms:  when to expire silent services   (default 15000 ms)
    registry.SetHeartbeat(5000, 15000);
    ```

=== "Rust"

    ```rust
    // interval_ms: how often services send heartbeats (default 5000 ms)
    // timeout_ms:  when to expire silent services   (default 15000 ms)
    registry.set_heartbeat(5000, 15000)?;
    ```

=== "Go"

    ```go
    // interval_ms: how often services send heartbeats (default 5000 ms)
    // timeout_ms:  when to expire silent services   (default 15000 ms)
    registry.SetHeartbeat(5000, 15000)
    ```

### 3.2 브로드캐스트 주기

=== "C"

    ```c
    /* How often the full SERVICE_LIST is published on PUB (default 30000 ms) */
    zlink_registry_set_broadcast_interval(registry, 30000);
    ```

=== "C++"

    ```cpp
    // How often the full SERVICE_LIST is published on PUB (default 30000 ms)
    registry.set_broadcast_interval(30000);
    ```

=== "Java"

    ```java
    // How often the full SERVICE_LIST is published on PUB (default 30000 ms)
    registry.setBroadcastInterval(30000);
    ```

=== "Python"

    ```python
    # How often the full SERVICE_LIST is published on PUB (default 30000 ms)
    registry.set_broadcast_interval(30000)
    ```

=== "Node/TypeScript"

    ```typescript
    // How often the full SERVICE_LIST is published on PUB (default 30000 ms)
    registry.setBroadcastInterval(30000);
    ```

=== "C#/.NET"

    ```csharp
    // How often the full SERVICE_LIST is published on PUB (default 30000 ms)
    registry.SetBroadcastInterval(30000);
    ```

=== "Rust"

    ```rust
    // How often the full SERVICE_LIST is published on PUB (default 30000 ms)
    registry.set_broadcast_interval(30000)?;
    ```

=== "Go"

    ```go
    // How often the full SERVICE_LIST is published on PUB (default 30000 ms)
    registry.SetBroadcastInterval(30000)
    ```

### 3.3 소켓 옵션

Registry의 내부 소켓에 저수준 소켓 옵션을 적용한다:

=== "C"

    ```c
    /* Example: set TLS on the PUB socket */
    zlink_registry_setsockopt(registry,
        ZLINK_REGISTRY_SOCKET_PUB,        /* target socket */
        ZLINK_TLS_CA_CERT,                /* option */
        ca_pem, strlen(ca_pem));          /* value */
    ```

=== "C++"

    ```cpp
    // Example: set TLS on the PUB socket
    registry.set_option(zlink::registry_socket::pub_,
        zlink::option::tls_ca_cert, ca_pem);
    ```

=== "Java"

    ```java
    // Example: set TLS on the PUB socket
    registry.setOption(RegistrySocket.PUB,
        SocketOption.TLS_CA_CERT, caPem);
    ```

=== "Python"

    ```python
    # Example: set TLS on the PUB socket
    registry.set_option(zlink.REGISTRY_SOCKET_PUB,
        zlink.TLS_CA_CERT, ca_pem)
    ```

=== "Node/TypeScript"

    ```typescript
    // Example: set TLS on the PUB socket
    registry.setOption(zlink.REGISTRY_SOCKET_PUB,
        zlink.TLS_CA_CERT, caPem);
    ```

=== "C#/.NET"

    ```csharp
    // Example: set TLS on the PUB socket
    registry.SetOption(RegistrySocket.Pub,
        SocketOption.TlsCaCert, caPem);
    ```

=== "Rust"

    ```rust
    // Example: set TLS on the PUB socket
    registry.set_option(zlink::RegistrySocket::Pub,
        zlink::SocketOption::TlsCaCert, &ca_pem)?;
    ```

=== "Go"

    ```go
    // Example: set TLS on the PUB socket
    registry.SetOption(zlink.RegistrySocketPub,
        zlink.TlsCaCert, caPem)
    ```

| 소켓 역할 | 상수 | 용도 |
|-----------|------|------|
| PUB | `ZLINK_REGISTRY_SOCKET_PUB` | 서비스 목록 브로드캐스트 |
| ROUTER | `ZLINK_REGISTRY_SOCKET_ROUTER` | 등록/하트비트 수신 |
| PEER_SUB | `ZLINK_REGISTRY_SOCKET_PEER_SUB` | 피어 Registry 브로드캐스트 구독 |

### 3.4 클러스터 ID

=== "C"

    ```c
    /* Assign a unique ID for cluster synchronization (must be unique per node) */
    zlink_registry_set_id(registry, 1);
    ```

=== "C++"

    ```cpp
    // Assign a unique ID for cluster synchronization (must be unique per node)
    registry.set_id(1);
    ```

=== "Java"

    ```java
    // Assign a unique ID for cluster synchronization (must be unique per node)
    registry.setId(1);
    ```

=== "Python"

    ```python
    # Assign a unique ID for cluster synchronization (must be unique per node)
    registry.set_id(1)
    ```

=== "Node/TypeScript"

    ```typescript
    // Assign a unique ID for cluster synchronization (must be unique per node)
    registry.setId(1);
    ```

=== "C#/.NET"

    ```csharp
    // Assign a unique ID for cluster synchronization (must be unique per node)
    registry.SetId(1);
    ```

=== "Rust"

    ```rust
    // Assign a unique ID for cluster synchronization (must be unique per node)
    registry.set_id(1)?;
    ```

=== "Go"

    ```go
    // Assign a unique ID for cluster synchronization (must be unique per node)
    registry.SetId(1)
    ```

### 3.5 TLS 설정

TLS는 해당 내부 소켓의 소켓 옵션을 통해 구성한다:

=== "C"

    ```c
    /* TLS on PUB (broadcast to Discovery) */
    zlink_registry_setsockopt(registry, ZLINK_REGISTRY_SOCKET_PUB,
        ZLINK_TLS_SERVER_CERT, cert_pem, strlen(cert_pem));
    zlink_registry_setsockopt(registry, ZLINK_REGISTRY_SOCKET_PUB,
        ZLINK_TLS_SERVER_KEY, key_pem, strlen(key_pem));

    /* TLS on ROUTER (registration/heartbeat) */
    zlink_registry_setsockopt(registry, ZLINK_REGISTRY_SOCKET_ROUTER,
        ZLINK_TLS_SERVER_CERT, cert_pem, strlen(cert_pem));
    zlink_registry_setsockopt(registry, ZLINK_REGISTRY_SOCKET_ROUTER,
        ZLINK_TLS_SERVER_KEY, key_pem, strlen(key_pem));
    ```

=== "C++"

    ```cpp
    // TLS on PUB (broadcast to Discovery)
    registry.set_option(zlink::registry_socket::pub_,
        zlink::option::tls_server_cert, cert_pem);
    registry.set_option(zlink::registry_socket::pub_,
        zlink::option::tls_server_key, key_pem);

    // TLS on ROUTER (registration/heartbeat)
    registry.set_option(zlink::registry_socket::router,
        zlink::option::tls_server_cert, cert_pem);
    registry.set_option(zlink::registry_socket::router,
        zlink::option::tls_server_key, key_pem);
    ```

=== "Java"

    ```java
    // TLS on PUB (broadcast to Discovery)
    registry.setOption(RegistrySocket.PUB,
        SocketOption.TLS_SERVER_CERT, certPem);
    registry.setOption(RegistrySocket.PUB,
        SocketOption.TLS_SERVER_KEY, keyPem);

    // TLS on ROUTER (registration/heartbeat)
    registry.setOption(RegistrySocket.ROUTER,
        SocketOption.TLS_SERVER_CERT, certPem);
    registry.setOption(RegistrySocket.ROUTER,
        SocketOption.TLS_SERVER_KEY, keyPem);
    ```

=== "Python"

    ```python
    # TLS on PUB (broadcast to Discovery)
    registry.set_option(zlink.REGISTRY_SOCKET_PUB,
        zlink.TLS_SERVER_CERT, cert_pem)
    registry.set_option(zlink.REGISTRY_SOCKET_PUB,
        zlink.TLS_SERVER_KEY, key_pem)

    # TLS on ROUTER (registration/heartbeat)
    registry.set_option(zlink.REGISTRY_SOCKET_ROUTER,
        zlink.TLS_SERVER_CERT, cert_pem)
    registry.set_option(zlink.REGISTRY_SOCKET_ROUTER,
        zlink.TLS_SERVER_KEY, key_pem)
    ```

=== "Node/TypeScript"

    ```typescript
    // TLS on PUB (broadcast to Discovery)
    registry.setOption(zlink.REGISTRY_SOCKET_PUB,
        zlink.TLS_SERVER_CERT, certPem);
    registry.setOption(zlink.REGISTRY_SOCKET_PUB,
        zlink.TLS_SERVER_KEY, keyPem);

    // TLS on ROUTER (registration/heartbeat)
    registry.setOption(zlink.REGISTRY_SOCKET_ROUTER,
        zlink.TLS_SERVER_CERT, certPem);
    registry.setOption(zlink.REGISTRY_SOCKET_ROUTER,
        zlink.TLS_SERVER_KEY, keyPem);
    ```

=== "C#/.NET"

    ```csharp
    // TLS on PUB (broadcast to Discovery)
    registry.SetOption(RegistrySocket.Pub,
        SocketOption.TlsServerCert, certPem);
    registry.SetOption(RegistrySocket.Pub,
        SocketOption.TlsServerKey, keyPem);

    // TLS on ROUTER (registration/heartbeat)
    registry.SetOption(RegistrySocket.Router,
        SocketOption.TlsServerCert, certPem);
    registry.SetOption(RegistrySocket.Router,
        SocketOption.TlsServerKey, keyPem);
    ```

=== "Rust"

    ```rust
    // TLS on PUB (broadcast to Discovery)
    registry.set_option(zlink::RegistrySocket::Pub,
        zlink::SocketOption::TlsServerCert, &cert_pem)?;
    registry.set_option(zlink::RegistrySocket::Pub,
        zlink::SocketOption::TlsServerKey, &key_pem)?;

    // TLS on ROUTER (registration/heartbeat)
    registry.set_option(zlink::RegistrySocket::Router,
        zlink::SocketOption::TlsServerCert, &cert_pem)?;
    registry.set_option(zlink::RegistrySocket::Router,
        zlink::SocketOption::TlsServerKey, &key_pem)?;
    ```

=== "Go"

    ```go
    // TLS on PUB (broadcast to Discovery)
    registry.SetOption(zlink.RegistrySocketPub,
        zlink.TlsServerCert, certPem)
    registry.SetOption(zlink.RegistrySocketPub,
        zlink.TlsServerKey, keyPem)

    // TLS on ROUTER (registration/heartbeat)
    registry.SetOption(zlink.RegistrySocketRouter,
        zlink.TlsServerCert, certPem)
    registry.SetOption(zlink.RegistrySocketRouter,
        zlink.TlsServerKey, keyPem)
    ```

## 4. 배포 패턴

### 4.1 독립 프로세스 배포

Registry를 전용 서비스로 실행한다. 여러 애플리케이션이 각자의 Discovery
인스턴스를 통해 연결한다.

```
┌─────────────────────────────────────────┐
│         Registry 프로세스               │
│  Registry (PUB:5550 + ROUTER:5551)      │
└──────────────┬──────────────────────────┘
               │ SERVICE_LIST 브로드캐스트
       ┌───────┼───────┐
       │       │       │
       v       v       v
   ┌──────┐ ┌──────┐ ┌──────┐
   │App A │ │App B │ │App C │
   │Disc. │ │Disc. │ │Disc. │
   │ SOCK │ │ SOCK │ │ SPOT │
   └──────┘ └──────┘ └──────┘
```

프로덕션 배포에 권장하는 패턴:

- Registry 수명 주기가 애플리케이션 재시작과 독립적
- 여러 서비스가 단일 Registry(또는 클러스터)를 공유
- 인프라와 애플리케이션의 명확한 관심사 분리

### 4.2 임베디드 배포

Registry, Discovery, 서비스(SPOT/Socket)가 모두 단일 프로세스에 존재한다.
개발, 테스트, 또는 단일 노드 배포에 유용하다.

=== "C"

    ```c
    void *ctx = zlink_ctx_new();

    /* Registry (embedded) */
    void *registry = zlink_registry_new(ctx);
    /* PUB: service list broadcast, ROUTER: registration/heartbeat/queries */
    zlink_registry_bind(registry, "tcp://*:5550", "tcp://*:5551");

    /* Discovery (same process) */
    void *discovery = zlink_discovery_new(ctx,
        ZLINK_SERVICE_TYPE_SOCKET, "echo-service");
    zlink_discovery_connect_registry(discovery, "tcp://127.0.0.1:5551");

    /* ROUTER socket (server, Discovery-managed) */
    void *server = zlink_socket_new(ctx, ZLINK_ROUTER);
    zlink_bind(server, "tcp://*:5555");
    zlink_socket_attach_discovery(server, discovery);

    /* DEALER socket (client, same process) */
    void *client_disc = zlink_discovery_new(ctx,
        ZLINK_SERVICE_TYPE_SOCKET, "echo-service");
    zlink_discovery_connect_registry(client_disc, "tcp://127.0.0.1:5551");

    void *client = zlink_socket_new(ctx, ZLINK_DEALER);
    zlink_socket_attach_discovery(client, client_disc);

    /* Send request */
    zlink_msg_t part;
    zlink_msg_init_size(&part, 5);
    memcpy(zlink_msg_data(&part), "hello", 5);
    zlink_send(client, &part, 1, 0);

    /* Receive reply */
    zlink_msg_t *reply_parts = NULL;
    size_t reply_count = 0;
    zlink_recv(client, &reply_parts, &reply_count, 0);

    /* Cleanup (reverse order) */
    zlink_close(client);
    zlink_discovery_destroy(&client_disc);
    zlink_close(server);
    zlink_discovery_destroy(&discovery);
    zlink_registry_destroy(&registry);
    zlink_ctx_term(ctx);
    ```

=== "C++"

    ```cpp
    auto ctx = zlink::context();

    // Registry (embedded)
    auto registry = zlink::registry(ctx);
    registry.bind("tcp://*:5550", "tcp://*:5551");

    // Discovery (same process)
    auto discovery = zlink::discovery(ctx,
        zlink::service_type::socket, "echo-service");
    discovery.connect_registry("tcp://127.0.0.1:5551");

    // ROUTER socket (server, Discovery-managed)
    auto server = zlink::socket(ctx, zlink::socket_type::router);
    server.bind("tcp://*:5555");
    server.attach_discovery(discovery);

    // DEALER socket (client, same process)
    auto client_disc = zlink::discovery(ctx,
        zlink::service_type::socket, "echo-service");
    client_disc.connect_registry("tcp://127.0.0.1:5551");

    auto client = zlink::socket(ctx, zlink::socket_type::dealer);
    client.attach_discovery(client_disc);

    // Send request
    client.send(zlink::message_t("hello", 5));

    // Receive reply
    auto [source_rid, parts] = client.recv();

    // Cleanup (reverse order)
    client.close();
    client_disc.close();
    server.close();
    discovery.close();
    registry.close();
    ctx.close();
    ```

=== "Java"

    ```java
    var ctx = Zlink.contextNew();

    // Registry (embedded)
    var registry = ctx.registryNew();
    registry.bind("tcp://*:5550", "tcp://*:5551");

    // Discovery (same process)
    var discovery = ctx.discoveryNew(ServiceType.SOCKET, "echo-service");
    discovery.connectRegistry("tcp://127.0.0.1:5551");

    // ROUTER socket (server, Discovery-managed)
    var server = ctx.socket(SocketType.ROUTER);
    server.bind("tcp://*:5555");
    server.attachDiscovery(discovery);

    // DEALER socket (client, same process)
    var clientDisc = ctx.discoveryNew(ServiceType.SOCKET, "echo-service");
    clientDisc.connectRegistry("tcp://127.0.0.1:5551");

    var client = ctx.socket(SocketType.DEALER);
    client.attachDiscovery(clientDisc);

    // Send request
    client.send(new Message("hello".getBytes()));

    // Receive reply
    RecvResult result = client.recv();

    // Cleanup (reverse order)
    client.close();
    clientDisc.destroy();
    server.close();
    discovery.destroy();
    registry.destroy();
    ctx.term();
    ```

=== "Python"

    ```python
    ctx = zlink.Context()

    # Registry (embedded)
    registry = zlink.Registry(ctx)
    registry.bind("tcp://*:5550", "tcp://*:5551")

    # Discovery (same process)
    discovery = zlink.Discovery(ctx, zlink.SERVICE_TYPE_SOCKET, "echo-service")
    discovery.connect_registry("tcp://127.0.0.1:5551")

    # ROUTER socket (server, Discovery-managed)
    server = ctx.socket(zlink.ROUTER)
    server.bind("tcp://*:5555")
    server.attach_discovery(discovery)

    # DEALER socket (client, same process)
    client_disc = zlink.Discovery(ctx, zlink.SERVICE_TYPE_SOCKET, "echo-service")
    client_disc.connect_registry("tcp://127.0.0.1:5551")

    client = ctx.socket(zlink.DEALER)
    client.attach_discovery(client_disc)

    # Send request
    client.send(b"hello")

    # Receive reply
    source_rid, parts = client.recv()

    # Cleanup (reverse order)
    client.close()
    client_disc.destroy()
    server.close()
    discovery.destroy()
    registry.destroy()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    const ctx = new zlink.Context();

    // Registry (embedded)
    const registry = new zlink.Registry(ctx);
    registry.bind("tcp://*:5550", "tcp://*:5551");

    // Discovery (same process)
    const discovery = new zlink.Discovery(ctx,
        zlink.SERVICE_TYPE_SOCKET, "echo-service");
    discovery.connectRegistry("tcp://127.0.0.1:5551");

    // ROUTER socket (server, Discovery-managed)
    const server = ctx.socket(zlink.ROUTER);
    server.bind("tcp://*:5555");
    server.attachDiscovery(discovery);

    // DEALER socket (client, same process)
    const clientDisc = new zlink.Discovery(ctx,
        zlink.SERVICE_TYPE_SOCKET, "echo-service");
    clientDisc.connectRegistry("tcp://127.0.0.1:5551");

    const client = ctx.socket(zlink.DEALER);
    client.attachDiscovery(clientDisc);

    // Send request
    client.send(Buffer.from("hello"));

    // Receive reply
    const { sourceRid, parts } = client.recv();

    // Cleanup (reverse order)
    client.close();
    clientDisc.destroy();
    server.close();
    discovery.destroy();
    registry.destroy();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using var ctx = new ZlinkContext();

    // Registry (embedded)
    using var registry = new Registry(ctx);
    registry.Bind("tcp://*:5550", "tcp://*:5551");

    // Discovery (same process)
    using var discovery = new Discovery(ctx,
        ServiceType.Socket, "echo-service");
    discovery.ConnectRegistry("tcp://127.0.0.1:5551");

    // ROUTER socket (server, Discovery-managed)
    using var server = ctx.CreateSocket(SocketType.Router);
    server.Bind("tcp://*:5555");
    server.AttachDiscovery(discovery);

    // DEALER socket (client, same process)
    using var clientDisc = new Discovery(ctx,
        ServiceType.Socket, "echo-service");
    clientDisc.ConnectRegistry("tcp://127.0.0.1:5551");

    using var client = ctx.CreateSocket(SocketType.Dealer);
    client.AttachDiscovery(clientDisc);

    // Send request
    client.Send(new Message("hello"u8));

    // Receive reply
    var (sourceRid, parts) = client.Recv();
    ```

=== "Rust"

    ```rust
    let ctx = zlink::Context::new()?;

    // Registry (embedded)
    let registry = zlink::Registry::new(&ctx)?;
    registry.bind("tcp://*:5550", "tcp://*:5551")?;

    // Discovery (same process)
    let discovery = zlink::Discovery::new(&ctx,
        zlink::ServiceType::Socket, "echo-service")?;
    discovery.connect_registry("tcp://127.0.0.1:5551")?;

    // ROUTER socket (server, Discovery-managed)
    let server = ctx.socket(zlink::ROUTER)?;
    server.bind("tcp://*:5555")?;
    server.attach_discovery(&discovery)?;

    // DEALER socket (client, same process)
    let client_disc = zlink::Discovery::new(&ctx,
        zlink::ServiceType::Socket, "echo-service")?;
    client_disc.connect_registry("tcp://127.0.0.1:5551")?;

    let client = ctx.socket(zlink::DEALER)?;
    client.attach_discovery(&client_disc)?;

    // Send request
    client.send(&zlink::Message::from("hello"))?;

    // Receive reply
    let (source_rid, parts) = client.recv()?;

    // Cleanup (reverse order)
    client.close()?;
    client_disc.destroy()?;
    server.close()?;
    discovery.destroy()?;
    registry.destroy()?;
    ctx.term()?;
    ```

=== "Go"

    ```go
    ctx, err := zlink.NewContext()
    if err != nil { log.Fatal(err) }

    // Registry (embedded)
    registry, err := zlink.NewRegistry(ctx)
    if err != nil { log.Fatal(err) }
    registry.Bind("tcp://*:5550", "tcp://*:5551")

    // Discovery (same process)
    discovery, err := zlink.NewDiscovery(ctx,
        zlink.ServiceTypeSocket, "echo-service")
    if err != nil { log.Fatal(err) }
    discovery.ConnectRegistry("tcp://127.0.0.1:5551")

    // ROUTER socket (server, Discovery-managed)
    server, err := ctx.RouterSocket()
    if err != nil { log.Fatal(err) }
    server.Bind("tcp://*:5555")
    server.AttachDiscovery(discovery)

    // DEALER socket (client, same process)
    clientDisc, err := zlink.NewDiscovery(ctx,
        zlink.ServiceTypeSocket, "echo-service")
    if err != nil { log.Fatal(err) }
    clientDisc.ConnectRegistry("tcp://127.0.0.1:5551")

    client, err := ctx.DealerSocket()
    if err != nil { log.Fatal(err) }
    client.AttachDiscovery(clientDisc)

    // Send request
    client.Send(zlink.NewMessage([]byte("hello")))

    // Receive reply
    received, err := client.Recv()
    if err != nil { log.Fatal(err) }
    defer received.Close()

    // Cleanup (reverse order)
    client.Close()
    clientDisc.Destroy()
    server.Close()
    discovery.Destroy()
    registry.Destroy()
    ctx.Term()
    ```

> **팁**: 모든 컴포넌트가 같은 프로세스에 있을 때 `inproc://` transport를
> 사용하면 Registry와 Discovery 간 zero-copy 통신이 가능하다.

## 5. 클러스터 구성 및 데이터 동기화

### 5.1 클러스터 구성

각 Registry 노드에 고유 ID와 피어의 PUB 엔드포인트가 필요하다:

=== "C"

    ```c
    /* Node 1 */
    void *reg1 = zlink_registry_new(ctx);
    zlink_registry_set_id(reg1, 1);
    zlink_registry_add_peer(reg1, "tcp://registry2:5550");
    zlink_registry_add_peer(reg1, "tcp://registry3:5550");
    /* PUB: service list broadcast, ROUTER: registration/heartbeat/queries */
    zlink_registry_bind(reg1, "tcp://*:5550", "tcp://*:5551");
    ```

=== "C++"

    ```cpp
    // Node 1
    auto reg1 = zlink::registry(ctx);
    reg1.set_id(1);
    reg1.add_peer("tcp://registry2:5550");
    reg1.add_peer("tcp://registry3:5550");
    // PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    reg1.bind("tcp://*:5550", "tcp://*:5551");
    ```

=== "Java"

    ```java
    // Node 1
    var reg1 = ctx.registryNew();
    reg1.setId(1);
    reg1.addPeer("tcp://registry2:5550");
    reg1.addPeer("tcp://registry3:5550");
    // PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    reg1.bind("tcp://*:5550", "tcp://*:5551");
    ```

=== "Python"

    ```python
    # Node 1
    reg1 = zlink.Registry(ctx)
    reg1.set_id(1)
    reg1.add_peer("tcp://registry2:5550")
    reg1.add_peer("tcp://registry3:5550")
    # PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    reg1.bind("tcp://*:5550", "tcp://*:5551")
    ```

=== "Node/TypeScript"

    ```typescript
    // Node 1
    const reg1 = new zlink.Registry(ctx);
    reg1.setId(1);
    reg1.addPeer("tcp://registry2:5550");
    reg1.addPeer("tcp://registry3:5550");
    // PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    reg1.bind("tcp://*:5550", "tcp://*:5551");
    ```

=== "C#/.NET"

    ```csharp
    // Node 1
    using var reg1 = new Registry(ctx);
    reg1.SetId(1);
    reg1.AddPeer("tcp://registry2:5550");
    reg1.AddPeer("tcp://registry3:5550");
    // PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    reg1.Bind("tcp://*:5550", "tcp://*:5551");
    ```

=== "Rust"

    ```rust
    // Node 1
    let reg1 = zlink::Registry::new(&ctx)?;
    reg1.set_id(1)?;
    reg1.add_peer("tcp://registry2:5550")?;
    reg1.add_peer("tcp://registry3:5550")?;
    // PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    reg1.bind("tcp://*:5550", "tcp://*:5551")?;
    ```

=== "Go"

    ```go
    // Node 1
    reg1, err := zlink.NewRegistry(ctx)
    if err != nil { log.Fatal(err) }
    reg1.SetId(1)
    reg1.AddPeer("tcp://registry2:5550")
    reg1.AddPeer("tcp://registry3:5550")
    // PUB: service list broadcast, ROUTER: registration/heartbeat/queries
    reg1.Bind("tcp://*:5550", "tcp://*:5551")
    ```

### 5.2 동기화 메커니즘

Registry는 PUB/SUB 기반 flooding 동기화를 사용한다:

```
┌────────────┐     PUB/SUB      ┌────────────┐
│ Registry 1 │◄────────────────►│ Registry 2 │
│ (id=1)     │                  │ (id=2)     │
│ PUB:5550   │                  │ PUB:5550   │
└────────────┘                  └────────────┘
      ▲                               ▲
      │           PUB/SUB             │
      └───────────────────────────────┘
                     │
              ┌────────────┐
              │ Registry 3 │
              │ (id=3)     │
              │ PUB:5550   │
              └────────────┘
```

- 각 Registry가 다른 모든 Registry의 PUB 엔드포인트를 구독
- 서비스 목록 변경이 flooding을 통해 즉시 전파
- **Eventually Consistent**: 모든 Registry가 동일한 상태로 수렴
- `registry_id` + `list_seq`를 통해 중복/역전 업데이트를 안전하게 무시

**Discovery 관점:** 서비스 목록이 flooding으로 전파되므로, Discovery는 클러스터의
**하나의** Registry에만 연결해도 전체 서비스를 발견할 수 있다. 여러 Registry에
연결하는 것은 장애 시 failover를 위한 것이다.

### 5.3 3노드 클러스터 전체 예제

=== "C"

    ```c
    void *ctx = zlink_ctx_new();

    /* === Node 1 === */
    void *reg1 = zlink_registry_new(ctx);
    zlink_registry_set_id(reg1, 1);
    zlink_registry_add_peer(reg1, "tcp://registry2:5550");
    zlink_registry_add_peer(reg1, "tcp://registry3:5550");
    zlink_registry_set_heartbeat(reg1, 5000, 15000);
    /* PUB: service list broadcast, ROUTER: registration/heartbeat/queries */
    zlink_registry_bind(reg1, "tcp://*:5550", "tcp://*:5551");

    /* === Node 2 === */
    void *reg2 = zlink_registry_new(ctx);
    zlink_registry_set_id(reg2, 2);
    zlink_registry_add_peer(reg2, "tcp://registry1:5550");
    zlink_registry_add_peer(reg2, "tcp://registry3:5550");
    zlink_registry_set_heartbeat(reg2, 5000, 15000);
    zlink_registry_bind(reg2, "tcp://*:5550", "tcp://*:5551");

    /* === Node 3 === */
    void *reg3 = zlink_registry_new(ctx);
    zlink_registry_set_id(reg3, 3);
    zlink_registry_add_peer(reg3, "tcp://registry1:5550");
    zlink_registry_add_peer(reg3, "tcp://registry2:5550");
    zlink_registry_set_heartbeat(reg3, 5000, 15000);
    zlink_registry_bind(reg3, "tcp://*:5550", "tcp://*:5551");

    /* Discovery connects to multiple Registries (HA — a single one suffices for service visibility) */
    void *discovery = zlink_discovery_new(ctx,
        ZLINK_SERVICE_TYPE_SOCKET, "my-service");
    zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");
    zlink_discovery_connect_registry(discovery, "tcp://registry2:5551");
    zlink_discovery_connect_registry(discovery, "tcp://registry3:5551");

    /* ... */

    /* Cleanup */
    zlink_discovery_destroy(&discovery);
    zlink_registry_destroy(&reg3);
    zlink_registry_destroy(&reg2);
    zlink_registry_destroy(&reg1);
    zlink_ctx_term(ctx);
    ```

=== "C++"

    ```cpp
    auto ctx = zlink::context();

    // === Node 1 ===
    auto reg1 = zlink::registry(ctx);
    reg1.set_id(1);
    reg1.add_peer("tcp://registry2:5550");
    reg1.add_peer("tcp://registry3:5550");
    reg1.set_heartbeat(5000, 15000);
    reg1.bind("tcp://*:5550", "tcp://*:5551");

    // === Node 2 ===
    auto reg2 = zlink::registry(ctx);
    reg2.set_id(2);
    reg2.add_peer("tcp://registry1:5550");
    reg2.add_peer("tcp://registry3:5550");
    reg2.set_heartbeat(5000, 15000);
    reg2.bind("tcp://*:5550", "tcp://*:5551");

    // === Node 3 ===
    auto reg3 = zlink::registry(ctx);
    reg3.set_id(3);
    reg3.add_peer("tcp://registry1:5550");
    reg3.add_peer("tcp://registry2:5550");
    reg3.set_heartbeat(5000, 15000);
    reg3.bind("tcp://*:5550", "tcp://*:5551");

    // Discovery connects to multiple Registries (HA)
    auto discovery = zlink::discovery(ctx,
        zlink::service_type::socket, "my-service");
    discovery.connect_registry("tcp://registry1:5551");
    discovery.connect_registry("tcp://registry2:5551");
    discovery.connect_registry("tcp://registry3:5551");

    // ...

    // Cleanup
    discovery.close();
    reg3.close();
    reg2.close();
    reg1.close();
    ctx.close();
    ```

=== "Java"

    ```java
    var ctx = Zlink.contextNew();

    // === Node 1 ===
    var reg1 = ctx.registryNew();
    reg1.setId(1);
    reg1.addPeer("tcp://registry2:5550");
    reg1.addPeer("tcp://registry3:5550");
    reg1.setHeartbeat(5000, 15000);
    reg1.bind("tcp://*:5550", "tcp://*:5551");

    // === Node 2 ===
    var reg2 = ctx.registryNew();
    reg2.setId(2);
    reg2.addPeer("tcp://registry1:5550");
    reg2.addPeer("tcp://registry3:5550");
    reg2.setHeartbeat(5000, 15000);
    reg2.bind("tcp://*:5550", "tcp://*:5551");

    // === Node 3 ===
    var reg3 = ctx.registryNew();
    reg3.setId(3);
    reg3.addPeer("tcp://registry1:5550");
    reg3.addPeer("tcp://registry2:5550");
    reg3.setHeartbeat(5000, 15000);
    reg3.bind("tcp://*:5550", "tcp://*:5551");

    // Discovery connects to multiple Registries (HA)
    var discovery = ctx.discoveryNew(ServiceType.SOCKET, "my-service");
    discovery.connectRegistry("tcp://registry1:5551");
    discovery.connectRegistry("tcp://registry2:5551");
    discovery.connectRegistry("tcp://registry3:5551");

    // ...

    // Cleanup
    discovery.destroy();
    reg3.destroy();
    reg2.destroy();
    reg1.destroy();
    ctx.term();
    ```

=== "Python"

    ```python
    ctx = zlink.Context()

    # === Node 1 ===
    reg1 = zlink.Registry(ctx)
    reg1.set_id(1)
    reg1.add_peer("tcp://registry2:5550")
    reg1.add_peer("tcp://registry3:5550")
    reg1.set_heartbeat(5000, 15000)
    reg1.bind("tcp://*:5550", "tcp://*:5551")

    # === Node 2 ===
    reg2 = zlink.Registry(ctx)
    reg2.set_id(2)
    reg2.add_peer("tcp://registry1:5550")
    reg2.add_peer("tcp://registry3:5550")
    reg2.set_heartbeat(5000, 15000)
    reg2.bind("tcp://*:5550", "tcp://*:5551")

    # === Node 3 ===
    reg3 = zlink.Registry(ctx)
    reg3.set_id(3)
    reg3.add_peer("tcp://registry1:5550")
    reg3.add_peer("tcp://registry2:5550")
    reg3.set_heartbeat(5000, 15000)
    reg3.bind("tcp://*:5550", "tcp://*:5551")

    # Discovery connects to multiple Registries (HA)
    discovery = zlink.Discovery(ctx,
        zlink.SERVICE_TYPE_SOCKET, "my-service")
    discovery.connect_registry("tcp://registry1:5551")
    discovery.connect_registry("tcp://registry2:5551")
    discovery.connect_registry("tcp://registry3:5551")

    # ...

    # Cleanup
    discovery.destroy()
    reg3.destroy()
    reg2.destroy()
    reg1.destroy()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    const ctx = new zlink.Context();

    // === Node 1 ===
    const reg1 = new zlink.Registry(ctx);
    reg1.setId(1);
    reg1.addPeer("tcp://registry2:5550");
    reg1.addPeer("tcp://registry3:5550");
    reg1.setHeartbeat(5000, 15000);
    reg1.bind("tcp://*:5550", "tcp://*:5551");

    // === Node 2 ===
    const reg2 = new zlink.Registry(ctx);
    reg2.setId(2);
    reg2.addPeer("tcp://registry1:5550");
    reg2.addPeer("tcp://registry3:5550");
    reg2.setHeartbeat(5000, 15000);
    reg2.bind("tcp://*:5550", "tcp://*:5551");

    // === Node 3 ===
    const reg3 = new zlink.Registry(ctx);
    reg3.setId(3);
    reg3.addPeer("tcp://registry1:5550");
    reg3.addPeer("tcp://registry2:5550");
    reg3.setHeartbeat(5000, 15000);
    reg3.bind("tcp://*:5550", "tcp://*:5551");

    // Discovery connects to multiple Registries (HA)
    const discovery = new zlink.Discovery(ctx,
        zlink.SERVICE_TYPE_SOCKET, "my-service");
    discovery.connectRegistry("tcp://registry1:5551");
    discovery.connectRegistry("tcp://registry2:5551");
    discovery.connectRegistry("tcp://registry3:5551");

    // ...

    // Cleanup
    discovery.destroy();
    reg3.destroy();
    reg2.destroy();
    reg1.destroy();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using var ctx = new ZlinkContext();

    // === Node 1 ===
    using var reg1 = new Registry(ctx);
    reg1.SetId(1);
    reg1.AddPeer("tcp://registry2:5550");
    reg1.AddPeer("tcp://registry3:5550");
    reg1.SetHeartbeat(5000, 15000);
    reg1.Bind("tcp://*:5550", "tcp://*:5551");

    // === Node 2 ===
    using var reg2 = new Registry(ctx);
    reg2.SetId(2);
    reg2.AddPeer("tcp://registry1:5550");
    reg2.AddPeer("tcp://registry3:5550");
    reg2.SetHeartbeat(5000, 15000);
    reg2.Bind("tcp://*:5550", "tcp://*:5551");

    // === Node 3 ===
    using var reg3 = new Registry(ctx);
    reg3.SetId(3);
    reg3.AddPeer("tcp://registry1:5550");
    reg3.AddPeer("tcp://registry2:5550");
    reg3.SetHeartbeat(5000, 15000);
    reg3.Bind("tcp://*:5550", "tcp://*:5551");

    // Discovery connects to multiple Registries (HA)
    using var discovery = new Discovery(ctx,
        ServiceType.Socket, "my-service");
    discovery.ConnectRegistry("tcp://registry1:5551");
    discovery.ConnectRegistry("tcp://registry2:5551");
    discovery.ConnectRegistry("tcp://registry3:5551");

    // ...
    ```

=== "Rust"

    ```rust
    let ctx = zlink::Context::new()?;

    // === Node 1 ===
    let reg1 = zlink::Registry::new(&ctx)?;
    reg1.set_id(1)?;
    reg1.add_peer("tcp://registry2:5550")?;
    reg1.add_peer("tcp://registry3:5550")?;
    reg1.set_heartbeat(5000, 15000)?;
    reg1.bind("tcp://*:5550", "tcp://*:5551")?;

    // === Node 2 ===
    let reg2 = zlink::Registry::new(&ctx)?;
    reg2.set_id(2)?;
    reg2.add_peer("tcp://registry1:5550")?;
    reg2.add_peer("tcp://registry3:5550")?;
    reg2.set_heartbeat(5000, 15000)?;
    reg2.bind("tcp://*:5550", "tcp://*:5551")?;

    // === Node 3 ===
    let reg3 = zlink::Registry::new(&ctx)?;
    reg3.set_id(3)?;
    reg3.add_peer("tcp://registry1:5550")?;
    reg3.add_peer("tcp://registry2:5550")?;
    reg3.set_heartbeat(5000, 15000)?;
    reg3.bind("tcp://*:5550", "tcp://*:5551")?;

    // Discovery connects to multiple Registries (HA)
    let discovery = zlink::Discovery::new(&ctx,
        zlink::ServiceType::Socket, "my-service")?;
    discovery.connect_registry("tcp://registry1:5551")?;
    discovery.connect_registry("tcp://registry2:5551")?;
    discovery.connect_registry("tcp://registry3:5551")?;

    // ...

    // Cleanup
    discovery.destroy()?;
    reg3.destroy()?;
    reg2.destroy()?;
    reg1.destroy()?;
    ctx.term()?;
    ```

=== "Go"

    ```go
    ctx, err := zlink.NewContext()
    if err != nil { log.Fatal(err) }

    // === Node 1 ===
    reg1, err := zlink.NewRegistry(ctx)
    if err != nil { log.Fatal(err) }
    reg1.SetId(1)
    reg1.AddPeer("tcp://registry2:5550")
    reg1.AddPeer("tcp://registry3:5550")
    reg1.SetHeartbeat(5000, 15000)
    reg1.Bind("tcp://*:5550", "tcp://*:5551")

    // === Node 2 ===
    reg2, err := zlink.NewRegistry(ctx)
    if err != nil { log.Fatal(err) }
    reg2.SetId(2)
    reg2.AddPeer("tcp://registry1:5550")
    reg2.AddPeer("tcp://registry3:5550")
    reg2.SetHeartbeat(5000, 15000)
    reg2.Bind("tcp://*:5550", "tcp://*:5551")

    // === Node 3 ===
    reg3, err := zlink.NewRegistry(ctx)
    if err != nil { log.Fatal(err) }
    reg3.SetId(3)
    reg3.AddPeer("tcp://registry1:5550")
    reg3.AddPeer("tcp://registry2:5550")
    reg3.SetHeartbeat(5000, 15000)
    reg3.Bind("tcp://*:5550", "tcp://*:5551")

    // Discovery connects to multiple Registries (HA)
    discovery, err := zlink.NewDiscovery(ctx,
        zlink.ServiceTypeSocket, "my-service")
    if err != nil { log.Fatal(err) }
    discovery.ConnectRegistry("tcp://registry1:5551")
    discovery.ConnectRegistry("tcp://registry2:5551")
    discovery.ConnectRegistry("tcp://registry3:5551")

    // ...

    // Cleanup
    discovery.Destroy()
    reg3.Destroy()
    reg2.Destroy()
    reg1.Destroy()
    ctx.Term()
    ```

## 6. 토폴로지 조회 (Topology Introspection)

Registry는 글로벌 서비스 토폴로지를 조회하는 API를 제공한다. **로컬**(같은
프로세스)과 **원격**(다른 프로세스, 쿼리 클라이언트 사용) 두 가지 접근
방식이 있다.

### 6.1 로컬 조회 (같은 프로세스)

#### 전체 스냅샷

=== "C"

    ```c
    /* Query required count first */
    size_t count = 0;
    zlink_registry_topology_snapshot(registry, NULL, &count);

    /* Allocate and fetch */
    zlink_registry_topology_entry_t *entries = malloc(
        count * sizeof(zlink_registry_topology_entry_t));
    zlink_registry_topology_snapshot(registry, entries, &count);

    for (size_t i = 0; i < count; i++) {
        printf("service=%s endpoint=%s state=%d\n",
               entries[i].service_name,
               entries[i].endpoint,
               entries[i].state);
    }
    free(entries);
    ```

=== "C++"

    ```cpp
    auto entries = registry.topology_snapshot();
    for (const auto& e : entries) {
        std::println("service={} endpoint={} state={}",
                     e.service_name(), e.endpoint(), e.state());
    }
    ```

=== "Java"

    ```java
    var entries = registry.topologySnapshot();
    for (var e : entries) {
        System.out.printf("service=%s endpoint=%s state=%d%n",
                          e.serviceName(), e.endpoint(), e.state());
    }
    ```

=== "Python"

    ```python
    entries = registry.topology_snapshot()
    for e in entries:
        print(f"service={e.service_name} endpoint={e.endpoint} state={e.state}")
    ```

=== "Node/TypeScript"

    ```typescript
    const entries = registry.topologySnapshot();
    for (const e of entries) {
        console.log(`service=${e.serviceName} endpoint=${e.endpoint} state=${e.state}`);
    }
    ```

=== "C#/.NET"

    ```csharp
    var entries = registry.TopologySnapshot();
    foreach (var e in entries) {
        Console.WriteLine($"service={e.ServiceName} endpoint={e.Endpoint} state={e.State}");
    }
    ```

=== "Rust"

    ```rust
    let entries = registry.topology_snapshot()?;
    for e in &entries {
        println!("service={} endpoint={} state={:?}",
                 e.service_name(), e.endpoint(), e.state());
    }
    ```

=== "Go"

    ```go
    entries, err := registry.TopologySnapshot()
    if err != nil { log.Fatal(err) }
    for _, e := range entries {
        fmt.Printf("service=%s endpoint=%s state=%d\n",
            e.ServiceName(), e.Endpoint(), e.State())
    }
    ```

#### 필터 기반 조회

=== "C"

    ```c
    /* Query only READY SOCKET instances of "payment-service" */
    zlink_registry_topology_filter_t filter;
    memset(&filter, 0, sizeof(filter));
    filter.service_kind = ZLINK_SERVICE_KIND_SOCKET;
    strncpy(filter.service_name, "payment-service",
            sizeof(filter.service_name) - 1);
    filter.state = ZLINK_TOPOLOGY_STATE_READY;

    size_t count = 64;
    zlink_registry_topology_entry_t entries[64];
    zlink_registry_topology_query(registry, &filter, entries, &count);

    printf("READY instances: %zu\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("  %s (ready_count=%u)\n",
               entries[i].endpoint, entries[i].ready_count);
    }
    ```

=== "C++"

    ```cpp
    // Query only READY SOCKET instances of "payment-service"
    zlink::topology_filter filter;
    filter.service_kind = zlink::service_kind::socket;
    filter.service_name = "payment-service";
    filter.state = zlink::topology_state::ready;

    auto entries = registry.topology_query(filter);
    std::println("READY instances: {}", entries.size());
    for (const auto& e : entries) {
        std::println("  {} (ready_count={})", e.endpoint(), e.ready_count());
    }
    ```

=== "Java"

    ```java
    // Query only READY SOCKET instances of "payment-service"
    var filter = new TopologyFilter();
    filter.setServiceKind(ServiceKind.SOCKET);
    filter.setServiceName("payment-service");
    filter.setState(TopologyState.READY);

    var entries = registry.topologyQuery(filter);
    System.out.printf("READY instances: %d%n", entries.length);
    for (var e : entries) {
        System.out.printf("  %s (ready_count=%d)%n", e.endpoint(), e.readyCount());
    }
    ```

=== "Python"

    ```python
    # Query only READY SOCKET instances of "payment-service"
    filter = zlink.TopologyFilter(
        service_kind=zlink.SERVICE_KIND_SOCKET,
        service_name="payment-service",
        state=zlink.TOPOLOGY_STATE_READY)

    entries = registry.topology_query(filter)
    print(f"READY instances: {len(entries)}")
    for e in entries:
        print(f"  {e.endpoint} (ready_count={e.ready_count})")
    ```

=== "Node/TypeScript"

    ```typescript
    // Query only READY SOCKET instances of "payment-service"
    const entries = registry.topologyQuery({
        serviceKind: zlink.SERVICE_KIND_SOCKET,
        serviceName: "payment-service",
        state: zlink.TOPOLOGY_STATE_READY,
    });
    console.log(`READY instances: ${entries.length}`);
    for (const e of entries) {
        console.log(`  ${e.endpoint} (ready_count=${e.readyCount})`);
    }
    ```

=== "C#/.NET"

    ```csharp
    // Query only READY SOCKET instances of "payment-service"
    var filter = new TopologyFilter {
        ServiceKind = ServiceKind.Socket,
        ServiceName = "payment-service",
        State = TopologyState.Ready,
    };

    var entries = registry.TopologyQuery(filter);
    Console.WriteLine($"READY instances: {entries.Length}");
    foreach (var e in entries) {
        Console.WriteLine($"  {e.Endpoint} (ready_count={e.ReadyCount})");
    }
    ```

=== "Rust"

    ```rust
    // Query only READY SOCKET instances of "payment-service"
    let filter = zlink::TopologyFilter::new()
        .service_kind(zlink::ServiceKind::Socket)
        .service_name("payment-service")
        .state(zlink::TopologyState::Ready);

    let entries = registry.topology_query(&filter)?;
    println!("READY instances: {}", entries.len());
    for e in &entries {
        println!("  {} (ready_count={})", e.endpoint(), e.ready_count());
    }
    ```

=== "Go"

    ```go
    // Query only READY SOCKET instances of "payment-service"
    filter := zlink.TopologyFilter{
        ServiceKind: zlink.ServiceKindSocket,
        ServiceName: "payment-service",
        State:       zlink.TopologyStateReady,
    }

    entries, err := registry.TopologyQuery(filter)
    if err != nil { log.Fatal(err) }
    fmt.Printf("READY instances: %d\n", len(entries))
    for _, e := range entries {
        fmt.Printf("  %s (ready_count=%d)\n", e.Endpoint(), e.ReadyCount())
    }
    ```

#### 토폴로지 엔트리 필드

| 필드 | 설명 |
|------|------|
| `routing_id` | 서비스 인스턴스의 라우팅 ID |
| `service_kind` | `SPOT_PUB`, `SPOT_SUB`, `SOCKET`, 또는 `DISCOVERY` |
| `service_name` | 논리적 서비스 이름 |
| `endpoint` | 광고된 엔드포인트 |
| `source` | 추가 방식 (`MANUAL`/`DISCOVERY`/`REGISTRY`) |
| `state` | `DISCOVERED`/`CONNECTING`/`READY`/`LOST`/`ERROR`/`STOPPED` |
| `desired_count` | 기대 피어 인스턴스 수 |
| `ready_count` | 현재 ready 상태 인스턴스 수 |
| `error_code` | `ERROR` 상태일 때 오류 코드 |
| `last_reported_ms` | 마지막 업데이트 타임스탬프 (epoch ms) |

#### 필터 필드

필드를 0이 아닌 값으로 설정하면 해당 기준으로 필터링한다. 0 값 필드는
와일드카드(전체 매칭)로 처리된다.

| 필드 | 설명 |
|------|------|
| `service_kind` | 서비스 종류로 필터링 |
| `service_name` | 서비스 이름으로 필터링 |
| `routing_id` | 라우팅 ID로 필터링 |
| `state` | 토폴로지 상태로 필터링 |
| `source` | 토폴로지 소스로 필터링 |

### 6.2 원격 조회 (다른 프로세스)

쿼리 클라이언트를 사용하여 별도 프로세스의 Registry를 조회한다.
운영 도구나 CLI 유틸리티에서 사용하는 패턴이다.

=== "C"

    ```c
    void *ctx = zlink_ctx_new();

    /* Create query client and connect to Registry ROUTER endpoint */
    void *client = zlink_registry_query_client_new(ctx);
    zlink_registry_query_client_connect(client, "tcp://registry1:5551");

    /* Unfiltered snapshot (pass NULL filter for all entries) */
    size_t count = 0;
    zlink_registry_query_snapshot(client, NULL, NULL, &count);

    zlink_registry_topology_entry_t *entries = malloc(
        count * sizeof(zlink_registry_topology_entry_t));
    zlink_registry_query_snapshot(client, NULL, entries, &count);

    /* Print topology dump */
    for (size_t i = 0; i < count; i++) {
        const char *kind_str = "?";
        if (entries[i].service_kind == ZLINK_SERVICE_KIND_SPOT_PUB
            || entries[i].service_kind == ZLINK_SERVICE_KIND_SPOT_SUB)
            kind_str = "SPOT";
        else if (entries[i].service_kind == ZLINK_SERVICE_KIND_SOCKET)
            kind_str = "SOCK";
        else if (entries[i].service_kind == ZLINK_SERVICE_KIND_DISCOVERY)
            kind_str = "DISC";
        printf("[%s] %s @ %s  state=%d  ready=%u/%u\n",
               kind_str,
               entries[i].service_name,
               entries[i].endpoint,
               entries[i].state,
               entries[i].ready_count,
               entries[i].desired_count);
    }
    free(entries);

    /* Filtered remote query */
    zlink_registry_topology_filter_t filter;
    memset(&filter, 0, sizeof(filter));
    filter.state = ZLINK_TOPOLOGY_STATE_LOST;

    size_t lost_count = 64;
    zlink_registry_topology_entry_t lost[64];
    zlink_registry_query_snapshot(client, &filter, lost, &lost_count);
    printf("LOST entries: %zu\n", lost_count);

    /* Cleanup */
    zlink_registry_query_destroy(&client);
    zlink_ctx_term(ctx);
    ```

=== "C++"

    ```cpp
    auto ctx = zlink::context();

    // Create query client and connect to Registry ROUTER endpoint
    auto client = zlink::registry_query_client(ctx);
    client.connect("tcp://registry1:5551");

    // Unfiltered snapshot
    auto entries = client.snapshot();

    // Print topology dump
    for (const auto& e : entries) {
        std::string kind_str = "?";
        if (e.service_kind() == zlink::service_kind::spot_pub
            || e.service_kind() == zlink::service_kind::spot_sub)
            kind_str = "SPOT";
        else if (e.service_kind() == zlink::service_kind::socket)
            kind_str = "SOCK";
        else if (e.service_kind() == zlink::service_kind::discovery)
            kind_str = "DISC";
        std::println("[{}] {} @ {}  state={}  ready={}/{}",
                     kind_str, e.service_name(), e.endpoint(),
                     e.state(), e.ready_count(), e.desired_count());
    }

    // Filtered remote query
    zlink::topology_filter filter;
    filter.state = zlink::topology_state::lost;
    auto lost = client.snapshot(filter);
    std::println("LOST entries: {}", lost.size());

    // Cleanup
    client.close();
    ctx.close();
    ```

=== "Java"

    ```java
    var ctx = Zlink.contextNew();

    // Create query client and connect to Registry ROUTER endpoint
    var client = ctx.registryQueryClientNew();
    client.connect("tcp://registry1:5551");

    // Unfiltered snapshot
    var entries = client.snapshot();

    // Print topology dump
    for (var e : entries) {
        String kindStr = "?";
        if (e.serviceKind() == ServiceKind.SPOT_PUB
            || e.serviceKind() == ServiceKind.SPOT_SUB)
            kindStr = "SPOT";
        else if (e.serviceKind() == ServiceKind.SOCKET)
            kindStr = "SOCK";
        else if (e.serviceKind() == ServiceKind.DISCOVERY)
            kindStr = "DISC";
        System.out.printf("[%s] %s @ %s  state=%d  ready=%d/%d%n",
                          kindStr, e.serviceName(), e.endpoint(),
                          e.state(), e.readyCount(), e.desiredCount());
    }

    // Filtered remote query
    var filter = new TopologyFilter();
    filter.setState(TopologyState.LOST);
    var lost = client.snapshot(filter);
    System.out.printf("LOST entries: %d%n", lost.length);

    // Cleanup
    client.destroy();
    ctx.term();
    ```

=== "Python"

    ```python
    ctx = zlink.Context()

    # Create query client and connect to Registry ROUTER endpoint
    client = zlink.RegistryQueryClient(ctx)
    client.connect("tcp://registry1:5551")

    # Unfiltered snapshot
    entries = client.snapshot()

    # Print topology dump
    for e in entries:
        if e.service_kind in (zlink.SERVICE_KIND_SPOT_PUB,
                              zlink.SERVICE_KIND_SPOT_SUB):
            kind_str = "SPOT"
        elif e.service_kind == zlink.SERVICE_KIND_SOCKET:
            kind_str = "SOCK"
        elif e.service_kind == zlink.SERVICE_KIND_DISCOVERY:
            kind_str = "DISC"
        else:
            kind_str = "?"
        print(f"[{kind_str}] {e.service_name} @ {e.endpoint}"
              f"  state={e.state}  ready={e.ready_count}/{e.desired_count}")

    # Filtered remote query
    filter = zlink.TopologyFilter(state=zlink.TOPOLOGY_STATE_LOST)
    lost = client.snapshot(filter)
    print(f"LOST entries: {len(lost)}")

    # Cleanup
    client.destroy()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    const ctx = new zlink.Context();

    // Create query client and connect to Registry ROUTER endpoint
    const client = new zlink.RegistryQueryClient(ctx);
    client.connect("tcp://registry1:5551");

    // Unfiltered snapshot
    const entries = client.snapshot();

    // Print topology dump
    for (const e of entries) {
        let kindStr = "?";
        if (e.serviceKind === zlink.SERVICE_KIND_SPOT_PUB
            || e.serviceKind === zlink.SERVICE_KIND_SPOT_SUB)
            kindStr = "SPOT";
        else if (e.serviceKind === zlink.SERVICE_KIND_SOCKET)
            kindStr = "SOCK";
        else if (e.serviceKind === zlink.SERVICE_KIND_DISCOVERY)
            kindStr = "DISC";
        console.log(`[${kindStr}] ${e.serviceName} @ ${e.endpoint}` +
            `  state=${e.state}  ready=${e.readyCount}/${e.desiredCount}`);
    }

    // Filtered remote query
    const lost = client.snapshot({ state: zlink.TOPOLOGY_STATE_LOST });
    console.log(`LOST entries: ${lost.length}`);

    // Cleanup
    client.destroy();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using var ctx = new ZlinkContext();

    // Create query client and connect to Registry ROUTER endpoint
    using var client = new RegistryQueryClient(ctx);
    client.Connect("tcp://registry1:5551");

    // Unfiltered snapshot
    var entries = client.Snapshot();

    // Print topology dump
    foreach (var e in entries) {
        string kindStr = e.ServiceKind switch {
            ServiceKind.SpotPub or ServiceKind.SpotSub => "SPOT",
            ServiceKind.Socket => "SOCK",
            ServiceKind.Discovery => "DISC",
            _ => "?"
        };
        Console.WriteLine($"[{kindStr}] {e.ServiceName} @ {e.Endpoint}" +
            $"  state={e.State}  ready={e.ReadyCount}/{e.DesiredCount}");
    }

    // Filtered remote query
    var filter = new TopologyFilter { State = TopologyState.Lost };
    var lost = client.Snapshot(filter);
    Console.WriteLine($"LOST entries: {lost.Length}");
    ```

=== "Rust"

    ```rust
    let ctx = zlink::Context::new()?;

    // Create query client and connect to Registry ROUTER endpoint
    let client = zlink::RegistryQueryClient::new(&ctx)?;
    client.connect("tcp://registry1:5551")?;

    // Unfiltered snapshot
    let entries = client.snapshot(None)?;

    // Print topology dump
    for e in &entries {
        let kind_str = match e.service_kind() {
            zlink::ServiceKind::SpotPub | zlink::ServiceKind::SpotSub => "SPOT",
            zlink::ServiceKind::Socket => "SOCK",
            zlink::ServiceKind::Discovery => "DISC",
            _ => "?",
        };
        println!("[{}] {} @ {}  state={:?}  ready={}/{}",
                 kind_str, e.service_name(), e.endpoint(),
                 e.state(), e.ready_count(), e.desired_count());
    }

    // Filtered remote query
    let filter = zlink::TopologyFilter::new()
        .state(zlink::TopologyState::Lost);
    let lost = client.snapshot(Some(&filter))?;
    println!("LOST entries: {}", lost.len());

    // Cleanup
    client.destroy()?;
    ctx.term()?;
    ```

=== "Go"

    ```go
    ctx, err := zlink.NewContext()
    if err != nil { log.Fatal(err) }

    // Create query client and connect to Registry ROUTER endpoint
    client, err := zlink.NewRegistryQueryClient(ctx)
    if err != nil { log.Fatal(err) }
    client.Connect("tcp://registry1:5551")

    // Unfiltered snapshot
    entries, err := client.Snapshot(nil)
    if err != nil { log.Fatal(err) }

    // Print topology dump
    for _, e := range entries {
        kindStr := "?"
        switch e.ServiceKind() {
        case zlink.ServiceKindSpotPub, zlink.ServiceKindSpotSub:
            kindStr = "SPOT"
        case zlink.ServiceKindSocket:
            kindStr = "SOCK"
        case zlink.ServiceKindDiscovery:
            kindStr = "DISC"
        }
        fmt.Printf("[%s] %s @ %s  state=%d  ready=%d/%d\n",
            kindStr, e.ServiceName(), e.Endpoint(),
            e.State(), e.ReadyCount(), e.DesiredCount())
    }

    // Filtered remote query
    filter := &zlink.TopologyFilter{State: zlink.TopologyStateLost}
    lost, err := client.Snapshot(filter)
    if err != nil { log.Fatal(err) }
    fmt.Printf("LOST entries: %d\n", len(lost))

    // Cleanup
    client.Destroy()
    ctx.Term()
    ```

### 6.3 Member Peer 조회

Registry와 Discovery는 서비스의 피어별 라우팅 속성(`value`)과 opaque
메타데이터를 노출하는 member peer 조회를 제공한다. 가중치 기반 라우팅
결정과 운영 모니터링에 유용하다.

#### Registry Member Peer 조회

=== "C"

    ```c
    /* Query member peers of a specific service from the local Registry */
    size_t count = 0;
    zlink_registry_member_peers(registry,
        ZLINK_SERVICE_TYPE_SOCKET, "payment-service", NULL, &count);

    zlink_member_peer_entry_t *peers = malloc(
        count * sizeof(zlink_member_peer_entry_t));
    zlink_registry_member_peers(registry,
        ZLINK_SERVICE_TYPE_SOCKET, "payment-service", peers, &count);

    for (size_t i = 0; i < count; i++) {
        printf("service=%s endpoint=%s value=%lld\n",
               peers[i].service_name,
               peers[i].endpoint,
               (long long)peers[i].value);
    }
    free(peers);
    ```

=== "C++"

    ```cpp
    // Query member peers of a specific service from the local Registry
    auto peers = registry.member_peers(
        zlink::service_type::socket, "payment-service");
    for (const auto& p : peers) {
        std::println("service={} endpoint={} value={}",
                     p.service_name(), p.endpoint(), p.value());
    }
    ```

=== "Java"

    ```java
    // Query member peers of a specific service from the local Registry
    var peers = registry.memberPeers(ServiceType.SOCKET, "payment-service");
    for (var p : peers) {
        System.out.printf("service=%s endpoint=%s value=%d%n",
                          p.serviceName(), p.endpoint(), p.value());
    }
    ```

=== "Python"

    ```python
    # Query member peers of a specific service from the local Registry
    peers = registry.member_peers(zlink.SERVICE_TYPE_SOCKET, "payment-service")
    for p in peers:
        print(f"service={p.service_name} endpoint={p.endpoint} value={p.value}")
    ```

=== "Node/TypeScript"

    ```typescript
    // Query member peers of a specific service from the local Registry
    const peers = registry.memberPeers(
        zlink.SERVICE_TYPE_SOCKET, "payment-service");
    for (const p of peers) {
        console.log(`service=${p.serviceName} endpoint=${p.endpoint} value=${p.value}`);
    }
    ```

=== "C#/.NET"

    ```csharp
    // Query member peers of a specific service from the local Registry
    var peers = registry.MemberPeers(ServiceType.Socket, "payment-service");
    foreach (var p in peers) {
        Console.WriteLine($"service={p.ServiceName} endpoint={p.Endpoint} value={p.Value}");
    }
    ```

=== "Rust"

    ```rust
    // Query member peers of a specific service from the local Registry
    let peers = registry.member_peers(
        zlink::ServiceType::Socket, "payment-service")?;
    for p in &peers {
        println!("service={} endpoint={} value={}",
                 p.service_name(), p.endpoint(), p.value());
    }
    ```

=== "Go"

    ```go
    // Query member peers of a specific service from the local Registry
    peers, err := registry.MemberPeers(
        zlink.ServiceTypeSocket, "payment-service")
    if err != nil { log.Fatal(err) }
    for _, p := range peers {
        fmt.Printf("service=%s endpoint=%s value=%d\n",
            p.ServiceName(), p.Endpoint(), p.Value())
    }
    ```

#### Member Peer 메타데이터

=== "C"

    ```c
    /* Retrieve opaque metadata blob for a specific peer */
    zlink_msg_t metadata;
    zlink_msg_init(&metadata);
    int rc = zlink_registry_member_peer_metadata(registry,
        ZLINK_SERVICE_TYPE_SOCKET, "payment-service",
        ZLINK_SERVICE_ROLE_ROUTER, "tcp://10.0.1.5:5555",
        &metadata);
    if (rc == 0) {
        printf("metadata size=%zu\n", zlink_msg_size(&metadata));
    }
    zlink_msg_close(&metadata);
    ```

=== "C++"

    ```cpp
    // Retrieve opaque metadata blob for a specific peer
    auto metadata = registry.member_peer_metadata(
        zlink::service_type::socket, "payment-service",
        zlink::service_role::router, "tcp://10.0.1.5:5555");
    if (metadata) {
        std::println("metadata size={}", metadata->size());
    }
    ```

=== "Java"

    ```java
    // Retrieve opaque metadata blob for a specific peer
    byte[] metadata = registry.memberPeerMetadata(
        ServiceType.SOCKET, "payment-service",
        ServiceRole.ROUTER, "tcp://10.0.1.5:5555");
    if (metadata != null) {
        System.out.printf("metadata size=%d%n", metadata.length);
    }
    ```

=== "Python"

    ```python
    # Retrieve opaque metadata blob for a specific peer
    metadata = registry.member_peer_metadata(
        zlink.SERVICE_TYPE_SOCKET, "payment-service",
        zlink.SERVICE_ROLE_ROUTER, "tcp://10.0.1.5:5555")
    if metadata is not None:
        print(f"metadata size={len(metadata)}")
    ```

=== "Node/TypeScript"

    ```typescript
    // Retrieve opaque metadata blob for a specific peer
    const metadata = registry.memberPeerMetadata(
        zlink.SERVICE_TYPE_SOCKET, "payment-service",
        zlink.SERVICE_ROLE_ROUTER, "tcp://10.0.1.5:5555");
    if (metadata) {
        console.log(`metadata size=${metadata.length}`);
    }
    ```

=== "C#/.NET"

    ```csharp
    // Retrieve opaque metadata blob for a specific peer
    var metadata = registry.MemberPeerMetadata(
        ServiceType.Socket, "payment-service",
        ServiceRole.Router, "tcp://10.0.1.5:5555");
    if (metadata != null) {
        Console.WriteLine($"metadata size={metadata.Length}");
    }
    ```

=== "Rust"

    ```rust
    // Retrieve opaque metadata blob for a specific peer
    let metadata = registry.member_peer_metadata(
        zlink::ServiceType::Socket, "payment-service",
        zlink::ServiceRole::Router, "tcp://10.0.1.5:5555")?;
    if let Some(data) = metadata {
        println!("metadata size={}", data.len());
    }
    ```

=== "Go"

    ```go
    // Retrieve opaque metadata blob for a specific peer
    metadata, err := registry.MemberPeerMetadata(
        zlink.ServiceTypeSocket, "payment-service",
        zlink.ServiceRoleRouter, "tcp://10.0.1.5:5555")
    if err != nil { log.Fatal(err) }
    if metadata != nil {
        fmt.Printf("metadata size=%d\n", len(metadata))
    }
    ```

#### Member Peer 엔트리 필드

| 필드 | 설명 |
|------|------|
| `service_type` | 서비스 타입 (`ZLINK_SERVICE_TYPE_*`) |
| `service_role` | 서비스 인스턴스의 역할 |
| `service_name` | null 종료 서비스 이름 |
| `endpoint` | null 종료 엔드포인트 |
| `routing_id` | 피어의 라우팅 아이덴티티 |
| `value` | 서비스별 숫자 값 (`int64_t`) |

#### Discovery Member Peer 조회

=== "C"

    ```c
    /* Query member peers from the local Discovery cache */
    size_t count = 0;
    zlink_discovery_member_peers(discovery, NULL, &count);

    zlink_member_peer_entry_t *peers = malloc(
        count * sizeof(zlink_member_peer_entry_t));
    zlink_discovery_member_peers(discovery, peers, &count);

    for (size_t i = 0; i < count; i++) {
        printf("[%s] endpoint=%s role=%u value=%lld\n",
               peers[i].service_name,
               peers[i].endpoint,
               peers[i].service_role,
               (long long)peers[i].value);
    }
    free(peers);

    /* Retrieve metadata for a specific peer via Discovery */
    zlink_msg_t metadata;
    zlink_msg_init(&metadata);
    zlink_discovery_member_peer_metadata(discovery,
        ZLINK_SERVICE_ROLE_ROUTER, "tcp://10.0.1.5:5555",
        &metadata);
    printf("metadata size=%zu\n", zlink_msg_size(&metadata));
    zlink_msg_close(&metadata);
    ```

=== "C++"

    ```cpp
    // Query member peers from the local Discovery cache
    auto peers = discovery.member_peers();
    for (const auto& p : peers) {
        std::println("[{}] endpoint={} role={} value={}",
                     p.service_name(), p.endpoint(), p.service_role(), p.value());
    }

    // Retrieve metadata for a specific peer via Discovery
    auto metadata = discovery.member_peer_metadata(
        zlink::service_role::router, "tcp://10.0.1.5:5555");
    std::println("metadata size={}", metadata.size());
    ```

=== "Java"

    ```java
    // Query member peers from the local Discovery cache
    var peers = discovery.memberPeers();
    for (var p : peers) {
        System.out.printf("[%s] endpoint=%s role=%d value=%d%n",
                          p.serviceName(), p.endpoint(), p.serviceRole(), p.value());
    }

    // Retrieve metadata for a specific peer via Discovery
    byte[] metadata = discovery.memberPeerMetadata(
        ServiceRole.ROUTER, "tcp://10.0.1.5:5555");
    System.out.printf("metadata size=%d%n", metadata.length);
    ```

=== "Python"

    ```python
    # Query member peers from the local Discovery cache
    peers = discovery.member_peers()
    for p in peers:
        print(f"[{p.service_name}] endpoint={p.endpoint}"
              f" role={p.service_role} value={p.value}")

    # Retrieve metadata for a specific peer via Discovery
    metadata = discovery.member_peer_metadata(
        zlink.SERVICE_ROLE_ROUTER, "tcp://10.0.1.5:5555")
    print(f"metadata size={len(metadata)}")
    ```

=== "Node/TypeScript"

    ```typescript
    // Query member peers from the local Discovery cache
    const peers = discovery.memberPeers();
    for (const p of peers) {
        console.log(`[${p.serviceName}] endpoint=${p.endpoint}` +
            ` role=${p.serviceRole} value=${p.value}`);
    }

    // Retrieve metadata for a specific peer via Discovery
    const metadata = discovery.memberPeerMetadata(
        zlink.SERVICE_ROLE_ROUTER, "tcp://10.0.1.5:5555");
    console.log(`metadata size=${metadata.length}`);
    ```

=== "C#/.NET"

    ```csharp
    // Query member peers from the local Discovery cache
    var peers = discovery.MemberPeers();
    foreach (var p in peers) {
        Console.WriteLine($"[{p.ServiceName}] endpoint={p.Endpoint}" +
            $" role={p.ServiceRole} value={p.Value}");
    }

    // Retrieve metadata for a specific peer via Discovery
    var metadata = discovery.MemberPeerMetadata(
        ServiceRole.Router, "tcp://10.0.1.5:5555");
    Console.WriteLine($"metadata size={metadata.Length}");
    ```

=== "Rust"

    ```rust
    // Query member peers from the local Discovery cache
    let peers = discovery.member_peers()?;
    for p in &peers {
        println!("[{}] endpoint={} role={:?} value={}",
                 p.service_name(), p.endpoint(), p.service_role(), p.value());
    }

    // Retrieve metadata for a specific peer via Discovery
    let metadata = discovery.member_peer_metadata(
        zlink::ServiceRole::Router, "tcp://10.0.1.5:5555")?;
    println!("metadata size={}", metadata.len());
    ```

=== "Go"

    ```go
    // Query member peers from the local Discovery cache
    peers, err := discovery.MemberPeers()
    if err != nil { log.Fatal(err) }
    for _, p := range peers {
        fmt.Printf("[%s] endpoint=%s role=%d value=%d\n",
            p.ServiceName(), p.Endpoint(), p.ServiceRole(), p.Value())
    }

    // Retrieve metadata for a specific peer via Discovery
    metadata, err := discovery.MemberPeerMetadata(
        zlink.ServiceRoleRouter, "tcp://10.0.1.5:5555")
    if err != nil { log.Fatal(err) }
    fmt.Printf("metadata size=%d\n", len(metadata))
    ```

## 7. 운영 패턴

### 7.1 서비스 등록/해제 흐름

```
SpotNode/Socket       Discovery              Registry
    │                     │                      │
    │ attach_discovery    │                      │
    │ + bind              │                      │
    │────────────────────►│                      │
    │                     │ bootstrap + REGISTER │
    │                     │─────────────────────►│
    │                     │                      │ (서비스 목록에 추가)
    │                     │      REGISTER_ACK    │
    │                     │◄─────────────────────│
    │                     │                      │
    │                     │   HEARTBEAT (5초)    │
    │                     │─────────────────────►│
    │                     │                      │
    │                     │   HEARTBEAT (5초)    │
    │                     │─────────────────────►│
    │                     │                      │
    │ destroy             │                      │
    │────────────────────►│                      │
    │                     │     UNREGISTER       │
    │                     │─────────────────────►│
    │                     │                      │ (목록에서 제거)
```

### 7.2 하트비트 타임아웃 및 자동 제거

서비스가 `timeout_ms`(기본 15초) 이내에 하트비트를 보내지 않으면, Registry가
자동으로 서비스 목록에서 제거한다. 제거는 다음 SERVICE_LIST 발행 시 모든
Discovery 인스턴스에 브로드캐스트된다.

### 7.3 Discovery Failover

- Discovery는 하나 이상의 Registry ROUTER 엔드포인트에 bootstrap 연결
- bootstrap 메타데이터로 내부 broadcast/uplink 경로를 학습
- 한 Registry 노드가 실패해도 다른 bootstrap 엔드포인트를 통해 계속 동작
- Discovery의 failover 로직을 통해 서비스가 자동으로 재등록

### 7.4 클러스터 내 Registry 노드 장애

- 생존 Registry 노드가 독립적으로 계속 동작
- 각 노드는 자체 서비스 목록을 유지
- 생존 노드에 연결된 Discovery 클라이언트는 영향 없음
- 장애 노드가 복구되면 flooding 메커니즘으로 재동기화
- Eventually consistent: 모든 노드가 동일 상태로 수렴

## 8. 역할 분리: Registry vs Monitor

Registry와 로컬 서비스 모니터는 다른 목적을 가진다:

| 측면 | Registry 토폴로지 | 로컬 서비스 모니터 |
|------|-------------------|-------------------|
| **범위** | 글로벌 서비스 요약 | 단일 handle 상세 상태 |
| **세분도** | `READY`/`LOST`/`ERROR` | 개별 연결 이벤트 |
| **최신성** | Eventually consistent | 실시간 (즉시 콜백) |
| **접근** | 로컬 또는 원격 쿼리 | 로컬만 (같은 프로세스) |

### 언제 어느 것을 사용할 것인가

- **Registry 토폴로지**: "클러스터 전체에서 `payment-service` 인스턴스가 몇
  개 READY인가?" — 1차 운영 판단
- **로컬 모니터**: "이 특정 서비스가 왜 peer X에 연결하지 못하는가?" — 상세
  원인 분석

권장 워크플로우:

1. Registry 토폴로지 스냅샷으로 글로벌 현황 파악
2. 이상 항목 식별 (`LOST`, `ERROR` 엔트리)
3. 해당 프로세스의 로컬 서비스 모니터로 상세 분석

## 9. 다음 단계

- [Service Discovery](07-1-discovery.ko.md) -- 기반 인프라
- [SPOT PUB/SUB](07-3-spot.ko.md) -- 위치투명 발행/구독
- [Registry API 레퍼런스](../api/registry.ko.md) -- 전체 API 문서

---
[← SPOT](07-3-spot.ko.md) | [Routing ID →](08-routing-id.ko.md)
