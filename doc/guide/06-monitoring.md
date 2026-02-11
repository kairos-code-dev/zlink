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

| Event | Value | Meaning | When It Occurs | routing_id |
|-------|-------|---------|----------------|:----------:|
| `CONNECTED` | -- | TCP connection established | Immediately after connect | None |
| `ACCEPTED` | -- | Accept completed | Immediately after listener accept | None |
| `CONNECTION_READY` | -- | **Connection ready** | Handshake succeeded | Possible |
| `DISCONNECTED` | reason | Session terminated | At any stage | Possible |
| `CONNECT_DELAYED` | -- | Connection delayed | First attempt failed | None |
| `CONNECT_RETRIED` | -- | Connection retried | Reconnection attempt | None |
| `LISTENING` | -- | Listener active | Bind succeeded | None |
| `BIND_FAILED` | errno | Bind failed | Bind error | None |
| `CLOSED` | -- | Socket closed | Immediately after close | None |
| `MONITOR_STOPPED` | -- | Monitor stopped | monitor(NULL) called | None |
| `HANDSHAKE_FAILED_NO_DETAIL` | errno | Handshake failed | Before READY | None |
| `HANDSHAKE_FAILED_PROTOCOL` | -- | Protocol error | ZMP handshake | None |
| `HANDSHAKE_FAILED_AUTH` | -- | Authentication failed | TLS handshake | None |

> Reference: `core/tests/testutil_monitoring.cpp` -- `get_zlinkEventName()` event name mapping

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
