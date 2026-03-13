[English](events.md) | [한국어](events.ko.md)

# Event Catalog

This document is the canonical catalog for raw socket monitor events and
service monitor events.

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
- `value` is event-specific. For `*_DELIVERY_READY_CHANGED`, it is the current
  readiness state/count.
- `subject` is populated when `detail_flags` contains
  `ZLINK_EVENT_DETAIL_SUBJECT`.
- `subject_kind` is valid only when `detail_flags` contains
  `ZLINK_EVENT_DETAIL_SUBJECT_KIND`.
- `routing_id` is valid only when `SUBJECT_RID` or `PEER_RID` is present.

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

- `PEER_UP` / `PEER_DOWN`: connection-level visibility only
- `SUB_FILTER_APPLIED`: local subscriber filter installed
- `SUBSCRIPTION_READY`: subscriber-side subscription path became ready
- `*_DELIVERY_READY_CHANGED`: first-delivery contract for a specific subject

Recommended gates:
- start publish only after `ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED` with
  `value >= 1`
- start subscriber measurement only after
  `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED` with `value == 1`
- do not use `PEER_UP` as a first-delivery gate

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
| `ZLINK_EVENT_CONNECTION_READY` | Transport handshake complete |
| `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL` | Protocol handshake error |
| `ZLINK_EVENT_HANDSHAKE_FAILED_AUTH` | Auth handshake error |

Disconnect reasons:
- `ZLINK_DISCONNECT_UNKNOWN`
- `ZLINK_DISCONNECT_LOCAL`
- `ZLINK_DISCONNECT_REMOTE`
- `ZLINK_DISCONNECT_HANDSHAKE_FAILED`
- `ZLINK_DISCONNECT_TRANSPORT_ERROR`
- `ZLINK_DISCONNECT_CTX_TERM`

## Service Monitor Events

### Common

| Constant | Meaning |
|---|---|
| `ZLINK_MONITOR_EVENT_READY` | Generic readiness alias |
| `ZLINK_MONITOR_EVENT_LOST` | Generic lost alias |
| `ZLINK_MONITOR_EVENT_PEER_UP` | Peer connected |
| `ZLINK_MONITOR_EVENT_PEER_DOWN` | Peer disconnected |
| `ZLINK_MONITOR_EVENT_ERROR` | Error occurred |
| `ZLINK_MONITOR_EVENT_CLOSED` | Monitor terminal event |

### Discovery

| Constant | Meaning |
|---|---|
| `ZLINK_DISCOVERY_SERVICE_UP` | A service provider became available |
| `ZLINK_DISCOVERY_SERVICE_DOWN` | A service provider disappeared |
| `ZLINK_DISCOVERY_PROVIDERS_CHANGED` | Provider set changed |

### Gateway

| Constant | Meaning |
|---|---|
| `ZLINK_GATEWAY_SERVICE_READY` | At least one route is ready |
| `ZLINK_GATEWAY_SERVICE_LOST` | All routes lost |
| `ZLINK_GATEWAY_CONNECTION_COUNT_CHANGED` | Connection count changed |
| `ZLINK_GATEWAY_ROUTE_UP` | A route became active |
| `ZLINK_GATEWAY_ROUTE_DOWN` | A route became inactive |

### SPOT

| Constant | Producer | Meaning |
|---|---|---|
| `ZLINK_SPOT_SUB_FILTER_APPLIED` | Spot sub / node-sub monitor | Local filter installed |
| `ZLINK_SPOT_SUB_SUBSCRIPTION_READY` | Spot sub / node-sub monitor | Legacy subscription-ready transition |
| `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED` | Spot sub / node-sub monitor | Subject-specific delivery-ready state changed; `value` is `0` or `1` |
| `ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED` | Spot pub / node-pub monitor | Subject-specific remote delivery-ready count changed; `value` is the current ready subscriber count |
| `ZLINK_SPOT_PUB_QUEUE_FULL` | Spot pub / node-pub monitor | Async queue saturated |
| `ZLINK_SPOT_PUB_QUEUE_DRAINED` | Spot pub / node-pub monitor | Async queue drained |

SPOT subject rules:
- sub-side `subject_kind` is populated for exact topic and pattern subscriptions
- pub-side `subject` is populated, but `subject_kind` can be absent because
  remote subscription frames do not always preserve exact-vs-pattern origin
- pattern subscriptions are exposed to sub-side monitors using the original
  public pattern string, including the trailing `*`

## Examples

Subscriber gate:

```c
if (event->event_type == ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
    && (event->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) != 0
    && strcmp(event->subject, "bench") == 0
    && event->value == 1) {
    /* first publish can be received now */
}
```

Publisher gate:

```c
if (event->event_type == ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED
    && (event->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) != 0
    && strcmp(event->subject, "bench") == 0
    && event->value >= 1) {
    /* first publish can be sent now */
}
```
