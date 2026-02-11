English | [한국어](03-0-socket-patterns.ko.md)

# Socket Patterns Overview and Selection Guide

## 1. Overview

zlink provides 8 socket types. Each socket implements a unique messaging pattern, and communication is only possible between valid socket combinations.

## 2. Socket Summary

| Socket | Pattern | Direction | Routing Strategy | Primary Use |
|--------|---------|-----------|------------------|-------------|
| **PAIR** | 1:1 Bidirectional | Bidirectional | Single pipe (1:1 exclusive) | Inter-thread signaling, worker coordination |
| **PUB** | Publish | Unidirectional (send) | `dist_t` (Fan-out) | Event broadcast |
| **SUB** | Subscribe | Unidirectional (recv) | `fq_t` (Fair-queue) | Topic-filtered reception |
| **XPUB** | Advanced Publish | Bidirectional | `dist_t` + subscription recv | Proxy/broker, subscription monitoring |
| **XSUB** | Advanced Subscribe | Bidirectional | `fq_t` + subscription send | Proxy/broker |
| **DEALER** | Async Request | Bidirectional | Send: `lb_t` (Round-robin), Recv: `fq_t` | Load balancing, async requests |
| **ROUTER** | ID Routing | Bidirectional | Directed send by routing_id | Server, broker, multi-client |
| **STREAM** | RAW Communication | Bidirectional | routing_id based (4B uint32) | External client integration |

## 3. Socket Compatibility Matrix

Only valid socket combinations can be connected. Connecting incompatible sockets causes a handshake failure.

| | PAIR | PUB | SUB | XPUB | XSUB | DEALER | ROUTER | STREAM |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **PAIR** | **O** | | | | | | | |
| **PUB** | | | **O** | | **O** | | | |
| **SUB** | | **O** | | **O** | | | | |
| **XPUB** | | | **O** | | **O** | | | |
| **XSUB** | | **O** | | **O** | | | | |
| **DEALER** | | | | | | **O** | **O** | |
| **ROUTER** | | | | | | **O** | **O** | |
| **STREAM** | | | | | | | | **External** |

> STREAM sockets are not compatible with other zlink sockets; they communicate only with external RAW clients.

## 4. Routing Strategy Summary

| Strategy | Behavior | Used By |
|----------|----------|---------|
| **Single pipe** | Communicates with only one peer (N:1 not possible) | PAIR |
| **Round-robin** (`lb_t`) | Distributes to connected peers in rotation | DEALER send |
| **Fair-queue** (`fq_t`) | Receives fairly from all peers | DEALER/SUB recv |
| **Fan-out** (`dist_t`) | Replicates and sends to all subscribers | PUB/XPUB |
| **ID routing** | Directs to a specific peer via routing_id frame | ROUTER/STREAM |

> For internal implementation details of routing strategies, see [architecture.md](../internals/architecture.md).

## 5. Pattern Selection Guide

### Decision Flow

```
Is the communication peer an external client (browser, game)?
├── Yes → STREAM (ws/wss/tcp/tls)
└── No → Communication between zlink sockets
         ├── Is it 1:1 exclusive?
         │   └── Yes → PAIR
         └── No → N:M communication
              ├── Publish-subscribe (broadcast)?
              │   ├── Proxy/broker needed → XPUB/XSUB
              │   └── Simple pub-sub → PUB/SUB
              └── Request-reply / routing?
                  └── DEALER/ROUTER
```

### Recommendations by Use Case

| Use Case | Recommended Pattern | Description |
|----------|---------------------|-------------|
| Inter-thread signaling | PAIR + inproc | Fastest 1:1 communication |
| Event broadcast | PUB/SUB | Topic-based filtering |
| Message broker/proxy | XPUB/XSUB | Subscription message access and transformation |
| Async request-reply server | DEALER/ROUTER | Multi-client handling |
| Load balancing | Multiple DEALERs → ROUTER | Round-robin distribution |
| Targeted peer delivery | ROUTER | Specify target via routing_id |
| Web client integration | STREAM + ws/wss | WebSocket RAW communication |
| External TCP client | STREAM + tcp/tls | Length-prefix RAW communication |

## 6. Sub-Documents

See the individual documents for detailed usage of each socket type.

| Document | Socket | Description |
|----------|--------|-------------|
| [03-1-pair.md](03-1-pair.md) | PAIR | 1:1 bidirectional exclusive connection |
| [03-2-pubsub.md](03-2-pubsub.md) | PUB/SUB/XPUB/XSUB | Publish-subscribe family |
| [03-3-dealer.md](03-3-dealer.md) | DEALER | Async request, round-robin |
| [03-4-router.md](03-4-router.md) | ROUTER | ID-based routing |
| [03-5-stream.md](03-5-stream.md) | STREAM | External client RAW communication |

## 7. Basic Usage Flow

The basic pattern common to all socket types:

```c
/* 1. Create Context */
void *ctx = zlink_ctx_new();

/* 2. Create Socket */
void *socket = zlink_socket(ctx, ZLINK_<TYPE>);

/* 3. Set socket options (before bind/connect) */
zlink_setsockopt(socket, ZLINK_<OPTION>, &value, sizeof(value));

/* 4. Establish connection (bind or connect) */
zlink_bind(socket, "tcp://*:5555");
// or
zlink_connect(socket, "tcp://127.0.0.1:5555");

/* 5. Send/receive messages */
zlink_send(socket, data, size, flags);
zlink_recv(socket, buf, buf_size, flags);

/* 6. Cleanup */
zlink_close(socket);
zlink_ctx_term(ctx);
```

> Socket options must be set **before** calling `zlink_bind()`/`zlink_connect()`.

---
[← Core API](02-core-api.md) | [PAIR →](03-1-pair.md)
