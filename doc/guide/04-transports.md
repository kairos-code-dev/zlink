English | [한국어](04-transports.ko.md)

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

### Wildcard Port (Auto-Assignment)

The OS automatically assigns an available port. Useful for tests or dynamic port environments.

```c
/* Use port 0 or * */
zlink_bind(socket, "tcp://127.0.0.1:*");

/* Query the assigned endpoint */
char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(socket, ZLINK_LAST_ENDPOINT, endpoint, &len);
/* endpoint = "tcp://127.0.0.1:53821" (example) */

/* Connect using the retrieved endpoint */
zlink_connect(other_socket, endpoint);
```

> Reference: `core/tests/test_pair_tcp.cpp` — `bind_loopback_ipv4()` wildcard bind pattern

### Using DNS Names

When a hostname is used with connect, DNS resolution is performed internally.

```c
/* Connect using DNS name */
zlink_connect(socket, "tcp://localhost:5555");
```

> Note: DNS resolution is blocking. Using IP addresses is recommended in production.
> Reference: `core/tests/test_pair_tcp.cpp` — `test_pair_tcp_connect_by_name()`

### Error Handling

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

### Characteristics

- **TCP_NODELAY** enabled (Nagle algorithm disabled)
- **Speculative write** — attempts synchronous write first, falls back to async on failure
- **Gather write** — sends header and body together (reduces system calls)

> For internal optimization details such as speculative write, see [architecture.md](../internals/architecture.md).

## 3. IPC

Local inter-process communication based on Unix domain sockets.

### Basic Usage

```c
/* Server */
zlink_bind(socket, "ipc:///tmp/myapp.ipc");

/* Client */
zlink_connect(socket, "ipc:///tmp/myapp.ipc");
```

### Wildcard Bind

```c
/* IPC wildcard — auto-assigns a temporary path */
zlink_bind(socket, "ipc://*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(socket, ZLINK_LAST_ENDPOINT, endpoint, &len);
```

> Reference: `core/tests/test_router_multiple_dealers.cpp` — `zlink_bind(router, "ipc://*")`

### Error Handling

```c
/* Path too long */
int rc = zlink_bind(socket, "ipc:///very/long/path/.../endpoint.ipc");
if (rc == -1 && errno == ENAMETOOLONG) {
    printf("IPC path exceeds system limit (108 characters)\n");
}
```

> Reference: `core/tests/test_pair_ipc.cpp` — `test_endpoint_too_long()`

### Characteristics

- **Supported on Linux/macOS only** (not supported on Windows)
- Lower overhead than TCP (bypasses network stack)
- File path-based address (max path length: 108 characters)

## 4. inproc

In-process communication. The fastest transport.

### Basic Usage

```c
/* bind must be called first */
zlink_bind(socket_a, "inproc://workers");
zlink_connect(socket_b, "inproc://workers");
```

### Error Handling

```c
/* Attempting connect without bind */
int rc = zlink_connect(socket, "inproc://nonexistent");
if (rc == -1) {
    printf("No bind exists yet\n");
}
```

### Characteristics

- Usable **only within the same context**
- **bind must be called before** connect
- Direct lock-free pipe connection (no network)
- Lowest latency, highest throughput

> Reference: `core/tests/test_pair_inproc.cpp` — bind → connect → bounce pattern

## 5. WebSocket (ws)

Integration with web browsers and external clients.

### Basic Usage

```c
/* Server */
zlink_bind(socket, "ws://*:8080");

/* Client */
zlink_connect(socket, "ws://server:8080");

/* Wildcard port */
zlink_bind(socket, "ws://127.0.0.1:*");
char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(socket, ZLINK_LAST_ENDPOINT, endpoint, &len);
```

> Reference: `core/tests/test_stream_socket.cpp` — `test_stream_ws_basic()`

### Characteristics

- RFC 6455 compliant
- Based on the Beast library
- Binary frame mode (Opcode=0x02)
- 64KB write buffer
- **Only usable with STREAM sockets**

## 6. WebSocket + TLS (wss)

Encrypted WebSocket communication.

### Basic Usage

```c
/* Server */
zlink_setsockopt(socket, ZLINK_TLS_CERT, cert_path, 0);
zlink_setsockopt(socket, ZLINK_TLS_KEY, key_path, 0);
zlink_bind(socket, "wss://*:8443");

/* Client */
int trust_system = 0;
zlink_setsockopt(socket, ZLINK_TLS_TRUST_SYSTEM, &trust_system, sizeof(trust_system));
zlink_setsockopt(socket, ZLINK_TLS_CA, ca_path, 0);
zlink_setsockopt(socket, ZLINK_TLS_HOSTNAME, "localhost", 9);
zlink_connect(socket, "wss://server:8443");
```

> Reference: `core/tests/test_stream_socket.cpp` — `test_stream_wss_basic()`

### Additional Configuration Compared to ws

| Setting | ws | wss |
|---------|:--:|:---:|
| `ZLINK_TLS_CERT` (server) | - | Required |
| `ZLINK_TLS_KEY` (server) | - | Required |
| `ZLINK_TLS_CA` (client) | - | Recommended |
| `ZLINK_TLS_HOSTNAME` (client) | - | Recommended |
| `ZLINK_TLS_TRUST_SYSTEM` (client) | - | Optional |

## 7. TLS

Native TLS encrypted communication.

### Basic Usage

```c
/* Server */
zlink_setsockopt(socket, ZLINK_TLS_CERT, "/path/to/cert.pem", 0);
zlink_setsockopt(socket, ZLINK_TLS_KEY, "/path/to/key.pem", 0);
zlink_bind(socket, "tls://*:5555");

/* Client */
zlink_setsockopt(socket, ZLINK_TLS_CA, "/path/to/ca.pem", 0);
zlink_connect(socket, "tls://server:5555");
```

For detailed TLS configuration, see the [TLS Security Guide](05-tls-security.md).

## 8. Transport Constraints

| Constraint | Description |
|------------|-------------|
| ws/wss → STREAM only | ws and wss transports support only STREAM sockets. tls supports all socket types |
| inproc bind first | inproc requires bind to be called before connect |
| ipc platform | ipc is only supported on Unix/Linux/macOS (not supported on Windows) |
| Same context | inproc is usable only within the same context |
| IPC path length | Unix domain socket path maximum of 108 characters |

## 9. Transport Selection Decision Flow

```
Is the communication peer an external client?
├── Yes → Encryption needed?
│         ├── Yes → WebSocket? → wss://
│         │         └── No → tls://
│         └── No → WebSocket? → ws://
│                   └── No → tcp:// (STREAM)
└── No → Same process?
         ├── Yes → inproc://
         └── No → Same machine?
                  ├── Yes → Unix? → ipc://
                  │         └── Windows → tcp://
                  └── No → Encryption needed?
                           ├── Yes → tls://
                           └── No → tcp://
```

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

```c
/* Multiple bind — listen on multiple interfaces */
zlink_bind(router, "tcp://192.168.1.10:5555");
zlink_bind(router, "tcp://10.0.0.1:5555");
zlink_bind(router, "ipc:///tmp/router.ipc");

/* Multiple connect — connect to multiple servers */
zlink_connect(dealer, "tcp://server1:5555");
zlink_connect(dealer, "tcp://server2:5555");
```

### ZLINK_LAST_ENDPOINT

Query the actual assigned endpoint after a wildcard bind.

```c
zlink_bind(socket, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(socket, ZLINK_LAST_ENDPOINT, endpoint, &len);
printf("Bound endpoint: %s\n", endpoint);
```

For performance comparisons, see the [Performance Guide](10-performance.md).

---
[← STREAM](03-5-stream.md) | [TLS Security →](05-tls-security.md)
