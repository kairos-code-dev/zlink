# TLS Usage Guide

This guide explains how to use TLS encryption in zlink, covering both native TLS transport (`tls://`) and WebSocket TLS (`wss://`).

## Overview

zlink provides TLS encryption through two mechanisms:

1. **Native TLS Transport** (`tls://`): Direct TLS-encrypted ZeroMQ connections
2. **WebSocket TLS** (`wss://`): TLS-encrypted WebSocket connections

Both use OpenSSL for encryption and share the same socket options for certificate configuration.

## Prerequisites

### Certificate Requirements

You'll need PEM-formatted certificates:

- **Server**: Certificate file and private key file
- **Client**: CA certificate for server validation (optional: client certificate for mutual TLS)

### Generating Test Certificates

For development/testing, generate self-signed certificates:

```bash
# Generate CA certificate
openssl req -x509 -newkey rsa:2048 -keyout ca-key.pem -out ca-cert.pem -days 365 -nodes \
  -subj "/CN=Test CA"

# Generate server certificate
openssl req -newkey rsa:2048 -keyout server-key.pem -out server-req.pem -days 365 -nodes \
  -subj "/CN=localhost"

openssl x509 -req -in server-req.pem -CA ca-cert.pem -CAkey ca-key.pem -CAcreateserial \
  -out server-cert.pem -days 365

# Generate client certificate (for mutual TLS)
openssl req -newkey rsa:2048 -keyout client-key.pem -out client-req.pem -days 365 -nodes \
  -subj "/CN=client"

openssl x509 -req -in client-req.pem -CA ca-cert.pem -CAkey ca-key.pem -CAcreateserial \
  -out client-cert.pem -days 365
```

## Native TLS Transport (tls://)

### Basic Server

```c
#include <zmq.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    void *ctx = zmq_ctx_new();
    void *socket = zmq_socket(ctx, ZMQ_DEALER);

    // Configure TLS
    const char *cert_path = "/path/to/server-cert.pem";
    const char *key_path = "/path/to/server-key.pem";

    zmq_setsockopt(socket, ZMQ_TLS_CERT, cert_path, strlen(cert_path));
    zmq_setsockopt(socket, ZMQ_TLS_KEY, key_path, strlen(key_path));

    // Bind to TLS endpoint
    int rc = zmq_bind(socket, "tls://*:5555");
    if (rc != 0) {
        printf("Bind failed: %s\n", zmq_strerror(zmq_errno()));
        return 1;
    }

    printf("Server listening on tls://*:5555\n");

    // Receive and send messages
    char buffer[256];
    while (1) {
        int size = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
        if (size > 0) {
            buffer[size] = '\0';
            printf("Received: %s\n", buffer);

            // Echo back
            zmq_send(socket, buffer, size, 0);
        }
    }

    zmq_close(socket);
    zmq_ctx_destroy(ctx);
    return 0;
}
```

### Basic Client

```c
#include <zmq.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    void *ctx = zmq_ctx_new();
    void *socket = zmq_socket(ctx, ZMQ_DEALER);

    // Configure TLS
    const char *ca_path = "/path/to/ca-cert.pem";
    const char *hostname = "localhost";

    zmq_setsockopt(socket, ZMQ_TLS_CA, ca_path, strlen(ca_path));
    zmq_setsockopt(socket, ZMQ_TLS_HOSTNAME, hostname, strlen(hostname));

    // Connect to TLS endpoint
    int rc = zmq_connect(socket, "tls://localhost:5555");
    if (rc != 0) {
        printf("Connect failed: %s\n", zmq_strerror(zmq_errno()));
        return 1;
    }

    printf("Connected to tls://localhost:5555\n");

    // Send a message
    const char *msg = "Hello, TLS!";
    zmq_send(socket, msg, strlen(msg), 0);

    // Receive response
    char buffer[256];
    int size = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
    if (size > 0) {
        buffer[size] = '\0';
        printf("Received: %s\n", buffer);
    }

    zmq_close(socket);
    zmq_ctx_destroy(ctx);
    return 0;
}
```

## WebSocket TLS (wss://)

WebSocket TLS uses the same certificate configuration as native TLS:

### WebSocket TLS Server

```c
#include <zmq.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    void *ctx = zmq_ctx_new();
    void *socket = zmq_socket(ctx, ZMQ_DEALER);

    // Configure TLS certificates
    const char *cert_path = "/path/to/server-cert.pem";
    const char *key_path = "/path/to/server-key.pem";

    zmq_setsockopt(socket, ZMQ_TLS_CERT, cert_path, strlen(cert_path));
    zmq_setsockopt(socket, ZMQ_TLS_KEY, key_path, strlen(key_path));

    // Bind to WebSocket TLS endpoint
    int rc = zmq_bind(socket, "wss://*:8080");
    if (rc != 0) {
        printf("Bind failed: %s\n", zmq_strerror(zmq_errno()));
        return 1;
    }

    printf("WebSocket server listening on wss://*:8080\n");

    // Handle messages
    char buffer[256];
    while (1) {
        int size = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
        if (size > 0) {
            buffer[size] = '\0';
            printf("Received: %s\n", buffer);
            zmq_send(socket, buffer, size, 0);
        }
    }

    zmq_close(socket);
    zmq_ctx_destroy(ctx);
    return 0;
}
```

### WebSocket TLS Client

```c
#include <zmq.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    void *ctx = zmq_ctx_new();
    void *socket = zmq_socket(ctx, ZMQ_DEALER);

    // Configure CA certificate for validation
    const char *ca_path = "/path/to/ca-cert.pem";
    zmq_setsockopt(socket, ZMQ_TLS_CA, ca_path, strlen(ca_path));

    // Connect to WebSocket TLS endpoint
    int rc = zmq_connect(socket, "wss://localhost:8080");
    if (rc != 0) {
        printf("Connect failed: %s\n", zmq_strerror(zmq_errno()));
        return 1;
    }

    printf("Connected to wss://localhost:8080\n");

    // Send message
    const char *msg = "Hello, WebSocket TLS!";
    zmq_send(socket, msg, strlen(msg), 0);

    // Receive response
    char buffer[256];
    int size = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
    if (size > 0) {
        buffer[size] = '\0';
        printf("Received: %s\n", buffer);
    }

    zmq_close(socket);
    zmq_ctx_destroy(ctx);
    return 0;
}
```

## TLS Socket Options Reference

### ZMQ_TLS_CERT

**Type**: String (path to file)
**Usage**: Server and client (for mutual TLS)
**Description**: Path to the certificate file in PEM format

```c
const char *cert = "/etc/ssl/certs/server.pem";
zmq_setsockopt(socket, ZMQ_TLS_CERT, cert, strlen(cert));
```

### ZMQ_TLS_KEY

**Type**: String (path to file)
**Usage**: Server and client (for mutual TLS)
**Description**: Path to the private key file in PEM format

```c
const char *key = "/etc/ssl/private/server-key.pem";
zmq_setsockopt(socket, ZMQ_TLS_KEY, key, strlen(key));
```

### ZMQ_TLS_CA

**Type**: String (path to file)
**Usage**: Client (and server for mutual TLS)
**Description**: Path to CA certificate for peer verification

```c
const char *ca = "/etc/ssl/certs/ca-bundle.pem";
zmq_setsockopt(socket, ZMQ_TLS_CA, ca, strlen(ca));
```

### ZMQ_TLS_HOSTNAME

**Type**: String
**Usage**: Client
**Description**: Expected hostname for certificate validation

```c
const char *hostname = "server.example.com";
zmq_setsockopt(socket, ZMQ_TLS_HOSTNAME, hostname, strlen(hostname));
```

## Advanced Configurations

### Mutual TLS (Client Authentication)

Both server and client provide certificates:

**Server:**
```c
// Server certificate
zmq_setsockopt(socket, ZMQ_TLS_CERT, "/path/to/server-cert.pem", ...);
zmq_setsockopt(socket, ZMQ_TLS_KEY, "/path/to/server-key.pem", ...);

// Require client certificates
zmq_setsockopt(socket, ZMQ_TLS_CA, "/path/to/ca-cert.pem", ...);
```

**Client:**
```c
// Client certificate
zmq_setsockopt(socket, ZMQ_TLS_CERT, "/path/to/client-cert.pem", ...);
zmq_setsockopt(socket, ZMQ_TLS_KEY, "/path/to/client-key.pem", ...);

// Server validation
zmq_setsockopt(socket, ZMQ_TLS_CA, "/path/to/ca-cert.pem", ...);
zmq_setsockopt(socket, ZMQ_TLS_HOSTNAME, "server.example.com", ...);
```

### PUB/SUB with TLS

TLS works with all supported socket types:

**Publisher:**
```c
void *pub = zmq_socket(ctx, ZMQ_PUB);
zmq_setsockopt(pub, ZMQ_TLS_CERT, cert_path, strlen(cert_path));
zmq_setsockopt(pub, ZMQ_TLS_KEY, key_path, strlen(key_path));
zmq_bind(pub, "tls://*:5556");
```

**Subscriber:**
```c
void *sub = zmq_socket(ctx, ZMQ_SUB);
zmq_setsockopt(sub, ZMQ_TLS_CA, ca_path, strlen(ca_path));
zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);  // Subscribe to all
zmq_connect(sub, "tls://publisher:5556");
```

## Troubleshooting

### Common Errors

**Error: "No such file or directory"**
- Verify certificate paths are correct
- Ensure files are readable by the process

**Error: "Certificate verification failed"**
- Check that CA certificate matches the server certificate
- Verify hostname matches certificate CN/SAN
- Ensure certificates haven't expired

**Error: "SSL handshake failed"**
- Verify both sides support compatible TLS versions
- Check that certificate and key match
- Ensure private key is not password-protected (or decrypt it first)

### Debugging Tips

1. **Use verbose logging**: Enable ZMQ logging to see detailed error messages
2. **Verify certificates**: Use OpenSSL command-line tools to verify certificates
   ```bash
   openssl verify -CAfile ca-cert.pem server-cert.pem
   openssl x509 -in server-cert.pem -text -noout
   ```
3. **Test with simple scenarios**: Start with self-signed certificates and localhost
4. **Check file permissions**: Ensure certificate files are readable

## Performance Considerations

- TLS adds encryption overhead (CPU usage for encryption/decryption)
- Connection setup is slower due to TLS handshake
- For best performance, reuse connections rather than creating new ones frequently
- Consider using `tcp://` for trusted internal networks where encryption isn't required

## Security Best Practices

1. **Certificate Management**:
   - Use proper CA-signed certificates in production
   - Rotate certificates before expiration
   - Protect private keys (restrict file permissions)

2. **Hostname Verification**:
   - Always set `ZMQ_TLS_HOSTNAME` on clients
   - Ensure certificates include correct hostname in CN or SAN

3. **Cipher Suites**:
   - zlink uses OpenSSL defaults (modern, secure ciphers)
   - Disable weak ciphers in production environments

4. **Certificate Validation**:
   - Always provide CA certificate on clients
   - Never disable certificate validation in production

## Migration from CURVE (libzmq)

zlink does not support CURVE. If you're migrating from libzmq CURVE, replace
the legacy options with TLS.

**Legacy libzmq CURVE code:**
```c
// Server
zmq_setsockopt(socket, ZMQ_CURVE_SERVER, &curve_server, sizeof(int));
zmq_setsockopt(socket, ZMQ_CURVE_SECRETKEY, server_secret, 41);

// Client
zmq_setsockopt(socket, ZMQ_CURVE_SERVERKEY, server_public, 41);
zmq_setsockopt(socket, ZMQ_CURVE_PUBLICKEY, client_public, 41);
zmq_setsockopt(socket, ZMQ_CURVE_SECRETKEY, client_secret, 41);
```

**New TLS code:**
```c
// Server
zmq_setsockopt(socket, ZMQ_TLS_CERT, cert_path, strlen(cert_path));
zmq_setsockopt(socket, ZMQ_TLS_KEY, key_path, strlen(key_path));

// Client
zmq_setsockopt(socket, ZMQ_TLS_CA, ca_path, strlen(ca_path));
zmq_setsockopt(socket, ZMQ_TLS_HOSTNAME, hostname, strlen(hostname));
```

**Key differences:**
- TLS uses file-based certificates instead of in-memory keys
- TLS provides standard PKI infrastructure compatibility
- TLS supports mutual authentication natively
- Connection string changes from `tcp://` to `tls://`

## Examples

Complete working examples are available in the `examples/` directory:

- `examples/tls_server.c`: Basic TLS server
- `examples/tls_client.c`: Basic TLS client
- `examples/wss_server.c`: WebSocket TLS server
- `examples/wss_client.c`: WebSocket TLS client

## References

- [OpenSSL Documentation](https://www.openssl.org/docs/)
- [ZeroMQ Guide](https://zguide.zeromq.org/)
- [PEM Format Specification](https://tools.ietf.org/html/rfc7468)
