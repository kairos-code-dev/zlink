[English](events.md) | [한국어](events.ko.md)

# Event Catalog

This document is the canonical catalog for raw socket monitor events and the
remaining service monitor events.

Use:
- [monitoring.md](monitoring.md) for monitor APIs and peer-inspection APIs
- this document for event semantics, payload fields, and recommended gates

## Service Event Model

```c
typedef struct zlink_service_event_t
{
    zlink_service_kind_t service_kind;
    uint32_t event_type;
    int32_t status;
    int32_t error_code;
    uint32_t value;
    zlink_service_event_detail_mask_t detail_flags;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    char subject[256];
    uint32_t subject_kind;
} zlink_service_event_t;
```

Field notes:
- `value` is event-specific and must not be interpreted as an aggregate
  readiness count.
- `subject` is populated when `detail_flags` contains
  `ZLINK_EVENT_DETAIL_SUBJECT`.
- `subject_kind` is valid only when `detail_flags` contains
  `ZLINK_EVENT_DETAIL_SUBJECT_KIND`.
- string/id fields are always initialized, but their contract meaning is valid
  only when the matching `detail_flags` bit is set.

Subject kind constants:
- `ZLINK_SERVICE_EVENT_SUBJECT_NONE`
- `ZLINK_SERVICE_EVENT_SUBJECT_TOPIC`
- `ZLINK_SERVICE_EVENT_SUBJECT_PATTERN`

Detail flags:
- `ZLINK_EVENT_DETAIL_SERVICE_NAME`
- `ZLINK_EVENT_DETAIL_ENDPOINT`
- `ZLINK_EVENT_DETAIL_SUBJECT_RID`
- `ZLINK_EVENT_DETAIL_PEER_RID`
- `ZLINK_EVENT_DETAIL_SUBJECT`
- `ZLINK_EVENT_DETAIL_SUBJECT_KIND`

## Semantic Levels

- `CONNECTION_READY`: low-cost ready edge for raw sockets only
  - raw socket: send/recv ready edge
- queue events: local backpressure observation only

Recommended perf gates:
- raw socket perf: count `ZLINK_EVENT_CONNECTION_READY` until the
  expected client count
- SPOT perf: do not use service monitor events; use an explicit
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

Disconnect reasons:
- `ZLINK_DISCONNECT_UNKNOWN`
- `ZLINK_DISCONNECT_HANDSHAKE_FAILED`
- `ZLINK_DISCONNECT_TRANSPORT_ERROR`
- `ZLINK_DISCONNECT_CTX_TERM`

## Service Monitor Events

### Common

| Constant | Meaning |
|---|---|
| `ZLINK_MONITOR_EVENT_ERROR` | Error occurred |
| `ZLINK_MONITOR_EVENT_CLOSED` | Monitor terminal event |

### Discovery

| Constant | Meaning |
|---|---|
| `ZLINK_DISCOVERY_SERVICE_UP` | A service provider became available |
| `ZLINK_DISCOVERY_SERVICE_DOWN` | A service provider disappeared |
| `ZLINK_DISCOVERY_PROVIDERS_CHANGED` | Provider set changed |

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
