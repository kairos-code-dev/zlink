English | [한국어](05-tls-security.ko.md)

<!-- zlink-nav:start -->
[← Transport](04-transports.md) | [Monitoring →](06-monitoring.md)
<!-- zlink-nav:end -->

# TLS/SSL Configuration and Security Guide

## 1. Overview

zlink natively supports `tls://` and `wss://` transports through OpenSSL. Encrypted communication can be configured directly without an external proxy.

For SPOT services, TLS/WSS configuration is node-owned. Apply
`zlink_set_tls_server()` / `zlink_set_tls_client()` to the `MeshNode`
handle before bind/connect. Unified `spot` and SPOT child pub/sub handles
are not TLS configuration surfaces and return `ZLINK_CONFIG_NOT_SUPPORTED`.

## 2. TLS Server Setup

```c
void *socket = zlink_socket(ctx, ZLINK_SOCKET_ROUTER);

/* Set certificate and key (before bind) */
zlink_set_tls_server(socket, "/path/to/server.crt", "/path/to/server.key", 0);

/* TLS bind */
zlink_bind(socket, "tls://*:5555");
```

## 3. TLS Client Setup

```c
void *socket = zlink_socket(ctx, ZLINK_SOCKET_DEALER);

/* Set CA certificate and hostname verification */
zlink_set_tls_client(socket, "/path/to/ca.crt", "server.example.com", 0);

/* TLS connect */
zlink_connect(socket, "tls://server.example.com:5555");
```

## 4. WSS (WebSocket + TLS) Setup

WSS is a transport that adds TLS encryption to ws. It requires additional configuration compared to ws.

### WSS Server

```c
void *socket = zlink_socket(ctx, ZLINK_SOCKET_STREAM);

/* Set TLS certificate/key */
zlink_set_tls_server(socket, "/path/to/cert.pem", "/path/to/key.pem", 0);

/* WSS bind */
zlink_bind(socket, "wss://*:8443");
```

### WSS Clients

A `STREAM` socket is bind-only, so a `STREAM`-based WSS server is reached by
**external** raw WebSocket/TLS clients. Normal zlink ZMP socket types
(PAIR/DEALER/etc.) can instead **connect** via `wss://` using zlink's own WSS
connecter together with `zlink_set_tls_client()`:

```c
void *socket = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_set_tls_client(socket, "ca.crt", "server.example.com", 0);
zlink_connect(socket, "wss://server:8443");
```

### ws vs wss Configuration Comparison

| Setting | ws | wss |
|---------|:--:|:---:|
| Basic socket creation | O | O |
| `zlink_set_tls_server()` (server cert+key) | - | Required |
| `zlink_set_tls_client()` (client CA+hostname+trust) | - | Required for zlink `wss://` connect |

## 5. TLS API Reference

TLS is configured through two dedicated functions instead of individual socket options.

### zlink_set_tls_server()

Configures the server-side TLS certificate and key.

```c
zlink_set_tls_server(socket, cert_path, key_path, require_client_cert);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `cert_path` | string | Certificate file path (PEM format) |
| `key_path` | string | Private key file path (PEM format) |
| `require_client_cert` | int | Whether to require client certificate (0 = no, 1 = yes) |

```c
/* PEM format file paths */
zlink_set_tls_server(socket, "server.crt", "server.key", 0);
```

- Must be set **before** `zlink_bind()`
- Only PEM format is supported
- Handshake fails if the certificate and key do not match

### zlink_set_tls_client()

Configures the client-side TLS CA certificate, hostname verification, and system CA trust.

```c
zlink_set_tls_client(socket, ca_cert_path, hostname, trust_system);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `ca_cert_path` | string | CA certificate path (for server certificate verification); pass `""` to use only the system store |
| `hostname` | string | Server hostname for CN/SAN verification (required) |
| `trust_system` | int | Whether to trust the system CA store (0 = no, 1 = yes) |

```c
/* Private CA with hostname verification */
zlink_set_tls_client(socket, "ca.crt", "server.example.com", 0);

/* System CA only (empty CA path), still verifying the hostname */
zlink_set_tls_client(socket, "", "server.example.com", 1);
```

- On raw sockets `ca_cert_path` and `hostname` must be non-`NULL` strings; a
  `NULL` argument is rejected with `ZLINK_CONFIG_INVALID_HANDLE` (`EFAULT`).
- Pass an empty `ca_cert_path` (`""`) to rely only on the system CA store
  (with `trust_system=1`); supply a path to add a private CA.
- `hostname` is used for CN/SAN verification and must match the certificate.
  Hostname verification is strongly recommended for production.

> Reference: `core/tests/e2e/spot/spot_pubsub_scenario_shared.cpp` — `trust_system = 0` followed by private CA usage

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

- [ ] Use TLS 1.2 or higher (zlink builds TLS 1.2 server/client contexts)
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

```c
#include <zlink.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    void *ctx = zlink_ctx_new();

    /* TLS Server */
    void *server = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
    zlink_set_tls_server(server, "server.crt", "server.key", 0);
    zlink_bind(server, "tls://*:5555");

    /* TLS Client */
    void *client = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
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

### WSS STREAM Server

```c
void *ctx = zlink_ctx_new();

/* WSS Server (STREAM) */
void *server = zlink_socket(ctx, ZLINK_SOCKET_STREAM);
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

---
<!-- zlink-nav:bottom:start -->
[← Transport](04-transports.md) | [Monitoring →](06-monitoring.md)
<!-- zlink-nav:bottom:end -->
