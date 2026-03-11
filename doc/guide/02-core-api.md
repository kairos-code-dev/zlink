English | [한국어](02-core-api.ko.md)

# Core C API Detailed Guide

## 1. Context API

A Context is the top-level object in zlink that manages the I/O thread pool and sockets.

```c
/* Create */
void *ctx = zlink_ctx_new();

/* Configure */
zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);     /* Number of I/O threads (default 2) */
zlink_ctx_set(ctx, ZLINK_MAX_SOCKETS, 2048); /* Max sockets (default 1023) */

/* Query */
int io_threads = zlink_ctx_get(ctx, ZLINK_IO_THREADS);

/* Terminate */
zlink_ctx_term(ctx);  /* Returns after all sockets are closed */
```

### Context Options

| Option | Default | Description |
|--------|---------|-------------|
| `ZLINK_IO_THREADS` | 2 | Number of I/O threads |
| `ZLINK_MAX_SOCKETS` | 1023 | Maximum number of sockets |
| `ZLINK_MAX_MSGSZ` | -1 | Maximum message size (-1: unlimited) |

## 2. Socket API

### 2.1 Socket Creation and Closing

```c
void *socket = zlink_socket(ctx, ZLINK_DEALER, NULL);
/* ... use ... */
zlink_close(socket);
```

### 2.2 Socket Type Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_PAIR` | 0 | 1:1 Bidirectional |
| `ZLINK_PUB` | 1 | Publisher |
| `ZLINK_SUB` | 2 | Subscriber |
| `ZLINK_DEALER` | 5 | Asynchronous request |
| `ZLINK_ROUTER` | 6 | Routing |
| `ZLINK_XPUB` | 9 | Advanced publisher |
| `ZLINK_XSUB` | 10 | Advanced subscriber |
| `ZLINK_STREAM` | 11 | RAW communication |

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
| `ZLINK_SNDHWM` | int | 300000 | Send High Water Mark |
| `ZLINK_RCVHWM` | int | 300000 | Receive High Water Mark |
| `ZLINK_SNDTIMEO` | int | -1 | Send timeout (ms, -1: unlimited) |
| `ZLINK_RCVTIMEO` | int | -1 | Receive timeout (ms, -1: unlimited) |
| `ZLINK_LINGER` | int | -1 | Wait time on socket close (ms) |
| `ZLINK_ROUTING_ID` | binary | auto | Socket routing ID |
| `ZLINK_SUBSCRIBE` | binary | - | Subscription filter (SUB only) |

## 3. Sending and Receiving Messages

### 3.1 Sending

```c
/* Simple send */
zlink_send(socket, "Hello", 5, 0);

/* Multipart send */
zlink_send(socket, "header", 6, ZLINK_SNDMORE);
zlink_send(socket, "body", 4, 0);
```

### 3.2 Receiving (Callback Handler)

All receives are dispatched through a handler callback registered at socket creation time.
There is no `recv()` function — the callback is invoked asynchronously when a message arrives.

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count)
{
    for (size_t i = 0; i < part_count; i++) {
        printf("Frame %zu: %.*s\n", i,
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}

zlink_socket_handler_t handler = {
    .kind = ZLINK_SOCKET_HANDLER_MSG,
    .fn.msg = on_message
};
void *socket = zlink_socket(ctx, ZLINK_PAIR, &handler);
```

### 3.3 Send Flags

| Flag | Description |
|------|-------------|
| `ZLINK_DONTWAIT` | Non-blocking mode (returns EAGAIN immediately if cannot send) |
| `ZLINK_SNDMORE` | Intermediate frame of a multipart message |

## 4. Handler Types

Each socket type accepts a specific handler kind at creation time:

| Socket Type | Handler Kind | Callback Signature |
|---|---|---|
| PAIR, DEALER, ROUTER | `ZLINK_SOCKET_HANDLER_MSG` | `void fn(const zlink_routing_id_t *rid, zlink_msg_t *parts, size_t count)` |
| SUB | `ZLINK_SOCKET_HANDLER_SPOT` | `void fn(const zlink_routing_id_t *rid, const char *topic, size_t topic_len, zlink_msg_t *parts, size_t count)` |
| XPUB | `ZLINK_SOCKET_HANDLER_XPUB` | `void fn(int subscribed, const uint8_t *topic, size_t topic_len)` |
| PUB, XSUB | N/A (NULL) | Send-only sockets |

Callbacks are invoked on the I/O thread. Avoid blocking work inside callbacks — if slow processing is needed, enqueue to a user queue and handle on a separate thread.

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
                       zlink_msg_t *parts, size_t part_count)
{
    printf("Received from [%.*s]: %.*s\n",
           (int)source_rid->size, source_rid->data,
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

void on_dealer_message(const zlink_routing_id_t *source_rid,
                       zlink_msg_t *parts, size_t part_count)
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
    zlink_socket_handler_t router_handler = {
        .kind = ZLINK_SOCKET_HANDLER_MSG,
        .fn.msg = on_router_message
    };
    void *router = zlink_socket(ctx, ZLINK_ROUTER, &router_handler);
    zlink_bind(router, "tcp://*:5555");

    /* DEALER (client) */
    zlink_socket_handler_t dealer_handler = {
        .kind = ZLINK_SOCKET_HANDLER_MSG,
        .fn.msg = on_dealer_message
    };
    void *dealer = zlink_socket(ctx, ZLINK_DEALER, &dealer_handler);
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
