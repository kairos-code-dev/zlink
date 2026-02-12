[English](06-monitoring.md) | [한국어](06-monitoring.ko.md)

# Monitoring API Usage

## 1. Overview

The zlink monitoring API allows real-time observation of socket events such as connection, disconnection, and handshake. It operates on a polling basis and receives events through a PAIR socket.

## 2. Enabling the Monitor

### 2.1 Automatic Creation (Recommended)

```c
void *server = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(server, "tcp://*:5555");

/* Automatically create monitor socket */
void *mon = zlink_socket_monitor_open(server, ZLINK_EVENT_ALL);
```

### 2.2 Manual Setup

```c
zlink_socket_monitor(server, "inproc://monitor", ZLINK_EVENT_ALL);

void *mon = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(mon, "inproc://monitor");
```

## 3. Receiving Events

```c
zlink_monitor_event_t ev;
int rc = zlink_monitor_recv(mon, &ev, ZLINK_DONTWAIT);
if (rc == 0) {
    printf("Event: 0x%llx\n", (unsigned long long)ev.event);
    printf("Local: %s\n", ev.local_addr);
    printf("Remote: %s\n", ev.remote_addr);

    if (ev.routing_id.size > 0) {
        printf("routing_id: ");
        for (uint8_t i = 0; i < ev.routing_id.size; ++i)
            printf("%02x", ev.routing_id.data[i]);
        printf("\n");
    }
}
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

### Timeout Configuration

You can control the event wait time by setting `ZLINK_RCVTIMEO` on the monitor socket.

```c
int timeout = 1000;  /* 1 second */
zlink_setsockopt(mon, ZLINK_RCVTIMEO, &timeout, sizeof(timeout));

zlink_monitor_event_t ev;
int rc = zlink_monitor_recv(mon, &ev, 0);  /* Wait up to 1 second */
if (rc == -1 && errno == EAGAIN) {
    /* Timeout: no event */
}
```

> Reference: `core/tests/testutil_monitoring.cpp` -- `get_monitor_event_with_timeout()`

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
- **Typical usage**: Trigger peer registration, start sending messages, or query peer info via `zlink_socket_peer_info()`.

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
| `ZLINK_PROTOCOL_ERROR_ZMP_UNSPECIFIED` | `0x10000000` | Unspecified ZMP protocol error. |
| `ZLINK_PROTOCOL_ERROR_ZMP_UNEXPECTED_COMMAND` | `0x10000001` | Unexpected ZMP command received during handshake. |
| `ZLINK_PROTOCOL_ERROR_ZMP_INVALID_SEQUENCE` | `0x10000002` | ZMP commands arrived in an invalid order. |
| `ZLINK_PROTOCOL_ERROR_ZMP_KEY_EXCHANGE` | `0x10000003` | Key exchange step of ZMP handshake failed. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_UNSPECIFIED` | `0x10000011` | A ZMP command was malformed (unspecified). |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_MESSAGE` | `0x10000012` | Malformed ZMP MESSAGE command. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | `0x10000013` | Malformed ZMP HELLO command. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_INITIATE` | `0x10000014` | Malformed ZMP INITIATE command. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_ERROR` | `0x10000015` | Malformed ZMP ERROR command. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_READY` | `0x10000016` | Malformed ZMP READY command. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_WELCOME` | `0x10000017` | Malformed ZMP WELCOME command. |
| `ZLINK_PROTOCOL_ERROR_ZMP_INVALID_METADATA` | `0x10000018` | Invalid metadata in ZMP handshake. |
| `ZLINK_PROTOCOL_ERROR_ZMP_CRYPTOGRAPHIC` | `0x11000001` | Cryptographic verification failed during ZMP handshake. |
| `ZLINK_PROTOCOL_ERROR_ZMP_MECHANISM_MISMATCH` | `0x11000002` | Client and server security mechanisms do not match. |
| `ZLINK_PROTOCOL_ERROR_WS_UNSPECIFIED` | `0x30000000` | Unspecified WebSocket protocol error. |

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
zlink_monitor_event_t ev;
zlink_monitor_recv(mon, &ev, 0);

if (ev.event == ZLINK_EVENT_DISCONNECTED) {
    switch (ev.value) {
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
```

## 7. Event Filtering and Subscription Presets

### Subscribing to Specific Events Only

```c
/* Connection/disconnection events only */
void *mon = zlink_socket_monitor_open(server,
    ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
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

void *mon = zlink_socket_monitor_open(server, MONITOR_PRESET_SECURITY);
```

## 8. Peer Information Queries

### Connected Peer Count

```c
int count = zlink_socket_peer_count(socket);
printf("Connected peers: %d\n", count);
```

### Specific Peer Information

```c
/* Query routing_id by index */
zlink_routing_id_t rid;
zlink_socket_peer_routing_id(socket, 0, &rid);

/* Query detailed info by routing_id */
zlink_peer_info_t info;
zlink_socket_peer_info(socket, &rid, &info);
printf("Remote: %s, Connected time: %llu\n", info.remote_addr, info.connected_time);
```

### Full Peer List

```c
zlink_peer_info_t peers[64];
size_t peer_count = 64;
zlink_socket_peers(socket, peers, &peer_count);

for (size_t i = 0; i < peer_count; i++) {
    printf("Peer %zu: remote=%s\n", i, peers[i].remote_addr);
}
```

### Combining Peer Information with Monitoring

```c
/* Query peer info when CONNECTION_READY event is received */
zlink_monitor_event_t ev;
zlink_monitor_recv(mon, &ev, 0);

if (ev.event == ZLINK_EVENT_CONNECTION_READY && ev.routing_id.size > 0) {
    zlink_peer_info_t info;
    zlink_socket_peer_info(socket, &ev.routing_id, &info);
    printf("New connection: remote=%s\n", info.remote_addr);
}
```

## 9. Multi-Socket Monitoring (Using Poller)

Handle events from multiple sockets in a single loop.

```c
void *mon_a = zlink_socket_monitor_open(sock_a, ZLINK_EVENT_ALL);
void *mon_b = zlink_socket_monitor_open(sock_b, ZLINK_EVENT_ALL);

zlink_pollitem_t items[] = {
    {mon_a, 0, ZLINK_POLLIN, 0},
    {mon_b, 0, ZLINK_POLLIN, 0},
};

while (1) {
    int rc = zlink_poll(items, 2, 1000);
    if (rc <= 0) continue;

    zlink_monitor_event_t ev;

    if (items[0].revents & ZLINK_POLLIN) {
        zlink_monitor_recv(mon_a, &ev, ZLINK_DONTWAIT);
        printf("Socket A event: 0x%llx\n", (unsigned long long)ev.event);
    }
    if (items[1].revents & ZLINK_POLLIN) {
        zlink_monitor_recv(mon_b, &ev, ZLINK_DONTWAIT);
        printf("Socket B event: 0x%llx\n", (unsigned long long)ev.event);
    }
}

/* Cleanup */
zlink_socket_monitor(sock_a, NULL, 0);
zlink_socket_monitor(sock_b, NULL, 0);
zlink_close(mon_a);
zlink_close(mon_b);
```

### Polling Monitor + Data Socket Simultaneously

```c
zlink_pollitem_t items[] = {
    {data_socket, 0, ZLINK_POLLIN, 0},  /* Data reception */
    {mon_socket, 0, ZLINK_POLLIN, 0},   /* Monitor events */
};

while (1) {
    zlink_poll(items, 2, 1000);

    if (items[0].revents & ZLINK_POLLIN) {
        /* Process data */
        char buf[256];
        zlink_recv(data_socket, buf, sizeof(buf), 0);
    }
    if (items[1].revents & ZLINK_POLLIN) {
        /* Process event */
        zlink_monitor_event_t ev;
        zlink_monitor_recv(mon_socket, &ev, ZLINK_DONTWAIT);
    }
}
```

## 10. Important Notes

### Monitor Thread Safety

Monitor setup must be called **only from the socket's owning thread**.

```c
/* Correct usage: set up monitor from the socket creation thread */
void *socket = zlink_socket(ctx, ZLINK_ROUTER);
void *mon = zlink_socket_monitor_open(socket, ZLINK_EVENT_ALL);

/* Incorrect usage: set up monitor from a different thread */
/* → Undefined behavior */
```

### Concurrent Monitor Limitation

Multiple monitors cannot be set on the same socket simultaneously.

### Monitor Speed

If the monitor socket's receive is slow, events **may be dropped**. It is recommended to process events immediately when using DONTWAIT, or process them in a separate thread.

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
