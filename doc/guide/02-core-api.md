English | [한국어](02-core-api.ko.md)

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
zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);     /* default 1; 4 is optimal under heavy load */

/* Query */
int io_threads = zlink_ctx_get(ctx, ZLINK_IO_THREADS);

/* Terminate */
zlink_ctx_term(ctx);  /* Returns after all sockets are closed */
```

### Context Options

| Option | Default | Description |
|--------|---------|-------------|
| `ZLINK_IO_THREADS` | 1 | Number of I/O threads |
| `ZLINK_MAX_SOCKETS` | 4095 | Maximum number of sockets |
| `ZLINK_MAX_MSGSZ` | -1 | Maximum message size (-1: unlimited) |

## 2. Socket API

Public socket handle APIs are thread-safe by default. Multiple threads
can share the same socket handle to call send/recv/bind/connect, etc.

> For detailed threading rules, see [Thread-Safety Guide](11-thread-safety.md).

### 2.1 Socket Creation and Closing

```c
void *socket = zlink_socket(ctx, ZLINK_DEALER);
/* ... use ... */
zlink_close(socket);
```

### 2.2 Socket Type Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_PAIR` | 0x1001 | 1:1 Bidirectional |
| `ZLINK_PUB` | 0x1002 | Publisher |
| `ZLINK_SUB` | 0x1003 | Subscriber |
| `ZLINK_DEALER` | 0x1004 | Asynchronous request |
| `ZLINK_ROUTER` | 0x1005 | Routing |
| `ZLINK_XPUB` | 0x1006 | Advanced publisher |
| `ZLINK_XSUB` | 0x1007 | Advanced subscriber |
| `ZLINK_STREAM` | 0x1008 | RAW communication |

### 2.3 Connection Management

```c
/* Bind (server) */
zlink_bind(socket, "tcp://*:5555");

/* Connect (client) */
zlink_connect(socket, "tcp://127.0.0.1:5555");

/* Unbind */
zlink_unbind(socket, "tcp://*:5555");
zlink_disconnect(socket, "tcp://127.0.0.1:5555");
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
| `ZLINK_OPT_SNDHWM` | int | 1000 | Send High Water Mark |
| `ZLINK_OPT_RCVHWM` | int | 1000 | Receive High Water Mark |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | Send timeout (ms, -1: unlimited) |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | Receive timeout (ms, -1: unlimited) |
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
Pass `ZLINK_DONTWAIT` to return `EAGAIN` immediately instead of blocking.
For advanced backpressure patterns, see
[Performance Guide](10-performance.md).

#### Logical Multipart Send

Multipart sends via `zlink_send()`,
`zlink_publish()`, and other public/service surfaces internally use a
shared **logical multipart send** module. This module provides the
following common guarantees:

- **nonblocking**: one-shot attempt with partial local state rollback on failure
- **blocking**: whole-message retry until the `sndtimeo` deadline
- **retry targets**: only `EAGAIN` and `EINTR` are retried; other errors fail immediately
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
void *socket = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(socket, "tcp://*:5556");

/* Blocking recv */
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
int rc = zlink_recv(socket, &source_rid, &parts, &part_count, 0);
if (rc == 0) {
    for (size_t i = 0; i < part_count; i++) {
        printf("Frame %zu: %.*s\n", i,
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}

/* Non-blocking recv */
rc = zlink_recv(socket, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
if (rc == -1 && zlink_errno() == EAGAIN) {
    /* No message available right now */
}
```

#### Callback Mode

Attach a handler callback after socket creation. Messages are dispatched
asynchronously on the I/O thread. Once attached, the handler cannot be
removed for the lifetime of the socket. If a handler has been attached,
`zlink_recv()` fails with `EBUSY`.

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

void *socket = zlink_socket(ctx, ZLINK_STREAM);
zlink_recv_handler(socket, on_message, NULL);
```

> For a comparison of the two modes and advanced patterns, see
> [Performance Guide](10-performance.md).

### 3.3 Send Flags

| Flag | Description |
|------|-------------|
| `ZLINK_DONTWAIT` | Non-blocking mode (returns EAGAIN immediately if cannot send/recv) |

## 4. Handler Types

Each socket type uses a dedicated registration function:

| Socket Type | Registration Call | Callback Signature |
|---|---|---|
| STREAM | `zlink_recv_handler(socket, fn, userdata)` | `void fn(const zlink_routing_id_t *rid, zlink_msg_t *parts, size_t count, void *userdata)` |
| ROUTER (request-reply) | `zlink_router_handler(router, fn, userdata)` | `void fn(const zlink_routing_id_t *peer_rid, uint64_t request_seq, zlink_msg_t *parts, size_t count, void *userdata)` |
| SPOT (routed) | `zlink_spot_handler(spot, fn, userdata)` | `void fn(const zlink_routing_id_t *source_rid, const zlink_routing_id_t *spot_rid, uint64_t request_seq, zlink_msg_t *parts, size_t count, void *userdata)` |
| ROUTER (from SPOT) | `zlink_router_spot_handler(router, fn, userdata)` | `void fn(const zlink_routing_id_t *source_node_rid, const zlink_routing_id_t *source_spot_rid, uint64_t request_seq, zlink_msg_t *parts, size_t count, void *userdata)` |
| spot, spot_node (topic) | `zlink_subscribe_handler(socket, fn, userdata)` | `void fn(const zlink_routing_id_t *rid, const char *topic, size_t topic_len, zlink_msg_t *parts, size_t count, void *userdata)` |
| DEALER (reply) | `zlink_reply_handler_fn` passed to `zlink_dealer_request()` | `void fn(int errno, zlink_msg_t *parts, size_t count, void *userdata)` |
| Timer | `zlink_timer_handler(timer, fn, userdata)` | `void fn(void *timer, uint64_t fire_count, void *userdata)` |
| PUB | N/A | Send-only socket |

Callbacks are invoked on the I/O thread. Avoid blocking work inside callbacks.
If slow processing is needed, enqueue to a user queue and handle it on a
separate thread.

## 5. Error Handling

```c
zlink_msg_t part;
zlink_msg_init_size(&part, size);
memcpy(zlink_msg_data(&part), data, size);
int rc = zlink_send(socket, &part, 1, 0);
if (rc == -1) {
    int err = zlink_errno();
    printf("Error: %s\n", zlink_strerror(err));
}
```

Key error codes:

| Error | Description |
|-------|-------------|
| `EAGAIN` | Cannot complete immediately in non-blocking mode |
| `EBUSY` | recv/callback mode conflict (handler already attached) |
| `ETERM` | Context has been terminated |
| `ENOTSOCK` | Invalid socket |
| `EINTR` | Interrupted by signal |
| `EFSM` | Operation not allowed in current state |
| `EHOSTUNREACH` | Host unreachable |
| `ENOTSUP` | Operation not supported on this subject type |
| `ENOENT` | Target not found (e.g. SPOT destination unknown) |

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
int rc = zlink_timer_recv(timer, &fire_count);

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

SPOT timers use the SpotNode-local shared scheduler instead of the global one.

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
| recv vs callback | Conflicts return `EBUSY` (same as sockets) |
| General timer | Uses global shared scheduler |
| SPOT timer | Uses SpotNode-local shared scheduler |

## 7. DEALER/ROUTER Example

```c
#include <zlink.h>
#include <string.h>
#include <stdio.h>

void on_router_message(const zlink_routing_id_t *source_rid,
                       zlink_msg_t *parts, size_t part_count,
                       void *userdata)
{
    printf("Received from [%.*s]: %.*s\n",
           (int)source_rid->size, source_rid->data,
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

void on_dealer_message(const zlink_routing_id_t *source_rid,
                       zlink_msg_t *parts, size_t part_count,
                       void *userdata)
{
    printf("Reply: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

int main(void) {
    void *ctx = zlink_ctx_new();

    /* ROUTER (server) */
    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    /* Receive with zlink_recv() */
    zlink_bind(router, "tcp://*:5555");

    /* DEALER (client) */
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);
    /* Receive with zlink_recv() */
    zlink_connect(dealer, "tcp://127.0.0.1:5555");

    /* DEALER → ROUTER */
    zlink_msg_t req;
    zlink_msg_init_size(&req, 7);
    memcpy(zlink_msg_data(&req), "request", 7);
    zlink_send(dealer, &req, 1, 0);

    /* Handler callbacks process messages asynchronously */
    msleep(100);

    zlink_close(dealer);
    zlink_close(router);
    zlink_ctx_term(ctx);
    return 0;
}
```

---
[← Overview](01-overview.md) | [Socket Patterns →](03-0-socket-patterns.md)
