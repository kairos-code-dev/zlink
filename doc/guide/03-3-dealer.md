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
void *dealer = zlink_socket(ctx, ZLINK_DEALER);

/* Set routing_id (optional, used for identification by ROUTER) */
zlink_set_routing_id(dealer, "client-1", 8);

/* Connect to server */
zlink_connect(dealer, "tcp://127.0.0.1:5558");
```

### Sending and Receiving Messages

```c
/* Send requests -- can send consecutively without ordering constraints */
zlink_msg_t msg1, msg2, msg3;
zlink_msg_init_size(&msg1, 9);
memcpy(zlink_msg_data(&msg1), "request-1", 9);
zlink_send(dealer, &msg1, 1, 0);

zlink_msg_init_size(&msg2, 9);
memcpy(zlink_msg_data(&msg2), "request-2", 9);
zlink_send(dealer, &msg2, 1, 0);

zlink_msg_init_size(&msg3, 9);
memcpy(zlink_msg_data(&msg3), "request-3", 9);
zlink_send(dealer, &msg3, 1, 0);

/* Responses are dispatched to the handler callback registered at creation */
```

### Receive Modes

DEALER registers a handler via `zlink_recv_handler()`. The callback
receives `source_rid` (always empty — DEALER strips the routing_id
frame) and a `parts[]` array.

**Callback mode** (recommended): attach a handler at socket creation.
Messages from all peers arrive via fair-queue and are dispatched
asynchronously.

**Pull mode**: without attaching a handler, call `zlink_recv()` to
receive synchronously.

```c
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
int rc = zlink_recv(dealer, &source_rid, &parts, &part_count, 0);
if (rc == 0) {
    /* process parts[0..part_count-1] */
    zlink_multipart_close(parts, part_count);
    free(parts);
}
```

> When HWM is reached, `zlink_send()` blocks (default) or returns
> `EAGAIN` with `ZLINK_DONTWAIT`. For advanced backpressure patterns,
> see [Performance Guide](10-performance.md).

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
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);
zlink_send(dealer, parts, 2, 0);

/* ROUTER receives: [routing_id] + [header] + [body] */
```

## 4. Socket Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `zlink_set_routing_id()` | binary | Auto (UUID) | ID for identification by ROUTER (dedicated function) |
| `ZLINK_PROBE_ROUTER` | int | 0 | Send empty message on connect (connection notification) |
| `ZLINK_OPT_SNDHWM` | int | 1000 | Maximum number of messages in the send queue |
| `ZLINK_OPT_RCVHWM` | int | 1000 | Maximum number of messages in the receive queue |
| `ZLINK_OPT_LINGER` | int | -1 | Wait time on close (ms) |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | Send timeout (ms) |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | Receive timeout (ms) |
| `ZLINK_CONNECT_ROUTING_ID` | binary | -- | Alias applied to the next connect |

### Setting routing_id

To allow ROUTER to identify a DEALER, explicitly set the routing_id.

```c
/* Set before bind/connect */
zlink_set_routing_id(dealer, "D1", 2);
zlink_connect(dealer, "tcp://127.0.0.1:5558");
```

> Reference: `core/tests/test_router_multiple_dealers.cpp` -- `zlink_set_routing_id(dealer1, "D1", 2)`

## 5. Usage Patterns

### Pattern 1: DEALER → ROUTER Request-Reply

The most basic pattern. DEALER sends requests, ROUTER replies.

```c
/* Server: ROUTER with handler */
void on_request(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    /* source_rid contains the DEALER's routing_id */
    printf("Received from [%.*s]: %.*s\n",
           (int)source_rid->size, source_rid->data,
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));

    /* Reply: send to the source peer using zlink_send_rid */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 5);
    memcpy(zlink_msg_data(&reply), "World", 5);
    zlink_send_rid(router, source_rid, &reply, 1, 0);

    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_recv_handler(router, on_request, NULL);
zlink_bind(router, "tcp://*:5558");

/* Client: DEALER */
void *dealer = zlink_socket(ctx, ZLINK_DEALER);
zlink_recv_handler(dealer, on_reply, NULL);
zlink_set_routing_id(dealer, "D1", 2);
zlink_connect(dealer, "tcp://127.0.0.1:5558");

/* Client request */
zlink_msg_t req;
zlink_msg_init_size(&req, 5);
memcpy(zlink_msg_data(&req), "Hello", 5);
zlink_send(dealer, &req, 1, 0);

/* on_request receives the message, replies with "World"
   on_reply receives the reply */
```

> Reference: `core/tests/test_router_multiple_dealers.cpp` -- TCP/IPC/inproc examples

### Pattern 2: Multiple DEALER Load Balancing

Multiple DEALERs connect to a single ROUTER. ROUTER distinguishes each DEALER by routing_id.

```c
/* ROUTER receives via handler callback and distinguishes each DEALER by source_rid */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    /* source_rid->data = "D1" or "D2" */
    /* Reply to specific DEALER */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 5);
    memcpy(zlink_msg_data(&reply), "reply", 5);
    zlink_send_rid(router, source_rid, &reply, 1, 0);
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

void *router = zlink_socket(ctx, ZLINK_ROUTER);
zlink_recv_handler(router, on_message, NULL);
zlink_bind(router, "tcp://127.0.0.1:*");

char endpoint[256];
size_t len = sizeof(endpoint);
zlink_get_option(router, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

void *dealer1 = zlink_socket(ctx, ZLINK_DEALER);
zlink_set_routing_id(dealer1, "D1", 2);
zlink_connect(dealer1, endpoint);

void *dealer2 = zlink_socket(ctx, ZLINK_DEALER);
zlink_set_routing_id(dealer2, "D2", 2);
zlink_connect(dealer2, endpoint);

/* Each DEALER sends a message */
zlink_msg_t m1;
zlink_msg_init_size(&m1, 12);
memcpy(zlink_msg_data(&m1), "from_dealer1", 12);
zlink_send(dealer1, &m1, 1, 0);

zlink_msg_t m2;
zlink_msg_init_size(&m2, 12);
memcpy(zlink_msg_data(&m2), "from_dealer2", 12);
zlink_send(dealer2, &m2, 1, 0);

/* on_message receives each DEALER's message with its routing_id */
```

> Reference: `core/tests/test_router_multiple_dealers.cpp` -- `test_router_multiple_dealers_tcp()`

### Pattern 3: Proxy Pattern (ROUTER-DEALER)

Build a multi-threaded server using ROUTER (frontend) + DEALER (backend).

```c
/* Frontend: clients connect here */
void *frontend = zlink_socket(ctx, ZLINK_ROUTER);
zlink_bind(frontend, "tcp://*:5558");

/* Backend: worker threads connect here */
void *backend = zlink_socket(ctx, ZLINK_DEALER);
zlink_bind(backend, "inproc://backend");

/* Start worker threads then run proxy */
zlink_proxy(frontend, backend, NULL);
```

```c
/* Worker thread */
void worker_thread(void *arg) {
    void on_work(const zlink_routing_id_t *source_rid,
                 zlink_msg_t *parts, size_t part_count,
                 void *userdata)
    {
        /* Process and reply with the same routing_id */
        zlink_send_rid(worker, source_rid, parts, part_count, 0);
    }

    void *worker = zlink_socket(ctx, ZLINK_DEALER);
    zlink_recv_handler(worker, on_work, NULL);
    zlink_connect(worker, "inproc://backend");

    /* Worker stays alive until socket is closed */
}
```

> Reference: `core/tests/test_proxy.cpp` -- ROUTER(frontend) + DEALER(backend) + worker pool

### Pattern 4: DEALER ↔ DEALER Asynchronous Communication

Both sides use DEALER for fully asynchronous P2P communication.

```c
void *a = zlink_socket(ctx, ZLINK_DEALER);
zlink_recv_handler(a, on_message_a, NULL);
zlink_bind(a, "tcp://*:5558");

void *b = zlink_socket(ctx, ZLINK_DEALER);
zlink_recv_handler(b, on_message_b, NULL);
zlink_connect(b, "tcp://127.0.0.1:5558");

/* Bidirectional free send */
zlink_msg_t ping;
zlink_msg_init_size(&ping, 4);
memcpy(zlink_msg_data(&ping), "ping", 4);
zlink_send(a, &ping, 1, 0);

zlink_msg_t pong;
zlink_msg_init_size(&pong, 4);
memcpy(zlink_msg_data(&pong), "pong", 4);
zlink_send(b, &pong, 1, 0);

/* on_message_b receives "ping", on_message_a receives "pong" */
```

## 6. Caveats

### Queuing When No Peer Is Connected

If no peer is connected, messages accumulate in the send queue. When the HWM is exceeded, the call blocks (default) or returns `EAGAIN` (`ZLINK_DONTWAIT`).

```c
/* Send with no peer connected */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 4);
memcpy(zlink_msg_data(&msg), "data", 4);
int rc = zlink_send(dealer, &msg, 1, ZLINK_DONTWAIT);
if (rc == -1 && errno == EAGAIN) {
    /* HWM exceeded or no peer connected */
}
```

### Round-Robin Distribution

When multiple peers are connected, messages are distributed in a round-robin fashion. To send to a specific peer, use ROUTER instead.

### Set routing_id Before connect

`zlink_set_routing_id()` must be called before `zlink_connect()`. Changes after connection are not applied.

```c
/* Correct order */
zlink_set_routing_id(dealer, "D1", 2);
zlink_connect(dealer, endpoint);  /* identified as D1 */
```

---
[← PUB/SUB](03-2-pubsub.md) | [ROUTER →](03-4-router.md)
