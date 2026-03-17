English | [한국어](01-overview.ko.md)

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
┌──────────────────────────────────────────────────────┐
│  Application Layer                                    │
│  zlink_ctx_new() · zlink_socket() · zlink_send() · callback dispatch │
├──────────────────────────────────────────────────────┤
│  Socket Logic Layer                                   │
│  PAIR · PUB/SUB · XPUB/XSUB · DEALER/ROUTER · STREAM│
│  Routing: lb_t(Round-robin) · fq_t · dist_t           │
├──────────────────────────────────────────────────────┤
│  Engine Layer (Boost.Asio)                            │
│  asio_zmp_engine — ZMP v1.0 Protocol (8B fixed hdr)  │
│  Proactor pattern · Speculative I/O · Backpressure    │
├──────────────────────────────────────────────────────┤
│  Transport Layer                                      │
│  tcp · ipc · inproc · ws — plaintext                  │
│  tls · wss             — OpenSSL encrypted            │
├──────────────────────────────────────────────────────┤
│  Core Infrastructure                                  │
│  msg_t(64B fixed) · pipe_t(Lock-free YPipe)           │
│  ctx_t(I/O Thread Pool) · session_base_t(Bridge)      │
└──────────────────────────────────────────────────────┘
```

## 3. Core Design

| Design Principle | Description |
|------------------|-------------|
| **Zero-Copy** | VSM (33B or less) stored inline; large messages use reference counting |
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

## 6. Quick Start

### Requirements

- CMake 3.10+, C++17 compiler, OpenSSL

### Build

```bash
cmake -B build -DWITH_TLS=ON -DBUILD_TESTS=ON
cmake --build build
```

### First Program

```c
#include <zlink.h>
#include <string.h>
#include <stdio.h>

void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    printf("Received: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

int main(void) {
    void *ctx = zlink_ctx_new();

    /* Server (with receive handler) */
    zlink_socket_handler_t handler = {
        .kind = ZLINK_SOCKET_HANDLER_MSG,
        .fn.msg = on_message,
        .userdata = NULL
    };
    void *server = zlink_socket(ctx, ZLINK_PAIR);
    zlink_socket_attach_handler(server, &handler);
    zlink_bind(server, "tcp://*:5555");

    /* Client (send only) */
    void *client = zlink_socket(ctx, ZLINK_PAIR);
    zlink_connect(client, "tcp://127.0.0.1:5555");

    /* Send */
    const char *msg = "Hello zlink!";
    zlink_send(client, msg, strlen(msg), 0);

    /* Handler callback receives the message asynchronously */
    msleep(100);

    zlink_close(client);
    zlink_close(server);
    zlink_ctx_term(ctx);
    return 0;
}
```

## 7. Next Steps

- [Core API Details](02-core-api.md)
- [Socket Pattern Usage](03-0-socket-patterns.md)
- [Transport Guide](04-transports.md)
- [TLS Security Configuration](05-tls-security.md)

---
[Core API →](02-core-api.md)
