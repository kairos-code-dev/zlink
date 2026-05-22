[English](./threading-model.md) | [한국어](./threading-model.ko.md)

# Threading and Concurrency Model

This document explains the internal threading architecture of zlink: which
threads exist, what each one does, how they communicate, and how work is
scheduled. The public API safety contract lives in
[Thread-Safety Internals](./thread-safety.md).

## 1. Thread Structure

### 1.1 Thread Types

| Thread | Role | Count |
|--------|------|-------|
| Application thread | Calls `zlink_send()`, `zlink_recv()`, `bind()`, `connect()`, etc. | User-defined |
| I/O thread | Runs a Boost.Asio `io_context`; performs async network I/O, frame encoding/decoding, and socket event dispatch | Configurable (default: 4) |
| Reaper thread | Deferred destruction of terminated sockets and sessions | 1 (global) |
| SpotNode data-plane thread | Exclusively owns `mesh-pub`, `fanout`, `external-router` sockets; drains ingress queues; handles local fanout and peer mesh forwarding | 1 per SpotNode |
| Dispatch worker thread | Executes Spot dispatch callbacks; per-Spot serialization and coalescing | N per SpotNode (default: `min(2, cpu_count)` – `max(1, cpu_count)`) |

Set the I/O thread count at context creation time:

```c
void *ctx = zlink_ctx_new();
zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);  /* default is ZLINK_IO_THREADS_DFLT = 4 */
```

I/O threads are started when the context is created and stopped when
`zlink_ctx_term()` is called. They cannot be added or removed after the context
is running.

### 1.2 Thread Diagram

```mermaid
flowchart TB
    subgraph APP["Application Threads (any count)"]
        direction LR
        A1["zlink_send() / zlink_recv()"]
        A2["bind() / connect() / set_option()"]
    end
    subgraph IO["I/O Threads (configurable)"]
        direction LR
        T0["Thread 0\n(io_context)"]
        T1["Thread 1"]
        TN["Thread N"]
    end
    subgraph REAPER["Reaper Thread (1)"]
        R1["Deferred socket/session cleanup"]
    end
    subgraph SPOT_DATA["SpotNode Data-plane Thread (1 per SpotNode)"]
        DP["mesh-pub / fanout / external-router sockets\ningress queue drain · local fanout · peer mesh forwarding"]
    end
    subgraph SPOT_WORKERS["Dispatch Worker Threads (N per SpotNode)"]
        W1["Execute Spot dispatch callbacks"]
    end
    APP -- "YPipe (lock-free)" --> IO
    IO -- "command_t (close/stop)" --> REAPER
    APP -- "publish_ingress_queue\nrouted_send_queue\n(signaler-based wakeup)" --> SPOT_DATA
    SPOT_DATA -- "post_dispatch_event()\n(ready queue)" --> SPOT_WORKERS
    SPOT_WORKERS -. "may call zlink_spot_* API" .-> APP
```

---

## 2. Design Rationale

The separation of application threads from I/O threads serves two goals:

1. **Latency isolation**: application threads are never blocked waiting for
   network I/O. A `zlink_send()` call writes to a lock-free pipe and returns;
   the I/O thread picks up the data asynchronously.
2. **Concurrency without per-socket locks**: because each socket is pinned to
   exactly one I/O thread's event loop, socket internals do not need locks for
   normal data-path operations. Synchronization is pushed to the
   application-thread entry points via the admission gate.

The Reaper thread avoids a class of use-after-free and double-free bugs:
resources that the I/O thread cannot safely release while its event loop is
running are handed off to the Reaper, which runs outside any event loop.

---

## 3. I/O Thread Assignment

### 3.1 Socket-to-thread pinning

When a socket is created it is assigned to one I/O thread. That assignment
never changes for the socket's lifetime. All data-plane operations for the
socket (send, recv, timers, callbacks) run on that single I/O thread.

### 3.2 Load balancing (least-load)

The assignment uses a **least-load** policy: the I/O thread with the fewest
currently assigned sockets receives the next socket. This spreads sockets
evenly across threads.

```
new_socket → argmin(socket_count[t] for t in io_threads)
```

If two threads are tied, the lower-index thread wins. The count is updated
atomically at socket creation and again at socket close; there is no
rebalancing after assignment.

### 3.3 Affinity mask

The per-socket `ZLINK_OPT_AFFINITY` option restricts which I/O threads are
eligible for that socket's assignment. Bit N set to `1` makes I/O thread N
eligible; `0` (the default) allows all threads. The mask is consulted only when
the socket is assigned at creation time; it does not rebalance a socket that is
already assigned.

```c
/* Restrict this socket to I/O threads 0 and 2 */
uint64_t mask = (1ULL << 0) | (1ULL << 2);
zlink_set_option(socket, ZLINK_OPT_AFFINITY, &mask, sizeof(mask));
```

---

## 4. Inter-Thread Communication

### 4.1 YPipe — lock-free data path

Message data travels from application threads to I/O threads through
**YPipe**, a single-producer single-consumer lock-free FIFO. Each socket has
one YPipe per direction (send / recv). The algorithm uses a CAS-based
two-pointer scheme with a "flush batch" optimization:

```
application thread        I/O thread
-----------------         ----------
push(msg) to YPipe  →  pop(msg) from YPipe
flush() signal      →  wake up io_context
```

Because YPipe is SPSC, the only atomic operation needed is the pointer swap
during flush. No mutex, no per-message atomic.

### 4.2 Mailbox — control commands

Low-frequency control operations (bind, connect, set_option, socket close) are
serialized through a **Mailbox** — a thread-safe command queue backed by a
`signaler_t` (an eventfd or pipe-based file descriptor):

```cpp
class mailbox_t {
    ypipe_t<command_t> _commands;  /* SPSC queue of commands */
    signaler_t _signaler;          /* wakes the io_context when commands are enqueued */
};
```

Command types include: `stop`, `plug`, `attach`, `bind`, `activate_read`,
`activate_write`, `hiccup`, and `term`. The I/O thread drains the Mailbox at
the start of each event-loop iteration.

### 4.3 Data flow summary

```mermaid
sequenceDiagram
    participant App as Application Thread
    participant Pipe as YPipe (lock-free)
    participant MBox as Mailbox
    participant IOT as I/O Thread (io_context)

    App->>Pipe: push msg_t frames
    App->>MBox: activate_write command
    MBox->>IOT: signaler wakes io_context
    IOT->>Pipe: pop frames
    IOT->>IOT: encode and transmit over network
```

---

## 5. Reaper Thread

When a socket is closed the I/O thread cannot always free its resources
immediately — in-flight async operations hold references to session objects.
The I/O thread sends a `term` command to the Reaper once all I/O for that
socket has completed. The Reaper then deallocates the resources from a
dedicated thread that holds no event-loop locks.

The Reaper uses its own Mailbox and runs a simple drain loop:

```
while (command = mailbox.recv()) {
    if (command.type == TERM) free(command.object);
    if (command.type == STOP) break;
}
```

---

## 6. SpotNode Data-plane Thread and Dispatch Worker Pool

### 6.1 Data-plane thread

Each `spot_node_t` runs one dedicated OS thread (`spot_runtime_t::data_plane_thread`)
executing `spot_data_plane_loop_t::run_until_shutdown()`. This thread exclusively
owns:

- `mesh-pub`, `fanout`, `external-router`, `mesh-xsub`, `peer_ctrl_pub`,
  `peer_ctrl_sub` sockets
- drain of `publish_ingress_queue`, `routed_send_queue`,
  `external_router_ingress_queue`
- local fanout delivery, remote mesh publish, inbound/outbound routed forwarding

Public threads do not access these sockets directly. Violating this boundary
mixes socket ownership, poller interest registration, and shutdown ordering into
the public call path.

```
Invariants:
  Public threads must not send/recv on mesh-pub, fanout, or external-router directly.
  The data-plane thread must not invoke application dispatch callbacks directly.
```

The data-plane thread loop uses a poller combined with signaler FDs from all
three queues. Any empty→non-empty queue transition wakes the thread immediately.
The idle tick is 100 ms.

Queue drain priority order:

| Order | Queue | Description |
|-------|-------|-------------|
| 1 | `external_router_ingress_queue` | Routing frames received from peers |
| 2 | `publish_ingress_queue` | Topic publishes submitted by the application |
| 3 | `routed_send_queue` | Routed sends submitted by the application |
| 4 | flush `mesh-pub` pending | Forward publishes to remote peers |
| 5 | flush `fanout` pending | Deliver to local subscribers |
| 6 | flush staged messages | Transmit remaining staged frames |

Batch limit: 2048 messages or 16 MiB per drain iteration.

### 6.2 Dispatch Worker Pool

`spot_runtime_t::dispatch_workers` (`spot_dispatch_worker_pool_t`) executes
application dispatch callbacks. The data-plane thread never calls callbacks
directly. Instead, when a target Spot state becomes ready, it calls
`post_dispatch_event(void* spot_)` on the pool. The pool deduplicates using a
`_queued` set: the same Spot pointer is never enqueued twice.

Per-Spot serialization: at most one worker runs a given Spot at a time. After a
callback returns, if `_dirty` shows unread events for that Spot, it is
re-enqueued into `_ready`.

Worker count defaults:

```text
cpu_count = max(1, hardware_concurrency)
default_min = min(2, cpu_count)
default_max = max(1, cpu_count)
idle_timeout = 1000 ms (internal constant)
```

| Option | Default | Meaning |
|--------|---------|---------|
| `ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN` | `min(2, cpu_count)` | Always-maintained worker count |
| `ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX` | `max(1, cpu_count)` | Burst ceiling |

Why the data-plane thread does not call callbacks directly:

1. Application callbacks may call SPOT send/recv APIs — reentrance into
   data-plane locks or socket ownership.
2. Slow callbacks stall `mesh-pub` and `external-router` flushes.
3. `ZLINK_POLLOUT` and send-ready callbacks are in the dispatch axis — mixing
   them with the forwarding loop corrupts readiness/forwarding ordering.

---

## 7. NUMA and CPU Pinning

zlink does not pin I/O threads to specific CPU cores internally. If NUMA
locality matters, the application should:

1. Set `ZLINK_IO_THREADS` to match the NUMA topology.
2. Use the affinity mask to assign groups of sockets to specific I/O threads.
3. Use OS-level CPU affinity (`pthread_setaffinity_np` or equivalent) on the
   application threads that call `zlink_send()` on those sockets.

Because a socket is pinned to one I/O thread for life, keeping the application
thread and its I/O thread on the same NUMA node eliminates cross-node YPipe
accesses.

---

## 8. Concurrent Access Patterns

The threading model makes the following concurrent patterns safe and
well-defined (see [Thread-Safety Internals](./thread-safety.md) for the full
contract). Hot paths such as `send`/`publish`/`send_rid` are admitted through
the lightweight data-plane gate; control paths serialize for correctness.

### 8.1 Multiple threads calling `zlink_send()` concurrently

Each `zlink_send()` call acquires the admission gate, writes to the shared
YPipe under a lightweight spinlock, and signals the Mailbox. The I/O thread
drains the YPipe independently. This means:

```c
/* Thread A */          /* Thread B */
zlink_send(s, &a, 1, 0);   zlink_send(s, &b, 1, 0);
/* both safe — hot-path admission guard serializes pipe writes */
```

### 8.2 One thread closing while another is sending

`zlink_close()` uses a fail-fast lifecycle gate: it returns `ZLINK_CLOSE_BUSY`
immediately if any thread is inside a hot-path API on that handle. The closing
thread must retry until it succeeds. Once accepted, new API calls on the
handle return `ZLINK_CLOSE_SHUTDOWN`.

### 8.3 Callback delivery vs. concurrent send

Callbacks registered with `*_handler()` run on the I/O thread. Application
threads can still call `zlink_send()` concurrently — the admission gate
separates the callback path from the send path. Never call `zlink_close()` on
the socket from inside its own callback; that creates a deadlock.

---

## 9. Context Thread Safety

The context object (`zlink_ctx_new()`) is fully thread-safe. Sockets may be
created and destroyed from different threads simultaneously:

```c
/* safe: multiple threads creating sockets from the same context */
void *s1 = zlink_socket(ctx, ZLINK_SOCKET_DEALER);  /* thread A */
void *s2 = zlink_socket(ctx, ZLINK_SOCKET_DEALER);  /* thread B */
```

`zlink_ctx_term()` blocks until all sockets are closed. Call it from the same
thread that manages socket lifetime, or ensure all sockets are closed before
calling it.

---

## 10. Summary

| Property | Value |
|----------|-------|
| Socket pinning | One I/O thread per socket, fixed at creation |
| Assignment policy | Least-load (fewest sockets) |
| Application→I/O data path | Lock-free YPipe (SPSC) |
| Application→I/O control path | Mailbox (thread-safe, signaler-based) |
| Deferred cleanup | Reaper thread (one global) |
| Concurrent sends | Safe via admission gate |
| Concurrent close + send | Safe; close returns `BUSY` until hot-path callers exit |
| Callback thread | Always the socket's assigned I/O thread |
| SpotNode data-plane thread | 1 per SpotNode; exclusively owns mesh-pub/fanout/external-router |
| Application→data-plane path | publish_ingress_queue / routed_send_queue (signaler-based wakeup) |
| Dispatch worker thread | N per SpotNode; per-Spot serialization and coalescing; default min(2,cpu)–max(1,cpu) |
| Dispatch worker idle timeout | 1000 ms (internal constant) |
| Spot dispatch callback thread | Dispatch worker thread (not the data-plane thread) |

---
[← Architecture](./architecture.md) | [Thread-Safety →](./thread-safety.md)
