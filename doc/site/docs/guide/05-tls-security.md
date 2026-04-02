
# TLS/SSL Configuration and Security Guide

## 1. Overview

zlink natively supports `tls://` and `wss://` transports through OpenSSL. Encrypted communication can be configured directly without an external proxy.

For SPOT services, TLS/WSS configuration is node-owned. Apply
`zlink_set_tls_server()` / `zlink_set_tls_client()` to the `SpotNode`
handle before bind/connect. Unified `spot` and SPOT child pub/sub handles
are not TLS configuration surfaces and fail with `ENOTSUP`.

## 2. TLS Server Setup

=== "C"

    ```c
    void *socket = zlink_socket(ctx, ZLINK_ROUTER);

    /* Set certificate and key (before bind) */
    zlink_set_tls_server(socket, "/path/to/server.crt", "/path/to/server.key", 0);

    /* TLS bind */
    zlink_bind(socket, "tls://*:5555");
    ```

=== "C++"

    ```cpp
    zlink::router_socket_t socket(ctx);
    socket.set_tls_server("/path/to/server.crt", "/path/to/server.key", 0);
    socket.bind("tls://*:5555");
    ```

=== "Java"

    ```java
    RouterSocket socket = new RouterSocket(ctx);
    socket.setTlsServer("/path/to/server.crt", "/path/to/server.key", 0);
    socket.bind("tls://*:5555");
    ```

=== "Python"

    ```python
    socket = zlink.RouterSocket(ctx)
    socket.set_tls_server("/path/to/server.crt", "/path/to/server.key", 0)
    socket.bind("tls://*:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = new zlink.RouterSocket(ctx);
    socket.setTlsServer("/path/to/server.crt", "/path/to/server.key", 0);
    socket.bind("tls://*:5555");
    ```

=== "C#/.NET"

    ```csharp
    using var socket = new RouterSocket(ctx);
    socket.SetTlsServer("/path/to/server.crt", "/path/to/server.key", 0);
    socket.Bind("tls://*:5555");
    ```

=== "Rust"

    ```rust
    let socket = ctx.router_socket()?;
    socket.set_tls_server("/path/to/server.crt", "/path/to/server.key", false)?;
    socket.bind("tls://*:5555")?;
    ```

=== "Go"

    ```go
    socket := ctx.RouterSocket()
    socket.SetTLSServer("/path/to/server.crt", "/path/to/server.key", false)
    socket.Bind("tls://*:5555")
    ```

## 3. TLS Client Setup

=== "C"

    ```c
    void *socket = zlink_socket(ctx, ZLINK_DEALER);

    /* Set CA certificate and hostname verification */
    zlink_set_tls_client(socket, "/path/to/ca.crt", "server.example.com", 0);

    /* TLS connect */
    zlink_connect(socket, "tls://server.example.com:5555");
    ```

=== "C++"

    ```cpp
    zlink::dealer_socket_t socket(ctx);
    socket.set_tls_client("/path/to/ca.crt", "server.example.com", 0);
    socket.connect("tls://server.example.com:5555");
    ```

=== "Java"

    ```java
    DealerSocket socket = new DealerSocket(ctx);
    socket.setTlsClient("/path/to/ca.crt", "server.example.com", 0);
    socket.connect("tls://server.example.com:5555");
    ```

=== "Python"

    ```python
    socket = zlink.DealerSocket(ctx)
    socket.set_tls_client("/path/to/ca.crt", "server.example.com", 0)
    socket.connect("tls://server.example.com:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = new zlink.DealerSocket(ctx);
    socket.setTlsClient("/path/to/ca.crt", "server.example.com", 0);
    socket.connect("tls://server.example.com:5555");
    ```

=== "C#/.NET"

    ```csharp
    using var socket = new DealerSocket(ctx);
    socket.SetTlsClient("/path/to/ca.crt", "server.example.com", 0);
    socket.Connect("tls://server.example.com:5555");
    ```

=== "Rust"

    ```rust
    let socket = ctx.dealer_socket()?;
    socket.set_tls_client("/path/to/ca.crt", "server.example.com", false)?;
    socket.connect("tls://server.example.com:5555")?;
    ```

=== "Go"

    ```go
    socket := ctx.DealerSocket()
    socket.SetTLSClient("/path/to/ca.crt", "server.example.com", false)
    socket.Connect("tls://server.example.com:5555")
    ```

## 4. WSS (WebSocket + TLS) Setup

WSS is a transport that adds TLS encryption to ws. It requires additional configuration compared to ws.

### WSS Server

=== "C"

    ```c
    void *socket = zlink_socket(ctx, ZLINK_STREAM);

    /* Set TLS certificate/key */
    zlink_set_tls_server(socket, "/path/to/cert.pem", "/path/to/key.pem", 0);

    /* WSS bind */
    zlink_bind(socket, "wss://*:8443");
    ```

=== "C++"

    ```cpp
    zlink::stream_socket_t socket(ctx);
    socket.set_tls_server("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.bind("wss://*:8443");
    ```

=== "Java"

    ```java
    StreamSocket socket = new StreamSocket(ctx);
    socket.setTlsServer("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.bind("wss://*:8443");
    ```

=== "Python"

    ```python
    socket = zlink.StreamSocket(ctx)
    socket.set_tls_server("/path/to/cert.pem", "/path/to/key.pem", 0)
    socket.bind("wss://*:8443")
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = new zlink.StreamSocket(ctx);
    socket.setTlsServer("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.bind("wss://*:8443");
    ```

=== "C#/.NET"

    ```csharp
    using var socket = new StreamSocket(ctx);
    socket.SetTlsServer("/path/to/cert.pem", "/path/to/key.pem", 0);
    socket.Bind("wss://*:8443");
    ```

=== "Rust"

    ```rust
    let socket = ctx.stream_socket()?;
    socket.set_tls_server("/path/to/cert.pem", "/path/to/key.pem", false)?;
    socket.bind("wss://*:8443")?;
    ```

=== "Go"

    ```go
    socket := ctx.StreamSocket()
    socket.SetTLSServer("/path/to/cert.pem", "/path/to/key.pem", false)
    socket.Bind("wss://*:8443")
    ```

### WSS Client (External Raw Client)

`ZLINK_STREAM` is server-only. For WSS clients, use an external WebSocket/TLS client stack.

Conceptual client requirements:

```text
target: wss://server:8443
- trust CA: /path/to/ca.pem
- verify hostname: localhost
```

### ws vs wss Configuration Comparison

| Setting | ws | wss |
|---------|:--:|:---:|
| Basic socket creation | O | O |
| `zlink_set_tls_server()` (server cert+key) | - | Required |
| `zlink_set_tls_client()` (client CA+hostname+trust) | - | Recommended (external raw client) |

## 5. TLS API Reference

TLS is configured through two dedicated functions instead of individual socket options.

### zlink_set_tls_server()

Configures the server-side TLS certificate and key.

=== "C"

    ```c
    zlink_set_tls_server(socket, cert_path, key_path, require_client_cert);
    ```

=== "C++"

    ```cpp
    socket.set_tls_server(cert_path, key_path, require_client_cert);
    ```

=== "Java"

    ```java
    socket.setTlsServer(certPath, keyPath, requireClientCert);
    ```

=== "Python"

    ```python
    socket.set_tls_server(cert_path, key_path, require_client_cert)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setTlsServer(certPath, keyPath, requireClientCert);
    ```

=== "C#/.NET"

    ```csharp
    socket.SetTlsServer(certPath, keyPath, requireClientCert);
    ```

=== "Rust"

    ```rust
    socket.set_tls_server(cert_path, key_path, require_client_cert)?;
    ```

=== "Go"

    ```go
    socket.SetTLSServer(cert_path, key_path, require_client_cert)
    ```

| Parameter | Type | Description |
|-----------|------|-------------|
| `cert_path` | string | Certificate file path (PEM format) |
| `key_path` | string | Private key file path (PEM format) |
| `require_client_cert` | int | Whether to require client certificate (0 = no, 1 = yes) |

=== "C"

    ```c
    /* PEM format file paths */
    zlink_set_tls_server(socket, "server.crt", "server.key", 0);
    ```

=== "C++"

    ```cpp
    socket.set_tls_server("server.crt", "server.key", 0);
    ```

=== "Java"

    ```java
    socket.setTlsServer("server.crt", "server.key", 0);
    ```

=== "Python"

    ```python
    socket.set_tls_server("server.crt", "server.key", 0)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setTlsServer("server.crt", "server.key", 0);
    ```

=== "C#/.NET"

    ```csharp
    socket.SetTlsServer("server.crt", "server.key", 0);
    ```

=== "Rust"

    ```rust
    socket.set_tls_server("server.crt", "server.key", false)?;
    ```

=== "Go"

    ```go
    socket.SetTLSServer("server.crt", "server.key", false)
    ```

- Must be set **before** `zlink_bind()`
- Only PEM format is supported
- Handshake fails if the certificate and key do not match

### zlink_set_tls_client()

Configures the client-side TLS CA certificate, hostname verification, and system CA trust.

=== "C"

    ```c
    zlink_set_tls_client(socket, ca_cert_path, hostname, trust_system);
    ```

=== "C++"

    ```cpp
    socket.set_tls_client(ca_cert_path, hostname, trust_system);
    ```

=== "Java"

    ```java
    socket.setTlsClient(caCertPath, hostname, trustSystem);
    ```

=== "Python"

    ```python
    socket.set_tls_client(ca_cert_path, hostname, trust_system)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setTlsClient(caCertPath, hostname, trustSystem);
    ```

=== "C#/.NET"

    ```csharp
    socket.SetTlsClient(caCertPath, hostname, trustSystem);
    ```

=== "Rust"

    ```rust
    socket.set_tls_client(ca_cert_path, hostname, trust_system)?;
    ```

=== "Go"

    ```go
    socket.SetTLSClient(ca_cert_path, hostname, trust_system)
    ```

| Parameter | Type | Description |
|-----------|------|-------------|
| `ca_cert_path` | string | CA certificate path (for server certificate verification), or NULL |
| `hostname` | string | Server hostname for CN/SAN verification, or NULL |
| `trust_system` | int | Whether to trust the system CA store (0 = no, 1 = yes) |

=== "C"

    ```c
    /* Private CA with hostname verification */
    zlink_set_tls_client(socket, "ca.crt", "server.example.com", 0);

    /* System CA only (no private CA, no hostname check) */
    zlink_set_tls_client(socket, NULL, NULL, 1);
    ```

=== "C++"

    ```cpp
    // Private CA with hostname verification
    socket.set_tls_client("ca.crt", "server.example.com", 0);

    // System CA only
    socket.set_tls_client("", "", true);
    ```

=== "Java"

    ```java
    // Private CA with hostname verification
    socket.setTlsClient("ca.crt", "server.example.com", 0);

    // System CA only
    socket.setTlsClient(null, null, true);
    ```

=== "Python"

    ```python
    # Private CA with hostname verification
    socket.set_tls_client("ca.crt", "server.example.com", 0)

    # System CA only
    socket.set_tls_client(None, None, True)
    ```

=== "Node/TypeScript"

    ```typescript
    // Private CA with hostname verification
    socket.setTlsClient("ca.crt", "server.example.com", 0);

    // System CA only
    socket.setTlsClient(null, null, true);
    ```

=== "C#/.NET"

    ```csharp
    // Private CA with hostname verification
    socket.SetTlsClient("ca.crt", "server.example.com", 0);

    // System CA only
    socket.SetTlsClient(null, null, true);
    ```

=== "Rust"

    ```rust
    // Private CA with hostname verification
    socket.set_tls_client("ca.crt", "server.example.com", false)?;

    // System CA only
    socket.set_tls_client(None, None, true)?;
    ```

=== "Go"

    ```go
    // Private CA with hostname verification
    socket.SetTLSClient("ca.crt", "server.example.com", false)

    // System CA only
    socket.SetTLSClient(None, None, true)
    ```

- When `ca_cert_path` is NULL, only the system CA store is used (if `trust_system=1`)
- Must set `ca_cert_path` when using a private CA
- If `hostname` is NULL, hostname verification is skipped (security warning)
- Hostname verification is strongly recommended for production
- Must match the certificate's CN or SAN

> Reference: `core/tests/e2e/spot/spot_pubsub_scenario_shared.cpp` -- `trust_system = 0` followed by private CA usage

## 6. Generating Test Certificates

### CA Key and Certificate

```bash
openssl req -x509 -newkey rsa:2048 -keyout ca.key -out ca.crt \
  -days 365 -nodes -subj "/CN=Test CA"
```

### Server Key and CSR

```bash
openssl req -newkey rsa:2048 -keyout server.key -out server.csr \
  -nodes -subj "/CN=localhost"
```

### Signing the Server Certificate with the CA

```bash
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
  -CAcreateserial -out server.crt -days 365
```

### Including SAN (Subject Alternative Name)

Generate a certificate with SAN for hostname verification:

```bash
openssl req -newkey rsa:2048 -keyout server.key -out server.csr \
  -nodes -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"

openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
  -CAcreateserial -out server.crt -days 365 \
  -copy_extensions copy
```

## 7. Common TLS Errors and Troubleshooting

### Certificate/Key Mismatch

```
Symptom: bind or handshake failure
Cause: Server certificate and private key do not match
Solution: Verify the certificate-key pair
```

```bash
# Compare the modulus of the certificate and key
openssl x509 -noout -modulus -in server.crt | openssl md5
openssl rsa -noout -modulus -in server.key | openssl md5
# Both values should match
```

### CA Certificate Not Set

```
Symptom: Client connection failure, handshake timeout
Cause: Client has no CA to verify the server certificate
Solution: Set ca_cert_path in zlink_set_tls_client() or check trust_system parameter
```

### Hostname Mismatch

```
Symptom: Handshake failure
Cause: hostname parameter in zlink_set_tls_client() does not match certificate CN/SAN
Solution: Include the correct CN/SAN in the certificate, or update the hostname parameter
```

### Certificate Expired

```
Symptom: Handshake failure
Cause: Server or CA certificate validity period has expired
Solution: Renew the certificate
```

```bash
# Check certificate validity period
openssl x509 -noout -dates -in server.crt
```

### Detecting TLS Errors via Monitoring

!!! note "C-only: monitor event API"
    The monitor event API is currently exposed only through the C interface.
    Bindings provide equivalent monitoring through their own event/callback mechanisms.

```c
void on_tls_error(const zlink_monitor_event_t *ev, void *userdata)
{
    printf("Handshake failed: event=0x%llx value=%llu\n",
           (unsigned long long)ev->event,
           (unsigned long long)ev->value);
}

zlink_socket_monitor_open_options_t opts = {
    .events = ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL |
              ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL |
              ZLINK_EVENT_HANDSHAKE_FAILED_AUTH,
};
void *mon = zlink_socket_monitor_open(socket, &opts);
zlink_socket_monitor_handler(mon, on_tls_error, NULL);
```

## 8. Production Environment Checklist

### Certificate Management

- [ ] Use TLS 1.2 or higher (OpenSSL default)
- [ ] Use publicly trusted CA certificates in production
- [ ] Establish automated certificate renewal before expiration
- [ ] Restrict private key file permissions (`chmod 600`)
- [ ] Verify certificate chain completeness

### Client Configuration

- [ ] Set `hostname` parameter in `zlink_set_tls_client()` (enable hostname verification)
- [ ] Explicitly set `ca_cert_path` in `zlink_set_tls_client()` or verify system CA
- [ ] Set `trust_system=0` in `zlink_set_tls_client()` when using a private CA

### Monitoring

- [ ] Monitor `HANDSHAKE_FAILED_*` events
- [ ] Set up certificate expiration alerts
- [ ] Log TLS connection failures

## 9. Complete Examples

### TLS Server-Client

=== "C"

    ```c
    #include <zlink.h>
    #include <stdio.h>
    #include <string.h>

    int main(void) {
        void *ctx = zlink_ctx_new();

        /* TLS Server */
        void *server = zlink_socket(ctx, ZLINK_PAIR);
        zlink_set_tls_server(server, "server.crt", "server.key", 0);
        zlink_bind(server, "tls://*:5555");

        /* TLS Client */
        void *client = zlink_socket(ctx, ZLINK_PAIR);
        zlink_set_tls_client(client, "ca.crt", "localhost", 0);
        zlink_connect(client, "tls://127.0.0.1:5555");

        /* Encrypted communication — server receives via handler callback */
        zlink_msg_t part;
        zlink_msg_init_size(&part, 12);
        memcpy(zlink_msg_data(&part), "Secure Hello", 12);
        zlink_send(client, &part, 1, 0);

        /* on_message callback receives: parts[0] = "Secure Hello" */

        zlink_close(client);
        zlink_close(server);
        zlink_ctx_term(ctx);
        return 0;
    }
    ```

=== "C++"

    ```cpp
    #include <zlink/socket.hpp>
    #include <iostream>

    int main() {
        zlink::context_t ctx;

        // TLS Server
        zlink::pair_socket_t server(ctx);
        server.set_tls_server("server.crt", "server.key", 0);
        server.bind("tls://*:5555");

        // TLS Client
        zlink::pair_socket_t client(ctx);
        client.set_tls_client("ca.crt", "localhost", 0);
        client.connect("tls://127.0.0.1:5555");

        // Encrypted communication
        client.send(zlink::message_t("Secure Hello", 12));

        auto [rid, parts] = server.recv();
        std::cout << "Received: " << parts[0].to_string() << "\n";

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class TlsExample {
        public static void main(String[] args) throws Exception {
            try (Context ctx = new Context()) {
                PairSocket server = new PairSocket(ctx);
                server.setTlsServer("server.crt", "server.key", 0);
                server.bind("tls://*:5555");

                PairSocket client = new PairSocket(ctx);
                client.setTlsClient("ca.crt", "localhost", 0);
                client.connect("tls://127.0.0.1:5555");

                client.send(new Message("Secure Hello".getBytes()));

                RecvResult result = server.recv();
                System.out.println("Received: "
                    + new String(result.parts()[0].data()));
            }
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    server = zlink.PairSocket(ctx)
    server.set_tls_server("server.crt", "server.key", 0)
    server.bind("tls://*:5555")

    client = zlink.PairSocket(ctx)
    client.set_tls_client("ca.crt", "localhost", 0)
    client.connect("tls://127.0.0.1:5555")

    client.send(b"Secure Hello")

    rid, parts = server.recv()
    print(f"Received: {parts[0].data().decode()}")

    client.close()
    server.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from "zlink";

    const ctx = new zlink.Context();

    const server = new zlink.PairSocket(ctx);
    server.setTlsServer("server.crt", "server.key", 0);
    server.bind("tls://*:5555");

    const client = new zlink.PairSocket(ctx);
    client.setTlsClient("ca.crt", "localhost", 0);
    client.connect("tls://127.0.0.1:5555");

    client.send(Buffer.from("Secure Hello"));

    const { sourceRid, parts } = server.recv();
    console.log(`Received: ${parts[0].data().toString()}`);

    client.close();
    server.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new Context();

    using var server = new PairSocket(ctx);
    server.SetTlsServer("server.crt", "server.key", 0);
    server.Bind("tls://*:5555");

    using var client = new PairSocket(ctx);
    client.SetTlsClient("ca.crt", "localhost", 0);
    client.Connect("tls://127.0.0.1:5555");

    client.Send(new Message("Secure Hello"u8));

    var (rid, parts) = server.Recv();
    Console.WriteLine($"Received: {parts[0].DataString()}");
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> zlink::Result<()> {
        let ctx = Context::new()?;

        let server = ctx.pair_socket()?;
        server.set_tls_server("server.crt", "server.key", false)?;
        server.bind("tls://*:5555")?;

        let client = ctx.pair_socket()?;
        client.set_tls_client("ca.crt", "localhost", false)?;
        client.connect("tls://127.0.0.1:5555")?;

        client.send(&zlink::Message::from("Secure Hello"))?;

        let (rid, parts) = server.recv()?;
        println!("Received: {}", parts[0].as_str()?);

        Ok(())
    }
    ```

=== "Go"

    ```go
    func main() {
        ctx := zlink.NewContext()

        server := ctx.PairSocket()
        server.SetTLSServer("server.crt", "server.key", false)
        server.Bind("tls://*:5555")

        client := ctx.PairSocket()
        client.SetTLSClient("ca.crt", "localhost", false)
        client.Connect("tls://127.0.0.1:5555")

        client.Send(zlink.NewMessage([]byte("Secure Hello")))

        rid, parts, _ := server.Recv()
        fmt.Printf("Received: %v\n", parts[0].as_str()?)


    }
    ```

### WSS STREAM Server

=== "C"

    ```c
    void *ctx = zlink_ctx_new();

    /* WSS Server (STREAM) */
    void *server = zlink_socket(ctx, ZLINK_STREAM);
    zlink_set_tls_server(server, "server.crt", "server.key", 0);
    int linger = 0;
    zlink_set_option(server, ZLINK_OPT_LINGER, &linger, sizeof(linger));
    zlink_bind(server, "wss://*:8443");

    /* External raw WSS client connects to this endpoint.
     * STREAM server receives [routing_id][0x01] and then data frames.
     */

    zlink_close(server);
    zlink_ctx_term(ctx);
    ```

=== "C++"

    ```cpp
    zlink::context_t ctx;

    zlink::stream_socket_t server(ctx);
    server.set_tls_server("server.crt", "server.key", 0);
    server.set_option(zlink::opt::linger, 0);
    server.bind("wss://*:8443");

    // External raw WSS client connects to this endpoint.
    ```

=== "Java"

    ```java
    Context ctx = new Context();

    StreamSocket server = new StreamSocket(ctx);
    server.setTlsServer("server.crt", "server.key", 0);
    server.setLinger(0);
    server.bind("wss://*:8443");

    // External raw WSS client connects to this endpoint.
    ```

=== "Python"

    ```python
    ctx = zlink.Context()

    server = zlink.StreamSocket(ctx)
    server.set_tls_server("server.crt", "server.key", 0)
    server.set_option(zlink.OPT_LINGER, 0)
    server.bind("wss://*:8443")

    # External raw WSS client connects to this endpoint.
    ```

=== "Node/TypeScript"

    ```typescript
    const ctx = new zlink.Context();

    const server = new zlink.StreamSocket(ctx);
    server.setTlsServer("server.crt", "server.key", 0);
    server.setOption(zlink.OPT_LINGER, 0);
    server.bind("wss://*:8443");

    // External raw WSS client connects to this endpoint.
    ```

=== "C#/.NET"

    ```csharp
    using var ctx = new Context();

    using var server = new StreamSocket(ctx);
    server.SetTlsServer("server.crt", "server.key", 0);
    server.Linger = 0;
    server.Bind("wss://*:8443");

    // External raw WSS client connects to this endpoint.
    ```

=== "Rust"

    ```rust
    let ctx = zlink::Context::new()?;

    let server = ctx.stream_socket()?;
    server.set_tls_server("server.crt", "server.key", false)?;
    server.set_linger(0)?;
    server.bind("wss://*:8443")?;

    // External raw WSS client connects to this endpoint.
    ```

=== "Go"

    ```go
    ctx := zlink.NewContext()

    server := ctx.StreamSocket()
    server.SetTLSServer("server.crt", "server.key", false)
    server.SetOption(zlink.OptionLinger, 0)
    server.Bind("wss://*:8443")

    // External raw WSS client connects to this endpoint.
    ```

---
[← Transport](04-transports.md) | [Monitoring →](06-monitoring.md)
