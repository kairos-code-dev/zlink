# Thread-Safety Guide

## 1. The Short Answer

**Yes, zlink handles are thread-safe.** You can share a single socket,
SPOT, or Discovery handle across multiple threads and call its
APIs without adding your own mutex or lock.

```
  Thread A ─── zlink_send(socket, ...) ───┐
  Thread B ─── zlink_send(socket, ...) ───┼──► same socket, no mutex needed
  Thread C ─── zlink_send(socket, ...) ───┘
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
| **Configuration** | `bind`, `connect`, `disconnect`, `set_option`, `subscribe`, `unsubscribe`, `monitor_open`, `attach_discovery`, queries | Yes — one at a time | Safe to call from any thread. The library processes these one at a time, so don't call them in a tight per-message loop. |
| **Cleanup** | `close`, `destroy` | Yes — with clear error codes | If another thread is still using the handle, close returns `EBUSY` instead of crashing. Details in [section 4](#4-closing-handles-safely). |

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

=== "C"

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
            zlink_send(w->socket, buf, len, 0);  /* no mutex needed */
        }
        return NULL;
    }

    int main(void)
    {
        void *ctx = zlink_ctx_new();
        void *socket = zlink_socket(ctx, ZLINK_DEALER);
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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
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
- `zlink_spot_node_attach_discovery()`
- `zlink_socket_monitor_open()` / `zlink_service_monitor_open()`
- `zlink_send_ready_handler()`
- `zlink_set_option()`
- `zlink_registry_add_peer()` / `zlink_registry_set_heartbeat()`
- Query/snapshot functions

**You can mix sending and configuration freely.** For example, one thread
can send messages while another thread connects additional endpoints:

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

Lightweight reads like `ZLINK_OPT_EVENTS` and `ZLINK_OPT_LAST_ENDPOINT` are also
in this category but carry less overhead than heavier query/snapshot calls.

## 3. Per-Handle Quick Reference

Every handle type follows the same three-category model:

| Handle | Sending (concurrent) | Configuration (serialized) | Cleanup |
|---|---|---|---|
| Socket (PAIR/DEALER/ROUTER/...) | `zlink_send` | bind, connect, disconnect, set_option, subscribe, monitor_open | `zlink_close` |
| SPOT | `zlink_publish` | subscribe, unsubscribe, set_pub_option, set_sub_option, monitor_open | `zlink_spot_destroy` |
| SPOT Node | `zlink_publish` | bind, connect_peer, disconnect_peer, attach_discovery, subscribe, unsubscribe, monitor_open | `zlink_spot_node_destroy` |
| Discovery | *(no sending — config only)* | connect_registry, set_routing_id, monitor_open | `zlink_discovery_destroy` |
| Registry | *(no sending — config only)* | bind, add_peer, set_heartbeat, set_broadcast_interval, topology_query | `zlink_registry_destroy` |

## 4. Closing Handles Safely

Closing a handle while other threads are still using it doesn't crash —
zlink returns a clear error code instead:

| Situation | What happens | Error code |
|---|---|---|
| You call `close`/`destroy` while another thread is mid-call on the same handle | Close is **rejected** — the handle stays alive | `EBUSY` |
| You call any API after `close` has been accepted | The call is **rejected** — the handle is shutting down | `ESHUTDOWN` |
| You call `close`/`destroy` twice | Second call returns immediately | `EALREADY` |

After `EBUSY`, the handle goes back to normal — nothing is damaged, you
can keep using it or try closing again later.

**Recommended shutdown pattern:**

=== "C"

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
            int rc = zlink_send(socket, &part, 1, 0);
            if (rc == -1 && zlink_errno() == ESHUTDOWN)
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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

**Self-close from callbacks:** If a send-ready or monitor callback calls
`close` on its own handle, the actual close is deferred until the callback
returns. This avoids use-after-free inside the callback.

## 5. The One Exception: zlink_msg_t Is NOT Thread-Safe

Handles are thread-safe. Message objects are not. Each `zlink_msg_t` must
be used by only one thread at a time.

This is usually natural — just create your message on the stack or heap
in each thread:

=== "C"

    ```c
    /* WRONG — two threads sharing the same msg */
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 100);
    /* Thread A: */ zlink_send_msg(socket, &msg, 0);
    /* Thread B: */ zlink_send_msg(socket, &msg, 0);  /* data race! */
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

=== "C"

    ```c
    /* RIGHT — each thread makes its own msg */
    /* Thread A */                       /* Thread B */
    zlink_msg_t msg_a;                   zlink_msg_t msg_b;
    zlink_msg_init_size(&msg_a, 100);    zlink_msg_init_size(&msg_b, 100);
    memcpy(zlink_msg_data(&msg_a),...);  memcpy(zlink_msg_data(&msg_b),...);
    zlink_send_msg(socket, &msg_a, 0);   zlink_send_msg(socket, &msg_b, 0);  /* safe */
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

**Callback ownership:** When your callback receives `zlink_msg_t *parts`,
ownership transfers to the callback. You must `zlink_msg_close()` each part
before returning and must not access them from another thread.

## 6. Callback Rules

All callbacks (message, SPOT, XPUB, monitor, send-ready) run on the I/O
thread. Here's what you need to know:

**What you CAN do in a callback:**
- Call `send` / `publish` on the same handle — this is the recommended
  request-reply pattern.
- Read message data and push it to your own queue.

**What you should NOT do in a callback:**
- **Block** (sleep, lock, heavy computation) — this stalls all I/O on that
  thread. Push work to a queue and process it on a worker thread.
- **Replace the send-ready handler from inside its own callback** — returns
  `EDEADLK`.

**Offload pattern — keep callbacks fast:**

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

## 7. Practical Patterns

### 7.1 Multi-threaded Worker Pool (Socket)

Multiple threads send through one socket — no locking needed:

!!! note "C API definition -- each binding wraps this into its idiomatic type."

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

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

## 8. Common Mistakes

| What went wrong | Why | How to fix |
|---|---|---|
| Two threads writing to the same `zlink_msg_t` | Message objects are not thread-safe | Create a separate `zlink_msg_t` in each thread |
| Callback does heavy work and throughput drops | Callbacks run on the I/O thread — blocking stalls everything | Push to a queue, process on a worker thread |
| Calling APIs after `close`/`destroy` | Returns `ESHUTDOWN` or undefined behavior | Coordinate shutdown; check return codes |
| Calling `connect`/`set_option` in a per-message loop | Configuration APIs are serialized — adds unnecessary overhead | Call them only when configuration actually changes |

## 9. Error Code Quick Reference

| Error | When you see it | What it means |
|---|---|---|
| `EBUSY` | `close`/`destroy` while another thread is using the handle | Wait for the other thread to finish, then try again |
| `ESHUTDOWN` | Any API call after `close` has been accepted | The handle is shutting down — stop using it |
| `EDEADLK` | Replacing the send-ready handler from inside its own callback | Don't do this — replace the handler from a different context |
| `EALREADY` | Calling `close`/`destroy` a second time | Already shutting down — nothing more to do |

---

> For implementation details (admission gates, ordering semantics, cost
> model), see [Thread-Safety Internals](../internals/thread-safety.md).

[← Performance](10-performance.md) | [Socket Options →](12-socket-options.md)
