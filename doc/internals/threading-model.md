[English](threading-model.md) | [한국어](threading-model.ko.md)

# Threading and Concurrency Model

## 1. Thread Structure

### 1.1 Thread Types

| Thread | Role | Count |
|--------|------|------|
| Application Thread | Calls zlink_send/recv | User-defined |
| I/O Thread | Boost.Asio io_context async processing | Configurable (default 1; see `ZLINK_IO_THREADS_DFLT`) |
| Reaper Thread | Resource cleanup for terminated sockets/sessions | 1 |

### 1.2 Thread Diagram
```mermaid
flowchart TB
    subgraph APP["Application Threads"]
        direction LR
        A1["zlink_send() / zlink_recv()"]
        A2["Thread-safe: concurrent sends allowed (see thread-safety guide)"]
    end
    subgraph IO["I/O Threads"]
        direction LR
        T0["Thread 0 (io_context)"]
        T1["Thread 1"]
        TN["Thread N"]
        IO_DESC["Async I/O, encoding/decoding, network send/receive"]
    end
    subgraph REAPER["Reaper Thread"]
        R1["Terminated socket/session resource cleanup, deferred deletion"]
    end
    APP -- "Lock-free Pipes (YPipe)" --> IO
    IO --> REAPER
```

## 2. Inter-Thread Communication

### 2.1 Mailbox System
```cpp
class mailbox_t {
    ypipe_t<command_t> _commands;  // Lock-free command queue
    signaler_t _signaler;           // Wake-up signal
};
```

Command types: stop, plug, attach, bind, activate_read, activate_write, etc.

### 2.2 Data Flow
```mermaid
sequenceDiagram
    participant App as Application Thread
    participant IOT as I/O Thread
    App->>App: zlink_send()
    App->>App: Push msg_t to YPipe
    App->>IOT: mailbox.send(activate_write)
    IOT->>IOT: Pop from YPipe
    IOT->>IOT: Encode and transmit
```

## 3. I/O Thread Selection
- Based on affinity mask
- Selects the thread with the least load
- Set count with zlink_ctx_set(ctx, ZLINK_IO_THREADS, n)

## 4. Concurrency Rules
- Public socket/service handles use a tiered contract: hot-path
  `send`/`publish`/`send_rid` can be called concurrently from multiple threads, low-frequency
  control paths serialize for correctness, and `close`/`destroy` use a
  stricter lifecycle gate
- Context: Thread-safe (sockets can be created from multiple threads)
- pipe_t: Lock-free (CAS-based YPipe)
- Cache line optimization, visibility guaranteed through memory barriers
- For the full concurrency contract implementation, see [Thread-Safety Internals](thread-safety.md)
