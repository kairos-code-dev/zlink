[English](06-monitoring.md) | [한국어](06-monitoring.ko.md)

# Monitoring API Usage

## 1. Overview

The zlink monitoring API allows real-time observation of socket events such as connection, disconnection, and handshake. It operates on a callback basis, automatically invoking the registered handler function when events occur.

## 2. Enabling the Monitor

### 2.1 Callback-Based (Recommended)

```c
/* Define event handler */
void on_monitor_event(const zlink_monitor_event_t *ev)
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

void *server = zlink_socket(ctx, ZLINK_ROUTER, NULL);
zlink_bind(server, "tcp://*:5555");

/* Create monitor (register handler) */
void *mon = zlink_socket_monitor_open(server, ZLINK_EVENT_ALL,
                                       on_monitor_event);
```

Events are dispatched automatically through the `on_monitor_event` callback.

### 2.2 Manual Setup (Legacy)

```c
zlink_socket_monitor(server, "inproc://monitor", ZLINK_EVENT_ALL);

void *mon = zlink_socket(ctx, ZLINK_PAIR, NULL);
zlink_connect(mon, "inproc://monitor");
```

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

#### CONNECTION_READY (`0x1000`)

Fired when the zlink handshake completes successfully and the connection is ready for data transfer. This is the most important event for application-level connection tracking.

- **`value`**: Not used.
- **`routing_id`**: Available for ROUTER sockets — contains the peer's assigned routing identity.
- **`local_addr`**: The local endpoint address.
- **`remote_addr`**: The remote endpoint address.
- **Typical usage**: Trigger peer registration, start sending messages, or
  read aggregate queue/readiness state via `zlink_monitor_snapshot()`.

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

Fired when the monitor is stopped by calling `zlink_socket_monitor(socket, NULL, 0)`. After this event, the monitor will produce no more events.

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
void on_monitor(const zlink_monitor_event_t *ev)
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
    on_monitor_event);
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
                                      on_monitor_event);
```

## 8. Monitor Snapshots

### Aggregate Socket State

```c
void *monitor = zlink_socket_monitor_open(socket, ZLINK_EVENT_ALL, NULL);
zlink_monitor_snapshot_t snapshot;
zlink_monitor_snapshot(monitor, &snapshot);
printf("Ready peers: %u, sndq=%llu, rcvq=%llu\n",
       snapshot.ready_peer_count,
       (unsigned long long) snapshot.snd_pending_msgs,
       (unsigned long long) snapshot.rcv_pending_msgs);
```

### Combining Snapshots with Monitoring

```c
void on_monitor(const zlink_monitor_event_t *ev)
{
    if (ev->event == ZLINK_EVENT_CONNECTION_READY) {
        zlink_monitor_snapshot_t snapshot;
        zlink_monitor_snapshot(g_monitor, &snapshot);
        printf("Ready peers now: %u\n", snapshot.ready_peer_count);
    }
}
```

## 9. Multi-Socket Monitoring

Handle events from multiple sockets with individual callback handlers.

```c
void on_event_a(const zlink_monitor_event_t *ev)
{
    printf("Socket A event: 0x%llx\n", (unsigned long long)ev->event);
}

void on_event_b(const zlink_monitor_event_t *ev)
{
    printf("Socket B event: 0x%llx\n", (unsigned long long)ev->event);
}

void *mon_a = zlink_socket_monitor_open(sock_a, ZLINK_EVENT_ALL, on_event_a);
void *mon_b = zlink_socket_monitor_open(sock_b, ZLINK_EVENT_ALL, on_event_b);

/* ... application logic ... */

/* Cleanup */
zlink_socket_monitor(sock_a, NULL, 0);
zlink_socket_monitor(sock_b, NULL, 0);
zlink_close(mon_a);
zlink_close(mon_b);
```

## 10. Important Notes

### Monitor Thread Safety

Monitor setup must be called **only from the socket's owning thread**.

```c
/* Correct usage: set up monitor from the socket creation thread */
void *socket = zlink_socket(ctx, ZLINK_ROUTER, NULL);
void *mon = zlink_socket_monitor_open(socket, ZLINK_EVENT_ALL,
                                       on_monitor_event);

/* Incorrect usage: set up monitor from a different thread */
/* → Undefined behavior */
```

### Concurrent Monitor Limitation

Multiple monitors cannot be set on the same socket simultaneously.

### Callback Processing Speed

Blocking work in the callback handler can delay other I/O. For slow
processing, enqueue from the callback and handle it on your own thread.

### Monitor Shutdown Procedure

```c
/* 1. Stop monitoring */
zlink_socket_monitor(socket, NULL, 0);

/* 2. Close monitor socket */
zlink_close(mon);
```

Both steps must be performed. Calling only `zlink_close(mon)` may leave internal resources uncleared.

---
[← TLS Security](05-tls-security.md) | [Services Overview →](07-0-services.md)
