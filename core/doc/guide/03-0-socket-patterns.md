English | [한국어](03-0-socket-patterns.ko.md)

<!-- zlink-nav:start -->
[← Core API](02-core-api.md) | [PAIR →](03-1-pair.md)
<!-- zlink-nav:end -->

# Socket Patterns Overview and Selection Guide

## 1. Overview

zlink provides 8 socket types. Each socket implements a unique messaging pattern, and communication is only possible between valid socket combinations.

> Terms such as **hot path**, **control path**, and **admission guard** used in this document are defined in [Section 9 (Terminology)](#9-terminology).

## 2. Socket Summary

| Socket | Pattern | Direction | Routing Strategy | Primary Use |
|--------|---------|-----------|------------------|-------------|
| **PAIR** | 1:1 Bidirectional | Bidirectional | Single pipe (1:1 exclusive) | Inter-thread signaling, worker coordination |
| **PUB** | Publish | Unidirectional (send) | `dist_t` (Fan-out) | Event broadcast |
| **SUB** | Subscribe | Unidirectional (recv) | `fq_t` (Fair-queue) | Topic-filtered reception |
| **XPUB** | Advanced Publish | Bidirectional | `dist_t` + subscription recv | Proxy/broker, subscription monitoring |
| **XSUB** | Advanced Subscribe | Unidirectional (recv) | `fq_t` (no local filter) | Proxy/broker |
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

> `lb_t`, `fq_t`, and `dist_t` are **internal implementation type names** that appear in the source tree and internals documentation. The parenthetical labels (Round-robin, Fair-queue, Fan-out) are their functional descriptions for everyday use. You do not need to know the internal type names unless you are reading or modifying the core source.

> For internal implementation details of routing strategies, see [architecture.md](../internals/architecture.md).

## 5. Pattern Selection Guide

### Decision Flow

```
Is the communication peer an external client (browser, game)?
+-- Yes → STREAM (ws/wss/tcp/tls)
+-- No → Communication between zlink sockets
         +-- Is it 1:1 exclusive?
         |   +-- Yes → PAIR
         +-- No → N:M communication
              +-- Publish-subscribe (broadcast)?
              |   +-- Proxy/broker needed → XPUB/XSUB
              |   +-- Simple pub-sub → PUB/SUB
              +-- Request-reply / routing?
                  +-- DEALER/ROUTER
```

### Recommendations by Use Case

| Use Case | Recommended Pattern | Description |
|----------|---------------------|-------------|
| Inter-thread signaling | PAIR + inproc | Fastest 1:1 communication |
| Event broadcast | PUB/SUB | Topic-based filtering |
| Message broker/proxy | XPUB/XSUB | Subscription message access and transformation |
| Async request-reply server | DEALER↔DEALER | Async bidirectional communication |
| Load balancing | Multiple DEALERs → ROUTER | Round-robin distribution |
| Targeted peer delivery | ROUTER | Specify target via routing_id |
| Web client integration | STREAM + ws/wss | WebSocket RAW communication |
| External TCP client | STREAM + tcp/tls | Length-prefix RAW communication |

> For location transparency (auto-connect, load balancing, topic mesh),
> use the service layer (SPOT) instead of raw sockets.
> See [Services Overview](07-0-services.md) for details.

## 6. Sub-Documents

See the individual documents for detailed usage of each socket type.

| Document | Socket | Description |
|----------|--------|-------------|
| [03-1-pair.md](03-1-pair.md) | PAIR | 1:1 bidirectional exclusive connection |
| [03-2-pubsub.md](03-2-pubsub.md) | PUB/SUB/XPUB/XSUB | Publish-subscribe family |
| [03-3-dealer.md](03-3-dealer.md) | DEALER | Async request, round-robin |
| [03-4-router.md](03-4-router.md) | ROUTER | ID-based routing |
| [03-5-stream.md](03-5-stream.md) | STREAM | External client RAW communication |
| [03-6-proxy.md](03-6-proxy.md) | (proxy) | XPUB/XSUB and DEALER/ROUTER message broker |

## 7. Disconnecting a Peer by Routing ID

The `connect`/`disconnect` lifecycle normally works with endpoint strings.
However, when a socket receives a message its `source_rid` identifies the
sending peer directly. If you need to tear down only that peer connection —
without knowing its endpoint string — use `zlink_disconnect_rid()`.

When a receive path gives you `source_rid` and you need to close only that
peer connection, use `zlink_disconnect_rid()`. You do not need to keep the
endpoint string just to close the peer you observed.

```c
zlink_connect_result_t rc = zlink_disconnect_rid(socket, &source_rid);
```

Missing targets return `ZLINK_CONNECT_NOT_FOUND`, duplicate peer routing ids
return `ZLINK_CONNECT_CONFLICT`, and sockets whose connection lifecycle is
managed by another owner (a higher-level runtime) return `ZLINK_CONNECT_BUSY`.

## 8. Common Receive Interface

The primary receive model for the raw socket family is `recv + poller`.
The server loop watches `ZLINK_POLLIN` on a poller and then pulls data
with the recv-family function appropriate to the socket. `zlink_recv()`
is the common entry point for that model.

```c
zlink_recv_result_t zlink_recv (
    void *socket,
    zlink_routing_id_t *source_rid,   /* sender routing_id */
    zlink_msg_t **parts,              /* multipart data */
    size_t *part_count,               /* frame count */
    zlink_recv_flags_t flags);
```

- **`source_rid`**: Populated with the sender peer's routing_id on all
  socket types that use this surface. This is not a message frame — zlink
  automatically resolves the connected peer's identity as a separate
  parameter.
- **`parts` / `part_count`**: Multipart is the default on all sockets.
  `part_count=1` for single frame, `part_count=2+` for multipart.

> **Difference from libzmq:** libzmq ROUTER returned routing_id as the
> first frame of `zmq_recv()`. In zlink, routing_id is a separate
> parameter.

**Socket-specific receive surfaces:**

- **PAIR / DEALER**: use `zlink_recv()`. DEALER additionally delivers
  replies through the `zlink_dealer_request()` completion callback.
- **ROUTER**: `zlink_recv()` on a ROUTER handle fails with
  `ZLINK_RECV_NOT_SUPPORTED`. ROUTER uses a single unified typed surface —
  `zlink_router_recv()` — that returns `source_node_rid` and `request_seq`.
  Request replies are delivered through a separate completion callback. See
  [03-4-router.md](03-4-router.md).
- **SUB / XSUB**: use `zlink_subscribe()`. They are recv-only; no direct
  topic callback surface is provided.
- **STREAM**: exception type. Choose one of three models:
  `zlink_recv()` (raw recv), `zlink_recv_handler()` (raw callback), or
  `zlink_stream_packet_handler()` (packet callback). A second attempt to
  activate a different mode on the same handle fails with `EBUSY`.
- **MeshNode/Spot/Actor**: use the ready handler or the poller for unified readiness, then read records through ready/claim/receive batches ([07-3 SPOT](07-3-spot.md) §5).
- **monitor / timer**: both recv and callback models are supported.

In short, data-plane receive defaults to `recv + poller`. Callback-based receive is kept only for exception types whose usage pattern justifies it: `STREAM` and monitor/timer. The MeshNode ready handler is only a wakeup signal; payload is still pulled by receive APIs. Request
completion callbacks live on a separate axis (async operation completion), not
on the data-plane receive axis.

## 9. Terminology

Terms used throughout the documentation:

| Term | Meaning |
|------|---------|
| **hot path** | High-frequency call path. Data transfer APIs like `send`, `publish`. Optimized for concurrent calls |
| **control path** | Low-frequency call path. Configuration/management APIs like `bind`, `connect`, `set_option`, `monitor`. Correctness guaranteed via internal serialization |
| **correctness** | Property that multiple threads can use the same handle concurrently without data corruption or crashes |
| **fail-fast lifecycle gate** | On `close`/`destroy`: returns `ZLINK_CLOSE_BUSY` immediately if other threads are using the handle; after close is accepted, new API calls return `ZLINK_CLOSE_SHUTDOWN` |
| **admission guard** | Internal gate that checks handle validity and whether shutdown is in progress on API entry |
| **approximate limit** | Not an exact hard limit. HWM allows slight overshoot for lock-free performance |

> For the full thread-safety contract, see the [Thread-Safety Guide](11-thread-safety.md).

## 10. Basic Usage Flow

The basic pattern common to raw socket types is a `recv + poller` loop.
For example, a DEALER client receiving replies uses the following shape:

```c
/* 1. Create Context */
void *ctx = zlink_ctx_new();

/* 2. Create Socket */
void *socket = zlink_socket(ctx, ZLINK_SOCKET_DEALER);

/* 3. Set options (before bind/connect) */
zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

/* 4. Establish connection */
zlink_connect(socket, "tcp://127.0.0.1:5555");

/* 5. Poll and recv */
void *poller = zlink_poller_new();
zlink_poller_add(poller, socket, user_data, ZLINK_POLLIN);

while (running) {
    zlink_poller_event_t ev;
    if (zlink_poller_wait(poller, &ev, 1, timeout_ms, NULL) <= 0) continue;
    if (ev.events & ZLINK_POLLIN) {
        zlink_routing_id_t rid;
        zlink_msg_t *parts = NULL;
        size_t n = 0;
        if (zlink_recv(socket, &rid, &parts, &n, 0) == ZLINK_RECV_OK) {
            /* process parts, then close each */
            zlink_multipart_close(parts, n);
        }
    }
}

/* 6. Cleanup */
zlink_poller_destroy(&poller);
zlink_close(socket);
zlink_ctx_term(ctx);
```

> The following options must be set **before** `zlink_bind()`/`zlink_connect()`
> as they are used during handshake or connection:
> `zlink_set_routing_id()`, `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` (via `zlink_set_router_option()`), `ZLINK_ROUTER_OPT_PROBE` (via `zlink_set_router_option()`), `zlink_set_tls_server()` / `zlink_set_tls_client()`.
> Other options (`ZLINK_OPT_SNDHWM`, `ZLINK_OPT_RCVHWM`, `ZLINK_OPT_LINGER`, `ZLINK_OPT_SNDTIMEO`, etc.) can be changed after bind/connect.

> **Why callbacks are not the default:** raw `PAIR`, `DEALER`, `SUB`,
> `XSUB`, and `ROUTER` receive through the synchronous pull-mode loop (no
> recv callback); they remain send-capable where the type allows. Multiple
> sockets, monitors, and
> timers compose naturally in the same poller loop, and the caller keeps
> explicit control over which thread runs what in which order. Callback
> surfaces are retained only where the usage pattern justifies them:
> `STREAM`, monitor/timer, SPOT dispatch events, and request
> completion.

---
<!-- zlink-nav:bottom:start -->
[← Core API](02-core-api.md) | [PAIR →](03-1-pair.md)
<!-- zlink-nav:bottom:end -->
