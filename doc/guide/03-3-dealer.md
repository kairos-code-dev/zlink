[English](03-3-dealer.md) | [한국어](03-3-dealer.ko.md)

# DEALER Socket

## 1. Overview

The DEALER socket is an asynchronous request socket. It sends to multiple peers using **round-robin** distribution and receives using **fair-queue**. Unlike the REQ socket, there is no enforced send/recv ordering, enabling free asynchronous messaging.

**Key characteristics:**
- Send: Round-robin (`lb_t`) -- cyclic distribution across connected peers
- Receive: Fair-queue (`fq_t`) -- fair reception from all peers
- No enforced send/recv ordering (asynchronous)
- No automatic routing_id frame handling

**Valid socket combinations:** DEALER ↔ ROUTER, DEALER ↔ DEALER

```
┌──────────┐                ┌────────┐
│ DEALER 1 │────────────────►│        │
└──────────┘  Round-robin   │ ROUTER │
┌──────────┐                │        │
│ DEALER 2 │────────────────►│        │
└──────────┘                └────────┘
```

## 2. Basic Usage

### Creation and Connection

```c
void *dealer = zlink_socket(ctx, ZLINK_DEALER, NULL);

/* Set routing_id (optional, used for identification by ROUTER) */
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "client-1", 8);

/* Connect to server */
zlink_connect(dealer, "tcp://127.0.0.1:5558");
```

### Sending and Receiving Messages

```c
/* Send requests -- can send consecutively without ordering constraints */
zlink_send(dealer, "request-1", 9, 0);
zlink_send(dealer, "request-2", 9, 0);
zlink_send(dealer, "request-3", 9, 0);

/* Responses are dispatched to the handler callback registered at creation */
```

## 3. Message Format

The DEALER socket does not automatically add a routing_id frame. The frames sent by the application are delivered as-is.

```
DEALER sends: [data]
ROUTER receives: [routing_id][data]   ← ROUTER adds routing_id

ROUTER sends: [routing_id][data]
DEALER receives: [data]              ← routing_id frame is stripped
```

### Multipart Messages

```c
/* DEALER → ROUTER: multipart send */
zlink_send(dealer, "header", 6, ZLINK_SNDMORE);
zlink_send(dealer, "body", 4, 0);

/* ROUTER receives: [routing_id] + [header] + [body] */
```

## 4. Socket Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `ZLINK_ROUTING_ID` | binary | Auto (UUID) | ID for identification by ROUTER |
| `ZLINK_PROBE_ROUTER` | int | 0 | Send empty message on connect (connection notification) |
| `ZLINK_SNDHWM` | int | 300000 | Maximum number of messages in the send queue |
| `ZLINK_RCVHWM` | int | 300000 | Maximum number of messages in the receive queue |
| `ZLINK_LINGER` | int | -1 | Wait time on close (ms) |
| `ZLINK_SNDTIMEO` | int | -1 | Send timeout (ms) |
| `ZLINK_RCVTIMEO` | int | -1 | Receive timeout (ms) |
| `ZLINK_CONNECT_ROUTING_ID` | binary | -- | Alias applied to the next connect |

### Setting routing_id

To allow ROUTER to identify a DEALER, explicitly set the routing_id.

```c
/* Set before bind/connect */
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer, "tcp://127.0.0.1:5558");
```

> Reference: `core/tests/test_router_multiple_dealers.cpp` -- `zlink_setsockopt(dealer1, ZLINK_ROUTING_ID, "D1", 2)`

## 5. Usage Patterns

### Pattern 1: DEALER → ROUTER Request-Reply

The most basic pattern. DEALER sends requests, ROUTER replies.

```c
/* Server: ROUTER with handler */
void on_request(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count)
{
    /* source_rid contains the DEALER's routing_id */
    printf("Received from [%.*s]: %.*s\n",
           (int)source_rid->size, source_rid->data,
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));

    /* Reply: prepend routing_id to send */
    zlink_send(router, source_rid->data, source_rid->size, ZLINK_SNDMORE);
    zlink_send(router, "World", 5, 0);

    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

zlink_socket_handler_t router_handler = {
    .kind = ZLINK_SOCKET_HANDLER_MSG,
    .fn.msg = on_request
};
void *router = zlink_socket(ctx, ZLINK_ROUTER, &router_handler);
zlink_bind(router, "tcp://*:5558");

/* Client: DEALER */
zlink_socket_handler_t dealer_handler = {
    .kind = ZLINK_SOCKET_HANDLER_MSG,
    .fn.msg = on_reply
};
void *dealer = zlink_socket(ctx, ZLINK_DEALER, &dealer_handler);
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer, "tcp://127.0.0.1:5558");

/* Client request */
zlink_send(dealer, "Hello", 5, 0);

/* on_request receives the message, replies with "World"
   on_reply receives the reply */
```

> Reference: `core/tests/test_router_multiple_dealers.cpp` -- TCP/IPC/inproc examples

### Pattern 2: Multiple DEALER Load Balancing

Multiple DEALERs connect to a single ROUTER. ROUTER distinguishes each DEALER by routing_id.

```c
/* ROUTER receives via handler callback and distinguishes each DEALER by source_rid */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count)
{
    /* source_rid->data = "D1" or "D2" */
    /* Reply to specific DEALER */
    zlink_send(router, source_rid->data, source_rid->size, ZLINK_SNDMORE);
    zlink_send(router, "reply", 5, 0);
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

zlink_socket_handler_t router_handler = {
    .kind = ZLINK_SOCKET_HANDLER_MSG,
    .fn.msg = on_message
};
void *router = zlink_socket(ctx, ZLINK_ROUTER, &router_handler);
zlink_bind(router, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_getsockopt(router, ZLINK_LAST_ENDPOINT, endpoint, &len);

void *dealer1 = zlink_socket(ctx, ZLINK_DEALER, NULL);
zlink_setsockopt(dealer1, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer1, endpoint);

void *dealer2 = zlink_socket(ctx, ZLINK_DEALER, NULL);
zlink_setsockopt(dealer2, ZLINK_ROUTING_ID, "D2", 2);
zlink_connect(dealer2, endpoint);

/* Each DEALER sends a message */
zlink_send(dealer1, "from_dealer1", 12, 0);
zlink_send(dealer2, "from_dealer2", 12, 0);

/* on_message receives each DEALER's message with its routing_id */
```

> Reference: `core/tests/test_router_multiple_dealers.cpp` -- `test_router_multiple_dealers_tcp()`

### Pattern 3: Proxy Pattern (ROUTER-DEALER)

Build a multi-threaded server using ROUTER (frontend) + DEALER (backend).

```c
/* Frontend: clients connect here */
void *frontend = zlink_socket(ctx, ZLINK_ROUTER, NULL);
zlink_bind(frontend, "tcp://*:5558");

/* Backend: worker threads connect here */
void *backend = zlink_socket(ctx, ZLINK_DEALER, NULL);
zlink_bind(backend, "inproc://backend");

/* Start worker threads then run proxy */
zlink_proxy(frontend, backend, NULL);
```

```c
/* Worker thread */
void worker_thread(void *arg) {
    void on_work(const zlink_routing_id_t *source_rid,
                 zlink_msg_t *parts, size_t part_count)
    {
        /* Process and reply with the same routing_id */
        zlink_send(worker, source_rid->data, source_rid->size, ZLINK_SNDMORE);
        for (size_t i = 0; i < part_count; i++) {
            int more = (i < part_count - 1) ? ZLINK_SNDMORE : 0;
            zlink_msg_send(&parts[i], worker, more);
        }
    }

    zlink_socket_handler_t handler = {
        .kind = ZLINK_SOCKET_HANDLER_MSG,
        .fn.msg = on_work
    };
    void *worker = zlink_socket(ctx, ZLINK_DEALER, &handler);
    zlink_connect(worker, "inproc://backend");

    /* Worker stays alive until socket is closed */
}
```

> Reference: `core/tests/test_proxy.cpp` -- ROUTER(frontend) + DEALER(backend) + worker pool

### Pattern 4: DEALER ↔ DEALER Asynchronous Communication

Both sides use DEALER for fully asynchronous P2P communication.

```c
zlink_socket_handler_t handler_a = {
    .kind = ZLINK_SOCKET_HANDLER_MSG,
    .fn.msg = on_message_a
};
void *a = zlink_socket(ctx, ZLINK_DEALER, &handler_a);
zlink_bind(a, "tcp://*:5558");

zlink_socket_handler_t handler_b = {
    .kind = ZLINK_SOCKET_HANDLER_MSG,
    .fn.msg = on_message_b
};
void *b = zlink_socket(ctx, ZLINK_DEALER, &handler_b);
zlink_connect(b, "tcp://127.0.0.1:5558");

/* Bidirectional free send */
zlink_send(a, "ping", 4, 0);
zlink_send(b, "pong", 4, 0);

/* on_message_b receives "ping", on_message_a receives "pong" */
```

## 6. Caveats

### Queuing When No Peer Is Connected

If no peer is connected, messages accumulate in the send queue. When the HWM is exceeded, the call blocks (default) or returns `EAGAIN` (`ZLINK_DONTWAIT`).

```c
/* Send with no peer connected */
int rc = zlink_send(dealer, "data", 4, ZLINK_DONTWAIT);
if (rc == -1 && errno == EAGAIN) {
    /* HWM exceeded or no peer connected */
}
```

### Round-Robin Distribution

When multiple peers are connected, messages are distributed in a round-robin fashion. To send to a specific peer, use ROUTER instead.

### Set routing_id Before connect

`ZLINK_ROUTING_ID` must be set before calling `zlink_connect()`. Changes after connection are not applied.

```c
/* Correct order */
zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "D1", 2);
zlink_connect(dealer, endpoint);  /* identified as D1 */
```

---
[← PUB/SUB](03-2-pubsub.md) | [ROUTER →](03-4-router.md)
