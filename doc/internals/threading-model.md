[English](threading-model.md) | [한국어](threading-model.ko.md)

# Threading and Concurrency Model

## 1. Thread Structure

### 1.1 Thread Types

| Thread | Role | Count |
|--------|------|------|
| Application Thread | Calls zlink_send/recv | User-defined |
| I/O Thread | Boost.Asio io_context async processing | Configurable (default 2) |
| Reaper Thread | Resource cleanup for terminated sockets/sessions | 1 |

### 1.2 Thread Diagram
```
┌─────────────────────────────────────────────────────────┐
│  Application Threads                                     │
│  zlink_send() / zlink_recv()                             │
│  Recommended: access each socket from a single thread    │
└──────────────────────┬──────────────────────────────────┘
                       │ Lock-free Pipes (YPipe)
┌──────────────────────┼──────────────────────────────────┐
│  I/O Threads                                             │
│  Thread 0 (io_context) │ Thread 1 │ ... │ Thread N       │
│  Async I/O, encoding/decoding, network send/receive      │
└──────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────┐
│  Reaper Thread                                           │
│  Terminated socket/session resource cleanup, deferred    │
│  deletion                                                │
└──────────────────────────────────────────────────────────┘
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
```
Application Thread              I/O Thread
      │                              │
      │  zlink_send()                │
      │  [Push msg_t to YPipe]       │
      │  mailbox.send(activate_write)│
      │─────────────────────────────►│
      │                         [Pop from YPipe]
      │                         [Encode and transmit]
```

## 3. I/O Thread Selection
- Based on affinity mask
- Selects the thread with the least load
- Set count with zlink_ctx_set(ctx, ZLINK_IO_THREADS, n)

## 4. Concurrency Rules
- Socket: Single-thread access recommended (non-thread-safe)
- Context: Thread-safe (sockets can be created from multiple threads)
- pipe_t: Lock-free (CAS-based YPipe)
- Cache line optimization, visibility guaranteed through memory barriers
