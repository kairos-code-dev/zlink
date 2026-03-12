[English](README.md) | [한국어](README.ko.md)

# zlink API Reference

The zlink C library provides a messaging and service-discovery toolkit built
on top of lightweight I/O threads. All message receiving is handled through
handler callbacks registered at creation time. This reference covers every
public function, type, and constant exported by `<zlink.h>`.

## API Groups

| Group | File | Description |
|-------|------|-------------|
| Error Handling & Version | [errors.md](errors.md) | Error codes, error strings, and version query |
| Context | [context.md](context.md) | Context creation, termination, and option tuning |
| Message | [message.md](message.md) | Message lifecycle, data access, and properties |
| Socket | [socket.md](socket.md) | Socket creation with handler, options, bind/connect, send, and STREAM API |
| Monitoring | [monitoring.md](monitoring.md) | Socket monitors, service monitors, and peer inspection |
| Registry | [registry.md](registry.md) | Service registry creation, configuration, topology, and clustering |
| Discovery | [discovery.md](discovery.md) | Service discovery, subscription, and receiver lookup |
| Gateway | [gateway.md](gateway.md) | Service-bound load-balanced request/reply |
| SPOT | [spot.md](spot.md) | Topic-based PUB/SUB nodes and unified spot facades |
| Proxy & Utilities | [polling.md](polling.md) | Proxy helpers and capability query |
| Utilities | [utilities.md](utilities.md) | Timers, threads, stopwatch, and atomics |

## Types

| Type | Defined in | Description |
|------|-----------|-------------|
| [`zlink_msg_t`](message.md) | message.md | Opaque message container (64-byte, stack-allocatable) |
| [`zlink_routing_id_t`](message.md) | message.md | Peer routing identity (1-byte size + 255-byte data) |
| [`zlink_socket_handler_t`](socket.md) | socket.md | Socket receive handler descriptor |
| [`zlink_monitor_event_t`](monitoring.md) | monitoring.md | Monitor event structure (event, value, addresses) |
| [`zlink_peer_info_t`](monitoring.md) | monitoring.md | Connected-peer statistics (routing id, address, counters) |
| [`zlink_service_event_t`](monitoring.md) | monitoring.md | Service monitor event structure |
| [`zlink_receiver_info_t`](discovery.md) | discovery.md | Discovered service-receiver entry (name, endpoint, weight) |
| [`zlink_gateway_peer_info_t`](gateway.md) | gateway.md | Gateway peer info with weight |

## Callback Types

| Type | Defined in | Description |
|------|-----------|-------------|
| [`zlink_socket_msg_handler_fn`](socket.md) | socket.md | Socket multipart message dispatch callback |
| [`zlink_spot_handler_fn`](socket.md) | socket.md | Topic-based message dispatch callback |
| [`zlink_xpub_handler_fn`](socket.md) | socket.md | XPUB subscription notification callback |
| [`zlink_stream_on_raw_fn`](socket.md) | socket.md | STREAM raw chunk dispatch callback |
| [`zlink_monitor_handler_fn`](monitoring.md) | monitoring.md | Socket monitor event callback |
| [`zlink_service_monitor_handler_fn`](monitoring.md) | monitoring.md | Service monitor event callback |
| [`zlink_send_ready_handler_fn`](socket.md) | socket.md | Send-ready transition callback |
| [`zlink_free_fn`](message.md) | message.md | Deallocation callback for zero-copy messages |
| [`zlink_timer_fn`](utilities.md) | utilities.md | Timer expiry callback |
| [`zlink_thread_fn`](utilities.md) | utilities.md | Thread entry-point function |

---

For conceptual guides and tutorials, see the [User Guide](../guide/01-overview.md).
