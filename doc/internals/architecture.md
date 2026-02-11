[English](architecture.md) | [한국어](architecture.ko.md)

# zlink System Architecture - Internal Developer Reference

This document describes the internal architecture of the **zlink** library in detail.
The intended audience is **internal developers** who develop or maintain the zlink library itself,
and it comprehensively covers the system's layer structure, core components, data flow, and source tree.

---

## Table of Contents

1. [Overview and Design Philosophy](#1-overview-and-design-philosophy)
2. [From Reactor to Proactor — I/O Model Migration](#2-from-reactor-to-proactor--io-model-migration)
3. [5-Layer Architecture](#3-5-layer-architecture)
4. [Component Relationships](#4-component-relationships)
5. [Socket Logic Layer Details](#5-socket-logic-layer-details)
6. [Engine Layer Details](#6-engine-layer-details)
7. [Core Components](#7-core-components)
8. [Data Flow](#8-data-flow)
9. [Source Tree Structure](#9-source-tree-structure)

---

## 1. Overview and Design Philosophy

### 1.1 What is zlink?

zlink is a high-performance messaging library based on libzmq.
While maintaining compatibility with libzmq's patterns and API, it applies the following modern design principles:

- **Boost.Asio-based I/O**: Uses Asio's unified asynchronous I/O instead of platform-specific pollers (epoll/kqueue/IOCP)
- **Native WebSocket/TLS support**: Built-in support for `ws://`, `wss://`, `tls://` protocols at the library level
- **Custom protocol stack**: Uses the lightweight **ZMP v1.0** protocol instead of ZMTP

### 1.2 Design Principles

```
┌──────────────────────┬──────────────────────────────────────────────────────┐
│       Principle       │                    Description                       │
├──────────────────────┼──────────────────────────────────────────────────────┤
│  Zero-Copy           │  Saves memory bandwidth by minimizing message copies │
│  Lock-Free           │  Uses lock-free data structures (YPipe) for ITC      │
│  True Async          │  True asynchronous I/O based on the Proactor pattern │
│  Protocol Agnostic   │  Clear separation of transport and protocol          │
└──────────────────────┴──────────────────────────────────────────────────────┘
```

### 1.3 Supported Sockets and Transports

**Socket Patterns (7 types)**

| Socket      | Type               | Description                        |
|-------------|--------------------|------------------------------------|
| PAIR        | 1:1 Bidirectional  | Single connection, bidirectional   |
| PUB / SUB   | Publish-Subscribe  | Topic-based broadcast              |
| XPUB / XSUB | Extended Pub-Sub  | Access to subscription messages    |
| DEALER      | Async Request      | Round-robin distribution           |
| ROUTER      | ID-based Routing   | Multi-client routing               |
| STREAM      | RAW TCP            | External client integration        |

**Transports (6 types)**

| Scheme     | Description                               |
|------------|-------------------------------------------|
| `tcp://`   | Standard TCP                              |
| `ipc://`   | Unix domain socket (Unix/Linux/macOS)     |
| `inproc://`| In-process communication (Lock-free pipe) |
| `ws://`    | WebSocket (Beast library)                 |
| `wss://`   | WebSocket over TLS                        |
| `tls://`   | Native TLS (OpenSSL)                      |

---

## 2. From Reactor to Proactor — I/O Model Migration

The most fundamental architectural change in zlink is the I/O model transition.
libzmq's **Reactor pattern** has been replaced with a Boost.Asio-based **Proactor pattern**.

### 2.1 Reactor Pattern (libzmq)

libzmq uses a classic **Reactor pattern**.
A central poller (`poller_t`) monitors fd readiness (readable/writable state)
and invokes engine handlers when fds become ready.

```
┌──────────────────────────────────────────────────────────────────┐
│                    libzmq Reactor Model                           │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │              poller_t (Central Event Loop)                │   │
│   │                                                          │   │
│   │   epoll_wait() / kqueue() / select() / IOCP              │   │
│   │              │                                           │   │
│   │              v                                           │   │
│   │   ┌──────────────────────┐                               │   │
│   │   │  fd ready (readable) │──→ engine->in_event()        │   │
│   │   │  fd ready (writable) │──→ engine->out_event()       │   │
│   │   │  fd error            │──→ engine->in_event()        │   │
│   │   └──────────────────────┘                               │   │
│   │                                                          │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│   Flow: register fd → wait for readiness → notify → read/write  │
│   Key: poller says "ready to read", then engine calls read()     │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

**Key characteristics:**
- Platform-specific poller implementations required (epoll, kqueue, devpoll, pollset, select, IOCP)
- Engines perform synchronous `read()`/`write()` inside `in_event()`/`out_event()` callbacks
- Each I/O thread owns one `poller_t` instance and runs an event loop
- Adding new transports requires conforming to the fd-based interface

### 2.2 Proactor Pattern (zlink)

zlink uses the Boost.Asio **Proactor pattern**.
Engines request asynchronous I/O operations from the OS, and the OS invokes completion callbacks when done.

```
┌──────────────────────────────────────────────────────────────────┐
│                    zlink Proactor Model                           │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │              asio_engine_t (Async Engine)                 │   │
│   │                                                          │   │
│   │   (1) async_read_some(buffer, handler)                   │   │
│   │       │  Delegate read to OS                             │   │
│   │       └──→ [OS kernel performs I/O] ──→ on_read_complete()│  │
│   │                                                          │   │
│   │   (2) async_write_some(buffer, handler)                  │   │
│   │       │  Delegate write to OS                            │   │
│   │       └──→ [OS kernel performs I/O] ──→ on_write_complete()│ │
│   │                                                          │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │              io_context (Boost.Asio)                      │   │
│   │                                                          │   │
│   │   io_context::run()                                      │   │
│   │   - Dispatches completion handlers for finished ops      │   │
│   │   - One io_context per I/O thread, single-threaded       │   │
│   │                                                          │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│   Flow: request async op → OS completes I/O → completion call   │
│   Key: engine never performs I/O directly, only handles results  │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

**Key characteristics:**
- Boost.Asio abstracts platform differences (epoll/kqueue/IOCP unified)
- Engines request via `async_read_some()`/`async_write_some()`, handle results in completion callbacks
- Each I/O thread owns an independent `io_context` — no contention between threads
- Transport abstraction (`i_asio_transport`) enables TCP/TLS/WS/WSS through a uniform interface

### 2.3 Reactor vs. Proactor Comparison

```
┌──────────────────┬──────────────────────────┬──────────────────────────────┐
│     Aspect        │   libzmq (Reactor)       │   zlink (Proactor)           │
├──────────────────┼──────────────────────────┼──────────────────────────────┤
│  I/O Model        │ Readiness-based          │ Completion-based             │
│                   │ "ready to read" → read() │ "read done" → callback       │
├──────────────────┼──────────────────────────┼──────────────────────────────┤
│  Main Loop        │ poller_t::loop()          │ io_context::run()            │
│                   │ (custom event loop)       │ (Boost.Asio event loop)      │
├──────────────────┼──────────────────────────┼──────────────────────────────┤
│  I/O Threads      │ poller_t per thread       │ io_context per thread        │
│                   │ + fd_table management     │ + independent execution       │
├──────────────────┼──────────────────────────┼──────────────────────────────┤
│  Engine Callbacks │ in_event() / out_event() │ on_read_complete()           │
│                   │                          │ on_write_complete()          │
├──────────────────┼──────────────────────────┼──────────────────────────────┤
│  Protocol         │ ZMTP 3.x                 │ ZMP v1.0 (8B fixed header)  │
├──────────────────┼──────────────────────────┼──────────────────────────────┤
│  Transport        │ Direct fd management     │ i_asio_transport abstraction │
│  Extension Cost   │ Must fit fd-based API    │ Implement interface only      │
├──────────────────┼──────────────────────────┼──────────────────────────────┤
│  Platform Pollers │ 6 implementations        │ Delegated to Boost.Asio      │
│                   │ (epoll,kqueue,IOCP,etc)  │ (single codebase)            │
├──────────────────┼──────────────────────────┼──────────────────────────────┤
│  Optimizations    │ Reactor event batching   │ Speculative I/O              │
│                   │                          │ Gather Write                 │
│                   │                          │ Backpressure (pending buf)   │
└──────────────────┴──────────────────────────┴──────────────────────────────┘
```

### 2.4 Migration Strategy

The port from libzmq to zlink used **selective per-layer replacement**, not a full rewrite.

```
┌─────────────────────────────────────────────────────────────────────┐
│                   Per-Layer: Preserved / Replaced / Added            │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ■ Preserved (kept from libzmq as-is)                               │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Socket Logic Layer                                           │ │
│  │  - socket_base_t, pair_t, dealer_t, router_t, pub_t, sub_t   │ │
│  │  - Routing strategies: lb_t, fq_t, dist_t                    │ │
│  │  - Subscription management: mtrie_t, radix_tree_t             │ │
│  ├───────────────────────────────────────────────────────────────┤ │
│  │  Inter-Thread Infrastructure                                  │ │
│  │  - YPipe (Lock-free queue, CAS-based)                         │ │
│  │  - pipe_t (Bidirectional message pipe)                        │ │
│  │  - mailbox_t + signaler_t (Inter-thread command delivery)     │ │
│  │  - command_t (20 internal command types)                      │ │
│  ├───────────────────────────────────────────────────────────────┤ │
│  │  Message System                                               │ │
│  │  - msg_t (64-byte fixed, VSM/LMSG/CMSG/ZCLMSG)              │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ■ Replaced (libzmq implementation swapped for new)                │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  poller_t (epoll/kqueue/select)  →  asio_poller_t            │ │
│  │  - Minimal reactor wrapper for mailbox monitoring             │ │
│  ├───────────────────────────────────────────────────────────────┤ │
│  │  zmtp_engine_t (ZMTP 3.x)  →  asio_engine_t (Proactor)      │ │
│  │  - Core I/O engine completely redesigned for completion-based │ │
│  ├───────────────────────────────────────────────────────────────┤ │
│  │  Direct fd management  →  i_asio_transport interface         │ │
│  │  - TCP/IPC wrapped with Boost.Asio sockets                   │ │
│  ├───────────────────────────────────────────────────────────────┤ │
│  │  ZMTP 3.x  →  ZMP v1.0                                       │ │
│  │  - Simplified to 8-byte fixed header, HELLO/READY handshake  │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
│  ■ Added (new in zlink)                                            │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Speculative I/O                                              │ │
│  │  - Synchronous attempt before async → eliminates callback     │ │
│  │    overhead on fast path                                      │ │
│  ├───────────────────────────────────────────────────────────────┤ │
│  │  Backpressure (pending_buffers)                               │ │
│  │  - Buffers received data up to 10MB when HWM reached         │ │
│  ├───────────────────────────────────────────────────────────────┤ │
│  │  Gather Write                                                 │ │
│  │  - Scatter/gather I/O sends header+payload in single syscall │ │
│  ├───────────────────────────────────────────────────────────────┤ │
│  │  Native WS/WSS/TLS Transports                                │ │
│  │  - Beast WebSocket + OpenSSL unified via i_asio_transport    │ │
│  ├───────────────────────────────────────────────────────────────┤ │
│  │  Service Layer (Registry, Discovery, Gateway, Receiver, SPOT)│ │
│  │  - Higher-level service abstractions not present in libzmq   │ │
│  └───────────────────────────────────────────────────────────────┘ │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

**Why wasn't the Reactor completely removed?**

`asio_poller_t` remains as a minimal Reactor-compatible wrapper for monitoring mailbox fds.
The existing libzmq `io_object_t` infrastructure receives mailbox events through poller callbacks,
so this path is wrapped with Asio's `async_wait()` to maintain compatibility.
The actual data I/O path (`asio_engine_t`) operates as a pure Proactor pattern.

---

## 3. 5-Layer Architecture

zlink is composed of 5 clearly separated layers.
Each layer has a single responsibility, and layers closer to the bottom are closer to the physical network.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          APPLICATION LAYER                              │
│                                                                         │
│   User code:                                                            │
│   zlink_ctx_new() -> zlink_socket() -> zlink_bind/connect()             │
│   -> zlink_send() / zlink_recv() -> zlink_close()                       │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                           PUBLIC API LAYER                              │
│                                                                         │
│   src/api/zlink.cpp                                                     │
│   - C API entry points (zlink_socket, zlink_send, zlink_recv, etc.)     │
│   - Error handling and parameter validation                             │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                          SOCKET LOGIC LAYER                             │
│                                                                         │
│   src/sockets/                                                          │
│   - socket_base_t: Base class for all sockets                           │
│   - pair_t, dealer_t, router_t, pub_t, sub_t, xpub_t, xsub_t, stream_t │
│   - Routing strategies: lb_t(RR), fq_t(Fair Queue), dist_t(Fan-out)    │
│   - Subscription management: mtrie_t(XPUB), radix_tree_t /             │
│     trie_with_size_t(XSUB)                                             │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                          ENGINE LAYER (ASIO)                            │
│                                                                         │
│   src/engine/asio/                                                      │
│   - asio_engine_t      : Proactor pattern-based async I/O engine (base) │
│   - asio_zmp_engine_t  : ZMP protocol (8B fixed header + handshake)     │
│   - asio_raw_engine_t  : RAW protocol (4B Length-Prefix, STREAM only)   │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                          PROTOCOL LAYER                                 │
│                                                                         │
│   ┌───────────────────────────┐    ┌───────────────────────────┐       │
│   │    ZMP v1.0 Protocol      │    │     RAW Protocol          │       │
│   │    src/protocol/zmp_*     │    │     src/protocol/raw_*    │       │
│   │    - 8-byte fixed header  │    │     - 4-byte length prefix│       │
│   │    - Handshake support    │    │     - No handshake        │       │
│   └───────────────────────────┘    └───────────────────────────┘       │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                          TRANSPORT LAYER                                │
│                                                                         │
│   src/transports/                                                       │
│   ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌──────────┐                 │
│   │   TCP   │  │   IPC   │  │   WS    │  │ TLS/WSS  │                 │
│   │  tcp_   │  │  ipc_   │  │  ws_    │  │  ssl_    │                 │
│   │transport│  │transport│  │transport│  │transport │                 │
│   └─────────┘  └─────────┘  └─────────┘  └──────────┘                 │
│                                                                         │
│   i_asio_transport: Unified async interface for all transports          │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

**Message passing path between layers**:
- Downward (Tx): Application -> API -> Socket Logic -> pipe_t -> Engine -> Protocol -> Transport
- Upward (Rx): Transport -> Protocol -> Engine -> pipe_t -> Socket Logic -> API -> Application

---

## 4. Component Relationships

The diagram below shows the ownership relationships and interactions between zlink internal objects.

```
┌──────────────────────────────────────────────────────────────────────┐
│                              ctx_t                                   │
│  (Global context: I/O thread pool, socket management, inproc        │
│   endpoints)                                                         │
└────────────────────────────────┬─────────────────────────────────────┘
                                 │ owns
            ┌────────────────────┼────────────────────┐
            │                    │                    │
            v                    v                    v
    ┌───────────────┐   ┌───────────────┐   ┌───────────────┐
    │  socket_base_t│   │  io_thread_t  │   │   reaper_t    │
    │ (socket inst.) │   │ (I/O worker)  │   │(resource      │
    │               │   │               │   │ cleanup)      │
    └───────┬───────┘   └───────┬───────┘   └───────────────┘
            │                   │
            │ owns              │ runs
            v                   v
    ┌───────────────┐   ┌───────────────┐
    │ session_base_t│   │  io_context   │
    │ (session mgmt)│   │ (Asio reactor)│
    └───────┬───────┘   └───────────────┘
            │
     ┌──────┴──────┐
     │             │
     v             v
┌─────────┐  ┌─────────────┐
│ pipe_t  │  │asio_engine_t│
│(msg que)│  │ (I/O engine) │
└─────────┘  └──────┬──────┘
                    │
                    v
            ┌───────────────┐
            │i_asio_transport│
            │  (transport)   │
            └───────────────┘
```

**Key ownership relationships**:

- `ctx_t` owns all `socket_base_t`, `io_thread_t`, and `reaper_t` instances.
- `socket_base_t` owns `session_base_t`, which acts as a bridge between the socket and the engine.
- `session_base_t` owns `pipe_t` (lock-free message queue) and `asio_engine_t` (I/O engine).
- `asio_engine_t` communicates with the physical transport layer through the `i_asio_transport` interface.
- `io_thread_t` holds an independent `io_context` for processing asynchronous I/O.
- `reaper_t` safely cleans up resources for terminated sockets/sessions.

---

## 5. Socket Logic Layer Details

### 5.1 Class Hierarchy

```
socket_base_t (base class)
├── pair_t              # PAIR socket: 1:1 bidirectional communication
├── dealer_t            # DEALER socket: async request, round-robin
├── router_t            # ROUTER socket: ID-based routing (inherits routing_socket_base_t)
├── xpub_t              # XPUB socket: can receive subscription messages
│   └── pub_t           # PUB socket: simplified XPUB (no subscription exposure)
├── xsub_t              # XSUB socket: can send subscription messages
│   └── sub_t           # SUB socket: simplified XSUB (subscribe via setsockopt)
└── stream_t            # STREAM socket: RAW TCP, external client integration
```

`socket_base_t` provides common functionality for all sockets:
- Connection management (`bind`, `connect`, `disconnect`, `unbind`)
- Pipe management (creation, termination, activation)
- Option management (`setsockopt`, `getsockopt`)
- Polling support (`has_in`, `has_out`)

### 5.2 Routing Strategy Classes

Strategy classes for message distribution and collection are separated by socket type:

```
┌──────────────────────────────────────────────────────────────────────┐
│                     Routing Strategies                                │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  lb_t (Load Balancer) - Sender-side round-robin              │    │
│  │                                                               │    │
│  │  Pipe A ──→ [ msg1 ]                                         │    │
│  │  Pipe B ──→ [ msg2 ]    ← Distributes in order               │    │
│  │  Pipe C ──→ [ msg3 ]                                         │    │
│  │                                                               │    │
│  │  Used by: DEALER (Tx)                                        │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  fq_t (Fair Queue) - Receiver-side fair queue                │    │
│  │                                                               │    │
│  │  Pipe A ←── [ msg ]                                          │    │
│  │  Pipe B ←── [ msg ]    ← Fairly receives from each pipe     │    │
│  │  Pipe C ←── [ msg ]                                          │    │
│  │                                                               │    │
│  │  Used by: DEALER (Rx), SUB (Rx)                              │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  dist_t (Distributor) - Broadcast fan-out                    │    │
│  │                                                               │    │
│  │  [ msg ] ──→ Pipe A                                          │    │
│  │          ──→ Pipe B    ← Sends the same message to all pipes │    │
│  │          ──→ Pipe C                                          │    │
│  │                                                               │    │
│  │  Used by: PUB, XPUB (Tx)                                    │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

### 5.3 Routing Strategy Mapping by Socket

| Socket  | Tx                    | Rx                   | Notes                            |
|---------|-----------------------|----------------------|----------------------------------|
| PAIR    | Single pipe           | Single pipe          | Only 1 pipe allowed              |
| DEALER  | `lb_t` (Round-robin)  | `fq_t` (Fair Queue)  | Async request-reply              |
| ROUTER  | ID-based direct route | `fq_t` (Fair Queue)  | Finds target pipe by Routing ID  |
| PUB     | `dist_t` (Fan-out)    | -                    | Cannot receive                   |
| SUB     | -                     | `fq_t` (Fair Queue)  | Topic filtering applied          |
| XPUB    | `dist_t` (Fan-out)    | Receives sub messages| Subscription managed by mtrie_t  |
| XSUB    | Sends sub messages    | `fq_t` (Fair Queue)  | Filtering + subscription sending |
| STREAM  | ID-based direct route | `fq_t` (Fair Queue)  | Uses RAW protocol                |

### 5.4 Subscription Data Structures

Trie-based data structures used for topic matching in PUB/SUB patterns:

```
┌─────────────────────────────────────────────────────────────┐
│                 Subscription Topic Trie Structure             │
│                                                              │
│                       (root)                                 │
│                      /      \                                │
│                  "news"    "stock"                            │
│                   /          /   \                            │
│              "weather"   "AAPL"  "GOOGL"                     │
│                                                              │
│  - XPUB: mtrie_t (multi-trie, per-pipe subscription tracking)│
│  - XSUB: Depends on ZLINK_USE_RADIX_TREE macro              │
│    - radix_tree_t (when enabled, memory-efficient)           │
│    - trie_with_size_t (default, fast lookup)                 │
│  - Lookup complexity: O(m), m = topic string length          │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## 6. Engine Layer Details

### 6.1 Engine Type Comparison

The Engine Layer handles asynchronous I/O processing based on Boost.Asio.

| Engine                | Protocol  | Transports               | Features                           |
|-----------------------|-----------|--------------------------|-------------------------------------|
| `asio_zmp_engine_t`   | ZMP v1.0  | TCP, TLS, IPC, WS, WSS  | Handshake + 8-byte fixed header    |
| `asio_raw_engine_t`   | RAW       | TCP, TLS, IPC, WS, WSS  | 4-byte length prefix, STREAM only  |

> WS/WSS also use `asio_zmp_engine_t` or `asio_raw_engine_t`;
> WebSocket framing is handled by `ws_transport_t`/`wss_transport_t`.

### 6.2 Proactor Pattern Structure

```
┌──────────────────────────────────────────────────────────────────┐
│                        asio_engine_t                             │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────────┐         ┌──────────────────────────────┐  │
│  │ async_read_some │--------→│      on_read_complete        │  │
│  │  (async read)   │         │  - Data receive complete CB   │  │
│  └─────────────────┘         │  - Parse message via decoder  │  │
│                              │  - Forward message to session │  │
│                              └──────────────────────────────┘  │
│                                                                  │
│  ┌─────────────────┐         ┌──────────────────────────────┐  │
│  │async_write_some │--------→│     on_write_complete        │  │
│  │  (async write)  │         │  - Data send complete CB      │  │
│  └─────────────────┘         │  - Encode next message        │  │
│                              │  - Repeat if more data to send│  │
│                              └──────────────────────────────┘  │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                  Speculative I/O (Optimization)          │   │
│  │                                                          │   │
│  │  speculative_read():                                     │   │
│  │    Attempts synchronous read first for immediately       │   │
│  │    available data                                        │   │
│  │    -> Improves throughput without async overhead          │   │
│  │                                                          │   │
│  │  speculative_write():                                    │   │
│  │    Writes synchronously if immediately possible          │   │
│  │    -> Completes instantly without callback on success    │   │
│  │    -> Falls back to async_write_some() on EAGAIN         │   │
│  │                                                          │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                   Backpressure                           │   │
│  │                                                          │   │
│  │  _pending_buffers: Temporary storage for unprocessed data│   │
│  │  max_pending_buffer_size: 10MB limit                     │   │
│  │  Pauses reading when limit exceeded -> resumes when      │   │
│  │  space becomes available                                 │   │
│  │                                                          │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### 6.3 Engine State Machine

```
          ┌─────────────────────┐
          │      Created        │
          └──────────┬──────────┘
                     │ plug()
                     v
          ┌─────────────────────┐
          │    Handshaking      │  TLS/WebSocket: transport handshake
          │  (_handshaking)     │  ZMP: protocol handshake
          └──────────┬──────────┘
                     │ handshake complete
                     v
          ┌─────────────────────┐
          │      Active         │ <-----------------┐
          │   Data send/recv    │                   │
          └──────────┬──────────┘                   │
                     │ I/O error                     │ restart
                     v                              │
          ┌─────────────────────┐                   │
          │       Error         │ -----------------┘
          └──────────┬──────────┘
                     │ terminate()
                     v
          ┌─────────────────────┐
          │    Terminated       │
          └─────────────────────┘
```

### 6.4 ZMP v1.0 Frame Structure

```
 Byte:   0         1         2         3         4    5    6    7
      ┌─────────┬─────────┬─────────┬─────────┬─────────────────────┐
      │  MAGIC  │ VERSION │  FLAGS  │RESERVED │   PAYLOAD SIZE      │
      │  (0x5A) │  (0x01) │         │ (0x00)  │   (32-bit BE)       │
      └─────────┴─────────┴─────────┴─────────┴─────────────────────┘
```

| Field        | Offset | Size | Description               |
|-------------|--------|------|---------------------------|
| MAGIC       | 0      | 1    | Magic number `0x5A` ('Z') |
| VERSION     | 1      | 1    | Protocol version `0x01`   |
| FLAGS       | 2      | 1    | Frame flags               |
| RESERVED    | 3      | 1    | Reserved (0x00)           |
| PAYLOAD SIZE| 4-7    | 4    | Payload size (Big Endian) |

**FLAGS bit definitions**:

| Bit  | Name      | Description            |
|------|-----------|------------------------|
| 0    | MORE      | Multipart message cont.|
| 1    | CONTROL   | Control frame          |
| 2    | IDENTITY  | Contains Routing ID    |
| 3    | SUBSCRIBE | Subscription request   |
| 4    | CANCEL    | Subscription cancel    |

### 6.5 RAW Protocol Frame Structure

A simple protocol for STREAM sockets and external client integration.

```
┌──────────────────────┬─────────────────────────────┐
│  Length (4 Bytes)    │     Payload (N Bytes)       │
│  (Big Endian)        │                             │
└──────────────────────┴─────────────────────────────┘
```

- No handshake (immediate data send/receive)
- Simple implementation: `read(4)` -> `read(length)`
- Easy integration with external clients

### 6.6 ZMP Handshake Sequence

```
    Client                              Server
       │                                   │
       │─────── HELLO (greeting) ─────────→│
       │                                   │
       │←────── HELLO (greeting) ──────────│
       │                                   │
       │                                   │  (Socket type compatibility check)
       │                                   │
       │─────── READY (metadata) ─────────→│
       │                                   │
       │←────── READY (metadata) ──────────│
       │                                   │
       │←─────── Data Exchange ───────────→│
       │                                   │
```

- **HELLO**: Socket type (1B) + Identity length (1B) + Identity value (0-255B)
- **READY**: Socket-Type property (always), Identity property (DEALER/ROUTER only)

### 6.7 Protocol-Transport-Engine Mapping

The engine is automatically selected based on the socket type:

```
┌─────────────────────────────────────────────────────────────────────┐
│                       Engine Selection Rules                         │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  Socket type == STREAM ?                                            │
│      ├─ YES → asio_raw_engine_t  (RAW protocol, no handshake)      │
│      └─ NO  → asio_zmp_engine_t  (ZMP protocol, HELLO/READY)      │
│                                                                     │
│  This rule is the same across all transports (TCP/TLS/IPC/WS/WSS). │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

**Full Mapping Matrix**:

| URL Scheme | Connecter               | Transport          | STREAM Engine       | Other Socket Engine  | Handshake           |
|-----------|-------------------------|--------------------|---------------------|---------------------|---------------------|
| `tcp://`  | `asio_tcp_connecter_t`  | `tcp_transport_t`  | `asio_raw_engine_t` | `asio_zmp_engine_t` | (none) / ZMP        |
| `tls://`  | `asio_tls_connecter_t`  | `ssl_transport_t`  | `asio_raw_engine_t` | `asio_zmp_engine_t` | SSL / SSL+ZMP       |
| `ws://`   | `asio_ws_connecter_t`   | `ws_transport_t`   | `asio_raw_engine_t` | `asio_zmp_engine_t` | WS / WS+ZMP        |
| `wss://`  | `asio_ws_connecter_t`   | `wss_transport_t`  | `asio_raw_engine_t` | `asio_zmp_engine_t` | SSL+WS / SSL+WS+ZMP|
| `ipc://`  | `asio_ipc_connecter_t`  | `ipc_transport_t`  | `asio_raw_engine_t` | `asio_zmp_engine_t` | (none) / ZMP        |

### 6.8 Handshake Stage Comparison

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      Handshake Stage Comparison                         │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  TCP + PAIR/DEALER/ROUTER/PUB/SUB                                      │
│  ┌─────────┐    ┌─────────────┐                                       │
│  │  TCP    │───→│  ZMP        │───→ Data Transfer                     │
│  │ Connect │    │  Handshake  │                                       │
│  └─────────┘    └─────────────┘                                       │
│                                                                         │
│  TCP + STREAM                                                          │
│  ┌─────────┐                                                           │
│  │  TCP    │───────────────────────→ Data Transfer (immediate)         │
│  │ Connect │                                                           │
│  └─────────┘                                                           │
│                                                                         │
│  TLS + PAIR/DEALER/ROUTER/PUB/SUB                                      │
│  ┌─────────┐    ┌─────────┐    ┌─────────────┐                        │
│  │  TCP    │───→│  SSL    │───→│  ZMP        │───→ Data Transfer      │
│  │ Connect │    │Handshake│    │  Handshake  │                        │
│  └─────────┘    └─────────┘    └─────────────┘                        │
│                                                                         │
│  WS + PAIR/DEALER/ROUTER/PUB/SUB                                       │
│  ┌─────────┐    ┌─────────┐    ┌─────────────┐                        │
│  │  TCP    │───→│   WS    │───→│  ZMP        │───→ Data Transfer      │
│  │ Connect │    │ Upgrade │    │  Handshake  │                        │
│  └─────────┘    └─────────┘    └─────────────┘                        │
│                                                                         │
│  WSS + PAIR/DEALER/ROUTER/PUB/SUB                                      │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────────┐        │
│  │  TCP    │───→│  SSL    │───→│   WS    │───→│  ZMP        │───→ Tx │
│  │ Connect │    │Handshake│    │ Upgrade │    │  Handshake  │        │
│  └─────────┘    └─────────┘    └─────────┘    └─────────────┘        │
│                                                                         │
│  WSS + STREAM                                                          │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐                            │
│  │  TCP    │───→│  SSL    │───→│   WS    │───────────────→ Data Tx    │
│  │ Connect │    │Handshake│    │ Upgrade │                            │
│  └─────────┘    └─────────┘    └─────────┘                            │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 6.9 Transport Characteristics Comparison

| Transport | Handshake  | Encryption | Speculative Write | Gather Write | Use Case                      |
|-----------|:----------:|:----------:|:-----------------:|:------------:|-------------------------------|
| TCP       | -          | -          | O                 | O            | Standard network communication|
| IPC       | -          | -          | Optional          | O            | Local inter-process comms     |
| TLS       | O          | O          | -                 | -            | Encrypted network communication|
| WS        | O          | -          | -                 | O            | Web client integration        |
| WSS       | O          | O          | -                 | O            | Encrypted web client          |

---

## 7. Core Components

### 7.1 msg_t - Message Container

A 64-byte fixed-size structure that holds all message data.
It is designed to handle small messages without `malloc` calls.

```
┌─────────────────────────────────────────────────────────────────┐
│                        msg_t (64 bytes)                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │  Common fields (base_t)                                    │ │
│  │  - metadata_t* metadata   (8 bytes)                        │ │
│  │  - uint32_t routing_id    (4 bytes)                        │ │
│  │  - group_t group          (16 bytes)                       │ │
│  │  - uint8_t flags          (1 byte)                         │ │
│  │  - uint8_t type           (1 byte)                         │ │
│  └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│  Type-specific data area (union):                               │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │  type_vsm (<=33B on 64-bit)                                │ │
│  │  Very Small Message: data stored directly in msg_t buffer  │ │
│  │  - uint8_t data[max_vsm_size]                              │ │
│  │  - uint8_t size                                            │ │
│  │  -> Inline storage without malloc, fastest path            │ │
│  └───────────────────────────────────────────────────────────┘ │
│                            OR                                   │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │  type_lmsg (>33B on 64-bit)                                │ │
│  │  Large Message: pointer to separately allocated buffer     │ │
│  │  - content_t* content                                      │ │
│  │    ├── void* data          (data pointer)                  │ │
│  │    ├── size_t size         (size)                           │ │
│  │    ├── msg_free_fn* ffn    (free function)                 │ │
│  │    └── atomic_counter_t refcnt (reference count)           │ │
│  └───────────────────────────────────────────────────────────┘ │
│                            OR                                   │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │  type_cmsg: Constant Message (const data ref, no free)     │ │
│  │  type_zclmsg: Zero-copy Large Message (direct user buffer) │ │
│  └───────────────────────────────────────────────────────────┘ │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Message flags**:

| Flag         | Value | Description                            |
|-------------|-------|----------------------------------------|
| `more`       | 0x01 | Intermediate frame of multipart message |
| `command`    | 0x02 | Control frame (handshake, heartbeat)   |
| `routing_id` | 0x40 | Contains Routing ID                    |
| `shared`     | 0x80 | Shared buffer (reference counted)      |

**Message types**:

| Type          | Value | Description                                     |
|--------------|-------|-------------------------------------------------|
| `type_vsm`    | 101  | Very Small Message (<=33B, no copy)             |
| `type_lmsg`   | 102  | Large Message (malloc'd buffer)                 |
| `type_cmsg`   | 104  | Constant Message (const data reference)         |
| `type_zclmsg` | 105  | Zero-copy Large Message (direct user buffer)    |

### 7.2 pipe_t - Lock-Free Message Queue

A bidirectional pipe for passing messages between threads.
It exchanges `msg_t` instances lock-free between the Application thread and the I/O thread.

```
┌───────────────────────────────────────────────────────────────┐
│                          pipe_t                               │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│  Thread A (Socket)              Thread B (I/O)                │
│       │                              │                        │
│       │    ┌──────────────────┐     │                        │
│       ├───→│   _out_pipe      │────→│  (Tx: Socket -> I/O)   │
│       │    │   (YPipe<msg_t>) │     │                        │
│       │    └──────────────────┘     │                        │
│       │                              │                        │
│       │    ┌──────────────────┐     │                        │
│       │←───│   _in_pipe       │←────┤  (Rx: I/O -> Socket)   │
│       │    │   (YPipe<msg_t>) │     │                        │
│       │    └──────────────────┘     │                        │
│                                                               │
│  High Water Mark (HWM): Message queue size limit              │
│  - _hwm: Outbound HWM (blocks send when queue exceeded)      │
│  - _lwm: Inbound Low Water Mark (half of HWM, resume point)  │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

**YPipe characteristics**:
- Lock-free FIFO queue (CAS operation-based)
- Cache line optimization
- Visibility guaranteed through memory barriers

**Pipe state machine**:

```
                    ┌────────────┐
                    │   active   │ <────────────────┐
                    └─────┬──────┘                  │
                          │ receive delimiter       │ connect
                          v                         │
              ┌───────────────────────┐             │
              │ delimiter_received    │             │
              └───────────┬───────────┘             │
                          │ send term_ack           │
                          v                         │
              ┌───────────────────────┐             │
              │    term_ack_sent      │             │
              └───────────┬───────────┘             │
                          │ receive term_ack        │
                          v                         │
                    ┌───────────┐                   │
                    │ terminated│ ──────────────────┘
                    └───────────┘     (on reconnect)
```

### 7.3 ctx_t - Context

The top-level object that manages global state.

**Key roles**:

1. **I/O Thread Pool Management**
   - Set thread count with `zlink_ctx_set(ctx, ZLINK_IO_THREADS, n)` (default: 1)
   - Each I/O thread holds an independent `io_context`
   - Selects the least-loaded I/O thread for new connections (affinity mask support)

2. **Socket Management**
   - Tracks socket creation/deletion
   - Maximum socket limit (default: 1023)
   - Empty slot reuse

3. **inproc Endpoint Management**
   - Maps `inproc://name` format addresses to endpoints
   - Holds connection requests made before bind in pending_connections

```
ctx_t internal structure:
┌──────────────────────────────────────────────────────────┐
│  _sockets: array_t<socket_base_t>     Active socket list  │
│  _empty_slots: vector<uint32_t>       Empty slot reuse    │
│  _io_threads: vector<io_thread_t*>    I/O thread pool     │
│  _slots: vector<i_mailbox*>           Inter-thread mailbox│
│  _endpoints: map<string, endpoint_t>  inproc registry     │
│  _pending_connections: multimap       Pending connections  │
│                                                          │
│  _max_sockets: int     (default: 1023)                   │
│  _io_thread_count: int (default: 1)                      │
│  _max_msgsz: int       (max message size)                │
└──────────────────────────────────────────────────────────┘
```

### 7.4 session_base_t - Session

Acts as a bridge between the socket and the engine.

```
┌─────────────────────────────────────────────────────────────┐
│                     session_base_t                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐    ┌─────────┐    ┌─────────────────┐   │
│  │ socket_base_t│←──→│ pipe_t  │←──→│ asio_engine_t   │   │
│  │              │    │         │    │                 │   │
│  │  zlink_send() │    │ YPipe   │    │ async_read/     │   │
│  │  zlink_recv() │    │         │    │ async_write     │   │
│  └──────────────┘    └─────────┘    └─────────────────┘   │
│                                                             │
│  push_msg(): Engine -> Session -> Pipe -> Socket            │
│  pull_msg(): Socket -> Pipe -> Session -> Engine            │
│                                                             │
│  Additional roles:                                          │
│  - Connection state management                              │
│  - Reconnection logic (exponential backoff)                 │
│  - Connecter selection (based on URL scheme)                │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 7.5 Threading Model

```
┌─────────────────────────────────────────────────────────────────┐
│                    zlink Threading Model                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                 Application Threads                      │   │
│  │  - Call zlink_send() / zlink_recv()                     │   │
│  │  - Recommended: access each socket from a single thread │   │
│  │  - Multiple sockets can be used from multiple threads   │   │
│  └──────────────────────────┬──────────────────────────────┘   │
│                              │                                  │
│                   Lock-free Pipes (YPipe)                       │
│                              │                                  │
│  ┌──────────────────────────v──────────────────────────────┐   │
│  │                    I/O Threads                           │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐                │   │
│  │  │ Thread 0 │ │ Thread 1 │ │ Thread N │  (configurable) │   │
│  │  │io_context│ │io_context│ │io_context│                │   │
│  │  └──────────┘ └──────────┘ └──────────┘                │   │
│  │                                                          │   │
│  │  - Asynchronous I/O processing (Proactor pattern)       │   │
│  │  - Encoder/decoder execution                             │   │
│  │  - Network send/receive                                  │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    Reaper Thread                         │   │
│  │  - Resource cleanup for terminated sockets/sessions     │   │
│  │  - Deferred deletion processing                          │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Inter-thread communication (Mailbox system)**:

```
Application Thread              I/O Thread
      │                              │
      │  zlink_send()                 │
      │      │                       │
      │      v                       │
      │  [Push msg_t to YPipe]       │
      │      │                       │
      │  mailbox.send(activate_write)│
      │─────────────────────────────→│
      │                              │  (signal received)
      │                              │
      │                              v
      │                         [Pop msg_t from YPipe]
      │                              │
      │                         [Encode and transmit]
```

- Each thread has its own `mailbox_t`.
- `mailbox_t` internally consists of `ypipe_t<command_t>` and `signaler_t`.
- Command types: `stop`, `plug`, `attach`, `bind`, `activate_read`, `activate_write`, etc.

---

## 8. Data Flow

### 8.1 Message Send (Outbound / Tx)

```
┌───────────────────────────────────────────────────────────────────┐
│                    APPLICATION THREAD                             │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  (1) zlink_send(socket, data, size, flags)                        │
│       │                                                           │
│       v                                                           │
│  (2) socket_base_t::send()                                       │
│       │  - Create msg_t (VSM or LMSG)                            │
│       │  - Select routing strategy by socket type                 │
│       │    . DEALER: lb_t (Round-robin)                          │
│       │    . ROUTER: ID-based direct routing                     │
│       │    . PUB: dist_t (send to all subscribers)               │
│       v                                                           │
│  (3) pipe_t::write()                                             │
│       │  - Push message to YPipe (Lock-free)                     │
│       │  - HWM check (block or drop when exceeded)               │
│       v                                                           │
│  (4) mailbox signal to I/O thread                                │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
                              │
                              v
┌───────────────────────────────────────────────────────────────────┐
│                      I/O THREAD                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  (5) asio_engine_t: receive activate_write event                  │
│       │                                                           │
│       v                                                           │
│  (6) pull_msg_from_session()                                     │
│       │  - Read message from pipe                                │
│       v                                                           │
│  (7) encoder: message -> byte stream                              │
│       │  - ZMP: 8-byte header + payload                          │
│       │  - RAW: 4-byte length + payload                          │
│       v                                                           │
│  (8) speculative_write() attempt                                 │
│       │  - Success: synchronous write completes immediately       │
│       │  - Failure (EAGAIN): schedule async_write_some()         │
│       v                                                           │
│  (9) transport: network transmission                              │
│       - TCP: direct send                                          │
│       - TLS: encrypt with SSL then send                          │
│       - WS: Beast WebSocket framing then send                    │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### 8.2 Message Receive (Inbound / Rx)

```
┌───────────────────────────────────────────────────────────────────┐
│                      I/O THREAD                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  (1) async_read_some() completion callback                        │
│       │  - Receive bytes from network                            │
│       v                                                           │
│  (2) on_read_complete()                                          │
│       │                                                           │
│       v                                                           │
│  (3) decoder: byte stream -> message                              │
│       │  - Parse header (ZMP 8B / RAW 4B)                        │
│       │  - Verify payload size                                   │
│       │  - Create msg_t                                          │
│       v                                                           │
│  (4) push_msg_to_session()                                       │
│       │                                                           │
│       v                                                           │
│  (5) session_base_t::push_msg()                                  │
│       │  - Message validation                                    │
│       │  - Forward to inbound pipe                               │
│       v                                                           │
│  (6) pipe_t::write() (inbound pipe)                              │
│       │  - Push message to YPipe                                 │
│       v                                                           │
│  (7) Signal read-ready to socket (activate_read)                 │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
                              │
                              v
┌───────────────────────────────────────────────────────────────────┐
│                    APPLICATION THREAD                             │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  (8) zlink_recv(socket, buffer, size, flags)                      │
│       │                                                           │
│       v                                                           │
│  (9) socket_base_t::recv()                                       │
│       │  - Receive strategy by socket type                       │
│       │    . DEALER/SUB: fq_t (Fair Queueing)                   │
│       │    . ROUTER: extract Routing ID then deliver message     │
│       │  - Topic filtering (SUB)                                 │
│       v                                                           │
│  (10) pipe_t::read()                                             │
│        │  - Pop message from YPipe (Lock-free)                   │
│        v                                                          │
│  (11) Copy data to user buffer                                   │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### 8.3 Connection Establishment Flow

```
┌───────────────────────────────────────────────────────────────────┐
│                   Connection Establishment Steps                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  zlink_connect("tcp://host:port")                                 │
│       │                                                           │
│       v                                                           │
│  (1) address_t parsing                                            │
│       │  - Identify protocol (tcp, tls, ws, wss, ipc)            │
│       │  - Extract address/port                                   │
│       v                                                           │
│  (2) Create session_base_t                                       │
│       │  - Set reconnection policy                                │
│       v                                                           │
│  (3) Create and start connecter                                   │
│       │  - Select connecter class based on URL scheme             │
│       │  - Call async_connect()                                   │
│       v                                                           │
│  (4) TCP connection complete (3-way handshake)                    │
│       │                                                           │
│       v                                                           │
│  (5) [TLS/WSS] Transport handshake                                │
│       │  - TLS: SSL_do_handshake()                               │
│       │  - WS: HTTP Upgrade request/response                     │
│       v                                                           │
│  (6) Create engine and plug()                                    │
│       │  - Select asio_zmp_engine_t or asio_raw_engine_t         │
│       │    based on socket type                                   │
│       v                                                           │
│  (7) [ZMP] Protocol handshake                                     │
│       │  - HELLO exchange (socket type, Identity)                │
│       │  - Socket type compatibility check                       │
│       │  - READY exchange (metadata)                             │
│       v                                                           │
│  (8) engine_ready()                                              │
│       - Create and connect pipe                                   │
│       - start_input() / start_output()                            │
│       -> Data send/receive is now possible                        │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

---

## 9. Source Tree Structure

```
core/
├── include/                         # Public headers (zlink.h)
│
├── src/
│   ├── api/                         # Public C API
│   │   ├── zlink.cpp                # Entry point for all zlink_* functions
│   │   └── zlink_utils.cpp          # Utility functions
│   │
│   ├── core/                        # System base components
│   │   ├── ctx.cpp/hpp              # Context (thread pool, socket management)
│   │   ├── msg.cpp/hpp              # Message container (64B fixed)
│   │   ├── pipe.cpp/hpp             # Lock-free bidirectional pipe
│   │   ├── session_base.cpp/hpp     # Socket-engine bridge
│   │   ├── io_thread.cpp/hpp        # I/O worker thread
│   │   ├── mailbox.cpp/hpp          # Inter-thread command delivery
│   │   ├── object.cpp/hpp           # Base object (command processing)
│   │   ├── own.cpp/hpp              # Ownership management
│   │   ├── reaper.cpp/hpp           # Terminated resource cleanup
│   │   ├── signaler.cpp/hpp         # Thread wake-up signal
│   │   ├── options.cpp/hpp          # Socket option storage
│   │   ├── address.cpp/hpp          # Address parsing
│   │   ├── endpoint.cpp/hpp         # Endpoint management
│   │   ├── command.hpp              # Inter-thread command definitions
│   │   ├── socket_poller.cpp/hpp    # Socket poller
│   │   └── ...
│   │
│   ├── sockets/                     # Socket type implementations
│   │   ├── socket_base.cpp/hpp      # Base class for all sockets
│   │   ├── pair.cpp/hpp             # PAIR socket
│   │   ├── dealer.cpp/hpp           # DEALER socket
│   │   ├── router.cpp/hpp           # ROUTER socket
│   │   ├── pub.cpp/hpp              # PUB socket
│   │   ├── sub.cpp/hpp              # SUB socket
│   │   ├── xpub.cpp/hpp             # XPUB socket
│   │   ├── xsub.cpp/hpp             # XSUB socket
│   │   ├── stream.cpp/hpp           # STREAM socket
│   │   ├── lb.cpp/hpp               # Load balancer (Round-robin)
│   │   ├── fq.cpp/hpp               # Fair queue (Fair Queueing)
│   │   ├── dist.cpp/hpp             # Distributor (Fan-out)
│   │   └── proxy.cpp/hpp            # Proxy utility
│   │
│   ├── engine/                      # I/O engines
│   │   ├── i_engine.hpp             # Engine interface
│   │   └── asio/
│   │       ├── asio_engine.cpp/hpp       # Base Proactor engine
│   │       ├── asio_zmp_engine.cpp/hpp   # ZMP protocol engine
│   │       ├── asio_raw_engine.cpp/hpp   # RAW protocol engine
│   │       ├── asio_poller.cpp/hpp       # io_context wrapper
│   │       ├── i_asio_transport.hpp      # Transport interface
│   │       ├── handler_allocator.hpp     # Handler memory management
│   │       └── asio_error_handler.hpp    # Error handling
│   │
│   ├── protocol/                    # Protocol encoding/decoding
│   │   ├── zmp_protocol.hpp         # ZMP v1.0 constant definitions
│   │   ├── zmp_encoder.cpp/hpp      # ZMP encoder
│   │   ├── zmp_decoder.cpp/hpp      # ZMP decoder
│   │   ├── zmp_metadata.hpp         # ZMP metadata
│   │   ├── raw_encoder.cpp/hpp      # RAW (Length-Prefix) encoder
│   │   ├── raw_decoder.cpp/hpp      # RAW decoder
│   │   ├── encoder.hpp              # Encoder base template
│   │   ├── decoder.hpp              # Decoder base template
│   │   ├── i_encoder.hpp            # Encoder interface
│   │   ├── i_decoder.hpp            # Decoder interface
│   │   ├── metadata.cpp/hpp         # Metadata processing
│   │   ├── wire.hpp                 # Byte order conversion
│   │   └── decoder_allocators.cpp/hpp # Decoder memory management
│   │
│   ├── transports/                  # Transport implementations
│   │   ├── tcp/                     # TCP transport
│   │   │   ├── tcp_transport.cpp/hpp
│   │   │   ├── asio_tcp_connecter.cpp/hpp
│   │   │   ├── asio_tcp_listener.cpp/hpp
│   │   │   ├── tcp_address.cpp/hpp
│   │   │   └── tcp.cpp/hpp
│   │   │
│   │   ├── ipc/                     # IPC transport (Unix only)
│   │   │   ├── ipc_transport.cpp/hpp
│   │   │   ├── asio_ipc_connecter.cpp/hpp
│   │   │   ├── asio_ipc_listener.cpp/hpp
│   │   │   └── ipc_address.cpp/hpp
│   │   │
│   │   ├── ws/                      # WebSocket transport (Beast)
│   │   │   ├── ws_transport.cpp/hpp
│   │   │   ├── asio_ws_connecter.cpp/hpp
│   │   │   ├── asio_ws_listener.cpp/hpp
│   │   │   ├── asio_ws_engine.cpp/hpp   # (unused, uses asio_zmp/raw_engine)
│   │   │   └── ws_address.cpp/hpp
│   │   │
│   │   └── tls/                     # TLS/SSL transport (OpenSSL)
│   │       ├── ssl_transport.cpp/hpp
│   │       ├── wss_transport.cpp/hpp
│   │       ├── asio_tls_connecter.cpp/hpp
│   │       ├── asio_tls_listener.cpp/hpp
│   │       ├── ssl_context_helper.cpp/hpp
│   │       └── wss_address.cpp/hpp
│   │
│   ├── services/                    # High-level services
│   │   ├── common/                  # Common service utilities
│   │   │   ├── heartbeat.hpp
│   │   │   ├── service_key.hpp
│   │   │   └── service_types.hpp
│   │   ├── discovery/               # Service discovery
│   │   │   ├── discovery.cpp/hpp
│   │   │   ├── discovery_protocol.hpp
│   │   │   └── registry.cpp/hpp
│   │   ├── gateway/                 # Gateway
│   │   │   ├── gateway.cpp/hpp
│   │   │   ├── receiver.cpp/hpp
│   │   │   └── routing_id_utils.hpp
│   │   └── spot/                    # SPOT service
│   │       ├── spot_pub.cpp/hpp     # Publish handle (thread-safe)
│   │       ├── spot_sub.cpp/hpp     # Subscribe/receive handle
│   │       └── spot_node.cpp/hpp    # Network control (PUB/SUB mesh)
│   │
│   └── utils/                       # Utilities
│       ├── ypipe.hpp                # Lock-free pipe
│       ├── yqueue.hpp               # Lock-free queue
│       ├── atomic_counter.hpp       # Atomic counter
│       ├── atomic_ptr.hpp           # Atomic pointer
│       ├── blob.hpp                 # Binary blob
│       ├── clock.cpp/hpp            # Time measurement
│       ├── random.cpp/hpp           # Random number generation
│       ├── ip_resolver.cpp/hpp      # IP address resolution
│       ├── mtrie.cpp/hpp            # Multi-trie (XPUB subscriptions)
│       ├── trie.cpp/hpp             # Trie
│       ├── radix_tree.cpp/hpp       # Radix tree (XSUB subscriptions)
│       ├── generic_mtrie.hpp        # Generic multi-trie template
│       ├── mutex.hpp                # Mutex wrapper
│       ├── condition_variable.hpp   # Condition variable wrapper
│       ├── err.cpp/hpp              # Error handling
│       ├── ip.cpp/hpp               # IP utilities
│       ├── config.hpp               # Compile-time configuration
│       └── ...
│
├── tests/                           # Functional tests
└── unittests/                       # Internal unit tests
```

---

## Appendix

### A. Related Documents

- [ZMP v1.0 Protocol Details](protocol-zmp.md)
- [RAW Protocol Details](protocol-raw.md)
- [STREAM Socket WS/WSS Optimization](stream-socket.md)
- [Threading and Concurrency Model](threading-model.md)
- [Performance Characteristics and Tuning Guide](../guide/10-performance.md)

### B. Core Interface Summary

**i_asio_transport** (common interface for all transports):

```
i_asio_transport
  +-- open(io_context, fd)              Open connection
  +-- close()                           Close connection
  +-- async_read_some(buffer, handler)  Asynchronous read
  +-- async_write_some(buffer, handler) Asynchronous write
  +-- read_some(buffer, size)           Synchronous (speculative) read
  +-- write_some(buffer, size)          Synchronous (speculative) write
  +-- requires_handshake()              Whether handshake is required
  +-- async_handshake(type, handler)    Asynchronous handshake
  +-- is_encrypted()                    Whether encrypted
  +-- supports_speculative_write()      Whether speculative write is supported
  +-- supports_gather_write()           Whether gather write is supported
```

**i_engine** (engine interface):

```
i_engine
  +-- plug(session)                     Connect to session
  +-- terminate()                       Terminate
  +-- restart_input()                   Restart receive
  +-- restart_output()                  Restart send
```

### C. Performance Optimization Techniques Summary

| Technique           | Description                                                         |
|--------------------|---------------------------------------------------------------------|
| Speculative I/O    | Attempts synchronous I/O before async call to eliminate callback overhead |
| Gather Write       | Sends header+body in a single system call via writev()             |
| Zero-Copy Message  | Stores only user buffer pointer in msg_t, transmits without copy   |
| VSM (Inline)       | Messages <=33 bytes stored directly in msg_t internal buffer (no malloc) |
| Lock-free YPipe    | CAS operation-based inter-thread message exchange, no mutex        |
| Cache Line Opt.    | YPipe nodes aligned to cache line size                             |
| Backpressure       | Pauses reading when 10MB limit exceeded to prevent memory blowup   |
