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
void *socket = zlink_socket(ctx, ZLINK_DEALER);
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
| `ZLINK_SNDHWM` | int | 1000 | Send High Water Mark |
| `ZLINK_RCVHWM` | int | 1000 | Receive High Water Mark |
| `ZLINK_SNDTIMEO` | int | -1 | Send timeout (ms, -1: unlimited) |
| `ZLINK_RCVTIMEO` | int | -1 | Receive timeout (ms, -1: unlimited) |
| `ZLINK_LINGER` | int | -1 | Wait time on socket close (ms) |
| `ZLINK_ROUTING_ID` | binary | auto | Socket routing ID |
| `ZLINK_SUBSCRIBE` | binary | - | Subscription filter (SUB only) |

## 3. Sending and Receiving Messages

### 3.1 Simple Send/Receive

```c
/* Send */
zlink_send(socket, "Hello", 5, 0);

/* Receive */
char buf[256];
int size = zlink_recv(socket, buf, sizeof(buf), 0);
```

### 3.2 Flags

| Flag | Description |
|------|-------------|
| `ZLINK_DONTWAIT` | Non-blocking mode (returns EAGAIN immediately if no data) |
| `ZLINK_SNDMORE` | Intermediate frame of a multipart message |

### 3.3 Non-Blocking Receive

```c
int size = zlink_recv(socket, buf, sizeof(buf), ZLINK_DONTWAIT);
if (size == -1 && zlink_errno() == EAGAIN) {
    /* No data available */
}
```

## 4. Poller API

### 4.1 zlink_poll

```c
zlink_pollitem_t items[] = {
    { socket1, 0, ZLINK_POLLIN, 0 },
    { socket2, 0, ZLINK_POLLIN, 0 },
};

int rc = zlink_poll(items, 2, 1000); /* 1-second timeout */
if (rc > 0) {
    if (items[0].revents & ZLINK_POLLIN) {
        /* Data available on socket1 */
    }
    if (items[1].revents & ZLINK_POLLIN) {
        /* Data available on socket2 */
    }
}
```

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

int main(void) {
    void *ctx = zlink_ctx_new();

    /* ROUTER (server) */
    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_bind(router, "tcp://*:5555");

    /* DEALER (client) */
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);
    zlink_connect(dealer, "tcp://127.0.0.1:5555");

    /* DEALER → ROUTER */
    zlink_send(dealer, "request", 7, 0);

    /* ROUTER: receive routing_id + data */
    zlink_msg_t id, body;
    zlink_msg_init(&id);
    zlink_msg_init(&body);
    zlink_msg_recv(&id, router, 0);    /* routing_id frame */
    zlink_msg_recv(&body, router, 0);  /* data frame */

    printf("Received: %.*s\n",
           (int)zlink_msg_size(&body),
           (char *)zlink_msg_data(&body));

    /* ROUTER → DEALER (reply) */
    zlink_msg_send(&id, router, ZLINK_SNDMORE);
    zlink_send(router, "reply", 5, 0);

    /* DEALER receives reply */
    char buf[256];
    int size = zlink_recv(dealer, buf, sizeof(buf), 0);
    buf[size] = '\0';
    printf("Reply: %s\n", buf);

    zlink_msg_close(&id);
    zlink_msg_close(&body);
    zlink_close(dealer);
    zlink_close(router);
    zlink_ctx_term(ctx);
    return 0;
}
```

---
[← Overview](01-overview.md) | [Socket Patterns →](03-0-socket-patterns.md)
