English | [한국어](02-core-api.ko.md)

<!-- zlink-nav:start -->
[← Overview](01-overview.md) | [Socket Patterns →](03-0-socket-patterns.md)
<!-- zlink-nav:end -->

# Core C API Detailed Guide

## 1. Context API

A Context is the top-level object in zlink that manages the I/O thread pool and sockets.

> For a deep dive into what I/O threads do internally (event loop,
> command processing, socket assignment), see
> [I/O Thread Internals](../internals/io-thread.md).

```c
/* Create */
void *ctx = zlink_ctx_new();

/* Configure — increase I/O threads for multi-connection servers */
zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);     /* default 4 */

/* Query (error_out receives ZLINK_CONFIG_OK on success) */
zlink_config_result_t err;
int io_threads = zlink_ctx_get(ctx, ZLINK_IO_THREADS, &err);

/* Optionally interrupt blocking calls without releasing resources */
zlink_ctx_shutdown(ctx);  /* wakes blocked zlink_send/recv; term still required */

/* Terminate */
zlink_ctx_term(ctx);  /* Returns after all sockets are closed */
```

### Context Options

| Option | Default | Description |
|--------|---------|-------------|
| `ZLINK_IO_THREADS` | `4` | Number of I/O threads |
| `ZLINK_MAX_SOCKETS` | `4095` | Maximum number of sockets |
| `ZLINK_SOCKET_LIMIT` | — | Read-only: actual socket limit |
| `ZLINK_THREAD_PRIORITY` | `-1` | OS thread priority for I/O threads |
| `ZLINK_THREAD_SCHED_POLICY` | `-1` | OS scheduling policy for I/O threads |
| `ZLINK_MAX_MSGSZ` | `INT_MAX` | Maximum message size in bytes (default `INT_MAX`, effectively unlimited; set accepts non-negative values) |
| `ZLINK_MSG_T_SIZE` | — | Read-only: `sizeof(zlink_msg_t)` |
| `ZLINK_THREAD_AFFINITY_CPU_ADD` | — | Add CPU index to I/O thread affinity set |
| `ZLINK_THREAD_AFFINITY_CPU_REMOVE` | — | Remove CPU index from I/O thread affinity set |
| `ZLINK_THREAD_NAME_PREFIX` | — | Name prefix for I/O threads (set via `zlink_ctx_set_data`) |
| `ZLINK_CTX_OPT_BLOCKY` | `1` | Legacy: block on context termination |
| `ZLINK_CTX_OPT_AUTO_HWM_ENABLE` | `1` | Enable automatic HWM sizing |
| `ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS` | `3000` | Debounce interval for auto HWM recalculation (ms) |
| `ZLINK_CTX_OPT_AUTO_HWM_PROFILE` | `ZLINK_AUTO_HWM_PROFILE_BALANCED` | Auto HWM sizing profile |
| `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES` | `0` | Per-message byte unit used for auto-HWM planning (`0`: runtime default) |

Use `zlink_ctx_set_data()` (instead of `zlink_ctx_set()`) for
`ZLINK_THREAD_NAME_PREFIX`, which takes a string rather than an `int`.

`zlink_ctx_auto_hwm_recalculate()` triggers an immediate auto HWM refresh for
all sockets in the context — useful after changing the profile or per-socket
message-unit size without waiting for the normal debounce interval.

## 2. Socket API

Public socket handle APIs are thread-safe by default. Multiple threads
can share the same socket handle to call send/recv/bind/connect, etc.

> For detailed threading rules, see [Thread-Safety Guide](11-thread-safety.md).

### 2.1 Socket Creation and Closing

```c
void *socket = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
/* ... use ... */
zlink_close(socket);
```

### 2.2 Socket Type Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_SOCKET_PAIR` | 0x1001 | 1:1 Bidirectional |
| `ZLINK_SOCKET_PUB` | 0x1002 | Publisher |
| `ZLINK_SOCKET_SUB` | 0x1003 | Subscriber |
| `ZLINK_SOCKET_DEALER` | 0x1004 | Asynchronous request |
| `ZLINK_SOCKET_ROUTER` | 0x1005 | Routing |
| `ZLINK_SOCKET_XPUB` | 0x1006 | Advanced publisher |
| `ZLINK_SOCKET_XSUB` | 0x1007 | Advanced subscriber |
| `ZLINK_SOCKET_STREAM` | 0x1008 | RAW communication |

### 2.3 Connection Management

```c
/* Bind (server) */
zlink_bind(socket, "tcp://*:5555");

/* Connect (client) */
zlink_connect(socket, "tcp://127.0.0.1:5555");

/* Unbind */
zlink_unbind(socket, "tcp://*:5555");
zlink_disconnect(socket, "tcp://127.0.0.1:5555");

/* Disconnect a peer by its routing id (ROUTER sockets) */
zlink_routing_id_t peer_rid = /* ... */;
zlink_disconnect_rid(socket, &peer_rid);
```

### 2.4 Socket Options

```c
/* Set option */
int hwm = 5000;
zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

/* Get option */
int value;
size_t len = sizeof(value);
zlink_get_option(socket, ZLINK_OPT_SNDHWM, &value, &len);
```

Key options:

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `ZLINK_OPT_SNDHWM` | int | automatic | Derived from the active auto-HWM profile (`balanced` by default), socket role, and message unit. Set `ZLINK_CTX_OPT_AUTO_HWM_PROFILE` on the context to change the profile globally; see [Socket Options Guide](12-socket-options.md) for profile values and per-socket overrides |
| `ZLINK_OPT_RCVHWM` | int | automatic | Same as `SNDHWM`: profile-driven unless overridden manually |
| `ZLINK_OPT_SNDTIMEO` | int | 1000 | Send timeout (ms). Set `-1` explicitly for unlimited wait |
| `ZLINK_OPT_RCVTIMEO` | int | 1000 | Receive timeout (ms). Set `-1` explicitly for unlimited wait |
| `ZLINK_OPT_LINGER` | int | -1 | Wait time on socket close (ms) |

Routing ID is now set/queried via dedicated functions:
`zlink_set_routing_id()` / `zlink_get_routing_id()`.
Subscription management uses `zlink_set_subscription()`.

Options and queries such as `ZLINK_OPT_EVENTS` and
`ZLINK_OPT_LAST_ENDPOINT` are meaningful during normal runtime use.
By contrast, most tuning knobs such as HWM, timeouts, and TLS settings
are usually closer to initial configuration.

For detailed option categories and the full option reference, see
[Socket Options Guide](12-socket-options.md).

## 3. Sending and Receiving Messages

### 3.1 Sending

```c
/* Simple send */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "Hello", 5);
zlink_send(socket, &part, 1, 0);

/* Multipart send */
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);
zlink_send(socket, parts, 2, 0);
```

By default `zlink_send()` blocks when the send queue is full (HWM reached).
Pass `ZLINK_DONTWAIT` to return `ZLINK_SUBMIT_BACKPRESSURED` immediately
instead of blocking.
For advanced backpressure patterns, see
[Performance Guide](10-performance.md).

#### Logical Multipart Send

Multipart sends via `zlink_send()`,
`zlink_publish()`, and other public/service surfaces internally use a
shared **logical multipart send** module. This module provides the
following common guarantees:

- **nonblocking**: one-shot attempt with partial local state rollback on failure
- **blocking**: whole-message retry until the `sndtimeo` deadline
- **retry targets**: only `ZLINK_SUBMIT_BACKPRESSURED` and `EINTR` are retried; other results fail immediately
- **whole-message guarantee**: a multipart message either succeeds entirely or fails entirely

This guarantees that a multipart message either succeeds entirely or
fails entirely -- no partial messages are ever queued.

> For wire-level frame structure, see
> [ZMP Protocol](../internals/protocol-zmp.md).

### 3.2 Receiving

zlink sockets support two receive modes:

#### Pull Mode (Synchronous)

Without attaching a handler, call `zlink_recv()` to receive messages
directly. Sockets start in pull mode by default.

```c
void *socket = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
zlink_bind(socket, "tcp://*:5556");

/* Blocking recv */
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
zlink_recv_result_t rc = zlink_recv(
    socket, &source_rid, &parts, &part_count, 0 /* flags */);
if (rc == ZLINK_RECV_OK) {
    for (size_t i = 0; i < part_count; i++) {
        printf("Frame %zu: %.*s\n", i,
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}

/* Non-blocking recv */
rc = zlink_recv(
    socket, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
if (rc == ZLINK_RECV_NO_DATA) {
    /* No message available right now */
}
```

#### Callback Mode

Callback receive (`zlink_recv_handler`) is supported only on raw `STREAM`
sockets; attaching it to any other socket type returns
`ZLINK_HANDLER_NOT_SUPPORTED`. Attach the handler after socket creation;
messages are dispatched asynchronously on the I/O thread. Once attached, the
handler cannot be removed for the lifetime of the socket. If a handler has
been attached, `zlink_recv()` returns `ZLINK_RECV_BUSY`.

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; i++) {
        printf("Frame %zu: %.*s\n", i,
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}

void *socket = zlink_socket(ctx, ZLINK_SOCKET_STREAM);
zlink_recv_handler(socket, on_message, NULL);
```

> For a comparison of the two modes and advanced patterns, see
> [Performance Guide](10-performance.md).

### 3.3 Send Flags

| Flag | Description |
|------|-------------|
| `ZLINK_DONTWAIT` | Non-blocking mode (returns `BACKPRESSURED` / `NO_DATA` immediately if cannot send/recv) |

## 4. Handler Types

There are two fundamental receive models in zlink:

- **Pull mode (default)**: read data by calling the relevant recv function
  (`zlink_recv()`, `zlink_spot_recv()`, etc.) explicitly, typically from inside
  a poller loop. The caller controls which thread runs the read and when.
- **Callback mode**: register a handler with a `*_handler()` call. The I/O
  thread delivers messages directly to the callback. Once a handler is attached
  it cannot be removed for the socket's lifetime. Calling `zlink_recv()` on a
  socket that already has a callback handler attached returns `ZLINK_RECV_BUSY`.

Most socket types support only one of these models. SPOT and STREAM are
exceptions:

- **MeshNode/Spot/Actor** use `zlink_mesh_node_set_ready_handler()` (or poller registration) as the unified readiness signal. Records are drained through ready/claim/receive batches after the wakeup.
- **STREAM** accepts one of three receive modes (`zlink_recv()`, raw callback, or
  packet callback) and locks once one is activated.

Each socket type uses a dedicated registration function:

| Socket Type | Registration Call | Callback Signature |
|---|---|---|
| STREAM (raw) | `zlink_recv_handler(socket, fn, userdata)` | `void fn(const zlink_routing_id_t *rid, zlink_msg_t *parts, size_t count, void *userdata)` |
| STREAM (packet) | `zlink_stream_packet_handler(socket, fn, userdata)` | `void fn(void *stream, const zlink_routing_id_t *source_rid, zlink_msg_t *header, zlink_msg_t *body, void *userdata)` |
| ROUTER (routed) | recv-only — `zlink_router_recv()` | N/A. `zlink_router_request()` reply is delivered through a separate completion callback |
| MeshNode (ready wakeup) | `zlink_mesh_node_set_ready_handler(node, fn, userdata)` — wakeup-only readiness callback; records are drained with claim receive batches | `zlink_mesh_ready_domain_mask_t fn(void *node, zlink_mesh_ready_domain_mask_t mask, void *userdata)` |
| SPOT (service-aware subscribe recv) | `zlink_spot_subscribe(spot, ..., service_name_out, topic_id_out, ...)` | N/A — recv-driven; drained after a `SUBSCRIBE_READABLE` dispatch event |
| SPOT (service-aware routed recv) | `zlink_spot_recv(spot, ...)` | N/A — recv-driven; drained after a `ROUTED_READABLE` dispatch event |
| PAIR / DEALER / SUB / XSUB | recv-only — `zlink_recv()` or `zlink_subscribe()` | N/A |
| DEALER / ROUTER request | `zlink_reply_handler_fn` passed to `zlink_dealer_request()` / `zlink_router_request()` | `void fn(zlink_request_result_t result, zlink_msg_t *parts, size_t count, void *userdata)` |
| Timer | `zlink_timer_handler(timer, fn, userdata)` | `void fn(void *timer, uint64_t fire_count, void *userdata)` |
| PUB | N/A | Send-only socket |

Callbacks are invoked on the I/O thread. Avoid blocking work inside callbacks;
enqueue to a user-owned queue and process on a separate thread if needed.

## 5. Error Handling

zlink's public C API uses **function-specific typed result enums**. Each
function returns a `zlink_<category>_result_t` enum where `0` is the
`OK` value and non-zero values identify specific failure modes. The
canonical enum values are defined in
[core/errno-map.md](../spec/core/04-errno-map.md).

The 8 result enum categories:

| Enum | Applies to |
|------|-----------|
| `zlink_submit_result_t` | send / publish / request submit / reply submit |
| `zlink_request_result_t` | request completion (callback) |
| `zlink_recv_result_t` | recv / subscribe / monitor recv / timer recv |
| `zlink_handler_result_t` | handler registration (`zlink_*_handler()`) |
| `zlink_close_result_t` | close / destroy |
| `zlink_bind_result_t` | bind |
| `zlink_connect_result_t` | connect / disconnect / unbind |
| `zlink_config_result_t` | option set/get, snapshot, poller mutation, message lifecycle, timer config |

```c
zlink_msg_t part;
zlink_msg_init_size(&part, size);
memcpy(zlink_msg_data(&part), data, size);
zlink_submit_result_t rc = zlink_send(socket, &part, 1, 0);
if (rc != ZLINK_SUBMIT_OK) {
    /* typical values: BACKPRESSURED, NOT_CONNECTED, NOT_FOUND,
       TERMINATED, INVALID_HANDLE, INVALID_ARGUMENT, NOT_SUPPORTED,
       INVALID_STATE, THREAD_VIOLATION, OUT_OF_MEMORY, INTERNAL_ERROR */
    printf("send failed: %d\n", (int)rc);
    if (rc == ZLINK_SUBMIT_INTERNAL_ERROR) {
        /* INTERNAL_ERROR aggregates rarer failures;
           zlink_errno() surfaces the underlying reason */
        int internal = zlink_errno();
        printf("  internal errno: %s\n", zlink_strerror(internal));
    }
}
```

**`zlink_errno()` is for `INTERNAL_ERROR` detail only.** Other result
codes are self-descriptive and do not require `zlink_errno()` lookup.

Language bindings surface this 8-category structure as typed
exception/error subclasses — see
[bindings Per-Function Error Type Hierarchy](../../../bindings/doc/spec/README.md).

## 6. Timer API

Timers are first-class event sources, like sockets. They support the same
recv/callback/poller model.

### 6.1 General Timer

```c
void *timer = zlink_timer_new();

/* Start: 100ms interval, repeat indefinitely (0 = infinite) */
zlink_timer_start(timer, 100000000ULL, 0);  /* interval_ns, repeat_count */

/* Pull mode */
uint64_t fire_count;
zlink_recv_result_t rc = zlink_timer_recv(timer, &fire_count);
/* rc values: ZLINK_RECV_OK, NO_DATA (no queued fire), TERMINATED,
   INVALID_HANDLE, NOT_SUPPORTED */

/* Callback mode */
void on_fire(void *timer, uint64_t fire_count, void *userdata) {
    /* handle timer event */
}
zlink_timer_handler(timer, on_fire, NULL);

/* Stop and destroy */
zlink_timer_stop(timer);
zlink_timer_destroy(&timer);
```

### 6.2 SPOT Timer

Spot timers use the same scheduling machinery as plain timers but are bound to the owning Spot's generation: once the Spot is destroyed or moved, ticks stop.

```c
void *spot_timer = zlink_spot_timer_new(spot);
zlink_timer_start(spot_timer, 50000000ULL, 10);  /* 50ms, 10 repetitions */
```

### 6.3 Poller Integration

Timers can be added to a poller alongside sockets and file descriptors.

```c
zlink_poller_add_timer(poller, timer, user_data);
/* ... zlink_poller_wait() returns timer events ... */
zlink_poller_remove_timer(poller, timer);
```

### Key Rules

| Rule | Description |
|------|-------------|
| `repeat_count=0` | Infinite repetition |
| `repeat_count=N` | Fires exactly N times then stops |
| recv vs callback | Conflicts return `ZLINK_RECV_BUSY` / `ZLINK_HANDLER_BUSY` (same as sockets) |
| General timer | Uses global shared scheduler |
| SPOT timer | Bound to the Spot generation — no ticks after it ends |

## 7. DEALER/ROUTER Example

```c
#include <zlink.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    void *ctx = zlink_ctx_new();

    /* ROUTER (server) */
    void *router = zlink_socket(ctx, ZLINK_SOCKET_ROUTER);
    zlink_bind(router, "tcp://*:5555");

    /* DEALER (client) */
    void *dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
    zlink_connect(dealer, "tcp://127.0.0.1:5555");

    /* DEALER → ROUTER */
    zlink_msg_t req;
    zlink_msg_init_size(&req, 7);
    memcpy(zlink_msg_data(&req), "request", 7);
    zlink_send(dealer, &req, 1, 0);

    /* Server loop: watch a poller for ZLINK_POLLIN on router, drain with
       zlink_router_recv(), and reply with zlink_router_reply() or
       zlink_send_rid(). The client drains replies with zlink_recv() in
       its own poller loop. */

    zlink_close(dealer);
    zlink_close(router);
    zlink_ctx_term(ctx);
    return 0;
}
```

---
<!-- zlink-nav:bottom:start -->
[← Overview](01-overview.md) | [Socket Patterns →](03-0-socket-patterns.md)
<!-- zlink-nav:bottom:end -->
