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
    #include <zlink/context.hpp>
    #include <zlink/socket_types.hpp>
    #include <thread>
    #include <cstdio>

    void worker(zlink::dealer_socket_t &socket, int id)
    {
        char buf[64];
        for (int i = 0; i < 10000; i++) {
            int len = std::snprintf(buf, sizeof(buf), "worker-%d msg-%d", id, i);
            zlink::message_t msg(static_cast<size_t>(len));
            std::memcpy(msg.data(), buf, len);
            socket.send(msg);  // no mutex needed
        }
    }

    int main()
    {
        zlink::context_t ctx;
        zlink::dealer_socket_t socket(ctx);
        socket.connect("tcp://127.0.0.1:5555");

        std::thread threads[4];
        for (int i = 0; i < 4; i++)
            threads[i] = std::thread(worker, std::ref(socket), i);
        for (auto &t : threads)
            t.join();

        socket.close();
        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class WorkerPool {
        public static void main(String[] args) throws Exception {
            try (Context ctx = new Context()) {
                DealerSocket socket = new DealerSocket(ctx);
                socket.connect("tcp://127.0.0.1:5555");

                Thread[] threads = new Thread[4];
                for (int i = 0; i < 4; i++) {
                    final int id = i;
                    threads[i] = new Thread(() -> {
                        for (int j = 0; j < 10000; j++) {
                            byte[] data = String.format("worker-%d msg-%d", id, j).getBytes();
                            socket.send(Message.copyOf(data));  // no mutex needed
                        }
                    });
                    threads[i].start();
                }
                for (Thread t : threads) t.join();
                socket.close();
            }
        }
    }
    ```

=== "Python"

    ```python
    import threading
    import zlink

    def worker(socket, worker_id):
        for i in range(10000):
            socket.send(f"worker-{worker_id} msg-{i}".encode())  # no lock needed

    ctx = zlink.Context()
    socket = zlink.DealerSocket(ctx)
    socket.connect("tcp://127.0.0.1:5555")

    threads = [threading.Thread(target=worker, args=(socket, i)) for i in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    socket.close()
    ctx.close()
    ```

=== "Node/TypeScript"

    ```typescript
    // Node.js runs on a single-threaded event loop, so concurrent
    // send() calls from the main thread are naturally serialized.
    // For true parallelism, use worker_threads — each worker gets
    // its own socket (handles cannot cross thread boundaries in V8).
    import { Context, DealerSocket, Message } from 'zlink';

    const ctx = new Context();
    const socket = new DealerSocket(ctx);
    socket.connect('tcp://127.0.0.1:5555');

    for (let id = 0; id < 4; id++) {
        for (let i = 0; i < 10000; i++) {
            socket.send(Buffer.from(`worker-${id} msg-${i}`));
        }
    }

    socket.close();
    ctx.close();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new Context();
    var socket = new DealerSocket(ctx);
    socket.Connect("tcp://127.0.0.1:5555");

    var tasks = new Task[4];
    for (int i = 0; i < 4; i++)
    {
        int id = i;
        tasks[i] = Task.Run(() =>
        {
            for (int j = 0; j < 10000; j++)
            {
                var msg = new Message($"worker-{id} msg-{j}"u8.ToArray());
                socket.Send(msg);  // no lock needed
            }
        });
    }
    Task.WaitAll(tasks);

    socket.Close();
    ```

=== "Rust"

    ```rust
    use zlink::{Context, DealerSocket, Message};
    use std::thread;

    fn main() -> Result<(), zlink::ZlinkError> {
        let ctx = Context::new()?;
        let socket = DealerSocket::new(&ctx)?;
        socket.connect("tcp://127.0.0.1:5555")?;

        let handle = socket.send_handle();  // cloneable, lightweight
        let threads: Vec<_> = (0..4).map(|id| {
            let h = handle.clone();
            thread::spawn(move || {
                for i in 0..10000 {
                    let data = format!("worker-{id} msg-{i}");
                    h.send(Message::from(data.as_bytes())).unwrap();
                }
            })
        }).collect();

        for t in threads { t.join().unwrap(); }
        socket.close()?;
        Ok(())
    }
    ```

=== "Go"

    ```go
    package main

    import (
        "fmt"
        "sync"
        "github.com/kairoscode/zlink"
    )

    func main() {
        ctx, _ := zlink.NewContext()
        socket, _ := ctx.DealerSocket()
        socket.Connect("tcp://127.0.0.1:5555")

        var wg sync.WaitGroup
        for id := 0; id < 4; id++ {
            wg.Add(1)
            go func(id int) {
                defer wg.Done()
                for i := 0; i < 10000; i++ {
                    msg := zlink.NewMessageFromBytes([]byte(fmt.Sprintf("worker-%d msg-%d", id, i)))
                    socket.Send(msg)  // no lock needed
                }
            }(id)
        }
        wg.Wait()

        socket.Close()
        ctx.Close()
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
    void send_thread(zlink::dealer_socket_t &socket)
    {
        for (int i = 0; i < 100000; i++) {
            zlink::message_t part(4);
            std::memcpy(part.data(), "data", 4);
            socket.send(part);  // hot path
        }
    }

    void setup_thread(zlink::dealer_socket_t &socket)
    {
        // Safe to call while send_thread is running
        socket.connect("tcp://10.0.0.2:5555");
        socket.connect("tcp://10.0.0.3:5555");

        int hwm = 5000;
        socket.set_option(zlink::socket_option_key::sndhwm, hwm);
    }
    ```

=== "Java"

    ```java
    Thread sender = new Thread(() -> {
        for (int i = 0; i < 100000; i++)
            socket.send(Message.copyOf("data".getBytes()));  // hot path
    });

    Thread setup = new Thread(() -> {
        // Safe to call while sender is running
        socket.connect("tcp://10.0.0.2:5555");
        socket.connect("tcp://10.0.0.3:5555");
        socket.options().sendHwm(5000);
    });
    ```

=== "Python"

    ```python
    def send_thread(socket):
        for _ in range(100000):
            socket.send(b"data")  # hot path

    def setup_thread(socket):
        # Safe to call while send_thread is running
        socket.connect("tcp://10.0.0.2:5555")
        socket.connect("tcp://10.0.0.3:5555")
        socket.options.send_hwm = 5000
    ```

=== "Node/TypeScript"

    ```typescript
    // In Node.js, the single-threaded event loop means send()
    // and connect() calls are already serialized. No concurrency
    // conflict can occur within one thread.
    socket.connect('tcp://10.0.0.2:5555');
    socket.connect('tcp://10.0.0.3:5555');
    socket.options.sendHwm = 5000;

    for (let i = 0; i < 100000; i++) {
        socket.send(Buffer.from('data'));
    }
    ```

=== "C#/.NET"

    ```csharp
    var sender = Task.Run(() =>
    {
        for (int i = 0; i < 100000; i++)
            socket.Send(new Message("data"u8));  // hot path
    });

    var setup = Task.Run(() =>
    {
        // Safe to call while sender is running
        socket.Connect("tcp://10.0.0.2:5555");
        socket.Connect("tcp://10.0.0.3:5555");
        socket.CommonOptions.SendHwm = 5000;
    });
    ```

=== "Rust"

    ```rust
    let handle = socket.send_handle();
    let sender = std::thread::spawn(move || {
        for _ in 0..100_000 {
            handle.send(Message::from(b"data".as_slice())).unwrap();
        }
    });

    // Safe to call while sender is running
    socket.connect("tcp://10.0.0.2:5555")?;
    socket.connect("tcp://10.0.0.3:5555")?;
    socket.common_options().set_send_hwm(5000)?;
    ```

=== "Go"

    ```go
    go func() {
        for i := 0; i < 100000; i++ {
            msg := zlink.NewMessageFromBytes([]byte("data"))
            socket.Send(msg)  // hot path
        }
    }()

    // Safe to call while goroutine is sending
    socket.Connect("tcp://10.0.0.2:5555")
    socket.Connect("tcp://10.0.0.3:5555")
    socket.SetSendHWM(5000)
    ```

Lightweight reads like `ZLINK_OPT_EVENTS` and `ZLINK_OPT_LAST_ENDPOINT` are also
in this category but carry less overhead than heavier query/snapshot calls.

## 3. Per-Handle Quick Reference

Every handle type follows the same three-category model:

| Handle | Sending (concurrent) | Configuration (serialized) | Cleanup |
|---|---|---|---|
| Socket (PAIR/DEALER/ROUTER/...) | `zlink_send` | bind, connect, disconnect, set_option, subscribe, monitor_open | `zlink_close` |
| SPOT | `zlink_publish` | subscribe, unsubscribe, set_pub_option, set_sub_option | `zlink_spot_destroy` |
| SPOT Node | `zlink_publish` | bind, connect_peer, disconnect_peer, attach_discovery, subscribe, unsubscribe | `zlink_spot_node_destroy` |
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
    #include <zlink/socket_types.hpp>
    #include <atomic>
    #include <thread>
    #include <cerrno>

    std::atomic<bool> g_running{true};

    void sender(zlink::dealer_socket_t &socket)
    {
        while (g_running.load()) {
            zlink::message_t part(32);
            try {
                socket.send(part);
            } catch (const zlink::error_t &e) {
                if (e.num() == ESHUTDOWN)
                    break;  // handle is shutting down
                throw;
            }
        }
    }

    void shutdown_socket(zlink::dealer_socket_t &socket)
    {
        g_running.store(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        socket.close();
    }
    ```

=== "Java"

    ```java
    import java.util.concurrent.atomic.AtomicBoolean;

    AtomicBoolean running = new AtomicBoolean(true);

    Thread sender = new Thread(() -> {
        while (running.get()) {
            try {
                socket.send(new Message(32));
            } catch (ZlinkException e) {
                if (e.errorCode() == ErrorCode.ESHUTDOWN)
                    break;  // handle is shutting down
                throw e;
            }
        }
    });

    // Shutdown
    running.set(false);
    Thread.sleep(50);
    socket.close();
    ```

=== "Python"

    ```python
    import threading

    running = threading.Event()
    running.set()

    def sender(socket):
        while running.is_set():
            try:
                socket.send(bytes(32))
            except zlink.ZlinkError as e:
                if e.errno == errno.ESHUTDOWN:
                    break  # handle is shutting down
                raise

    # Shutdown
    running.clear()
    time.sleep(0.05)
    socket.close()
    ```

=== "Node/TypeScript"

    ```typescript
    // Node.js is single-threaded — shutdown is straightforward.
    // Just stop sending and close the socket.
    let running = true;

    function sendLoop() {
        if (!running) return;
        socket.send(Buffer.alloc(32));
        setImmediate(sendLoop);
    }

    // Shutdown
    running = false;
    socket.close();
    ctx.close();
    ```

=== "C#/.NET"

    ```csharp
    using var cts = new CancellationTokenSource();

    var sender = Task.Run(() =>
    {
        while (!cts.Token.IsCancellationRequested)
        {
            try {
                socket.Send(new Message(32));
            } catch (ZlinkException e) when (e.ErrorCode == ErrorCode.Shutdown) {
                break;  // handle is shutting down
            }
        }
    });

    // Shutdown
    cts.Cancel();
    await Task.Delay(50);
    socket.Close();
    ```

=== "Rust"

    ```rust
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;

    let running = Arc::new(AtomicBool::new(true));

    let flag = running.clone();
    let handle = socket.send_handle();
    let sender = std::thread::spawn(move || {
        while flag.load(Ordering::Relaxed) {
            if let Err(e) = handle.send(Message::new(32)) {
                if e.is_shutdown() { break; }
            }
        }
    });

    // Shutdown
    running.store(false, Ordering::Relaxed);
    std::thread::sleep(std::time::Duration::from_millis(50));
    socket.close()?;
    ```

=== "Go"

    ```go
    import "sync/atomic"

    var running int32 = 1

    go func() {
        for atomic.LoadInt32(&running) == 1 {
            msg := zlink.NewMessage(32)
            err := socket.Send(msg)
            if err != nil && zlink.IsShutdown(err) {
                break  // handle is shutting down
            }
        }
    }()

    // Shutdown
    atomic.StoreInt32(&running, 0)
    time.Sleep(50 * time.Millisecond)
    socket.Close()
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
    /* WRONG — two threads sharing the same message_t */
    zlink::message_t msg(100);
    // Thread A:
    socket.send(msg);
    // Thread B:
    socket.send(msg);  // data race!
    ```

=== "Java"

    ```java
    /* WRONG — two threads sharing the same Message */
    Message msg = new Message(100);
    // Thread A:
    socket.send(msg);
    // Thread B:
    socket.send(msg);  // data race!
    ```

=== "Python"

    ```python
    # WRONG — two threads sharing the same Message
    msg = zlink.Message(100)
    # Thread A:
    socket.send(msg)
    # Thread B:
    socket.send(msg)  # data race!
    ```

=== "Node/TypeScript"

    ```typescript
    // Node.js is single-threaded, so this scenario does not
    // arise in practice. If using worker_threads, each worker
    // must create its own Message — Message objects cannot be
    // shared across V8 isolates.
    ```

=== "C#/.NET"

    ```csharp
    /* WRONG — two threads sharing the same Message */
    var msg = new Message(100);
    // Thread A:
    socket.Send(msg);
    // Thread B:
    socket.Send(msg);  // data race!
    ```

=== "Rust"

    ```rust
    // Rust prevents this at compile time — Message does not
    // implement Clone or Copy, and send() consumes the message.
    // let msg = Message::new(100);
    // socket.send(msg)?;      // msg moved here
    // socket.send(msg)?;      // compile error: use of moved value
    ```

=== "Go"

    ```go
    /* WRONG — two goroutines sharing the same Message */
    msg := zlink.NewMessage(100)
    // Goroutine A:
    socket.Send(msg)
    // Goroutine B:
    socket.Send(msg)  // data race!
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
    /* RIGHT — each thread makes its own message_t */
    // Thread A                          // Thread B
    zlink::message_t msg_a(100);         zlink::message_t msg_b(100);
    std::memcpy(msg_a.data(), ...);      std::memcpy(msg_b.data(), ...);
    socket.send(msg_a);                  socket.send(msg_b);  // safe
    ```

=== "Java"

    ```java
    /* RIGHT — each thread makes its own Message */
    // Thread A                           // Thread B
    Message msgA = new Message(100);      Message msgB = new Message(100);
    // ... fill msgA ...                  // ... fill msgB ...
    socket.send(msgA);                    socket.send(msgB);  // safe
    ```

=== "Python"

    ```python
    # RIGHT — each thread makes its own message
    # Thread A                            # Thread B
    msg_a = zlink.Message(100)            msg_b = zlink.Message(100)
    socket.send(msg_a)                    socket.send(msg_b)  # safe
    ```

=== "Node/TypeScript"

    ```typescript
    // In Node.js each Buffer is its own allocation — just create
    // separate buffers and send them. No sharing issue arises.
    socket.send(Buffer.alloc(100));  // each call owns its buffer
    ```

=== "C#/.NET"

    ```csharp
    /* RIGHT — each thread makes its own Message */
    // Thread A                           // Thread B
    var msgA = new Message(100);          var msgB = new Message(100);
    socket.Send(msgA);                    socket.Send(msgB);  // safe
    ```

=== "Rust"

    ```rust
    // RIGHT — each thread creates its own Message.
    // Rust enforces this: send() consumes the message, so you
    // must create a fresh one for each send call.
    // Thread A                           // Thread B
    let msg_a = Message::new(100);        let msg_b = Message::new(100);
    socket.send(msg_a)?;                  socket.send(msg_b)?;  // safe
    ```

=== "Go"

    ```go
    /* RIGHT — each goroutine makes its own Message */
    // Goroutine A                        // Goroutine B
    msgA := zlink.NewMessage(100)         msgB := zlink.NewMessage(100)
    socket.Send(msgA)                     socket.Send(msgB)  // safe
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
    // Assuming app_queue is a thread-safe queue (e.g., from your application)
    socket.on_receive([](const zlink::routing_id_t &source_rid,
                         std::vector<zlink::message_t> &parts) {
        for (auto &part : parts) {
            // Copy data and push to your own thread-safe queue
            app_queue.push(std::string(
                static_cast<const char *>(part.data()), part.size()));
        }
        // Return quickly — a worker thread processes the queue
    });
    ```

=== "Java"

    ```java
    BlockingQueue<byte[]> queue = new LinkedBlockingQueue<>();

    socket.onReceive((routingId, parts) -> {
        for (Message part : parts) {
            // Copy data and push to your own thread-safe queue
            queue.offer(part.toByteArray());
            part.close();
        }
        // Return quickly — a worker thread processes the queue
    });
    ```

=== "Python"

    ```python
    import queue

    work_queue = queue.Queue()

    def on_message(received):
        for part in received.parts:
            # Copy data and push to your own thread-safe queue
            work_queue.put(bytes(part))
            part.close()
        # Return quickly — a worker thread processes the queue

    socket.on_receive(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    // Node callbacks run on the event loop — avoid blocking.
    const workQueue: Buffer[] = [];

    socket.onReceive((routingId, parts) => {
        for (const part of parts) {
            // Copy data to your own queue
            workQueue.push(Buffer.from(part.data));
        }
        // Return quickly — process the queue asynchronously
    });
    ```

=== "C#/.NET"

    ```csharp
    var queue = new ConcurrentQueue<byte[]>();

    socket.OnReceive((routingId, parts) =>
    {
        foreach (var part in parts)
        {
            // Copy data and push to your own thread-safe queue
            queue.Enqueue(part.ToArray());
            part.Dispose();
        }
        // Return quickly — a worker thread processes the queue
    });
    ```

=== "Rust"

    ```rust
    use std::sync::mpsc;

    let (tx, rx) = mpsc::channel::<Vec<u8>>();

    socket.on_receive(move |received| {
        for part in &received.parts {
            // Copy data and push to channel
            let _ = tx.send(part.as_slice().to_vec());
        }
        // Return quickly — a worker thread receives from rx
    })?;
    ```

=== "Go"

    ```go
    workCh := make(chan []byte, 1024)

    socket.OnReceive(func(received *zlink.Received) {
        for _, part := range received.Parts {
            // Copy data and push to channel
            data := make([]byte, part.Size())
            copy(data, part.Data())
            workCh <- data
            part.Close()
        }
        // Return quickly — a goroutine processes the channel
    })
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
    void publisher(zlink::service::spot_t &spot)
    {
        for (int i = 0; i < 100000; i++) {
            zlink::message_t part(16);
            spot.publish("prices", part);
        }
    }

    void control(zlink::service::spot_t &spot)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        spot.set_subscription("audit.*");      // safe while publishing
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        spot.unset_subscription("audit.*");    // also safe
    }
    ```

=== "Java"

    ```java
    Thread publisher = new Thread(() -> {
        for (int i = 0; i < 100000; i++)
            spot.publish("prices", new Message(16));
    });

    Thread control = new Thread(() -> {
        Thread.sleep(100);
        spot.setSubscription("audit.*");      // safe while publishing
        Thread.sleep(200);
        spot.unsetSubscription("audit.*");    // also safe
    });
    ```

=== "Python"

    ```python
    def publisher(spot):
        for _ in range(100000):
            spot.publish("prices", bytes(16))

    def control(spot):
        time.sleep(0.1)
        spot.set_subscription("audit.*")      # safe while publishing
        time.sleep(0.2)
        spot.unset_subscription("audit.*")    # also safe
    ```

=== "Node/TypeScript"

    ```typescript
    // Single-threaded — publish and subscription changes are
    // naturally serialized. Both are safe to interleave.
    spot.setSubscription('audit.*');

    for (let i = 0; i < 100000; i++) {
        spot.publish('prices', Buffer.alloc(16));
    }

    spot.unsetSubscription('audit.*');
    ```

=== "C#/.NET"

    ```csharp
    var publisher = Task.Run(() =>
    {
        for (int i = 0; i < 100000; i++)
            spot.Publish("prices", new Message(16));
    });

    var control = Task.Run(async () =>
    {
        await Task.Delay(100);
        spot.SetSubscription("audit.*");      // safe while publishing
        await Task.Delay(200);
        spot.UnsetSubscription("audit.*");    // also safe
    });
    ```

=== "Rust"

    ```rust
    let handle = spot.send_handle();
    let publisher = std::thread::spawn(move || {
        for _ in 0..100_000 {
            handle.publish("prices", Message::new(16)).unwrap();
        }
    });

    std::thread::sleep(Duration::from_millis(100));
    spot.set_subscription("audit.*")?;      // safe while publishing
    std::thread::sleep(Duration::from_millis(200));
    spot.unset_subscription("audit.*")?;    // also safe
    ```

=== "Go"

    ```go
    go func() {
        for i := 0; i < 100000; i++ {
            msg := zlink.NewMessage(16)
            spot.Publish("prices", msg)
        }
    }()

    time.Sleep(100 * time.Millisecond)
    spot.SetSubscription("audit.*")      // safe while publishing
    time.Sleep(200 * time.Millisecond)
    spot.UnsetSubscription("audit.*")    // also safe
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
