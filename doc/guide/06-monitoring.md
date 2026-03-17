[English](06-monitoring.md) | [한국어](06-monitoring.ko.md)

# Monitoring API Usage

## 1. Overview

The zlink monitoring API allows real-time observation of socket events such as connection, disconnection, and handshake. It operates on a callback basis, automatically invoking the registered handler function when events occur.

The family-level control contract and regression matrix are documented in
[socket-family-monitor-contract-spec.ko.md](../plan/direct-callback-recv/socket-family-monitor-contract-spec.ko.md).
This guide focuses on which events may be used as gates in real code.

## 2. Enabling the Monitor

### 2.1 Callback-Based (Recommended)

```c
/* Define event handler */
void on_monitor_event(const zlink_monitor_event_t *ev, void *userdata)
{
    printf("Event: 0x%llx\n", (unsigned long long)ev->event);
    printf("Local: %s\n", ev->local_addr);
    printf("Remote: %s\n", ev->remote_addr);

    if (ev->routing_id.size > 0) {
        printf("routing_id: ");
        for (uint8_t i = 0; i < ev->routing_id.size; ++i)
            printf("%02x", ev->routing_id.data[i]);
        printf("\n");
    }
}

void *server = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(server, "tcp://*:5555");

/* Create monitor (register handler) */
void *mon = zlink_socket_monitor_open(server, ZLINK_EVENT_ALL,
                                       on_monitor_event, NULL);
```

Events are dispatched automatically through the `on_monitor_event` callback.

### Event Structure

```c
typedef struct {
    uint64_t event;               /* Event type */
    uint64_t value;               /* Auxiliary value (fd, errno, reason, etc.) */
    zlink_routing_id_t routing_id; /* Peer routing_id */
    char local_addr[256];         /* Local address */
    char remote_addr[256];        /* Remote address */
} zlink_monitor_event_t;
```

## 4. Event Types

### Summary

| Event | Value | `value` Field | `routing_id` | Side |
|-------|-------|---------------|:------------:|:----:|
| `CONNECTED` | `0x0001` | fd | None | Client |
| `CONNECT_DELAYED` | `0x0002` | errno | None | Client |
| `CONNECT_RETRIED` | `0x0004` | -- | None | Client |
| `LISTENING` | `0x0008` | fd | None | Server |
| `BIND_FAILED` | `0x0010` | errno | None | Server |
| `ACCEPTED` | `0x0020` | fd | None | Server |
| `ACCEPT_FAILED` | `0x0040` | errno | None | Server |
| `CLOSED` | `0x0080` | -- | None | Both |
| `CLOSE_FAILED` | `0x0100` | errno | None | Both |
| `DISCONNECTED` | `0x0200` | reason code | Possible | Both |
| `MONITOR_STOPPED` | `0x0400` | -- | None | Both |
| `HANDSHAKE_FAILED_NO_DETAIL` | `0x0800` | errno | None | Both |
| `CONNECTION_READY` | `0x1000` | -- | Possible | Both |
| `HANDSHAKE_FAILED_PROTOCOL` | `0x2000` | protocol error code | None | Both |
| `HANDSHAKE_FAILED_AUTH` | `0x4000` | -- | None | Both |

> Reference: `core/tests/testutil_monitoring.cpp` -- `get_zlinkEventName()` event name mapping

### 4.1 Connection Lifecycle Events

#### CONNECTED (`0x0001`)

Fired on the **client side** when the TCP connection to a remote peer is established. At this point only the transport-layer connection is complete — the zlink handshake has not yet occurred.

- **`value`**: The file descriptor of the new connection.
- **`routing_id`**: Not available (empty).
- **`local_addr`**: The local TCP endpoint (e.g. `tcp://192.168.1.10:54321`).
- **`remote_addr`**: The remote TCP endpoint (e.g. `tcp://192.168.1.20:5555`).
- **Next event**: `CONNECTION_READY` on success, or `HANDSHAKE_FAILED_*` / `DISCONNECTED` on failure.

#### ACCEPTED (`0x0020`)

Fired on the **server side** when an incoming TCP connection is accepted by a listening socket. Similar to `CONNECTED`, the zlink handshake has not yet occurred.

- **`value`**: The file descriptor of the accepted connection.
- **`routing_id`**: Not available (empty). The identity is assigned after the handshake.
- **`local_addr`**: The listening endpoint address.
- **`remote_addr`**: The remote peer's address.
- **Next event**: `CONNECTION_READY` on success, or `HANDSHAKE_FAILED_*` / `DISCONNECTED` on failure.
- **Control rule**: safe for transport acceptance bookkeeping, not safe as a
  business-message or first-delivery gate.

#### CONNECTION_READY (`0x1000`)

Fired when the zlink handshake completes successfully and the connection is ready for data transfer. This is the most important event for application-level connection tracking.

- **`value`**: Not used.
- **`routing_id`**: Available for ROUTER sockets — contains the peer's assigned routing identity.
- **`local_addr`**: The local endpoint address.
- **`remote_addr`**: The remote endpoint address.
- **Typical usage**: Trigger peer registration, start sending messages, or
  read aggregate queue/readiness state via `zlink_monitor_snapshot()`.
- **Family rule**:
  - `PAIR`, `DEALER/ROUTER`, `STREAM`: valid raw first-I/O gate
  - `PUB/SUB`: transport/session readiness only, not a first-publish
    delivery gate

#### DISCONNECTED (`0x0200`)

Fired when an established session terminates. Can occur at any stage of the connection lifecycle.

- **`value`**: A `ZLINK_DISCONNECT_*` reason code (see [Section 6](#6-disconnected-reason-codes)).
- **`routing_id`**: Available if the handshake had completed (i.e. `CONNECTION_READY` was previously fired for this peer).
- **`local_addr`**: The local endpoint address.
- **`remote_addr`**: The remote endpoint address.
- **Typical usage**: Trigger reconnection logic, update peer state, or log the disconnection reason.

#### CLOSED (`0x0080`)

Fired when a connection is closed normally via `zlink_close()` or `zlink_disconnect()`.

- **`value`**: Not used.
- **`routing_id`**: Not available (empty).
- **Note**: Unlike `DISCONNECTED`, this event signals an intentional local close operation rather than an unexpected session termination.

#### CLOSE_FAILED (`0x0100`)

Fired when a connection close operation fails.

- **`value`**: The `errno` value describing the failure.
- **`routing_id`**: Not available (empty).
- **Note**: Rare in practice. May indicate an internal error during resource cleanup.

### 4.2 Connect-Side Events

#### CONNECT_DELAYED (`0x0002`)

Fired on the **client side** when a synchronous connect attempt cannot complete immediately and an asynchronous retry has been scheduled.

- **`value`**: The `errno` from the initial connect attempt (typically `EINPROGRESS`).
- **`routing_id`**: Not available (empty).
- **`remote_addr`**: The target endpoint address.
- **Next event**: `CONNECTED` when the connection eventually succeeds, or `CONNECT_RETRIED` for subsequent attempts.

#### CONNECT_RETRIED (`0x0004`)

Fired on the **client side** when an asynchronous reconnection attempt is in progress. Occurs after a prior `CONNECT_DELAYED` or `DISCONNECTED` event.

- **`value`**: Not used.
- **`routing_id`**: Not available (empty).
- **`remote_addr`**: The target endpoint address.
- **Typical sequence**: `DISCONNECTED` → `CONNECT_DELAYED` → `CONNECT_RETRIED` → `CONNECTED` → `CONNECTION_READY`.

### 4.3 Bind-Side Events

#### LISTENING (`0x0008`)

Fired on the **server side** when `zlink_bind()` succeeds and the socket is actively listening for incoming connections.

- **`value`**: The file descriptor of the listening socket.
- **`routing_id`**: Not available (empty).
- **`local_addr`**: The bound endpoint address (e.g. `tcp://0.0.0.0:5555`).

#### BIND_FAILED (`0x0010`)

Fired on the **server side** when `zlink_bind()` fails.

- **`value`**: The `errno` value describing the failure (e.g. `EADDRINUSE`).
- **`routing_id`**: Not available (empty).
- **`local_addr`**: The address that failed to bind.
- **Typical causes**: Port already in use, permission denied, invalid address.

#### ACCEPT_FAILED (`0x0040`)

Fired on the **server side** when accepting an incoming connection fails.

- **`value`**: The `errno` value describing the failure.
- **`routing_id`**: Not available (empty).
- **Typical causes**: File descriptor limit reached (`EMFILE`), resource exhaustion.

### 4.4 Handshake Failure Events

These events fire when the zlink protocol handshake fails after a TCP connection has been established.

#### HANDSHAKE_FAILED_NO_DETAIL (`0x0800`)

A generic handshake failure with no protocol-specific information.

- **`value`**: The `errno` value at the time of failure.
- **`routing_id`**: Not available (empty).
- **Typical causes**: Connection reset during handshake, unexpected socket closure, timeout.

#### HANDSHAKE_FAILED_PROTOCOL (`0x2000`)

The handshake failed due to a ZMP or WebSocket protocol error. The `value` field carries a specific protocol error code.

- **`value`**: A `ZLINK_PROTOCOL_ERROR_*` code (see [Protocol Error Codes](#protocol-error-codes) below).
- **`routing_id`**: Not available (empty).
- **Typical causes**: Version mismatch, malformed commands, invalid metadata, cryptographic errors.

#### HANDSHAKE_FAILED_AUTH (`0x4000`)

The handshake failed due to authentication or security mechanism failure.

- **`value`**: Not used.
- **`routing_id`**: Not available (empty).
- **Typical causes**: TLS certificate validation failure, security mechanism mismatch, invalid credentials.

### 4.5 Monitor Control Events

#### MONITOR_STOPPED (`0x0400`)

Fired when the monitor is stopped by calling `zlink_close(mon)`. After this event, the monitor will produce no more events.

- **`value`**: Not used.
- **`routing_id`**: Not available (empty).
- **Note**: This is the last event the monitor will ever emit. After receiving it, close the monitor handle with `zlink_close()`.

### Protocol Error Codes

When `HANDSHAKE_FAILED_PROTOCOL` fires, the `value` field contains one of these codes:

| Constant | Value | Description |
|---|---|---|
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | `0x10000013` | Malformed ZMP HELLO command. |

## 5. Event Flow Diagrams

### Successful Connection

```
Client side:
  CONNECT_DELAYED (optional) → CONNECTED → CONNECTION_READY

Server side:
  ACCEPTED → CONNECTION_READY
```

### Handshake Failure

```
Client side:
  CONNECTED → HANDSHAKE_FAILED_* → DISCONNECTED

Server side:
  ACCEPTED → HANDSHAKE_FAILED_* → DISCONNECTED
```

### Normal Disconnection

```
CONNECTION_READY → DISCONNECTED (reason=LOCAL or REMOTE)
```

### Reconnection

```
CONNECTED → CONNECTION_READY → DISCONNECTED →
CONNECT_DELAYED → CONNECT_RETRIED → CONNECTED → CONNECTION_READY
```

## 6. DISCONNECTED Reason Codes

The `value` field of the `DISCONNECTED` event contains the reason for disconnection.

| Code | Name | Meaning | Recommended Action |
|------|------|---------|-------------------|
| 0 | UNKNOWN | Unknown cause | Log and observe |
| 1 | LOCAL | Intentional local shutdown | Normal operation, no action needed |
| 2 | REMOTE | Remote peer gracefully closed | Execute reconnection logic |
| 3 | HANDSHAKE_FAILED | Handshake failure | Check TLS/protocol configuration |
| 4 | TRANSPORT_ERROR | Transport layer error | Check network status |
| 5 | CTX_TERM | Context terminated | Handle shutdown |

### Reason Code Handling Example

```c
void on_monitor(const zlink_monitor_event_t *ev, void *userdata)
{
    if (ev->event == ZLINK_EVENT_DISCONNECTED) {
        switch (ev->value) {
            case 0: printf("Unknown disconnection\n"); break;
            case 1: printf("Local shutdown\n"); break;
            case 2:
                printf("Remote peer closed -- attempting reconnection\n");
                /* Reconnection logic */
                break;
            case 3:
                printf("Handshake failed -- check TLS configuration\n");
                break;
            case 4:
                printf("Transport error -- check network\n");
                break;
            case 5:
                printf("Context terminated\n");
                break;
        }
    }
}
```

## 7. Event Filtering and Subscription Presets

### Subscribing to Specific Events Only

```c
/* Connection/disconnection events only */
void *mon = zlink_socket_monitor_open(server,
    ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED,
    on_monitor_event, NULL);
```

### Recommended Subscription Presets

| Preset | Event Mask | Purpose |
|--------|-----------|---------|
| Basic | `CONNECTION_READY \| DISCONNECTED` | Connection state tracking |
| Debug | Basic + `CONNECTED \| ACCEPTED \| CONNECT_DELAYED \| CONNECT_RETRIED` | Detailed connection process |
| Security | Basic + `HANDSHAKE_FAILED_*` | Authentication failure detection |
| Full | `ZLINK_EVENT_ALL` | All events |

### Preset Implementation Example

```c
/* Basic preset */
#define MONITOR_PRESET_BASIC \
    (ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED)

/* Debug preset */
#define MONITOR_PRESET_DEBUG \
    (MONITOR_PRESET_BASIC | ZLINK_EVENT_CONNECTED | \
     ZLINK_EVENT_ACCEPTED | ZLINK_EVENT_CONNECT_DELAYED | \
     ZLINK_EVENT_CONNECT_RETRIED)

/* Security preset */
#define MONITOR_PRESET_SECURITY \
    (MONITOR_PRESET_BASIC | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL | \
     ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL | \
     ZLINK_EVENT_HANDSHAKE_FAILED_AUTH)

void *mon = zlink_socket_monitor_open(server, MONITOR_PRESET_SECURITY,
                                      on_monitor_event, NULL);
```

## 8. Monitor Snapshots

### Aggregate Socket State

```c
void *monitor = zlink_socket_monitor_open(socket, ZLINK_EVENT_ALL,
                                          zlink_monitor_ignore_handler, NULL);
zlink_monitor_snapshot_t snapshot;
zlink_monitor_snapshot(monitor, &snapshot);
printf("Ready peers: %u, sndq=%llu, rcvq=%llu\n",
       snapshot.ready_peer_count,
       (unsigned long long) snapshot.snd_pending_msgs,
       (unsigned long long) snapshot.rcv_pending_msgs);
```

### Combining Snapshots with Monitoring

```c
void on_monitor(const zlink_monitor_event_t *ev, void *userdata)
{
    if (ev->event == ZLINK_EVENT_CONNECTION_READY) {
        zlink_monitor_snapshot_t snapshot;
        zlink_monitor_snapshot(g_monitor, &snapshot);
        printf("Ready peers now: %u\n", snapshot.ready_peer_count);
    }
}
```

### Initial Gates for Service Monitors

Service overlays sit one level above raw sockets, so the right pattern for
`Gateway` and `SPOT` is `open -> snapshot -> incremental events`.

- `Gateway`
  - `SERVICE_READY` means local publication/bind readiness.
  - The actual first-request gate is the monitor-handle snapshot showing
    `SEND_READY` with `ready_peer_count > 0`.
  - Subsequent transitions are driven by `SEND_READY_CHANGED` and
    `ROUTE_UP/DOWN`.
- `SPOT`
  - `FILTER_APPLIED` and `SUBSCRIPTION_READY` are control-plane progress.
  - Subscriber-side first receive gate is `SUB_DELIVERY_READY_CHANGED`.
  - Publisher-side first publish gate is
    `PUB_FIRST_DELIVERY_READY_CHANGED`.
  - Snapshots complement that by exposing aggregate peer and queue state.

So the rule is not "some public events are control events and some are not".
The real rule is "every public event is usable for control at its advertised
level, but must not be reinterpreted as a stronger gate".

## 9. Multi-Socket Monitoring

Handle events from multiple sockets with individual callback handlers.

```c
void on_event_a(const zlink_monitor_event_t *ev, void *userdata)
{
    printf("Socket A event: 0x%llx\n", (unsigned long long)ev->event);
}

void on_event_b(const zlink_monitor_event_t *ev, void *userdata)
{
    printf("Socket B event: 0x%llx\n", (unsigned long long)ev->event);
}

void *mon_a = zlink_socket_monitor_open(sock_a, ZLINK_EVENT_ALL, on_event_a, NULL);
void *mon_b = zlink_socket_monitor_open(sock_b, ZLINK_EVENT_ALL, on_event_b, NULL);

/* ... application logic ... */

/* Cleanup */
zlink_close(mon_a);
zlink_close(mon_b);
```

## 10. Important Notes

### Monitor Thread Safety

`zlink_socket_monitor_open()` and monitor-handle close belong to the
low-frequency control-path contract of raw and service handles. That means
they may be called from application threads and remain correct when mixed with
other concurrent operations on the same handle. The monitor callback itself still runs on the
I/O path, so slow callback work should be offloaded to a user queue.

```c
/* Open a monitor from an application thread */
void *socket = zlink_socket(ctx, ZLINK_ROUTER);
void *mon = zlink_socket_monitor_open(socket, ZLINK_EVENT_ALL,
                                       on_monitor_event, NULL);

/* Snapshot reads may happen later from another worker thread */
zlink_monitor_snapshot_t snapshot;
zlink_monitor_snapshot(mon, &snapshot);
```

### Concurrent Monitor Limitation

Multiple monitors cannot be set on the same socket simultaneously.

### Callback Processing Speed

Blocking work in the callback handler can delay other I/O. For slow
processing, enqueue from the callback and handle it on your own thread.

### Monitor Shutdown Procedure

```c
/* Close the monitor handle */
zlink_close(mon);
```

## 11. Family Gate Rules

The core rule is simple: a public event may be used for control, but only
within the level it actually guarantees. This is not an arbitrary exception
list; transport, session, and delivery are different levels, so their gates
differ as well.

| Family | Safe raw/socket-monitor gate | Do not use as gate | Use instead |
|---|---|---|---|
| `PAIR` | first bidirectional send/recv after `CONNECTION_READY` on both sides | starting I/O on bind-side `ACCEPTED` alone | snapshot `READY` and `ready_peer_count` as needed |
| `DEALER/ROUTER` | dealer `CONNECTION_READY`, router `CONNECTION_READY.routing_id` for first request/reply | routed send on router `ACCEPTED` alone | snapshot plus ready-event `routing_id` |
| `PUB/SUB` | observing bind/connect/handshake state | using raw `CONNECTION_READY` as first publish delivery gate | application barrier or higher service event |
| `STREAM` | first payload send/recv after server `CONNECTION_READY.routing_id` | starting payload I/O on `ACCEPTED` alone | snapshot plus stream `routing_id` |
| `Gateway` | first request after `ZLINK_GATEWAY_SEND_READY_CHANGED(value=1)` | inferring sendability from `SERVICE_READY` or `ROUTE_UP` alone | service monitor plus `zlink_monitor_snapshot()` |
| `SPOT` | sub: `SUB_DELIVERY_READY_CHANGED`, pub: `PUB_FIRST_DELIVERY_READY_CHANGED` | using raw `CONNECTION_READY`, `PEER_UP`, or `FILTER_APPLIED` alone as delivery gates | `SPOT` service monitor |

Operational rules:

- `ACCEPTED` is a transport-progress event.
- raw `CONNECTION_READY` is a raw session-ready event.
- patterns that need first-delivery readiness must use a stronger service-level event.
- if an event still requires sleeps or retries after the gate, that event is too weak for the intended control decision.

---
[← TLS Security](05-tls-security.md) | [Services Overview →](07-0-services.md)
