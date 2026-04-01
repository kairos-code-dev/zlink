
# Transport Guide

## 1. Transport Types

| Transport | URI Format | Example | Encryption | Handshake |
|-----------|------------|---------|:----------:|:---------:|
| tcp | `tcp://host:port` | `tcp://127.0.0.1:5555` | - | - |
| ipc | `ipc://path` | `ipc:///tmp/test.ipc` | - | - |
| inproc | `inproc://name` | `inproc://workers` | - | - |
| ws | `ws://host:port` | `ws://127.0.0.1:8080` | - | O |
| wss | `wss://host:port` | `wss://server:8443` | O | O |
| tls | `tls://host:port` | `tls://server:5555` | O | O |

## 2. TCP

Standard TCP/IP network communication.

### Basic Usage

=== "C"

    ```c
    /* Server: specific interface */
    zlink_bind(socket, "tcp://192.168.1.10:5555");

    /* Server: all interfaces */
    zlink_bind(socket, "tcp://*:5555");

    /* Client: IP address */
    zlink_connect(socket, "tcp://127.0.0.1:5555");

    /* Client: DNS name */
    zlink_connect(socket, "tcp://server.example.com:5555");
    ```

=== "C++"

    ```cpp
    socket.bind("tcp://192.168.1.10:5555");
    socket.bind("tcp://*:5555");
    socket.connect("tcp://127.0.0.1:5555");
    socket.connect("tcp://server.example.com:5555");
    ```

=== "Java"

    ```java
    socket.bind("tcp://192.168.1.10:5555");
    socket.bind("tcp://*:5555");
    socket.connect("tcp://127.0.0.1:5555");
    socket.connect("tcp://server.example.com:5555");
    ```

=== "Python"

    ```python
    socket.bind("tcp://192.168.1.10:5555")
    socket.bind("tcp://*:5555")
    socket.connect("tcp://127.0.0.1:5555")
    socket.connect("tcp://server.example.com:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("tcp://192.168.1.10:5555");
    socket.bind("tcp://*:5555");
    socket.connect("tcp://127.0.0.1:5555");
    socket.connect("tcp://server.example.com:5555");
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("tcp://192.168.1.10:5555");
    socket.Bind("tcp://*:5555");
    socket.Connect("tcp://127.0.0.1:5555");
    socket.Connect("tcp://server.example.com:5555");
    ```

=== "Rust"

    ```rust
    socket.bind("tcp://192.168.1.10:5555")?;
    socket.bind("tcp://*:5555")?;
    socket.connect("tcp://127.0.0.1:5555")?;
    socket.connect("tcp://server.example.com:5555")?;
    ```

### Wildcard Port (Auto-Assignment)

The OS automatically assigns an available port. Useful for tests or dynamic port environments.

=== "C"

    ```c
    /* Use port 0 or * */
    zlink_bind(socket, "tcp://127.0.0.1:*");

    /* Query the assigned endpoint */
    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(socket, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);
    /* endpoint = "tcp://127.0.0.1:53821" (example) */

    /* Connect using the retrieved endpoint */
    zlink_connect(other_socket, endpoint);
    ```

=== "C++"

    ```cpp
    socket.bind("tcp://127.0.0.1:*");
    std::string endpoint = socket.last_endpoint();
    other_socket.connect(endpoint);
    ```

=== "Java"

    ```java
    socket.bind("tcp://127.0.0.1:*");
    String endpoint = socket.lastEndpoint();
    otherSocket.connect(endpoint);
    ```

=== "Python"

    ```python
    socket.bind("tcp://127.0.0.1:*")
    endpoint = socket.last_endpoint()
    other_socket.connect(endpoint)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("tcp://127.0.0.1:*");
    const endpoint = socket.lastEndpoint();
    otherSocket.connect(endpoint);
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("tcp://127.0.0.1:*");
    var endpoint = socket.LastEndpoint;
    otherSocket.Connect(endpoint);
    ```

=== "Rust"

    ```rust
    socket.bind("tcp://127.0.0.1:*")?;
    let endpoint = socket.last_endpoint()?;
    other_socket.connect(&endpoint)?;
    ```

> Reference: `core/tests/test_pair_tcp.cpp` -- `bind_loopback_ipv4()` wildcard bind pattern

### Using DNS Names

When a hostname is used with connect, DNS resolution is performed internally.

=== "C"

    ```c
    /* Connect using DNS name */
    zlink_connect(socket, "tcp://localhost:5555");
    ```

=== "C++"

    ```cpp
    socket.connect("tcp://localhost:5555");
    ```

=== "Java"

    ```java
    socket.connect("tcp://localhost:5555");
    ```

=== "Python"

    ```python
    socket.connect("tcp://localhost:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.connect("tcp://localhost:5555");
    ```

=== "C#/.NET"

    ```csharp
    socket.Connect("tcp://localhost:5555");
    ```

=== "Rust"

    ```rust
    socket.connect("tcp://localhost:5555")?;
    ```

> Note: DNS resolution is blocking. Using IP addresses is recommended in production.
> Reference: `core/tests/test_pair_tcp.cpp` -- `test_pair_tcp_connect_by_name()`

### Error Handling

=== "C"

    ```c
    /* bind failure: port already in use */
    int rc = zlink_bind(socket, "tcp://*:5555");
    if (rc == -1) {
        if (errno == EADDRINUSE)
            printf("Port 5555 already in use\n");
    }

    /* connect failure: invalid address */
    rc = zlink_connect(socket, "tcp://invalid:99999");
    if (rc == -1) {
        printf("Connection failed: %s\n", zlink_strerror(errno));
    }
    ```

=== "C++"

    ```cpp
    try {
        socket.bind("tcp://*:5555");
    } catch (const zlink::error_t& e) {
        // Port 5555 already in use
    }

    try {
        socket.connect("tcp://invalid:99999");
    } catch (const zlink::error_t& e) {
        std::cerr << "Connection failed: " << e.what() << "\n";
    }
    ```

=== "Java"

    ```java
    try {
        socket.bind("tcp://*:5555");
    } catch (ZlinkException e) {
        // Port 5555 already in use
    }

    try {
        socket.connect("tcp://invalid:99999");
    } catch (ZlinkException e) {
        System.err.println("Connection failed: " + e.getMessage());
    }
    ```

=== "Python"

    ```python
    try:
        socket.bind("tcp://*:5555")
    except zlink.ZlinkError:
        pass  # Port 5555 already in use

    try:
        socket.connect("tcp://invalid:99999")
    except zlink.ZlinkError as e:
        print(f"Connection failed: {e}")
    ```

=== "Node/TypeScript"

    ```typescript
    try {
        socket.bind("tcp://*:5555");
    } catch (e) {
        // Port 5555 already in use
    }

    try {
        socket.connect("tcp://invalid:99999");
    } catch (e) {
        console.error(`Connection failed: ${e}`);
    }
    ```

=== "C#/.NET"

    ```csharp
    try {
        socket.Bind("tcp://*:5555");
    } catch (ZlinkException) {
        // Port 5555 already in use
    }

    try {
        socket.Connect("tcp://invalid:99999");
    } catch (ZlinkException e) {
        Console.Error.WriteLine($"Connection failed: {e.Message}");
    }
    ```

=== "Rust"

    ```rust
    if let Err(e) = socket.bind("tcp://*:5555") {
        // Port 5555 already in use
    }

    if let Err(e) = socket.connect("tcp://invalid:99999") {
        eprintln!("Connection failed: {}", e);
    }
    ```

### Characteristics

- **TCP_NODELAY** enabled (Nagle algorithm disabled)
- **Speculative write** -- attempts synchronous write first, falls back to async on failure
- **Gather write** -- sends header and body together (reduces system calls)

> For internal optimization details such as speculative write, see [architecture.md](../internals/architecture.md).

## 3. IPC

Local inter-process communication based on Unix domain sockets.

### Basic Usage

=== "C"

    ```c
    /* Server */
    zlink_bind(socket, "ipc:///tmp/myapp.ipc");

    /* Client */
    zlink_connect(socket, "ipc:///tmp/myapp.ipc");
    ```

=== "C++"

    ```cpp
    socket.bind("ipc:///tmp/myapp.ipc");
    socket.connect("ipc:///tmp/myapp.ipc");
    ```

=== "Java"

    ```java
    socket.bind("ipc:///tmp/myapp.ipc");
    socket.connect("ipc:///tmp/myapp.ipc");
    ```

=== "Python"

    ```python
    socket.bind("ipc:///tmp/myapp.ipc")
    socket.connect("ipc:///tmp/myapp.ipc")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("ipc:///tmp/myapp.ipc");
    socket.connect("ipc:///tmp/myapp.ipc");
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("ipc:///tmp/myapp.ipc");
    socket.Connect("ipc:///tmp/myapp.ipc");
    ```

=== "Rust"

    ```rust
    socket.bind("ipc:///tmp/myapp.ipc")?;
    socket.connect("ipc:///tmp/myapp.ipc")?;
    ```

### Wildcard Bind

=== "C"

    ```c
    /* IPC wildcard — auto-assigns a temporary path */
    zlink_bind(socket, "ipc://*");

    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(socket, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);
    ```

=== "C++"

    ```cpp
    socket.bind("ipc://*");
    std::string endpoint = socket.last_endpoint();
    ```

=== "Java"

    ```java
    socket.bind("ipc://*");
    String endpoint = socket.lastEndpoint();
    ```

=== "Python"

    ```python
    socket.bind("ipc://*")
    endpoint = socket.last_endpoint()
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("ipc://*");
    const endpoint = socket.lastEndpoint();
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("ipc://*");
    var endpoint = socket.LastEndpoint;
    ```

=== "Rust"

    ```rust
    socket.bind("ipc://*")?;
    let endpoint = socket.last_endpoint()?;
    ```

> Reference: `core/tests/test_router_multiple_dealers.cpp` -- `zlink_bind(router, "ipc://*")`

### Error Handling

=== "C"

    ```c
    /* Path too long */
    int rc = zlink_bind(socket, "ipc:///very/long/path/.../endpoint.ipc");
    if (rc == -1 && errno == ENAMETOOLONG) {
        printf("IPC path exceeds system limit (108 characters)\n");
    }
    ```

=== "C++"

    ```cpp
    try {
        socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    } catch (const zlink::error_t& e) {
        // IPC path exceeds system limit (108 characters)
    }
    ```

=== "Java"

    ```java
    try {
        socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    } catch (ZlinkException e) {
        // IPC path exceeds system limit (108 characters)
    }
    ```

=== "Python"

    ```python
    try:
        socket.bind("ipc:///very/long/path/.../endpoint.ipc")
    except zlink.ZlinkError:
        pass  # IPC path exceeds system limit (108 characters)
    ```

=== "Node/TypeScript"

    ```typescript
    try {
        socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    } catch (e) {
        // IPC path exceeds system limit (108 characters)
    }
    ```

=== "C#/.NET"

    ```csharp
    try {
        socket.Bind("ipc:///very/long/path/.../endpoint.ipc");
    } catch (ZlinkException) {
        // IPC path exceeds system limit (108 characters)
    }
    ```

=== "Rust"

    ```rust
    if let Err(e) = socket.bind("ipc:///very/long/path/.../endpoint.ipc") {
        // IPC path exceeds system limit (108 characters)
    }
    ```

> Reference: `core/tests/test_pair_ipc.cpp` -- `test_endpoint_too_long()`

### Characteristics

- **Supported on Linux/macOS only** (not supported on Windows)
- Lower overhead than TCP (bypasses network stack)
- File path-based address (max path length: 108 characters)

## 4. inproc

In-process communication. The fastest transport.

### Basic Usage

=== "C"

    ```c
    /* bind must be called first */
    zlink_bind(socket_a, "inproc://workers");
    zlink_connect(socket_b, "inproc://workers");
    ```

=== "C++"

    ```cpp
    socket_a.bind("inproc://workers");
    socket_b.connect("inproc://workers");
    ```

=== "Java"

    ```java
    socketA.bind("inproc://workers");
    socketB.connect("inproc://workers");
    ```

=== "Python"

    ```python
    socket_a.bind("inproc://workers")
    socket_b.connect("inproc://workers")
    ```

=== "Node/TypeScript"

    ```typescript
    socketA.bind("inproc://workers");
    socketB.connect("inproc://workers");
    ```

=== "C#/.NET"

    ```csharp
    socketA.Bind("inproc://workers");
    socketB.Connect("inproc://workers");
    ```

=== "Rust"

    ```rust
    socket_a.bind("inproc://workers")?;
    socket_b.connect("inproc://workers")?;
    ```

### Error Handling

=== "C"

    ```c
    /* Attempting connect without bind */
    int rc = zlink_connect(socket, "inproc://nonexistent");
    if (rc == -1) {
        printf("No bind exists yet\n");
    }
    ```

=== "C++"

    ```cpp
    try {
        socket.connect("inproc://nonexistent");
    } catch (const zlink::error_t& e) {
        // No bind exists yet
    }
    ```

=== "Java"

    ```java
    try {
        socket.connect("inproc://nonexistent");
    } catch (ZlinkException e) {
        // No bind exists yet
    }
    ```

=== "Python"

    ```python
    try:
        socket.connect("inproc://nonexistent")
    except zlink.ZlinkError:
        pass  # No bind exists yet
    ```

=== "Node/TypeScript"

    ```typescript
    try {
        socket.connect("inproc://nonexistent");
    } catch (e) {
        // No bind exists yet
    }
    ```

=== "C#/.NET"

    ```csharp
    try {
        socket.Connect("inproc://nonexistent");
    } catch (ZlinkException) {
        // No bind exists yet
    }
    ```

=== "Rust"

    ```rust
    if let Err(e) = socket.connect("inproc://nonexistent") {
        // No bind exists yet
    }
    ```

### Characteristics

- Usable **only within the same context**
- **bind must be called before** connect
- Direct lock-free pipe connection (no network)
- Lowest latency, highest throughput

> Reference: `core/tests/test_pair_inproc.cpp` -- bind -> connect -> bounce pattern

## 5. WebSocket (ws)

Integration with web browsers and external clients.

### Basic Usage

=== "C"

    ```c
    /* Server */
    zlink_bind(socket, "ws://*:8080");

    /* Client */
    zlink_connect(socket, "ws://server:8080");

    /* Wildcard port */
    zlink_bind(socket, "ws://127.0.0.1:*");
    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(socket, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);
    ```

=== "C++"

    ```cpp
    socket.bind("ws://*:8080");
    socket.connect("ws://server:8080");
    socket.bind("ws://127.0.0.1:*");
    std::string endpoint = socket.last_endpoint();
    ```

=== "Java"

    ```java
    socket.bind("ws://*:8080");
    socket.connect("ws://server:8080");
    socket.bind("ws://127.0.0.1:*");
    String endpoint = socket.lastEndpoint();
    ```

=== "Python"

    ```python
    socket.bind("ws://*:8080")
    socket.connect("ws://server:8080")
    socket.bind("ws://127.0.0.1:*")
    endpoint = socket.last_endpoint()
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("ws://*:8080");
    socket.connect("ws://server:8080");
    socket.bind("ws://127.0.0.1:*");
    const endpoint = socket.lastEndpoint();
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("ws://*:8080");
    socket.Connect("ws://server:8080");
    socket.Bind("ws://127.0.0.1:*");
    var endpoint = socket.LastEndpoint;
    ```

=== "Rust"

    ```rust
    socket.bind("ws://*:8080")?;
    socket.connect("ws://server:8080")?;
    socket.bind("ws://127.0.0.1:*")?;
    let endpoint = socket.last_endpoint()?;
    ```

> Reference: `core/tests/test_stream_socket.cpp` -- `test_stream_ws_basic()`

### Characteristics

- RFC 6455 compliant
- Based on the Beast library
- Binary frame mode (Opcode=0x02)
- 64KB write buffer
- **Only usable with STREAM sockets**

## 6. WebSocket + TLS (wss)

Encrypted WebSocket communication.

### Basic Usage

=== "C"

    ```c
    /* Server */
    zlink_set_tls_server(socket, cert_path, key_path, 0);
    zlink_bind(socket, "wss://*:8443");

    /* Client */
    zlink_set_tls_client(socket, ca_path, "localhost", 0);
    zlink_connect(socket, "wss://server:8443");
    ```

=== "C++"

    ```cpp
    socket.set_tls_server(cert_path, key_path, 0);
    socket.bind("wss://*:8443");

    socket.set_tls_client(ca_path, "localhost", 0);
    socket.connect("wss://server:8443");
    ```

=== "Java"

    ```java
    socket.setTlsServer(certPath, keyPath, 0);
    socket.bind("wss://*:8443");

    socket.setTlsClient(caPath, "localhost", 0);
    socket.connect("wss://server:8443");
    ```

=== "Python"

    ```python
    socket.set_tls_server(cert_path, key_path, 0)
    socket.bind("wss://*:8443")

    socket.set_tls_client(ca_path, "localhost", 0)
    socket.connect("wss://server:8443")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setTlsServer(certPath, keyPath, 0);
    socket.bind("wss://*:8443");

    socket.setTlsClient(caPath, "localhost", 0);
    socket.connect("wss://server:8443");
    ```

=== "C#/.NET"

    ```csharp
    socket.SetTlsServer(certPath, keyPath, 0);
    socket.Bind("wss://*:8443");

    socket.SetTlsClient(caPath, "localhost", 0);
    socket.Connect("wss://server:8443");
    ```

=== "Rust"

    ```rust
    socket.set_tls_server(cert_path, key_path, false)?;
    socket.bind("wss://*:8443")?;

    socket.set_tls_client(ca_path, "localhost", false)?;
    socket.connect("wss://server:8443")?;
    ```

> Reference: `core/tests/test_stream_socket.cpp` -- `test_stream_wss_basic()`

### Additional Configuration Compared to ws

| Setting | ws | wss |
|---------|:--:|:---:|
| `zlink_set_tls_server()` (server cert+key) | - | Required |
| `zlink_set_tls_client()` (client CA+hostname+trust) | - | Recommended |

## 7. TLS

Native TLS encrypted communication.

### Basic Usage

=== "C"

    ```c
    /* Server */
    zlink_set_tls_server(socket, "/path/to/cert.pem", "/path/to/key.pem", 0);
    zlink_bind(socket, "tls://*:5555");

    /* Client */
    zlink_set_tls_client(socket, "/path/to/ca.pem", NULL, 1);
    zlink_connect(socket, "tls://server:5555");
    ```

=== "C++"

    ```cpp
    socket.set_tls_server("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.bind("tls://*:5555");

    socket.set_tls_client("/path/to/ca.pem", "", true);
    socket.connect("tls://server:5555");
    ```

=== "Java"

    ```java
    socket.setTlsServer("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.bind("tls://*:5555");

    socket.setTlsClient("/path/to/ca.pem", null, true);
    socket.connect("tls://server:5555");
    ```

=== "Python"

    ```python
    socket.set_tls_server("/path/to/cert.pem", "/path/to/key.pem", 0)
    socket.bind("tls://*:5555")

    socket.set_tls_client("/path/to/ca.pem", None, True)
    socket.connect("tls://server:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setTlsServer("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.bind("tls://*:5555");

    socket.setTlsClient("/path/to/ca.pem", null, true);
    socket.connect("tls://server:5555");
    ```

=== "C#/.NET"

    ```csharp
    socket.SetTlsServer("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.Bind("tls://*:5555");

    socket.SetTlsClient("/path/to/ca.pem", null, true);
    socket.Connect("tls://server:5555");
    ```

=== "Rust"

    ```rust
    socket.set_tls_server("/path/to/cert.pem", "/path/to/key.pem", false)?;
    socket.bind("tls://*:5555")?;

    socket.set_tls_client("/path/to/ca.pem", None, true)?;
    socket.connect("tls://server:5555")?;
    ```

For detailed TLS configuration, see the [TLS Security Guide](05-tls-security.md).

## 8. Transport Constraints

| Constraint | Description |
|------------|-------------|
| ws/wss -> STREAM only | ws and wss transports support only STREAM sockets. tls supports all socket types |
| inproc bind first | inproc requires bind to be called before connect |
| ipc platform | ipc is only supported on Unix/Linux/macOS (not supported on Windows) |
| Same context | inproc is usable only within the same context |
| IPC path length | Unix domain socket path maximum of 108 characters |

## 9. Transport Selection Guide

| Use Case | Recommended Transport | Notes |
|----------|----------------------|-------|
| Inter-thread communication | inproc | Best performance |
| Local inter-process (Unix) | ipc | Lower overhead than TCP |
| Local inter-process (Windows) | tcp | IPC not supported |
| Inter-server communication | tcp | Standard network communication |
| Encrypted communication | tls | Native TLS |
| Web clients | ws or wss | WebSocket |
| Performance ranking | inproc > ipc > tcp > ws | Increasing overhead |

## 10. bind vs connect

### Basic Principles

- **bind**: The side providing a stable address (server, well-known address)
- **connect**: The side that knows the peer's address and connects (client)

### Multiple bind/connect

A single socket can bind or connect to multiple endpoints.

=== "C"

    ```c
    /* Multiple bind — listen on multiple interfaces */
    zlink_bind(router, "tcp://192.168.1.10:5555");
    zlink_bind(router, "tcp://10.0.0.1:5555");
    zlink_bind(router, "ipc:///tmp/router.ipc");

    /* Multiple connect — connect to multiple servers */
    zlink_connect(dealer, "tcp://server1:5555");
    zlink_connect(dealer, "tcp://server2:5555");
    ```

=== "C++"

    ```cpp
    router.bind("tcp://192.168.1.10:5555");
    router.bind("tcp://10.0.0.1:5555");
    router.bind("ipc:///tmp/router.ipc");

    dealer.connect("tcp://server1:5555");
    dealer.connect("tcp://server2:5555");
    ```

=== "Java"

    ```java
    router.bind("tcp://192.168.1.10:5555");
    router.bind("tcp://10.0.0.1:5555");
    router.bind("ipc:///tmp/router.ipc");

    dealer.connect("tcp://server1:5555");
    dealer.connect("tcp://server2:5555");
    ```

=== "Python"

    ```python
    router.bind("tcp://192.168.1.10:5555")
    router.bind("tcp://10.0.0.1:5555")
    router.bind("ipc:///tmp/router.ipc")

    dealer.connect("tcp://server1:5555")
    dealer.connect("tcp://server2:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    router.bind("tcp://192.168.1.10:5555");
    router.bind("tcp://10.0.0.1:5555");
    router.bind("ipc:///tmp/router.ipc");

    dealer.connect("tcp://server1:5555");
    dealer.connect("tcp://server2:5555");
    ```

=== "C#/.NET"

    ```csharp
    router.Bind("tcp://192.168.1.10:5555");
    router.Bind("tcp://10.0.0.1:5555");
    router.Bind("ipc:///tmp/router.ipc");

    dealer.Connect("tcp://server1:5555");
    dealer.Connect("tcp://server2:5555");
    ```

=== "Rust"

    ```rust
    router.bind("tcp://192.168.1.10:5555")?;
    router.bind("tcp://10.0.0.1:5555")?;
    router.bind("ipc:///tmp/router.ipc")?;

    dealer.connect("tcp://server1:5555")?;
    dealer.connect("tcp://server2:5555")?;
    ```

### ZLINK_OPT_LAST_ENDPOINT

Query the actual assigned endpoint after a wildcard bind.

=== "C"

    ```c
    zlink_bind(socket, "tcp://127.0.0.1:*");

    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(socket, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);
    printf("Bound endpoint: %s\n", endpoint);
    ```

=== "C++"

    ```cpp
    socket.bind("tcp://127.0.0.1:*");
    std::cout << "Bound endpoint: " << socket.last_endpoint() << "\n";
    ```

=== "Java"

    ```java
    socket.bind("tcp://127.0.0.1:*");
    System.out.println("Bound endpoint: " + socket.lastEndpoint());
    ```

=== "Python"

    ```python
    socket.bind("tcp://127.0.0.1:*")
    print(f"Bound endpoint: {socket.last_endpoint()}")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("tcp://127.0.0.1:*");
    console.log(`Bound endpoint: ${socket.lastEndpoint()}`);
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("tcp://127.0.0.1:*");
    Console.WriteLine($"Bound endpoint: {socket.LastEndpoint}");
    ```

=== "Rust"

    ```rust
    socket.bind("tcp://127.0.0.1:*")?;
    println!("Bound endpoint: {}", socket.last_endpoint()?);
    ```

For performance comparisons, see the [Performance Guide](10-performance.md).

---
[← STREAM](03-5-stream.md) | [TLS Security →](05-tls-security.md)
