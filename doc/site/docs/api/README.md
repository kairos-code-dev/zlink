[English](README.md) | [한국어](README.ko.md)

[Spec Index](../README.md)

# zlink Core Specification

This specification defines the public C interface of the zlink library.
A conforming implementation MUST provide every function, type, and constant
described in this section with the specified semantics. The public surface
is defined in `core/include/zlink.h`.

## Spec Documents

| Document | Description |
|----------|-------------|
| [errors.md](errors.md) | Error codes, error strings, and version query |
| [errno-map.md](errno-map.md) | Errno matrix for send, request, and reply functions |
| [context.md](context.md) | Context creation, termination, and option tuning |
| [message.md](message.md) | Message lifecycle, data access, ownership, and properties |
| [socket/](socket/README.md) | Socket specifications (common + per-type) |
| [monitoring.md](monitoring.md) | Socket monitors, monitor snapshots, and peer inspection |
| [events.md](events.md) | Canonical event catalog and readiness semantics |
| [service/README.md](service/README.md) | Shared service-layer concepts and document split |
| [service/registry.md](service/registry.md) | Service registry creation, configuration, and clustering |
| [service/discovery.md](service/discovery.md) | Service discovery, subscription, and peer lookup |
| [service/spot.md](service/spot.md) | SPOT topic-based PUB/SUB and routed messaging |
| [polling.md](polling.md) | Proxy helpers and capability query |
| [utilities.md](utilities.md) | Timers, threads, stopwatch, and atomics |

## Types

| Type | Defined in | Description |
|------|-----------|-------------|
| [`zlink_msg_t`](message.md) | message.md | Opaque message container (64-byte, stack-allocatable) |
| [`zlink_routing_id_t`](message.md) | message.md | Peer routing identity (1-byte size + 255-byte data) |
| `zlink_socket_msg_handler_fn` | [socket/](socket/README.md) | Raw `STREAM` raw receive callback |
| [`zlink_monitor_event_t`](monitoring.md) | monitoring.md | Monitor event structure (event, value, addresses) |
| [`zlink_monitor_snapshot_t`](monitoring.md) | monitoring.md | Monitor snapshot (state and queue depth) |
| [`zlink_fd_t`](polling.md) | polling.md | Platform-dependent file descriptor type |

## Callback Types

| Type | Defined in | Description |
|------|-----------|-------------|
| [`zlink_socket_msg_handler_fn`](socket/README.md) | socket/ | Raw receive callback type for raw `STREAM` |
| [`zlink_stream_packet_handler_fn`](socket/README.md) | socket/ | Packet receive callback type for raw `STREAM` |
| [`zlink_reply_handler_fn`](socket/README.md) | socket/ | Asynchronous request-reply completion callback |
| [`zlink_spot_handler_fn`](service/spot.md) | service/spot.md | SPOT routed message dispatch callback |
| [`zlink_spot_dispatch_event_handler_fn`](service/spot.md) | service/spot.md | SPOT dispatch event callback |
| [`zlink_monitor_handler_fn`](monitoring.md) | monitoring.md | Socket monitor event callback |
| [`zlink_send_ready_handler_fn`](socket/README.md) | socket/ | Send-ready transition callback |
| [`zlink_free_fn`](message.md) | message.md | Deallocation callback for zero-copy messages |
| [`zlink_timer_handler_fn`](utilities.md) | utilities.md | Timer expiry callback |
| [`zlink_thread_fn`](utilities.md) | utilities.md | Thread entry-point function |

## Internal Architecture

The public C API is defined in `core/include/zlink.h` and serves as the
external contract including bindings. The internal implementation follows
POSD (Philosophy of Software Design) principles and is organized into
the following layers:

```
Public API Facade  →  Service Access Layer  →  Service/Socket Runtime
     (api/)            (*_access.hpp)            (services/ · sockets/)
                                                      ↓
                                              Runtime Core (core/)
                                              Engine (engine/asio/)
                                              Transport/Protocol
```

| Layer | Source Location | Role |
|-------|-----------------|------|
| API Facade | `core/src/api/` | C API entry point. Validate + delegate. 37 files split by concern |
| Service Access | `core/src/services/*/` | Service-local access seam (`*_access.hpp`). Contract between API and concrete implementation |
| Socket Runtime | `core/src/sockets/` | Socket semantic (per-family) + runtime components (dispatch/monitor/endpoint/lifecycle) separation |
| Runtime Core | `core/src/core/` | ctx, options dispatch (core_socket/transport/protocol), multipart_send_txn, close/drain |
| Engine | `core/src/engine/` | Boost.Asio-based poller, io_context, mailbox execution backbone |

Option dispatch is split into three categories, each handled by its own
domain owner for validation/apply: core_socket, transport_network,
protocol_metadata.

For internal architecture details, see the
[POSD Module Structure](../../internals/posd-module-structure.md) document.

---

For conceptual guides and tutorials, see the [User Guide](../../guide/01-overview.md).
