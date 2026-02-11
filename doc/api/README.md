[English](README.md) | [한국어](README.ko.md)

# zlink API Reference

The zlink C library provides a messaging and service-discovery toolkit built
on top of lightweight I/O threads and lock-free queues. This reference covers
every public function, type, and constant exported by `<zlink.h>`.

## API Groups

| Group | File | Description | Functions |
|-------|------|-------------|-----------|
| Error Handling & Version | [errors.md](errors.md) | Error codes, error strings, and version query | 3 |
| Context | [context.md](context.md) | Context creation, termination, and option tuning | 5 |
| Message | [message.md](message.md) | Message lifecycle, data access, and properties | 16 |
| Socket | [socket.md](socket.md) | Socket creation, options, bind/connect, and send/recv | 13 |
| Monitoring | [monitoring.md](monitoring.md) | Socket monitors, events, and peer inspection | 7 |
| Registry | [registry.md](registry.md) | Service registry creation, configuration, and clustering | 9 |
| Discovery | [discovery.md](discovery.md) | Service discovery, subscription, and receiver lookup | 9 |
| Gateway | [gateway.md](gateway.md) | Load-balanced request/reply gateway | 10 |
| Receiver | [receiver.md](receiver.md) | Server-side request receiver and service registration | 11 |
| SPOT | [spot.md](spot.md) | Topic-based PUB/SUB nodes, publishers, and subscribers | 27 |
| Polling | [polling.md](polling.md) | I/O multiplexing and proxy helpers | 4 |
| Utilities | [utilities.md](utilities.md) | Timers, threads, stopwatch, atomics, and capability query | ~20 |

## Types

| Type | Defined in | Description |
|------|-----------|-------------|
| [`zlink_msg_t`](message.md) | message.md | Opaque message container (64-byte, stack-allocatable) |
| [`zlink_routing_id_t`](message.md) | message.md | Peer routing identity (1-byte size + 255-byte data) |
| [`zlink_monitor_event_t`](monitoring.md) | monitoring.md | Monitor event structure (event, value, addresses) |
| [`zlink_peer_info_t`](monitoring.md) | monitoring.md | Connected-peer statistics (routing id, address, counters) |
| [`zlink_receiver_info_t`](discovery.md) | discovery.md | Discovered service-receiver entry (name, endpoint, weight) |
| [`zlink_pollitem_t`](polling.md) | polling.md | Poll item for I/O multiplexing (socket or fd) |

## Callback Types

| Type | Defined in | Description |
|------|-----------|-------------|
| [`zlink_free_fn`](message.md) | message.md | Deallocation callback for zero-copy messages |
| [`zlink_timer_fn`](utilities.md) | utilities.md | Timer expiry callback |
| [`zlink_thread_fn`](utilities.md) | utilities.md | Thread entry-point function |
| [`zlink_spot_sub_handler_fn`](spot.md) | spot.md | SPOT subscriber message-dispatch callback |

---

For conceptual guides and tutorials, see the [User Guide](../guide/01-overview.md).
