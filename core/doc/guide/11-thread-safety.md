[English](11-thread-safety.md) | [한국어](11-thread-safety.ko.md)

<!-- zlink-nav:start -->
[← Performance](10-performance.md) | [Socket Options →](12-socket-options.md)
<!-- zlink-nav:end -->

# Thread-Safety Guide

## 1. The Short Answer

**Yes, zlink handles are thread-safe.** You can share a single socket or SPOT
handle across multiple threads and call its APIs without adding your own mutex
or lock.

```
  Thread A --- zlink_send(socket, ...) ---+
  Thread B --- zlink_send(socket, ...) ---+--> same socket, no mutex needed
  Thread C --- zlink_send(socket, ...) ---+
```

There is only **one thing that is NOT thread-safe**: `zlink_msg_t`.
Each message object must stay on one thread (see [section 5](#5-the-one-exception-zlink_msg_t-is-not-thread-safe)).

The rest of this guide explains the details — what happens when you
mix `send` with `connect`, how `close` works while other threads are
still sending, and a few callback rules to keep in mind.

## 2. Three Categories of API

Not every function works the same way internally. zlink groups its
public APIs into three categories so you know what to expect:

| Category | What it covers | Thread-safe? | Notes |
|---|---|---|---|
| **Sending** | `send`, `publish`, `send_rid` | Yes — fully concurrent | Multiple threads can call these on the same handle simultaneously. This is the fast path, optimized for throughput. |
| **Configuration** | `bind`, `connect`, `disconnect`, `set_option`, `subscribe`, `unsubscribe`, `monitor_open`, queries | Yes — one at a time | Safe to call from any thread. The library processes these one at a time, so don't call them in a tight per-message loop. |
| **Cleanup** | `close`, `destroy` | Yes — with clear error codes | If another thread is still using the handle, close returns `ZLINK_CLOSE_BUSY` instead of crashing. Details in [section 4](#4-closing-handles-safely). |

**In plain terms:** send as much as you want from any thread. Connect,
subscribe, and change options whenever you need to — even while sending.
When you're done, close the handle and check the return code.

### 2.1 Sending (the Fast Path)

These functions allow fully concurrent calls on the same handle:

- `zlink_send()` — raw sockets
- `zlink_publish()` — SPOT
- Calling `send` / `publish` from inside a callback is also safe

**Ordering:**
- From one thread, messages go out in the order you call `send`.
- From multiple threads, each message is delivered intact, but the
  interleaving order between threads is not guaranteed (whichever thread
  gets there first wins).

**Example — 4 worker threads sharing 1 socket:**

```c
#include <zlink.h>
#include <pthread.h>
#include <string.h>

typedef struct { void *socket; int id; } worker_arg_t;

void *worker(void *arg)
{
    worker_arg_t *w = (worker_arg_t *)arg;
    char buf[64];
    for (int i = 0; i < 10000; i++) {
        int len = snprintf(buf, sizeof(buf), "worker-%d msg-%d", w->id, i);
        zlink_msg_t part;
        zlink_msg_init_size(&part, (size_t)len);
        memcpy(zlink_msg_data(&part), buf, (size_t)len);
        zlink_send(w->socket, &part, 1, ZLINK_SEND_FLAGS_NONE); /* no mutex needed */
    }
    return NULL;
}

int main(void)
{
    void *ctx = zlink_ctx_new();
    void *socket = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
    zlink_connect(socket, "tcp://127.0.0.1:5555");

    pthread_t threads[4];
    worker_arg_t args[4];
    for (int i = 0; i < 4; i++) {
        args[i] = (worker_arg_t){socket, i};
        pthread_create(&threads[i], NULL, worker, &args[i]);
    }
    for (int i = 0; i < 4; i++)
        pthread_join(threads[i], NULL);

    zlink_close(socket);
    zlink_ctx_term(ctx);
    return 0;
}
```

### 2.2 Configuration (Setup and Runtime Changes)

These functions are thread-safe — the library processes them one at a
time to keep things correct. This is fine for operations you do
occasionally (connecting a new endpoint, changing an option), but don't
call them in a per-message loop.

Includes:
- `zlink_bind()` / `zlink_connect()` / `zlink_disconnect()`
- `zlink_set_option()` / `zlink_get_option()`
- `zlink_set_subscription()` / `zlink_unset_subscription()`
- `zlink_socket_monitor_open()`
- `zlink_send_ready_handler()`
- `zlink_set_option()`
- Query/snapshot functions

**You can mix sending and configuration freely.** For example, one thread
can send messages while another thread connects additional endpoints:

```c
void *send_thread(void *arg)
{
    void *socket = arg;
    char buf[] = "data";
    for (int i = 0; i < 100000; i++)
        zlink_msg_t part;
        zlink_msg_init_size(&part, sizeof(buf) - 1);
        memcpy(zlink_msg_data(&part), buf, sizeof(buf) - 1);
        zlink_send(socket, &part, 1, 0);  /* hot path */
    return NULL;
}

void *setup_thread(void *arg)
{
    void *socket = arg;
    /* These are safe to call while send_thread is running */
    zlink_connect(socket, "tcp://10.0.0.2:5555");
    zlink_connect(socket, "tcp://10.0.0.3:5555");

    int hwm = 5000;
    zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));
    return NULL;
}
```

Lightweight reads like `ZLINK_OPT_EVENTS` and `ZLINK_OPT_LAST_ENDPOINT` are also
in this category but carry less overhead than heavier query/snapshot calls.

## 3. Per-Handle Quick Reference

Every handle type follows the same three-category model:

| Handle | Sending (concurrent) | Configuration (serialized) | Cleanup |
|---|---|---|---|
| Socket (PAIR/DEALER/ROUTER/...) | `zlink_send` | bind, connect, disconnect, set_option, subscribe, monitor_open | `zlink_close` |
| SPOT | `zlink_publish` | subscribe, unsubscribe, set_pub_option, set_sub_option | `zlink_spot_destroy` |
| MeshNode | send/request/publish (thread-safe) | set_bind, connect_peer, disconnect_peer, set_channel_weight | `zlink_mesh_node_destroy` |

## 4. Closing Handles Safely

Closing a handle while other threads are still using it doesn't crash —
zlink returns a clear error code instead:

| Situation | What happens | Result |
|---|---|---|
| You call `close`/`destroy` while another thread is mid-call on the same handle | Close is **rejected** — the handle stays alive | `ZLINK_CLOSE_BUSY` |
| You call any API after `close` has been accepted | The call is **rejected** — the handle is shutting down | `ZLINK_CLOSE_SHUTDOWN` (or matching `*_TERMINATED` on the per-function result) |
| You call `close`/`destroy` twice | Second call returns immediately | Socket: `EALREADY`; SPOT service handle: `ESHUTDOWN` |

After `ZLINK_CLOSE_BUSY`, the handle goes back to normal — nothing is
damaged, you can keep using it or try closing again later.

**Recommended shutdown pattern:**

```c
#include <zlink.h>
#include <errno.h>
#include <stdatomic.h>

atomic_int g_running = 1;

/* Worker threads check g_running and also handle ESHUTDOWN */
void *sender(void *arg)
{
    void *socket = arg;
    while (atomic_load(&g_running)) {
        zlink_msg_t part;
        zlink_msg_init_size(&part, 32);
        zlink_submit_result_t rc = zlink_send(socket, &part, 1, 0);
        if (rc == ZLINK_SUBMIT_TERMINATED)
            break;  /* handle is shutting down, stop gracefully */
    }
    return NULL;
}

void shutdown_socket(void *socket)
{
    /* Step 1: tell workers to stop */
    atomic_store(&g_running, 0);

    /* Step 2: give workers a moment to finish, then close */
    msleep(50);
    zlink_close(socket);
}
```

**Self-close from callbacks:** If a send-ready or monitor callback calls
`close` on its own handle, the actual close is deferred until the callback
returns (the call returns OK). This avoids use-after-free inside the callback.
A self-close from inside a socket message handler or STREAM raw/packet
dispatch callback is **not** deferred — it returns `EBUSY`/`ZLINK_CLOSE_BUSY`.

## 5. The One Exception: zlink_msg_t Is NOT Thread-Safe

Handles are thread-safe. Message objects are not. Each `zlink_msg_t` must
be used by only one thread at a time.

This is usually natural — just create your message on the stack or heap
in each thread:

```c
/* WRONG — two threads sharing the same msg */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 100);
/* Thread A: */ zlink_send(socket, &msg, 1, 0);
/* Thread B: */ zlink_send(socket, &msg, 1, 0);  /* data race! */
```

```c
/* RIGHT — each thread makes its own msg */
/* Thread A */                       /* Thread B */
zlink_msg_t msg_a;                   zlink_msg_t msg_b;
zlink_msg_init_size(&msg_a, 100);    zlink_msg_init_size(&msg_b, 100);
memcpy(zlink_msg_data(&msg_a),...);  memcpy(zlink_msg_data(&msg_b),...);
zlink_send(socket, &msg_a, 1, 0);    zlink_send(socket, &msg_b, 1, 0);  /* safe */
```

**Callback ownership:** When your callback receives `zlink_msg_t *parts`,
ownership transfers to the callback. You must `zlink_msg_close()` each part
before returning and must not access them from another thread.

## 6. Callback Rules

There is no single "callback thread" — each callback runs on a different
thread:

| Callback | Thread |
|----------|--------|
| Socket message handler (`zlink_recv_handler`) | An I/O thread |
| Monitor handler | The service-control runtime thread (not the socket I/O thread) |
| Send-ready handler | May run synchronously on the caller's send thread |
| SPOT dispatch event handler | The SPOT dispatch worker pool |

The no-blocking and offload rules apply to all of them.
Here's what you need to know:

**What you CAN do in a callback:**
- Call `send` / `publish` on the same handle — this is the recommended
  request-reply pattern.
- Read message data and push it to your own queue.

**What you should NOT do in a callback:**
- **Block** (sleep, lock, heavy computation) — for the socket message handler
  this stalls I/O on its thread; for monitor/send-ready/SPOT-dispatch callbacks
  it delays that subsystem. Push work to a queue and process it on a worker
  thread.
- **Replace the send-ready handler from inside its own callback** — returns
  `EDEADLK`.

**Offload pattern — keep callbacks fast:**

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; i++) {
        /* Push to your own thread-safe queue */
        app_queue_push(app_queue,
                       zlink_msg_data(&parts[i]),
                       zlink_msg_size(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
    /* Return quickly — a worker thread processes the queue */
}
```

## 7. Practical Patterns

### 7.1 Multi-threaded Worker Pool (Socket)

Multiple threads send through one socket — no locking needed:

```c
typedef struct { void *socket; int id; } socket_worker_t;

void *socket_worker(void *arg)
{
    socket_worker_t *w = (socket_worker_t *)arg;
    for (int i = 0; i < 50000; i++) {
        zlink_msg_t part;
        zlink_msg_init_size(&part, 64);
        snprintf(zlink_msg_data(&part), 64, "worker-%d-%d", w->id, i);
        zlink_send(w->socket, &part, 1, 0);
    }
    return NULL;
}

void run_socket_pool(void *socket)
{
    pthread_t threads[4];
    socket_worker_t args[4];
    for (int i = 0; i < 4; i++) {
        args[i] = (socket_worker_t){socket, i};
        pthread_create(&threads[i], NULL, socket_worker, &args[i]);
    }
    for (int i = 0; i < 4; i++)
        pthread_join(threads[i], NULL);
}
```

### 7.2 Publishing While Changing Subscriptions

One thread publishes, another manages subscriptions at runtime:

```c
/* Data thread: publishes at high frequency */
void *publisher(void *arg)
{
    void *spot = arg;
    for (int i = 0; i < 100000; i++) {
        zlink_msg_t part;
        zlink_msg_init_size(&part, 16);
        zlink_publish(spot, "prices", &part, 1, 0);
    }
    return NULL;
}

/* Control thread: adjusts subscriptions while publisher is running */
void *control(void *arg)
{
    void *spot = arg;
    msleep(100);
    zlink_set_subscription(spot, "audit.*");      /* safe while publishing */
    msleep(200);
    zlink_unset_subscription(spot, "audit.*");    /* also safe */
    return NULL;
}
```

## 8. Common Mistakes

| What went wrong | Why | How to fix |
|---|---|---|
| Two threads writing to the same `zlink_msg_t` | Message objects are not thread-safe | Create a separate `zlink_msg_t` in each thread |
| Callback path does heavy work and throughput drops | It stalls the callback's background execution path and reduces delivery throughput | Push to a queue, process on a worker thread |
| Calling APIs after `close`/`destroy` | Returns `ZLINK_CLOSE_SHUTDOWN` (or per-function `*_TERMINATED`) or undefined behavior | Coordinate shutdown; check return codes |
| Calling `connect`/`set_option` in a per-message loop | Configuration APIs are serialized — adds unnecessary overhead | Call them only when configuration actually changes |

## 9. Error Code Quick Reference

| Result | When you see it | What it means |
|---|---|---|
| `ZLINK_CLOSE_BUSY` | `close`/`destroy` while another thread is using the handle | Wait for the other thread to finish, then try again |
| `ZLINK_CLOSE_SHUTDOWN` | Any API call after `close` has been accepted (or second `close`) | The handle is shutting down — stop using it |
| `ZLINK_HANDLER_DEADLOCK` | Replacing the send-ready handler from inside its own callback | Don't do this — replace the handler from a different context |
| per-function `*_TERMINATED` | Data-plane call (`send`/`recv`/...) after context `term` | Context has been terminated — abort use |

---

> For implementation details (admission gates, ordering semantics, cost
> model), see [Thread-Safety Internals](../internals/thread-safety.md).

---
<!-- zlink-nav:bottom:start -->
[← Performance](10-performance.md) | [Socket Options →](12-socket-options.md)
<!-- zlink-nav:bottom:end -->
