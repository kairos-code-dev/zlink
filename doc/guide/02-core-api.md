English | [한국어](02-core-api.ko.md)

# Core C API Detailed Guide

## 1. Context API

A Context is the top-level object in zlink that manages the I/O thread pool and sockets.

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
zlink_setsockopt(socket, ZLINK_SNDHWM, &hwm, sizeof(hwm));

/* Get option */
int value;
size_t len = sizeof(value);
zlink_getsockopt(socket, ZLINK_SNDHWM, &value, &len);
```

Key options:

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `ZLINK_SNDHWM` | int | 1000 | Send High Water Mark |
| `ZLINK_RCVHWM` | int | 1000 | Receive High Water Mark |
| `ZLINK_SNDTIMEO` | int | -1 | Send timeout (ms, -1: unlimited) |
| `ZLINK_RCVTIMEO` | int | -1 | Receive timeout (ms, -1: unlimited) |
| `ZLINK_LINGER` | int | -1 | Wait time on socket close (ms) |
| `ZLINK_ROUTING_ID` | binary | auto | Socket routing ID |
| `ZLINK_SUBSCRIBE` | binary | - | Subscription filter (SUB only) |

Options and queries such as `ZLINK_SUBSCRIBE` / `ZLINK_UNSUBSCRIBE`,
`ZLINK_EVENTS`, `ZLINK_LAST_ENDPOINT`, and `ZLINK_RCVMORE` are meaningful
during normal runtime use. By contrast, most tuning knobs such as HWM,
timeouts, and TLS settings are usually closer to initial configuration.

## 3. Sending and Receiving Messages

### 3.1 Sending

```c
/* Simple send */
zlink_send(socket, "Hello", 5, 0);

/* Multipart send */
zlink_send(socket, "header", 6, ZLINK_SNDMORE);
zlink_send(socket, "body", 4, 0);
```

By default `zlink_send()` blocks when the send queue is full (HWM reached).
Pass `ZLINK_DONTWAIT` to return `EAGAIN` immediately instead of blocking.
For advanced backpressure patterns, see
[Performance Guide](10-performance.md).

### 3.2 Receiving

zlink sockets support two receive modes:

#### Pull Mode (Synchronous)

Without attaching a handler, call `zlink_recv()` to receive messages
directly. Sockets start in pull mode by default.

```c
void *socket = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(socket, "tcp://*:5556");

/* Blocking recv */
char buf[256];
int nbytes = zlink_recv(socket, buf, sizeof(buf), 0);

/* Non-blocking recv */
nbytes = zlink_recv(socket, buf, sizeof(buf), ZLINK_DONTWAIT);
if (nbytes == -1 && zlink_errno() == EAGAIN) {
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

void *socket = zlink_socket(ctx, ZLINK_PAIR);
zlink_recv_handler(socket, on_message, NULL);
```

> For a comparison of the two modes and advanced patterns, see
> [Performance Guide](10-performance.md).

### 3.3 Send Flags

| Flag | Description |
|------|-------------|
| `ZLINK_DONTWAIT` | Non-blocking mode (returns EAGAIN immediately if cannot send/recv) |
| `ZLINK_SNDMORE` | Intermediate frame of a multipart message |

## 4. Handler Types

Each socket type uses a dedicated registration function:

| Socket Type | Registration Call | Callback Signature |
|---|---|---|
| PAIR, DEALER, ROUTER, STREAM | `zlink_recv_handler(socket, fn, userdata)` | `void fn(const zlink_routing_id_t *rid, zlink_msg_t *parts, size_t count, void *userdata)` |
| SUB, XSUB | `zlink_recv_spot_handler(socket, fn, userdata)` | `void fn(const zlink_routing_id_t *rid, const char *topic, size_t topic_len, zlink_msg_t *parts, size_t count, void *userdata)` |
| XPUB | `zlink_recv_xpub_handler(socket, fn, userdata)` | `void fn(int subscribed, const uint8_t *topic, size_t topic_len, void *userdata)` |
| PUB | N/A | Send-only socket |

Callbacks are invoked on the I/O thread. Avoid blocking work inside callbacks.
If slow processing is needed, enqueue to a user queue and handle it on a
separate thread.

## 5. Error Handling

```c
int rc = zlink_send(socket, data, size, 0);
if (rc == -1) {
    int err = zlink_errno();
    printf("Error: %s\n", zlink_strerror(err));
}
```

Key error codes:

| Error | Description |
|-------|-------------|
| `EAGAIN` | Cannot complete immediately in non-blocking mode |
| `ETERM` | Context has been terminated |
| `ENOTSOCK` | Invalid socket |
| `EINTR` | Interrupted by signal |
| `EFSM` | Operation not allowed in current state |
| `EHOSTUNREACH` | Host unreachable |

## 6. DEALER/ROUTER Example

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
    zlink_recv_handler(router, on_router_message, NULL);
    zlink_bind(router, "tcp://*:5555");

    /* DEALER (client) */
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);
    zlink_recv_handler(dealer, on_dealer_message, NULL);
    zlink_connect(dealer, "tcp://127.0.0.1:5555");

    /* DEALER → ROUTER */
    zlink_send(dealer, "request", 7, 0);

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
