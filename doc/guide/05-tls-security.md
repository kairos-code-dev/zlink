English | [한국어](05-tls-security.ko.md)

# TLS/SSL Configuration and Security Guide

## 1. Overview

zlink natively supports `tls://` and `wss://` transports through OpenSSL. Encrypted communication can be configured directly without an external proxy.

## 2. TLS Server Setup

```c
void *socket = zlink_socket(ctx, ZLINK_ROUTER);

/* Set certificate and key (before bind) */
zlink_setsockopt(socket, ZLINK_TLS_CERT, "/path/to/server.crt", 0);
zlink_setsockopt(socket, ZLINK_TLS_KEY, "/path/to/server.key", 0);

/* TLS bind */
zlink_bind(socket, "tls://*:5555");
```

## 3. TLS Client Setup

```c
void *socket = zlink_socket(ctx, ZLINK_DEALER);

/* Set CA certificate */
zlink_setsockopt(socket, ZLINK_TLS_CA, "/path/to/ca.crt", 0);

/* (Optional) Hostname verification */
zlink_setsockopt(socket, ZLINK_TLS_HOSTNAME, "server.example.com", 0);

/* TLS connect */
zlink_connect(socket, "tls://server.example.com:5555");
```

## 4. WSS (WebSocket + TLS) Setup

WSS is a transport that adds TLS encryption to ws. It requires additional configuration compared to ws.

### WSS Server

```c
void *socket = zlink_socket(ctx, ZLINK_STREAM);

/* Set TLS certificate/key */
zlink_setsockopt(socket, ZLINK_TLS_CERT, "/path/to/cert.pem", 0);
zlink_setsockopt(socket, ZLINK_TLS_KEY, "/path/to/key.pem", 0);

/* WSS bind */
zlink_bind(socket, "wss://*:8443");
```

### WSS Client

```c
void *socket = zlink_socket(ctx, ZLINK_STREAM);

/* Disable system CA (when using private certificates) */
int trust_system = 0;
zlink_setsockopt(socket, ZLINK_TLS_TRUST_SYSTEM, &trust_system, sizeof(trust_system));

/* Set CA certificate */
zlink_setsockopt(socket, ZLINK_TLS_CA, "/path/to/ca.pem", 0);

/* Hostname verification */
zlink_setsockopt(socket, ZLINK_TLS_HOSTNAME, "localhost", 9);

/* WSS connect */
zlink_connect(socket, "wss://server:8443");
```

> Reference: `core/tests/test_stream_socket.cpp` — `test_stream_wss_basic()`

### ws vs wss Configuration Comparison

| Setting | ws | wss |
|---------|:--:|:---:|
| Basic socket creation | O | O |
| `ZLINK_TLS_CERT` / `ZLINK_TLS_KEY` (server) | - | Required |
| `ZLINK_TLS_CA` (client) | - | Recommended |
| `ZLINK_TLS_HOSTNAME` (client) | - | Recommended |
| `ZLINK_TLS_TRUST_SYSTEM` (client) | - | Optional |

## 5. TLS Socket Options Reference

| Option | Type | Direction | Default | Description |
|--------|------|-----------|---------|-------------|
| `ZLINK_TLS_CERT` | string | Server | — | Certificate file path (PEM format) |
| `ZLINK_TLS_KEY` | string | Server | — | Private key file path (PEM format) |
| `ZLINK_TLS_CA` | string | Client | — | CA certificate path (for server certificate verification) |
| `ZLINK_TLS_HOSTNAME` | string | Client | — | Server hostname (CN/SAN verification) |
| `ZLINK_TLS_TRUST_SYSTEM` | int | Client | 1 | Whether to trust the system CA store |

### ZLINK_TLS_CERT / ZLINK_TLS_KEY

The certificate and private key for the server to authenticate itself to clients.

```c
/* PEM format file paths */
zlink_setsockopt(socket, ZLINK_TLS_CERT, "server.crt", 0);
zlink_setsockopt(socket, ZLINK_TLS_KEY, "server.key", 0);
```

- Must be set **before** `zlink_bind()`
- Only PEM format is supported
- Handshake fails if the certificate and key do not match

### ZLINK_TLS_CA

The CA certificate for the client to verify the server's certificate.

```c
zlink_setsockopt(socket, ZLINK_TLS_CA, "ca.crt", 0);
```

- When no CA is set, the system CA store is used (`ZLINK_TLS_TRUST_SYSTEM=1`)
- Must be set when using a private CA

### ZLINK_TLS_HOSTNAME

The client verifies the server certificate's CN (Common Name) or SAN (Subject Alternative Name).

```c
zlink_setsockopt(socket, ZLINK_TLS_HOSTNAME, "server.example.com", 0);
```

- If not set, hostname verification is skipped (security warning)
- Strongly recommended for production
- Must match the certificate's CN or SAN

### ZLINK_TLS_TRUST_SYSTEM

Whether to trust the system CA store (root certificates installed in the OS).

```c
/* Disable system CA (use only private certificates) */
int trust = 0;
zlink_setsockopt(socket, ZLINK_TLS_TRUST_SYSTEM, &trust, sizeof(trust));

/* Enable system CA (default) */
int trust = 1;
zlink_setsockopt(socket, ZLINK_TLS_TRUST_SYSTEM, &trust, sizeof(trust));
```

- Default: 1 (trust system CA)
- Set to 0 in environments using only a private CA, and specify `ZLINK_TLS_CA` explicitly
- Keep the default when using publicly issued certificates

> Reference: `core/tests/test_stream_socket.cpp` — `trust_system = 0` followed by private CA usage

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
Solution: Set ZLINK_TLS_CA or check ZLINK_TLS_TRUST_SYSTEM
```

### Hostname Mismatch

```
Symptom: Handshake failure
Cause: ZLINK_TLS_HOSTNAME does not match certificate CN/SAN
Solution: Include the correct CN/SAN in the certificate, or update the HOSTNAME setting
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
void *mon = zlink_socket_monitor_open(socket,
    ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL |
    ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL |
    ZLINK_EVENT_HANDSHAKE_FAILED_AUTH);

zlink_monitor_event_t ev;
if (zlink_monitor_recv(mon, &ev, ZLINK_DONTWAIT) == 0) {
    printf("Handshake failed: event=0x%llx value=%llu\n",
           (unsigned long long)ev.event,
           (unsigned long long)ev.value);
}
```

## 8. Production Environment Checklist

### Certificate Management

- [ ] Use TLS 1.2 or higher (OpenSSL default)
- [ ] Use publicly trusted CA certificates in production
- [ ] Establish automated certificate renewal before expiration
- [ ] Restrict private key file permissions (`chmod 600`)
- [ ] Verify certificate chain completeness

### Client Configuration

- [ ] Set `ZLINK_TLS_HOSTNAME` (enable hostname verification)
- [ ] Explicitly set `ZLINK_TLS_CA` or verify system CA
- [ ] Set `ZLINK_TLS_TRUST_SYSTEM=0` when using a private CA

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
    void *server = zlink_socket(ctx, ZLINK_PAIR);
    zlink_setsockopt(server, ZLINK_TLS_CERT, "server.crt", 0);
    zlink_setsockopt(server, ZLINK_TLS_KEY, "server.key", 0);
    zlink_bind(server, "tls://*:5555");

    /* TLS Client */
    void *client = zlink_socket(ctx, ZLINK_PAIR);
    zlink_setsockopt(client, ZLINK_TLS_CA, "ca.crt", 0);
    zlink_setsockopt(client, ZLINK_TLS_HOSTNAME, "localhost", 9);
    zlink_connect(client, "tls://127.0.0.1:5555");

    /* Encrypted communication */
    zlink_send(client, "Secure Hello", 12, 0);

    char buf[256];
    int size = zlink_recv(server, buf, sizeof(buf), 0);
    buf[size] = '\0';
    printf("Received: %s\n", buf);

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
void *server = zlink_socket(ctx, ZLINK_STREAM);
zlink_setsockopt(server, ZLINK_TLS_CERT, "server.crt", 0);
zlink_setsockopt(server, ZLINK_TLS_KEY, "server.key", 0);
int linger = 0;
zlink_setsockopt(server, ZLINK_LINGER, &linger, sizeof(linger));
zlink_bind(server, "wss://*:8443");

/* WSS Client (STREAM) */
void *client = zlink_socket(ctx, ZLINK_STREAM);
int trust = 0;
zlink_setsockopt(client, ZLINK_TLS_TRUST_SYSTEM, &trust, sizeof(trust));
zlink_setsockopt(client, ZLINK_TLS_CA, "ca.crt", 0);
zlink_setsockopt(client, ZLINK_TLS_HOSTNAME, "localhost", 9);
zlink_setsockopt(client, ZLINK_LINGER, &linger, sizeof(linger));

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(server, ZLINK_LAST_ENDPOINT, endpoint, &len);
zlink_connect(client, endpoint);

/* Receive connection event, then exchange encrypted data */
unsigned char server_id[4], client_id[4], code;
zlink_recv(server, server_id, 4, 0);
zlink_recv(server, &code, 1, 0);  /* 0x01 = connected */

zlink_recv(client, client_id, 4, 0);
zlink_recv(client, &code, 1, 0);  /* 0x01 = connected */

/* Send data */
zlink_send(client, client_id, 4, ZLINK_SNDMORE);
zlink_send(client, "secure data", 11, 0);

zlink_close(client);
zlink_close(server);
zlink_ctx_term(ctx);
```

---
[← Transport](04-transports.md) | [Monitoring →](06-monitoring.md)
