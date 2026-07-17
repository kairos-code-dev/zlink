English | [한국어](01-overview.ko.md)

<!-- zlink-nav:start -->
[Core API →](02-core-api.md)
<!-- zlink-nav:end -->

# zlink Overview and Getting Started

## 1. What Is zlink?

zlink is a modern messaging library based on [libzmq](https://github.com/zeromq/libzmq) v4.3.5. It focuses on core patterns and provides Boost.Asio-based I/O with a developer-friendly API.

### Changes Compared to libzmq

| | libzmq | zlink |
|---|--------|-------|
| **Socket Types** | 17 (including draft) | **8** — PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER, STREAM |
| **I/O Engine** | Custom poll/epoll/kqueue | **Boost.Asio** (bundled, no external dependencies) |
| **Encryption** | CURVE (libsodium) | **TLS** (OpenSSL) — `tls://`, `wss://` |
| **Transport** | 10+ (PGM, TIPC, VMCI, etc.) | **6** — `tcp`, `ipc`, `inproc`, `ws`, `wss`, `tls` |
| **Dependencies** | libsodium, libbsd, etc. | **OpenSSL only** |

Note: `pgm://` and `epgm://` are currently disabled and unsupported in zlink.

## 2. Architecture Overview

```
+------------------------------------------------------+
|  Application / Bindings                              |
|  C callers · cpp · dotnet · java · node · python     |
+------------------------------------------------------+
|  Public API Facade  (core/src/api/)                  |
|  context_api · socket_api · message_api              |
|  service_api · poller_api · monitor_api              |
|  validate + delegate, per-handle admission guard     |
+------------------------------------------------------+
|  Service Layer                                       |
|  SPOT · Actor (public) · internal location runtime   |
|  service access seam (*_access) · lifecycle · runtime|
+------------------------------------------------------+
|  Socket Semantic / Runtime                           |
|  PAIR · PUB/SUB · XPUB/XSUB · DEALER/ROUTER · STREAM |
|  semantic entrypoint + runtime components            |
|  (dispatch · monitor · endpoint · lifecycle)         |
+------------------------------------------------------+
|  Runtime Core  (core/src/core/)                      |
|  ctx · own · reaper · multipart_send_txn             |
|  options dispatch (core_socket · transport · protocol|
|  close/drain/finalization contract                   |
+------------------------------------------------------+
|  Engine Layer (Boost.Asio)                           |
|  asio_zmp_engine — ZMP v1.0 Protocol (8B fixed hdr)  |
|  Proactor pattern · Speculative I/O · Backpressure   |
+------------------------------------------------------+
|  Transport / Protocol                                |
|  tcp · ipc · inproc · ws — plaintext                 |
|  tls · wss             — OpenSSL encrypted           |
+------------------------------------------------------+
|  Core Infrastructure                                 |
|  msg_t(64B fixed) · pipe_t(Lock-free YPipe)          |
|  ctx_t(I/O Thread Pool) · session_base_t(Bridge)     |
+------------------------------------------------------+
```

Key roles per layer:

| Layer | Role |
|-------|------|
| Public API Facade | C API entry point. Validate + delegate only; does not know concrete service/socket details |
| Service Layer | SPOT(+Actor) semantics and lifecycle. Connected to the API layer via service-local access seams |
| Socket Semantic/Runtime | Socket family semantics (semantic) and common mechanism (runtime components) are separated |
| Runtime Core | Context, shutdown, close/drain orchestration, option dispatch, logical multipart send |
| Engine Layer | Boost.Asio-based poller, io_context, mailbox execution backbone |
| Transport/Protocol | Wire format, TLS handshake, address scheme details |

## 3. Core Design

| Design Principle | Description |
|------------------|-------------|
| **Zero-Copy** | VSM (41B or less) stored inline; large messages use reference counting |
| **Lock-Free** | YPipe (CAS-based FIFO) used for inter-thread communication |
| **True Async** | Asynchronous I/O based on the Proactor pattern |
| **Protocol Agnostic** | Clear separation between Transport and Protocol |

## 4. Socket Types

| Socket Type | Pattern | Description |
|-------------|---------|-------------|
| PAIR | 1:1 Bidirectional | Inter-thread signaling, simple communication |
| PUB/SUB | Publish-Subscribe | Topic-based message distribution |
| XPUB/XSUB | Advanced Pub-Sub | Subscription message access, proxying |
| DEALER/ROUTER | Async Routing | Request-reply, load balancing |
| STREAM | RAW Communication | External client integration (tcp/tls/ws/wss) |

## 5. Transport

| Transport | URI Format | Description |
|-----------|------------|-------------|
| tcp | `tcp://host:port` | Standard TCP |
| ipc | `ipc://path` | Unix domain socket |
| inproc | `inproc://name` | In-process communication |
| ws | `ws://host:port` | WebSocket |
| wss | `wss://host:port` | WebSocket + TLS |
| tls | `tls://host:port` | Native TLS |

## 6. Service Layer

The service layer provides high-level distributed features on top of sockets.
It automates socket connection management, peer address tracking, and service
lifecycle.

| Service | Role |
|---------|------|
| **SPOT** | Dynamic state units on the MeshNode: channel subscriptions, Logical Multicast and direct messaging. The `MeshNode` owns the transport; `Spot` facades provide the data plane |
| **Actor** | Session-based addressing unit joined to a Spot. The `MeshNode` manages the Actor registry; the `Entry Spot` handles initial dispatch |

See the [Service Layer Overview](07-0-services.md), [SPOT Guide](07-3-spot.md),
and [SPOT Actor Guide](07-4-actor.md) for details.

## 7. Quick Start

### Requirements

- CMake 3.10+, C++17 compiler, OpenSSL

### Build

```bash
cmake -S core -B core/build -DWITH_TLS=ON -DBUILD_TESTS=ON
cmake --build core/build
```

### First Program

```c
#include <zlink.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    void *ctx = zlink_ctx_new();

    /* Server */
    void *server = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
    zlink_bind(server, "tcp://*:5555");

    /* Client */
    void *client = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
    zlink_connect(client, "tcp://127.0.0.1:5555");

    /* Send */
    zlink_msg_t part;
    zlink_msg_init_size(&part, 12);
    memcpy(zlink_msg_data(&part), "Hello zlink!", 12);
    zlink_send(client, &part, 1, 0);

    /* Receive */
    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    zlink_recv_result_t rc = zlink_recv(server, &source_rid, &parts, &part_count, 0);
    if (rc == ZLINK_RECV_OK)
        printf("Received: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);

    zlink_close(client);
    zlink_close(server);
    zlink_ctx_term(ctx);
    return 0;
}
```

## 8. Next Steps

- [Core API Details](02-core-api.md)
- [Socket Pattern Usage](03-0-socket-patterns.md)
- [Transport Guide](04-transports.md)
- [TLS Security Configuration](05-tls-security.md)
- [Service Layer Overview](07-0-services.md)
- [SPOT Guide](07-3-spot.md)
- [SPOT Actor Guide](07-4-actor.md)

---
<!-- zlink-nav:bottom:start -->
[Core API →](02-core-api.md)
<!-- zlink-nav:bottom:end -->
