[English](README.md) | [한국어](README.ko.md)

# zlink API Reference

The zlink C library provides a messaging and service-discovery toolkit built
on top of lightweight I/O threads. Message receiving supports two modes:
callback dispatch via an attached handler and synchronous pull via
`zlink_recv()`. Sockets start in recv mode; attaching a handler transitions
to callback mode. This reference covers every public function, type, and
constant exported by `<zlink.h>`.

## API Groups

| Group | File | Description |
|-------|------|-------------|
| Error Handling & Version | [errors.md](errors.md) | Error codes, error strings, and version query |
| Context | [context.md](context.md) | Context creation, termination, and option tuning |
| Message | [message.md](message.md) | Message lifecycle, data access, and properties |
| Socket | [socket.md](socket.md) | Socket creation, handler, options, bind/connect, send/recv, pub/sub data-plane, and STREAM API |
| Monitoring | [monitoring.md](monitoring.md) | Socket monitors, service monitors, and peer inspection |
| Events | [events.md](events.md) | Canonical event catalog and readiness semantics |
| Registry | [registry.md](registry.md) | Service registry creation, configuration, topology, and clustering |
| Discovery | [discovery.md](discovery.md) | Service discovery, subscription, and peer lookup |
| SPOT | [spot.md](spot.md) | Topic-based PUB/SUB nodes and unified spot facades |
| Proxy & Utilities | [polling.md](polling.md) | Proxy helpers and capability query |
| Utilities | [utilities.md](utilities.md) | Timers, threads, stopwatch, and atomics |

## Types

| Type | Defined in | Description |
|------|-----------|-------------|
| [`zlink_msg_t`](message.md) | message.md | Opaque message container (64-byte, stack-allocatable) |
| [`zlink_routing_id_t`](message.md) | message.md | Peer routing identity (1-byte size + 255-byte data) |
| `zlink_socket_msg_handler_fn` | socket.md | Socket message receive callback (see [Callback Types](#callback-types)) |
| [`zlink_monitor_event_t`](monitoring.md) | monitoring.md | Monitor event structure (event, value, addresses) |
| [`zlink_monitor_snapshot_t`](monitoring.md) | monitoring.md | Monitor snapshot (state and queue depth) |
| [`zlink_service_event_t`](events.md) | events.md | Service monitor event structure and subject-aware payload |
| [`zlink_fd_t`](polling.md) | polling.md | Platform-dependent file descriptor type |

## Callback Types

| Type | Defined in | Description |
|------|-----------|-------------|
| [`zlink_socket_msg_handler_fn`](socket.md) | socket.md | Socket multipart message dispatch callback |
| [`zlink_subscribe_handler_fn`](socket.md) | socket.md | Topic-based message dispatch callback |
| [`zlink_monitor_handler_fn`](monitoring.md) | monitoring.md | Socket monitor event callback |
| [`zlink_service_monitor_handler_fn`](monitoring.md) | monitoring.md | Service monitor event callback |
| [`zlink_send_ready_handler_fn`](socket.md) | socket.md | Send-ready transition callback |
| [`zlink_free_fn`](message.md) | message.md | Deallocation callback for zero-copy messages |
| [`zlink_timer_fn`](utilities.md) | utilities.md | Timer expiry callback |
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
[POSD Module Structure](../internals/posd-module-structure.md) document.

---

For conceptual guides and tutorials, see the [User Guide](../guide/01-overview.md).
