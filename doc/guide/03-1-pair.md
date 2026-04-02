[English](03-1-pair.md) | [한국어](03-1-pair.ko.md)

# PAIR Socket

## 1. Overview

The PAIR socket forms an exclusive 1:1 bidirectional connection with exactly one peer. If a second peer connects, the first connection is dropped.

**Key characteristics:**
- Only a single pipe is allowed (1:1 exclusive)
- Bidirectional free messaging (send/recv order does not matter)
- The simplest socket type

**Valid socket combinations:** PAIR ↔ PAIR

```
┌────────┐              ┌────────┐
│ PAIR A │◄────────────►│ PAIR B │
└────────┘   양방향     └────────┘
```

## 2. Basic Usage

### Creation and Connection

```c
void *ctx = zlink_ctx_new();

/* Server side */
void *server = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(server, "tcp://*:5555");

/* Client side */
void *client = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(client, "tcp://127.0.0.1:5555");
```

### Message Exchange

```c
/* Define receive handler */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    printf("Received: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

/* Server stays in recv model */
void *server = zlink_socket(ctx, ZLINK_PAIR);

/* Client (send only) */
void *client = zlink_socket(ctx, ZLINK_PAIR);

/* ... bind/connect ... */

/* Client → Server */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 5);
memcpy(zlink_msg_data(&msg), "Hello", 5);
zlink_send(client, &msg, 1, 0);
/* Server receives with zlink_recv() or poller + zlink_recv() */

/* Server → Client (bidirectional, but client needs handler too for receiving) */
zlink_msg_t reply;
zlink_msg_init_size(&reply, 5);
memcpy(zlink_msg_data(&reply), "World", 5);
zlink_send(server, &reply, 1, 0);
```

??? example "Full Sample Code"

    | Language | Source |
    |----------|--------|
    | C | [pair_callback_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/pair_callback_sample.c) |
    | C++ | [pair_callback_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/pair_callback_sample.cpp) |
    | Java | [PairCallbackSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/PairCallbackSample.java) |
    | Python | [pair_callback.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/pair_callback.py) |
    | Node | [pair_callback_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/pair_callback_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/PairCallback/Program.cs) |
    | Rust | [pair_callback_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/pair_callback_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/pair_callback_sample/main.go) |

### Sending Multipart Data

Multipart data is sent as a parts array in a single `zlink_send` call.

```c
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 3);
memcpy(zlink_msg_data(&parts[0]), "foo", 3);
zlink_msg_init_size(&parts[1], 6);
memcpy(zlink_msg_data(&parts[1]), "foobar", 6);
zlink_send(server, parts, 2, 0);

/* Receiver pulls both frames from one zlink_recv() call:
   parts[0] = "foo", parts[1] = "foobar", part_count = 2 */
```

> Reference: `core/tests/test_pair_inproc.cpp` -- `test_zlink_send_multipart()` test

### Receive Modes

PAIR is recv/poller-only in the public API.
Use `zlink_recv()` to receive synchronously.

```c
void *pair = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(pair, "tcp://*:5556");

zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
int rc = zlink_recv(pair, &source_rid, &parts, &part_count, 0);
if (rc == 0) {
    /* process parts[0..part_count-1] */
    zlink_multipart_close(parts, part_count);
    free(parts);
}
```

> When HWM is reached, `zlink_send()` blocks (default) or returns
> `EAGAIN` with `ZLINK_DONTWAIT`. For advanced backpressure patterns,
> see [Performance Guide](10-performance.md).

??? example "Full Sample Code"

    | Language | Source |
    |----------|--------|
    | C | [pair_recv_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/pair_recv_sample.c) |
    | C++ | [pair_recv_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/pair_recv_sample.cpp) |
    | Java | [PairRecvSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/PairRecvSample.java) |
    | Python | [pair_recv.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/pair_recv.py) |
    | Node | [pair_recv_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/pair_recv_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/PairRecv/Program.cs) |
    | Rust | [pair_recv_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/pair_recv_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/pair_recv_sample/main.go) |

## 3. Message Format

PAIR socket message frames contain **application data only**.

```
Single frame:     [data]
Multipart frame:  [frame1][frame2]...[frameN]
```

> For `source_rid` and the common receive interface, see
> [Socket Patterns Overview](03-0-socket-patterns.md#7-common-receive-interface).

Multipart send:

```c
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);
zlink_send(server, parts, 2, 0);
```

## 4. Socket Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `ZLINK_OPT_SNDHWM` | int | 1000 | Maximum number of messages in the send queue |
| `ZLINK_OPT_RCVHWM` | int | 1000 | Maximum number of messages in the receive queue |
| `ZLINK_OPT_LINGER` | int | -1 | Wait time for unsent messages on close (ms), -1=infinite |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | Send timeout (ms), -1=infinite |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | Receive timeout (ms), -1=infinite |

```c
int hwm = 5000;
zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

int linger = 0;  /* return immediately on close */
zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
```

## 5. Usage Patterns

### Pattern 1: Inter-thread Signaling (inproc)

The most common PAIR use case. Zero-copy communication between threads via the inproc transport.

```c
/* Main thread */
void *signal = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(signal, "inproc://signal");

/* Worker thread */
void *worker_signal = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(worker_signal, "inproc://signal");

/* Worker → Main: task completion signal */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 4);
memcpy(zlink_msg_data(&msg), "DONE", 4);
zlink_send(worker_signal, &msg, 1, 0);

/* Main: on_signal callback receives "DONE" asynchronously */
```

> Reference: `core/tests/test_pair_inproc.cpp` -- bind → connect → bounce pattern

### Pattern 2: TCP Communication

1:1 communication over the network. Wildcard bind enables automatic port assignment.

```c
/* Server: wildcard port */
void *server = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(server, "tcp://127.0.0.1:*");

/* Query the assigned endpoint */
char endpoint[256];
size_t len = sizeof(endpoint);
zlink_get_option(server, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

/* Client: connect using the queried endpoint */
void *client = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(client, endpoint);
```

> Reference: `core/tests/test_pair_tcp.cpp` -- `bind_loopback_ipv4()` + wildcard bind

### Pattern 3: Connection by DNS Name

You can also connect using a hostname.

```c
void *client = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(client, "tcp://localhost:5555");
```

> Reference: `core/tests/test_pair_tcp.cpp` -- `test_pair_tcp_connect_by_name()`

### Pattern 4: IPC Communication

Inter-process communication on the same machine (Linux/macOS).

```c
void *server = zlink_socket(ctx, ZLINK_PAIR);
zlink_bind(server, "ipc:///tmp/myapp.ipc");

void *client = zlink_socket(ctx, ZLINK_PAIR);
zlink_connect(client, "ipc:///tmp/myapp.ipc");
```

> Reference: `core/tests/test_pair_ipc.cpp` -- includes IPC path length validation

## 6. Caveats

### Only a Single Peer Allowed

A PAIR socket maintains only one connection. If a second peer connects, the first connection is dropped.

```
 Allowed:  PAIR A ↔ PAIR B      (1:1)
 Invalid:  PAIR A ← PAIR B      (N:1 attempt drops existing connection)
               ← PAIR C
```

Use DEALER/ROUTER if N:1 communication is needed.

### inproc bind Order

With the inproc transport, **bind must be called before connect**.

```c
/* Correct order */
zlink_bind(a, "inproc://signal");     /* 1. bind first */
zlink_connect(b, "inproc://signal");  /* 2. connect */

/* Wrong order -- fails */
zlink_connect(b, "inproc://signal");  /* fails because bind has not been called yet */
zlink_bind(a, "inproc://signal");
```

### IPC Path Length

The file path of an IPC endpoint cannot exceed the system limit (typically 108 characters).

```c
/* Path too long → ENAMETOOLONG error */
zlink_bind(socket, "ipc:///very/long/path/.../endpoint.ipc");
```

> Reference: `core/tests/test_pair_ipc.cpp` -- `test_endpoint_too_long()`

### HWM Behavior

When there is no peer or the peer is slow, outgoing messages are queued up to the HWM. When the HWM is exceeded, `zlink_send()` blocks (default) or returns `EAGAIN` (`ZLINK_DONTWAIT`).

### LINGER Setting

When `zlink_close()` is called and there are unsent messages remaining, it waits for the LINGER duration. For tests or when a fast shutdown is needed:

```c
int linger = 0;
zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
```

---
[← Socket Patterns](03-0-socket-patterns.md) | [PUB/SUB →](03-2-pubsub.md)
