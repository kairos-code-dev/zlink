[English](06-monitoring.md) | [한국어](06-monitoring.ko.md)

# Monitoring API Usage

## 1. Overview

The zlink monitoring API allows real-time observation of socket events such as connection, disconnection, and handshake. Like other sockets, monitors support both recv mode (pull) and callback mode.

## 2. Enabling the Monitor

### 2.1 Callback Mode

The handler is invoked on the I/O thread immediately when an event occurs.
Callback mode is suitable for real-time processing without event loss.

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

/* Create monitor with options */
zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
void *mon = zlink_socket_monitor_open(server, &opts);
zlink_socket_monitor_handler(mon, on_monitor_event, NULL);
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

## 4. Socket Monitor Events

Events observed via `zlink_socket_monitor_open()`.
These report transport/session state for raw sockets.

### Event table

| Constant | Value | Description | `value` | `routing_id` | Side | After this event |
|---|---|---|---|---|---|---|
| `CONNECTION_READY_CHANGED` | `0x1000` | Handshake complete, messaging ready | `current_ready_count` | peer id (all sockets) | Both | **start send/recv** |
| `CONNECTED` | `0x0001` | TCP connection established (pre-handshake) | fd | — | Client | wait for `CONNECTION_READY_CHANGED` |
| `ACCEPTED` | `0x0020` | Incoming connection accepted (pre-handshake) | fd | — | Server | wait for `CONNECTION_READY_CHANGED` |
| `DISCONNECTED` | `0x0200` | Session terminated | reason code | Possible | Both | trigger reconnection |
| `LISTENING` | `0x0008` | Bind succeeded, listening | fd | — | Server | — |
| `CLOSED` | `0x0080` | Intentional close completed | — | — | Both | — |
| `CONNECT_DELAYED` | `0x0002` | Async connection retry scheduled | errno | — | Client | automatic retry |
| `CONNECT_RETRIED` | `0x0004` | Async reconnection in progress | — | — | Client | automatic retry |
| `BIND_FAILED` | `0x0010` | Bind failed | errno | — | Server | check address/permissions |
| `ACCEPT_FAILED` | `0x0040` | Accept failed | errno | — | Server | check fd limits |
| `CLOSE_FAILED` | `0x0100` | Close failed | errno | — | Both | — |
| `HANDSHAKE_FAILED_NO_DETAIL` | `0x0800` | Handshake failed (generic) | errno | — | Both | check network |
| `HANDSHAKE_FAILED_PROTOCOL` | `0x2000` | Handshake failed (protocol error) | error code | — | Both | check version/config |
| `HANDSHAKE_FAILED_AUTH` | `0x4000` | Handshake failed (auth) | — | — | Both | check TLS/auth config |
| `SUB_DELIVERY_READY_CHANGED` | `0x8000` | SUB subscription propagated | `0`/`1` (boolean) | — | SUB side | **start `zlink_subscribe()` recv** |
| `PUB_DELIVERY_READY_CHANGED` | `0x10000` | PUB subscriber ready | `current_ready_count` | — | PUB side | **start `zlink_publish()` delivery** |
| `MONITOR_STOPPED` | `0x0400` | Monitor stopped | — | — | Both | `zlink_monitor_close()` |

### Connection flow

```
Client: CONNECT_DELAYED (optional) → CONNECTED → CONNECTION_READY_CHANGED → start send/recv
Server: LISTENING → ACCEPTED → CONNECTION_READY_CHANGED → start send/recv
Close:  CONNECTION_READY_CHANGED → DISCONNECTED → CONNECT_DELAYED → reconnect...
```

### CONNECTION_READY_CHANGED details

Fired after a successful handshake. Once received, messaging can start immediately.
The `value` field contains `current_ready_count` -- the absolute number of ready peers.

- `ev->routing_id` contains the peer identity on all socket types.

### DISCONNECTED reason codes

| Code | Name | Meaning |
|------|------|---------|
| 0 | `UNKNOWN` | Reason unknown |
| 3 | `HANDSHAKE_FAILED` | Handshake failure |
| 4 | `TRANSPORT_ERROR` | Transport-layer error |
| 5 | `CTX_TERM` | Context terminated |

### Protocol error codes

When `HANDSHAKE_FAILED_PROTOCOL` fires, the `value` field contains one of these codes:

| Constant | Value | Description |
|---|---|---|
| `ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO` | `0x10000013` | Malformed ZMP HELLO command. |

## 5. Event Flow Diagrams

### Successful Connection

```
Client side:
  CONNECT_DELAYED (optional) → CONNECTED → CONNECTION_READY_CHANGED

Server side:
  ACCEPTED → CONNECTION_READY_CHANGED
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
CONNECTION_READY_CHANGED → DISCONNECTED
```

### Reconnection

```
CONNECTED → CONNECTION_READY_CHANGED → DISCONNECTED →
CONNECT_DELAYED → CONNECT_RETRIED → CONNECTED → CONNECTION_READY_CHANGED
```

## 6. DISCONNECTED Reason Codes

The `value` field of the `DISCONNECTED` event contains the reason for disconnection.

| Code | Name | Meaning | Recommended Action |
|------|------|---------|-------------------|
| 0 | UNKNOWN | Unknown cause | Log and observe |
| 3 | HANDSHAKE_FAILED | Handshake failure | Check TLS/protocol configuration |
| 4 | TRANSPORT_ERROR | Transport layer error | Check network status |
| 5 | CTX_TERM | Context terminated | Handle shutdown |

### Reason Code Handling Example

```c
void on_monitor(const zlink_monitor_event_t *ev, void *userdata)
{
    if (ev->event == ZLINK_EVENT_DISCONNECTED) {
        switch (ev->value) {
            case ZLINK_DISCONNECT_REASON_UNKNOWN:
                printf("Unknown disconnection\n");
                break;
            case ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED:
                printf("Handshake failed -- check TLS configuration\n");
                break;
            case ZLINK_DISCONNECT_REASON_TRANSPORT_ERROR:
                printf("Transport error -- check network\n");
                break;
            case ZLINK_DISCONNECT_REASON_CTX_TERM:
                printf("Context terminated\n");
                break;
            default:
                printf("Unknown reason=%llu\n", (unsigned long long)ev->value);
                break;
        }
    }
}
```

## 7. Event Filtering and Subscription Presets

### Subscribing to Specific Events Only

```c
/* Connection/disconnection events only */
zlink_socket_monitor_open_options_t opts = {
    .events = ZLINK_EVENT_CONNECTION_READY_CHANGED | ZLINK_EVENT_DISCONNECTED,
};
void *mon = zlink_socket_monitor_open(server, &opts);
zlink_socket_monitor_handler(mon, on_monitor_event, NULL);
```

### Recommended Subscription Presets

| Preset | Event Mask | Purpose |
|--------|-----------|---------|
| Basic | `CONNECTION_READY_CHANGED \| DISCONNECTED` | Connection state tracking |
| Debug | Basic + `CONNECTED \| ACCEPTED \| CONNECT_DELAYED \| CONNECT_RETRIED` | Detailed connection process |
| Security | Basic + `HANDSHAKE_FAILED_*` | Authentication failure detection |
| Full | `ZLINK_EVENT_ALL` | All events |

### Preset Implementation Example

```c
/* Basic preset */
#define MONITOR_PRESET_BASIC \
    (ZLINK_EVENT_CONNECTION_READY_CHANGED | ZLINK_EVENT_DISCONNECTED)

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

zlink_socket_monitor_open_options_t sec_opts = { .events = MONITOR_PRESET_SECURITY };
void *mon = zlink_socket_monitor_open(server, &sec_opts);
zlink_socket_monitor_handler(mon, on_monitor_event, NULL);
```

## 8. Socket Monitor Snapshot

Query the current aggregate state from a monitor handle at any time.

| Field | Description |
|-------|-------------|
| `ready_count` | Number of currently ready peers |
| `snd_pending_msgs` | Messages pending in send queue (capped by SNDHWM) |
| `rcv_pending_msgs` | Messages pending in receive queue (capped by RCVHWM, approximate) |

`snd_pending_msgs` and `rcv_pending_msgs` are directly related to HWM settings.
When these values approach the HWM, backpressure is occurring.

**Note — pending values can exceed the HWM setting:**

1. **inproc transport**: inproc has no session/engine in between, so both
   sides' HWMs are summed. For example, if both sides have SNDHWM=1000
   and RCVHWM=1000, the actual pipe HWM is `1000 + 1000 = 2000`.
   Pending values can appear as twice the configured value.

2. **Slight HWM overshoot**: The write side's view of the read counter
   (`_peers_msgs_read`) is not real-time — it is a snapshot reported by
   the read side only when LWM is reached. Synchronizing on every message
   would eliminate the lock-free pipe's performance advantage, so batch
   notification is used instead. As a result, HWM is an **approximate
   limit**, not a hard limit, and can be slightly exceeded between
   notifications.

| transport | actual pipe HWM | reason |
|-----------|----------------|--------|
| tcp/ipc/tls/ws/wss | `SNDHWM` (as configured) | session manages each side independently |
| inproc | `SNDHWM + peer.RCVHWM` | direct connection without session; buffers summed |

```c
zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
void *monitor = zlink_socket_monitor_open(socket, &opts);
zlink_monitor_snapshot_t snapshot;
zlink_monitor_snapshot(monitor, &snapshot);
printf("Ready peers: %u, sndq=%llu, rcvq=%llu\n",
       snapshot.ready_count,
       (unsigned long long) snapshot.snd_pending_msgs,
       (unsigned long long) snapshot.rcv_pending_msgs);
```

You can also combine snapshot queries inside event callbacks.

```c
void on_monitor(const zlink_monitor_event_t *ev, void *userdata)
{
    if (ev->event == ZLINK_EVENT_CONNECTION_READY_CHANGED) {
        zlink_monitor_snapshot_t snapshot;
        zlink_monitor_snapshot(g_monitor, &snapshot);
        printf("Ready peers now: %u\n", snapshot.ready_count);
    }
}
```

## 8.1 Service Monitor

The service monitor observes state changes on service handles such as
SPOT and Discovery. It is a separate API from the socket monitor.

- **Event type**: `zlink_service_event_t` (different from socket monitor's `zlink_monitor_event_t`)
- **Callback type**: `zlink_service_monitor_handler_fn`
- **Open**: `zlink_service_monitor_open(target, &options)`
- **Close**: `zlink_monitor_close(&mon)` (same as socket monitor)

### Opening a service monitor

```c
/* SPOT service monitor */
zlink_service_monitor_open_options_t opts = {
    .events = ZLINK_SERVICE_MONITOR_EVENT_ALL
};
void *mon = zlink_service_monitor_open(spot_node, &opts);
```

Pass any service handle (discovery, spot, spot_node).
The handle kind is determined automatically at runtime.

### Callback mode

```c
void on_spot_event(const zlink_service_event_t *ev, void *userdata)
{
    if (ev->event_type & ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED) {
        printf("sub delivery ready\n");
    }
    if (ev->event_type & ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED) {
        printf("pub first delivery ready\n");
    }
}

zlink_service_monitor_handler(mon, on_spot_event, NULL);
```

### Recv mode

```c
zlink_service_event_t ev;
int rc = zlink_service_monitor_recv(mon, &ev);
if (rc == 0) {
    printf("event: 0x%x, value: %u\n", ev.event_type, ev.value);
}
```

### Service event table

Events observed via `zlink_service_monitor_open()`.
Different services emit different events.

#### SPOT events

| Constant | Description | `value` | After this event |
|---|---|---|---|
| `SUB_DELIVERY_READY_CHANGED` | sub delivery readiness changed | — | **start receiving** |
| `PUB_FIRST_DELIVERY_READY_CHANGED` | at least one subscriber ready | — | **start `zlink_publish()` delivery** |
| `SPOT_SUB_FILTER_APPLIED` | subscription filter propagated to peer | — | — |
| `SPOT_SUB_SUBSCRIPTION_READY` | subscription receive ready | — | — |
| `SPOT_PUB_DELIVERY_READY_CHANGED` | subject-specific remote delivery-ready changed | — | — |

#### Discovery events

| Constant | Description | `value` | After this event |
|---|---|---|---|
| `DISCOVERY_SERVICE_UP` | discovered service came up | — | — |
| `DISCOVERY_SERVICE_DOWN` | discovered service went down | — | — |
| `DISCOVERY_PROVIDERS_CHANGED` | provider set changed | — | — |

#### Common events (all services)

| Constant | Description |
|---|---|
| `MONITOR_EVENT_ERROR` | error occurred |
| `MONITOR_EVENT_CLOSED` | monitor closed |

See [events.md](../api/events.md) for the full event catalog.

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

zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
void *mon_a = zlink_socket_monitor_open(sock_a, &opts);
zlink_socket_monitor_handler(mon_a, on_event_a, NULL);
void *mon_b = zlink_socket_monitor_open(sock_b, &opts);
zlink_socket_monitor_handler(mon_b, on_event_b, NULL);

/* ... application logic ... */

/* Cleanup */
zlink_monitor_close(&mon_a);
zlink_monitor_close(&mon_b);
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
zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
void *mon = zlink_socket_monitor_open(socket, &opts);
zlink_socket_monitor_handler(mon, on_monitor_event, NULL);

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
zlink_monitor_close(&mon);
```

## 11. Knowing When Messaging Is Ready

When you need to know the exact moment a socket or service can send and
receive, wait for the right event.

### 11.1 Raw sockets — PAIR, DEALER, ROUTER

Ready to send/recv immediately after `CONNECTION_READY_CHANGED`.

```c
/* DEALER/ROUTER example */
zlink_socket_monitor_open_options_t opts = {
    .events = ZLINK_EVENT_CONNECTION_READY_CHANGED
};
void *mon = zlink_socket_monitor_open(router, &opts);

void on_ready(const zlink_monitor_event_t *ev, void *userdata) {
    if (ev->event & ZLINK_EVENT_CONNECTION_READY_CHANGED) {
        /* ROUTER: ev->routing_id contains the peer identity */
        /* routed send is possible now */
    }
}
zlink_socket_monitor_handler(mon, on_ready, NULL);
```

| Family | Wait for | Then you can |
|---|---|---|
| PAIR | `CONNECTION_READY_CHANGED` on both sides | bidirectional send/recv |
| DEALER | `CONNECTION_READY_CHANGED` | send/recv |
| ROUTER | `CONNECTION_READY_CHANGED` | routed send/recv using `ev->routing_id` |

### 11.2 Raw sockets — STREAM

STREAM works like ROUTER — the routing_id is assigned when the TCP
connection is established, not when the first payload arrives.
`CONNECTION_READY_CHANGED` fires with the routing_id before any payload
is delivered to the application. Sequence:

1. Client connects via raw TCP
2. Server receives `CONNECTION_READY_CHANGED` with `ev->routing_id`
3. Server can now send to the client using the routing_id
4. Client payload (if any) arrives after the ready event

```c
/* STREAM server: CONNECTION_READY_CHANGED → routing_id available → send/recv */
void on_ready(const zlink_monitor_event_t *ev, void *userdata) {
    if (ev->event & ZLINK_EVENT_CONNECTION_READY_CHANGED) {
        /* ev->routing_id contains the peer's routing_id */
        /* send to this peer immediately, or wait for inbound payload */
    }
}

zlink_socket_monitor_open_options_t opts = {
    .events = ZLINK_EVENT_CONNECTION_READY_CHANGED
};
void *mon = zlink_socket_monitor_open(stream_server, &opts);
zlink_socket_monitor_handler(mon, on_ready, NULL);
```

| Family | Wait for | Then you can |
|---|---|---|
| STREAM | `CONNECTION_READY_CHANGED` | send/recv using `ev->routing_id` |

### 11.3 Raw sockets — PUB/SUB

Raw PUB/SUB sockets provide delivery-ready events via the socket monitor.
Open separate monitors on PUB and SUB and wait for both before messaging.

- `SUB_DELIVERY_READY_CHANGED(value=1)` — subscription propagated, receiving possible (0/1 boolean)
- `PUB_DELIVERY_READY_CHANGED(value>0)` — ready subscriber count (absolute count); check against expected subscriber count

```c
zlink_set_subscription(sub, "topic");

/* SUB monitor: wait for subscription propagation */
zlink_socket_monitor_open_options_t sub_opts = {
    .events = ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED
};
void *sub_mon = zlink_socket_monitor_open(sub, &sub_opts);

/* PUB monitor: wait for subscriber readiness */
zlink_socket_monitor_open_options_t pub_opts = {
    .events = ZLINK_EVENT_PUB_DELIVERY_READY_CHANGED
};
void *pub_mon = zlink_socket_monitor_open(pub, &pub_opts);

/* Start messaging after both delivery-ready events */
/* SUB_DELIVERY_READY_CHANGED(value=1) + PUB_DELIVERY_READY_CHANGED(value>=expected_subs) */
zlink_publish(pub, NULL, &part, 1, 0);  /* raw PUB: topic_id is NULL */
zlink_subscribe(sub, &parts, &count, 0, topic_buf, &topic_len);

zlink_monitor_close(&pub_mon);
zlink_monitor_close(&sub_mon);
```

| Family | Wait for | Then you can |
|---|---|---|
| PUB | `PUB_DELIVERY_READY_CHANGED(value>=expected_subs)` | `zlink_publish()` delivery |
| SUB | `SUB_DELIVERY_READY_CHANGED(value=1)` | `zlink_subscribe()` recv |

### 11.4 Services — SPOT

SPOT uses separate service monitors for sub and pub, each subscribing to
different events.

```c
/* Sub monitor: subscribe to SUB_DELIVERY_READY_CHANGED */
zlink_service_monitor_open_options_t sub_opts = {
    .events = ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED
              | ZLINK_MONITOR_EVENT_ERROR
};
void *sub_mon = zlink_service_monitor_open(sub_node, &sub_opts);

/* Pub monitor: subscribe to PUB_FIRST_DELIVERY_READY_CHANGED */
zlink_service_monitor_open_options_t pub_opts = {
    .events = ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED
              | ZLINK_MONITOR_EVENT_ERROR
};
void *pub_mon = zlink_service_monitor_open(pub_node, &pub_opts);

/* Start messaging after both are ready */
/* sub ready → zlink_subscribe() for receiving */
/* pub ready → zlink_publish() for delivery */
```

| Service | Wait for | Then you can |
|---|---|---|
| SPOT sub | `SUB_DELIVERY_READY_CHANGED` | start receiving via `zlink_subscribe()` |
| SPOT pub | `PUB_FIRST_DELIVERY_READY_CHANGED` | start delivering via `zlink_publish()` |

### 11.5 Snapshots

`zlink_monitor_snapshot()` and `zlink_*_status_snapshot()` return
a point-in-time view of the current state. Use them for dashboards,
health checks, and debugging.

```c
/* Check current discovery health */
zlink_discovery_status_t status;
zlink_discovery_status_snapshot(discovery, &status);
printf("state=%d\n", status.state);
```

---
[← TLS Security](05-tls-security.md) | [Services Overview →](07-0-services.md)
