# Phase 3: TLS Protocol Implementation

## Overview
This document describes the implementation of Phase 3: Add tls:// Protocol + TLS Options API.

## Implementation Date
2026-01-13

## Objectives
- Add `tls://` protocol support for ZeroMQ
- Implement TLS configuration options API
- Enable TLS capability detection via `zmq_has("tls")`
- Prepare infrastructure for future TLS transport implementation

## Changes Made

### 1. TLS Option Constants (`include/zmq.h`)
Added 8 new TLS option constants:
```c
#define ZMQ_TLS_CERT                95  // Server certificate file path
#define ZMQ_TLS_KEY                 96  // Server private key file path
#define ZMQ_TLS_CA                  97  // CA certificate file path
#define ZMQ_TLS_VERIFY              98  // Verify client certificate (default: 1)
#define ZMQ_TLS_REQUIRE_CLIENT_CERT 99  // Require client cert for mTLS (default: 0)
#define ZMQ_TLS_HOSTNAME           100  // SNI + hostname verification
#define ZMQ_TLS_TRUST_SYSTEM       101  // Use system CA store (default: 1)
#define ZMQ_TLS_PASSWORD           102  // Private key password (optional)
```

### 2. Protocol Name (`src/address.hpp`)
Added `tls` protocol name constant:
```cpp
#ifdef ZMQ_HAVE_TLS
static const char tls[] = "tls";
#endif
```

### 3. Options Structure (`src/options.hpp`)
Added TLS configuration fields to `options_t`:
```cpp
#ifdef ZMQ_HAVE_TLS
    std::string tls_cert;              // Server certificate file path
    std::string tls_key;               // Server private key file path
    std::string tls_ca;                // CA certificate file path
    int tls_verify;                    // Verify client certificate (default: 1)
    int tls_require_client_cert;       // Require client certificate for mTLS (default: 0)
    std::string tls_hostname;          // SNI + hostname verification
    int tls_trust_system;              // Use system CA store (default: 1)
    std::string tls_password;          // Private key password (optional)
#endif
```

### 4. Options Implementation (`src/options.cpp`)
- Initialized TLS fields in constructor with defaults:
  - `tls_verify = 1` (verification enabled)
  - `tls_require_client_cert = 0` (client cert optional)
  - `tls_trust_system = 1` (use system CA)
- Implemented `setsockopt()` for all 8 TLS options
- Implemented `getsockopt()` for all 8 TLS options

### 5. Protocol Support (`src/socket_base.cpp`)
Added `tls://` protocol support in:
- `check_protocol()`: Validates protocol is supported
- `bind()`: Accepts `tls://` for binding (uses TCP listener)
- `connect()`: Accepts `tls://` for connecting (uses TCP connecter)
- `unbind()`: Handles `tls://` address resolution

### 6. Address Handling (`src/address.cpp`)
Updated address lifecycle management:
- `~address_t()`: Cleanup for `tls://` addresses (shares TCP address structure)
- `to_string()`: Convert `tls://` addresses back to URI format

### 7. Session Connecter (`src/session_base.cpp`)
Added `tls://` protocol handling in `start_connecting()`:
- Uses ASIO TCP connecter for `tls://` addresses
- Shares TCP transport infrastructure

### 8. ASIO TCP Connecter (`src/asio/asio_tcp_connecter.cpp`)
Updated protocol assertion to accept `tls://`:
- Constructor now validates both `tcp://` and `tls://` protocols

### 9. Capability Detection (`src/zmq.cpp`)
Implemented `zmq_has("tls")`:
```cpp
#if defined(ZMQ_HAVE_TLS)
    if (strcmp (capability_, "tls") == 0)
        return true;
#endif
```

### 10. Build System
**CMakeLists.txt:**
- Set `ZMQ_HAVE_TLS` flag when ASIO SSL is available
- Added informational message about TLS transport

**builds/cmake/platform.hpp.in:**
- Added `#cmakedefine ZMQ_HAVE_TLS` for conditional compilation

## Architecture Notes

### TLS vs TCP Address Format
- `tls://` uses the same address format as `tcp://`
- Format: `tls://host:port` (e.g., `tls://localhost:5555`)
- Currently shares TCP transport infrastructure
- Future phases will implement actual TLS/SSL encryption

### Integration with ASIO SSL
- `ZMQ_HAVE_TLS` is enabled when `ZMQ_HAVE_ASIO_WSS` is available
- Depends on ASIO Boost.Beast + OpenSSL
- Shares SSL infrastructure with WebSocket Secure (WSS) transport

### Current Limitations
1. **No Actual TLS Encryption Yet**:
   - `tls://` protocol is parsed and accepted
   - Options can be set/get successfully
   - Actual SSL/TLS handshake not implemented (future phase)

2. **Uses TCP Transport**:
   - Currently routes through TCP listeners/connecters
   - Will be upgraded to use SSL streams in future phase

3. **WSS Hostname Integration**:
   - `wss_hostname` still exists separately
   - Future optimization: unify with `tls_hostname`

## Testing

### Test Coverage
Created and executed test program that validates:
1. ✅ `zmq_has("tls")` returns true when ASIO SSL is enabled
2. ✅ Socket creation succeeds
3. ✅ All 8 TLS options can be set via `zmq_setsockopt()`
4. ✅ All 8 TLS options can be retrieved via `zmq_getsockopt()`
5. ✅ `tls://` protocol is accepted by `zmq_connect()`
6. ✅ No `EPROTONOSUPPORT` errors

### Build Verification
- ✅ Compiles successfully on Linux x64
- ✅ No compiler warnings or errors
- ✅ All existing tests pass

## Files Modified

### Header Files
- `/home/ulalax/project/ulalax/zlink/include/zmq.h`
- `/home/ulalax/project/ulalax/zlink/src/address.hpp`
- `/home/ulalax/project/ulalax/zlink/src/options.hpp`

### Source Files
- `/home/ulalax/project/ulalax/zlink/src/address.cpp`
- `/home/ulalax/project/ulalax/zlink/src/options.cpp`
- `/home/ulalax/project/ulalax/zlink/src/socket_base.cpp`
- `/home/ulalax/project/ulalax/zlink/src/session_base.cpp`
- `/home/ulalax/project/ulalax/zlink/src/zmq.cpp`
- `/home/ulalax/project/ulalax/zlink/src/asio/asio_tcp_connecter.cpp`

### Build Configuration
- `/home/ulalax/project/ulalax/zlink/CMakeLists.txt`
- `/home/ulalax/project/ulalax/zlink/builds/cmake/platform.hpp.in`

## API Usage Example

```c
#include <zmq.h>

// Check TLS support
if (zmq_has("tls")) {
    printf("TLS is available\n");
}

// Create socket
void *ctx = zmq_ctx_new();
void *sock = zmq_socket(ctx, ZMQ_DEALER);

// Configure TLS options
const char *cert = "/path/to/server-cert.pem";
const char *key = "/path/to/server-key.pem";
const char *ca = "/path/to/ca-cert.pem";
const char *hostname = "example.com";

zmq_setsockopt(sock, ZMQ_TLS_CERT, cert, strlen(cert));
zmq_setsockopt(sock, ZMQ_TLS_KEY, key, strlen(key));
zmq_setsockopt(sock, ZMQ_TLS_CA, ca, strlen(ca));
zmq_setsockopt(sock, ZMQ_TLS_HOSTNAME, hostname, strlen(hostname));

int verify = 1;
zmq_setsockopt(sock, ZMQ_TLS_VERIFY, &verify, sizeof(verify));

// Connect using tls:// protocol
zmq_connect(sock, "tls://secure-server.example.com:5555");

// ... use socket ...

zmq_close(sock);
zmq_ctx_term(ctx);
```

## Future Work

### Phase 4 (Planned): Actual TLS Transport Implementation
1. Create `asio_ssl_connecter_t` for TLS client connections
2. Create `asio_ssl_listener_t` for TLS server bindings
3. Implement SSL context initialization using TLS options
4. Add SSL handshake logic
5. Integrate with ZMTP protocol over SSL streams
6. Add mutual TLS (mTLS) support
7. Certificate validation and hostname verification
8. Error handling for SSL-specific failures

### Phase 5 (Planned): Advanced Features
1. Session resumption support
2. ALPN (Application-Layer Protocol Negotiation)
3. Certificate pinning
4. Custom SSL context configuration
5. Performance optimization

## Completion Criteria
✅ All objectives completed:
- [x] TLS option constants defined
- [x] Protocol name added
- [x] Options structure extended
- [x] setsockopt/getsockopt implemented
- [x] Protocol parsing support
- [x] zmq_has("tls") implemented
- [x] Build system updated
- [x] Tests passing
- [x] Documentation complete

## Status
**PHASE 3: COMPLETE** ✅

The TLS protocol API infrastructure is fully implemented and ready for integration with actual TLS transport in future phases.
