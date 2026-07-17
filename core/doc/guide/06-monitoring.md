[English](06-monitoring.md) | [한국어](06-monitoring.ko.md)

<!-- zlink-nav:start -->
[← TLS/Security](05-tls-security.md) | [Services →](07-0-services.md)
<!-- zlink-nav:end -->

# Monitoring API Usage

## 1. Overview

Monitoring enables real-time observation of socket connection state — essential for diagnosing connectivity issues, detecting peer failures, and triggering application-level recovery.

The zlink monitoring API allows real-time observation of socket events such as connection, disconnection, and handshake. Like other sockets, monitors support both recv mode (pull) and callback mode.

## 2. Enabling the Monitor

### 2.1 Callback Mode

The handler is invoked on the service-control runtime thread (not the parent
socket's I/O thread) when an event occurs. Callback mode is convenient for
real-time processing; note that monitor delivery is lossy — events may be
dropped under load, so do not rely on receiving every event.

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

void *server = zlink_socket(ctx, ZLINK_SOCKET_ROUTER);
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
| `CONNECTION_READY` | `0x1000` | Ready edge after handshake | reserved | peer id | Both | **start send/recv** |
| `CONNECTED` | `0x0001` | TCP connection established (pre-handshake) | provider-specific | peer id or sentinel | Client | wait for `CONNECTION_READY` |
| `ACCEPTED` | `0x0020` | Incoming connection accepted (pre-handshake) | provider-specific | peer id or sentinel | Server | wait for `CONNECTION_READY` |
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
| `MONITOR_STOPPED` | `0x0400` | Monitor stopped | — | — | Both | `zlink_monitor_close()` |
| `PEER_WEIGHT_CHANGED` | `0x8000` | Connected peer's weight changed | new `0..100` weight | peer id | Both | re-evaluate dispatch / dashboard |

### Connection flow

```mermaid
flowchart LR
    subgraph Client
        CD1[CONNECT_DELAYED\noptional] --> CO1[CONNECTED] --> CR1[CONNECTION_READY] --> SR1[start send/recv]
    end
    subgraph Server
        L1[LISTENING] --> A1[ACCEPTED] --> CR2[CONNECTION_READY] --> SR2[start send/recv]
    end
    subgraph Close
        CR3[CONNECTION_READY] --> D1[DISCONNECTED] --> CD2[CONNECT_DELAYED] --> RE1[reconnect...]
    end
```

### CONNECTION_READY details

Fired after a successful handshake. Once received, messaging can start immediately.
The `value` field of `CONNECTION_READY` is reserved and must not be used
as an aggregate ready-count contract.

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

### Peer weight changes

When a peer connected to a ROUTER or DEALER changes its own weight,
`ZLINK_EVENT_PEER_WEIGHT_CHANGED` is delivered through the raw socket
monitor. The event's `routing_id` identifies the peer that changed;
`value` holds the new `0..100` weight.

```c
void on_weight(const zlink_monitor_event_t *ev, void *userdata)
{
    if (!(ev->event & ZLINK_EVENT_PEER_WEIGHT_CHANGED))
        return;

    printf("orders-exec peer weight -> %" PRIu64 "\n", ev->value);
}

void *dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_connect(dealer, "tcp://orders-exec-1:7100");

zlink_socket_monitor_open_options_t opts = {
    .events = ZLINK_EVENT_CONNECTION_READY
            | ZLINK_EVENT_DISCONNECTED
            | ZLINK_EVENT_PEER_WEIGHT_CHANGED,
};
void *mon = zlink_socket_monitor_open(dealer, &opts);
zlink_socket_monitor_handler(mon, on_weight, NULL);
```

DEALER automatically excludes weight-`0` peers from its candidate set, so
the application typically only needs this event to update diagnostics or
dashboards. Once every known peer is `0`, new
submits start failing with `ZLINK_SUBMIT_NOT_ADMITTED`.

For service-layer diagnostics, use current SPOT node snapshots. The core C API
does not provide a Discovery service view.

## 5. Event Flow Diagrams

### Successful Connection

```mermaid
flowchart LR
    subgraph Client side
        CD[CONNECT_DELAYED\noptional] --> CO[CONNECTED] --> CR1[CONNECTION_READY]
    end
    subgraph Server side
        A[ACCEPTED] --> CR2[CONNECTION_READY]
    end
```

### Handshake Failure

```mermaid
flowchart LR
    subgraph Client side
        CO[CONNECTED] --> HF1[HANDSHAKE_FAILED_*] --> D1[DISCONNECTED]
    end
    subgraph Server side
        A[ACCEPTED] --> HF2[HANDSHAKE_FAILED_*] --> D2[DISCONNECTED]
    end
```

### Normal Disconnection

```mermaid
flowchart LR
    CR[CONNECTION_READY] --> D[DISCONNECTED]
```

### Reconnection

```mermaid
flowchart LR
    CO1[CONNECTED] --> CR1[CONNECTION_READY] --> D[DISCONNECTED]
    D --> CD[CONNECT_DELAYED] --> RT[CONNECT_RETRIED]
    RT --> CO2[CONNECTED] --> CR2[CONNECTION_READY]
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
    .events = ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED,
};
void *mon = zlink_socket_monitor_open(server, &opts);
zlink_socket_monitor_handler(mon, on_monitor_event, NULL);
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

zlink_socket_monitor_open_options_t sec_opts = { .events = MONITOR_PRESET_SECURITY };
void *mon = zlink_socket_monitor_open(server, &sec_opts);
zlink_socket_monitor_handler(mon, on_monitor_event, NULL);
```

## 8. Socket Monitor Snapshot

Query the current aggregate state from a monitor handle at any time.

| Field | Description |
|-------|-------------|
| `snd_pending_msgs` | Messages pending in send queue (capped by SNDHWM) |
| `rcv_pending_msgs` | Messages pending in receive queue (capped by RCVHWM, approximate) |
| `auto_hwm_profile` / `auto_hwm_policy_class` | Active auto-HWM profile and planner policy class |
| `auto_hwm_applied_sndhwm` / `auto_hwm_applied_rcvhwm` | Automatically applied HWM values on the socket |
| `auto_hwm_unit_budget_bytes` / `auto_hwm_size_cap` | Profile unit budget and message-count cap used by the planner |
| `auto_hwm_effective_message_bytes` | Message unit used to convert the profile envelope into HWM slots |
| `auto_hwm_socket_message_slots` | Message slots selected from the profile unit budget and message unit |
| `auto_hwm_last_recalc_reason` / `auto_hwm_send_blocked_ratio_ppm` | Recalculation reason and recent send-blocked ratio |
| `auto_hwm_deferred_sndhwm` / `auto_hwm_deferred_rcvhwm` | Deferred HWM shrink values, or `-1` when no shrink is deferred |

`snd_pending_msgs` and `rcv_pending_msgs` are directly related to HWM settings.
When these values approach the HWM, backpressure is occurring.
When automatic HWM is enabled, the same snapshot also tells you why the
current HWM was chosen through the profile, unit-budget, message-unit, and
applied-HWM fields.

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
zlink_monitor_status_t snapshot;
zlink_monitor_status(monitor, &snapshot);
printf("sndq=%llu, rcvq=%llu\n",
       (unsigned long long) snapshot.snd_pending_msgs,
       (unsigned long long) snapshot.rcv_pending_msgs);
```

You can also combine snapshot queries inside event callbacks.

```c
void on_monitor(const zlink_monitor_event_t *ev, void *userdata)
{
    if (ev->event == ZLINK_EVENT_CONNECTION_READY) {
        zlink_monitor_status_t snapshot;
        zlink_monitor_status(g_monitor, &snapshot);
        printf("Monitor snapshot updated\n");
    }
}
```

## 8.1 Service-Layer State Checks

The current public C API does not expose a separate service-event
handle. For service-layer checks, read snapshots or query results and
compare them over time.

- MeshNode state: `zlink_mesh_node_status()`,
  `zlink_mesh_node_peers()`,
  `zlink_mesh_node_peer_channels()`
- MeshNode event stream: the `zlink_mesh_node_monitor_open()` family
  (see [monitoring spec §2–5](../spec/core/07-monitoring.md))

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
other concurrent operations on the same handle. The monitor callback runs on
the service-control runtime thread (not the socket's I/O thread), so slow
callback work should be offloaded to a user queue.

```c
/* Open a monitor from an application thread */
void *socket = zlink_socket(ctx, ZLINK_SOCKET_ROUTER);
zlink_socket_monitor_open_options_t opts = { .events = ZLINK_EVENT_ALL };
void *mon = zlink_socket_monitor_open(socket, &opts);
zlink_socket_monitor_handler(mon, on_monitor_event, NULL);

/* Snapshot reads may happen later from another worker thread */
zlink_monitor_status_t snapshot;
zlink_monitor_status(mon, &snapshot);
```

### Concurrent Monitor Limitation

Multiple monitors cannot be set on the same socket simultaneously.

### Callback Processing Speed

Blocking work in the callback handler does not stall the socket's I/O path
(the callback runs on the service-control thread), but it delays subsequent
monitor events. For slow processing, enqueue from the callback and handle it
on your own thread.

### Monitor Shutdown Procedure

```c
/* Close the monitor handle */
zlink_monitor_close(&mon);
```

Calling `zlink_monitor_close()` from inside the monitor callback is allowed:
the close is deferred until the callback returns (it returns OK rather than
failing), and final cleanup runs once the callback depth reaches zero.

## 11. Knowing When Messaging Is Ready

When you need to know the exact moment a socket can send and receive,
wait for the right event.

### 11.1 Raw sockets — PAIR, DEALER, ROUTER

Ready to send/recv immediately after `CONNECTION_READY`.

```c
/* DEALER/ROUTER example */
zlink_socket_monitor_open_options_t opts = {
    .events = ZLINK_EVENT_CONNECTION_READY
};
void *mon = zlink_socket_monitor_open(router, &opts);

void on_ready(const zlink_monitor_event_t *ev, void *userdata) {
    if (ev->event & ZLINK_EVENT_CONNECTION_READY) {
        /* ROUTER: ev->routing_id contains the peer identity */
        /* routed send is possible now */
    }
}
zlink_socket_monitor_handler(mon, on_ready, NULL);
```

| Family | Wait for | Then you can |
|---|---|---|
| PAIR | `CONNECTION_READY` on both sides | bidirectional send/recv |
| DEALER | `CONNECTION_READY` | send/recv |
| ROUTER | `CONNECTION_READY` | routed send/recv using `ev->routing_id` |

### 11.2 Raw sockets — STREAM

STREAM works like ROUTER — the routing_id is assigned when the TCP
connection is established, not when the first payload arrives.
`CONNECTION_READY` fires with the routing_id before any payload
is delivered to the application. Sequence:

1. Client connects via raw TCP
2. Server receives `CONNECTION_READY` with `ev->routing_id`
3. Server can now send to the client using the routing_id
4. Client payload (if any) arrives after the ready event

```c
/* STREAM server: CONNECTION_READY → routing_id available → send/recv */
void on_ready(const zlink_monitor_event_t *ev, void *userdata) {
    if (ev->event & ZLINK_EVENT_CONNECTION_READY) {
        /* ev->routing_id contains the peer's routing_id */
        /* send to this peer immediately, or wait for inbound payload */
    }
}

zlink_socket_monitor_open_options_t opts = {
    .events = ZLINK_EVENT_CONNECTION_READY
};
void *mon = zlink_socket_monitor_open(stream_server, &opts);
zlink_socket_monitor_handler(mon, on_ready, NULL);
```

| Family | Wait for | Then you can |
|---|---|---|
| STREAM | `CONNECTION_READY` | send/recv using `ev->routing_id` |

### 11.3 Raw sockets — PUB/SUB

For internal perf on raw PUB/SUB, use `CONNECTION_READY` for each
expected client before messaging. Perf does not use delivery-ready
monitor events.

```c
zlink_set_subscription(sub, "topic");

/* SUB/PUB perf gate: wait for connection-ready */
zlink_socket_monitor_open_options_t sub_opts = {
    .events = ZLINK_EVENT_CONNECTION_READY
};
void *sub_mon = zlink_socket_monitor_open(sub, &sub_opts);

zlink_socket_monitor_open_options_t pub_opts = {
    .events = ZLINK_EVENT_CONNECTION_READY
};
void *pub_mon = zlink_socket_monitor_open(pub, &pub_opts);

/* Start after expected clients are connection-ready */
zlink_publish(pub, NULL, &part, 1, 0);  /* raw PUB: topic_id is NULL */
zlink_subscribe(sub, &source_rid, &parts, &count, topic_buf, &topic_len, 0);

zlink_monitor_close(&pub_mon);
zlink_monitor_close(&sub_mon);
```

| Family | Wait for | Then you can |
|---|---|---|
| PUB | `CONNECTION_READY` + expected client counting | `zlink_publish()` delivery |
| SUB | `CONNECTION_READY` + expected client counting | `zlink_subscribe()` recv |

### 11.4 Services — SPOT

SPOT does not expose a separate public monitor handle. For internal
perf on SPOT, use an explicit benchmark control barrier instead of
monitor events.

```c
/* SPOT perf gate: explicit READY/START barrier */
send_control_ready(client_id);
wait_ready_count(expected_clients);
broadcast_control_start();
```

| Service | Wait for | Then you can |
|---|---|---|
| SPOT sub | explicit `READY/START` barrier | start receiving via `zlink_subscribe()` |
| SPOT pub | explicit `READY/START` barrier | start delivering via `zlink_publish()` |

### 11.5 Snapshots

`zlink_monitor_status()` and `zlink_*_status()` return
a point-in-time view of the current state. Use them for dashboards,
health checks, and debugging.

Use current socket and SPOT monitor snapshots for core-level dashboards.

## 12. Poller API

The Poller API (`zlink_poller_*`) provides a unified event loop that multiplexes
zlink sockets, raw file descriptors, and timers in a single `wait` call. It is
the recommended approach for event-driven applications that manage multiple
sources.

### 12.1 Basic Usage

```c
void *poller = zlink_poller_new();
zlink_poller_event_t events[16];

/* add a socket (POLLIN) */
zlink_poller_add(poller, router, my_router_ctx, ZLINK_POLLIN);

/* add a raw fd */
zlink_poller_add_fd(poller, event_fd, my_fd_ctx, ZLINK_POLLIN);

/* add a timer */
void *timer = zlink_timer_new();
zlink_timer_start(timer, 1000000000, 0);  /* 1 s interval */
zlink_poller_add_timer(poller, timer, my_timer_ctx);

for (;;) {
    int n = zlink_poller_wait(poller, events, 16, -1, NULL);
    if (n < 0)
        break;

    for (int i = 0; i < n; i++) {
        zlink_poller_event_t *event = &events[i];
        switch (event->source_kind) {
        case ZLINK_POLLER_SOURCE_SOCKET:
            handle_socket(event->socket, event->user_data, event->events);
            break;
        case ZLINK_POLLER_SOURCE_FD:
            handle_fd(event->fd, event->user_data, event->events);
            break;
        case ZLINK_POLLER_SOURCE_TIMER:
            handle_timer(event->timer, event->user_data);
            break;
        }
    }
}

zlink_poller_destroy(&poller);
```

`zlink_poller_wait()` returns the number of events written, 0 on timeout,
and -1 on error (with `error_out` set). Pass `timeout` in milliseconds;
`-1` means block indefinitely, `0` means non-blocking. Allocate the event
buffer once and reuse it in the loop. Only `events[0:n]` is valid after each
wait.

### 12.2 Batch Wait

To drain multiple ready events without re-entering the poll loop:

```c
zlink_poller_event_t events[16];
int n = zlink_poller_wait(poller, events, 16, 0, NULL);
for (int i = 0; i < n; i++) {
    /* handle events[i] */
}
```

### 12.3 Modify and Remove

```c
/* change watched events on an existing socket */
zlink_poller_modify(poller, router, ZLINK_POLLIN | ZLINK_POLLOUT);

/* remove a socket */
zlink_poller_remove(poller, router);

/* remove an fd */
zlink_poller_remove_fd(poller, event_fd);

/* remove a timer */
zlink_poller_remove_timer(poller, timer);
```

### 12.4 `zlink_poller_event_t` Fields

| Field | Description |
|-------|-------------|
| `source_kind` | `ZLINK_POLLER_SOURCE_SOCKET`, `_FD`, or `_TIMER` |
| `socket` | Socket handle (valid when `source_kind == SOCKET`) |
| `fd` | File descriptor (valid when `source_kind == FD`) |
| `timer` | Timer handle (valid when `source_kind == TIMER`) |
| `user_data` | Pointer registered with `add` / `add_fd` / `add_timer` |
| `events` | Ready event flags (`ZLINK_POLLIN` / `ZLINK_POLLOUT`) |

### 12.5 Low-level `zlink_poll`

For simple one-shot polls without the Poller object, use `zlink_poll()`:

```c
zlink_pollitem_t items[2];
items[0].socket = router;
items[0].fd     = 0;
items[0].events = ZLINK_POLLIN;
items[1].socket = NULL;
items[1].fd     = pipe_fd;
items[1].events = ZLINK_POLLIN;

int n = zlink_poll(items, 2, 1000, NULL);
if (items[0].revents & ZLINK_POLLIN)
    /* router has data */;
```

`zlink_poll()` is lower-level and does not support timers. Prefer the Poller
API for production event loops.

---
[← TLS Security](05-tls-security.md) | [Services Overview →](07-0-services.md)


## Full language examples

=== "C++"

    ```cpp
    --8<-- "bindings/cpp/samples/monitor_recv_sample.cpp:doc"
    ```

=== "C#/.NET"

    ```csharp
    --8<-- "bindings/dotnet/samples/MonitorRecv/Program.cs:doc"
    ```

=== "Java"

    ```java
    --8<-- "bindings/java/samples/Zlink.Samples/src/main/java/systems/zlink/samples/MonitorRecvSample.java:doc"
    ```

=== "Kotlin"

    ```kotlin
    --8<-- "bindings/kotlin/samples/src/main/kotlin/systems/zlink/samples/MonitorRecvSample.kt:doc"
    ```

=== "Python"

    ```python
    --8<-- "bindings/python/samples/monitor_recv_sample.py:doc"
    ```

=== "Node/TypeScript"

    ```typescript
    --8<-- "bindings/node/samples/monitor_recv_sample.ts:doc"
    ```

=== "JavaScript"

    ```javascript
    --8<-- "bindings/javascript/samples/monitor_recv_sample.js:doc"
    ```

=== "Go"

    ```go
    --8<-- "bindings/go/samples/monitor_recv_sample/main.go:doc"
    ```

=== "Rust"

    ```rust
    --8<-- "bindings/rust/samples/monitor_recv_sample.rs:doc"
    ```

---
<!-- zlink-nav:bottom:start -->
[← TLS/Security](05-tls-security.md) | [Services →](07-0-services.md)
<!-- zlink-nav:bottom:end -->
