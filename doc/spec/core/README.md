[English](README.md) | [한국어](README.ko.md)

[Spec Index](../README.md)

# zlink Core Specification

This specification defines the public core C ABI of the zlink library.
A conforming implementation MUST provide every function, type, and constant
described in this section with the specified semantics. The public ABI surface
is defined by `core/include/zlink.h` and the domain headers under
`core/include/zlink/`.

`core/include/zlink.h` remains the aggregate compatibility header. New code may
include domain headers directly when it wants to inspect or depend on one API
area. The domain headers are still public ABI headers, not internal helper
headers.

## Public ABI Header Layout

| Header | Public ABI Area |
|--------|-----------------|
| `core/include/zlink.h` | Aggregate public header; includes every domain header |
| `core/include/zlink/common.h` | Version macros, shared includes, export macro, enum/error includes |
| `core/include/zlink/core.h` | Errno/string/version helpers, context lifecycle, proxy, capability, atomics, stopwatch, sleep, and thread utilities |
| `core/include/zlink/message.h` | Message storage, routing id, zero-copy free callback, message lifecycle, and multipart close |
| `core/include/zlink/actor.h` | Actor value types and actor result structures |
| `core/include/zlink/socket.h` | Socket creation, options, TLS, bind/connect, send/recv part substrate, request/reply, pub/sub, stream, and socket callback types |
| `core/include/zlink/monitoring.h` | Socket monitors, monitor snapshots, poll/poller, and timers |
| `core/include/zlink/spot.h` | SPOT handle, SPOT node, actor operations, dispatch, and SPOT node attachment APIs |
| `core/include/zlink/service_common.h` | Shared service-layer query types |
| `core/include/zlink/registry.h` | Registry creation, configuration, topology, query client, and registry snapshots |
| `core/include/zlink/discovery.h` | Discovery creation, registry connection, SPOT/Actor resolve, and discovery peer snapshots |
| `core/include/zlink/service.h` | Compatibility aggregate header for the service layer |
| `core/include/zlink_enum.h` | Public enum domains |
| `core/include/zlink_errno.h` | Public errno domain |

`core/src/` is the runtime implementation. Headers under `core/src/` are
internal implementation contracts and are not public ABI, even when they are
included by several core translation units.

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

The public C ABI is defined in `core/include/` and serves as the external core
contract used by bindings. The internal implementation follows POSD
(Philosophy of Software Design) principles and is organized into the following
layers:

```text
Public Contract  ->  API Facade  ->  Runtime Implementation
 (include/)             (api/)            (runtime/)
```

| Layer | Source Location | Role |
|-------|-----------------|------|
| Public Contract | `core/include/` | Public C ABI contract seen by bindings and users |
| API Facade | `core/src/api/` | Implementation of exported C ABI functions. Validate inputs, convert public results, call runtime |
| Runtime Implementation | `core/src/runtime/` | Internal socket, service, engine, transport, protocol, and utility implementation |

`core/src/api/` is the implementation facade for the public C ABI functions
declared under `core/include/`. It validates external inputs, converts results
to public result types, and delegates the actual behavior to
`core/src/runtime/`. `core/src/api/` is not a public header location and is not
installed.

The `core/src/api/` structure is fixed by category so it matches the public
contract header domains:

```text
core/src/api/
|-- actor/
|-- core/
|-- discovery/
|-- message/
|-- monitoring/
|-- registry/
|-- service/
|-- socket/
`-- spot/
```

The `core/src/runtime/` structure is fixed by category:

```text
core/src/runtime/
|-- core/
|-- engine/
|-- protocol/
|-- services/
|   |-- actor/
|   |-- common/
|   |-- control/
|   |-- discovery/
|   |-- registry/
|   `-- spot/
|-- sockets/
|   |-- common/
|   |-- dealer/
|   |-- internal/
|   |-- pair/
|   |-- proxy/
|   |-- pubsub/
|   |-- router/
|   `-- stream/
|-- transports/
`-- utils/
```

Option dispatch is split into three categories, each handled by its own
domain owner for validation/apply: core_socket, transport_network,
protocol_metadata.

For internal architecture details, see the
[POSD Module Structure](../../internals/posd-module-structure.md) document.

---

For conceptual guides and tutorials, see the [User Guide](../../guide/01-overview.md).
