[English](events.md) | [한국어](events.ko.md)

[Spec Index](../README.md) · [Core Index](README.md)

# Event Catalog

This document is the canonical catalog for raw socket monitor events.

Use:
- [monitoring.md](monitoring.md) for monitor APIs and peer-inspection APIs
- this document for event semantics, payload fields, and recommended gates

## Semantic Levels

- `CONNECTION_READY`: low-cost ready edge for raw sockets only
  - raw socket: send/recv ready edge
- queue events: local backpressure observation only

Recommended perf gates:
- raw socket perf: count `ZLINK_EVENT_CONNECTION_READY` until the
  expected client count
- SPOT perf: do not use a separate readiness stream; use an explicit
  `READY/START` barrier protocol
- do not use delivery-ready or aggregate-ready monitor events as perf gates

## Raw Socket Monitor Events

| Constant | Meaning |
|---|---|
| `ZLINK_EVENT_CONNECTED` | Outbound connection established |
| `ZLINK_EVENT_CONNECT_DELAYED` | Sync connect failed, retry scheduled |
| `ZLINK_EVENT_CONNECT_RETRIED` | Async retry in progress |
| `ZLINK_EVENT_LISTENING` | Bind/listen active |
| `ZLINK_EVENT_BIND_FAILED` | Bind failed |
| `ZLINK_EVENT_ACCEPTED` | Incoming connection accepted |
| `ZLINK_EVENT_ACCEPT_FAILED` | Accept failed |
| `ZLINK_EVENT_CLOSED` | Connection closed normally |
| `ZLINK_EVENT_CLOSE_FAILED` | Close failed |
| `ZLINK_EVENT_DISCONNECTED` | Session disconnected |
| `ZLINK_EVENT_MONITOR_STOPPED` | Socket monitor stopped |
| `ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL` | Handshake failed without detail |
| `ZLINK_EVENT_CONNECTION_READY` | Ready edge after transport handshake / first usable send path |
| `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL` | Protocol handshake error |
| `ZLINK_EVENT_HANDSHAKE_FAILED_AUTH` | Auth handshake error |
| `ZLINK_EVENT_PEER_WEIGHT_CHANGED` | A connected raw peer's weight changed. `routing_id` identifies the peer and `value` carries the new `0..100` weight. Alias for `ZLINK_SOCKET_MONITOR_EVENT_PEER_WEIGHT_CHANGED`. |

Disconnect reasons:
- `ZLINK_DISCONNECT_UNKNOWN`
- `ZLINK_DISCONNECT_HANDSHAKE_FAILED`
- `ZLINK_DISCONNECT_TRANSPORT_ERROR`
- `ZLINK_DISCONNECT_CTX_TERM`

## Examples

Raw perf gate:

```c
if (event->event == ZLINK_EVENT_CONNECTION_READY) {
    ++ready_clients;
}
```

SPOT perf gate:

```c
/* client sends READY over the control topic */
/* server waits until READY == expected_clients */
/* server broadcasts START */
```
